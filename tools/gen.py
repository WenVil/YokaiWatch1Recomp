"""Generate C for a whole NSO module.

Usage: gen.py <name> <nso> <base> <outdir> [--hle hle_symbols.txt] [--chunk N] [--simd asm|c]

--simd: FP/SIMD/CRC32 as AArch64 inline asm (default on arm64 hosts) or portable C (default elsewhere).
"""
import argparse
import bisect
import os
import platform
import re
import struct
import sys
import time
from collections import Counter

import unicorn as uc
from unicorn import arm64_const as A

from analyze import eh_frame_functions
from lifter import Insn, Lifter, Unsupported, bits, emit_asm_batch, hx, sext
from nso import Nso, R_AARCH64_RELATIVE

SCRATCH_MEM = 0x7300000000


def sext26(w):
    v = w & 0x03FFFFFF
    return v - 0x04000000 if v & 0x02000000 else v


def fname(a):
    return f'f_{a:x}'


def sanitize(sym):
    return 'hle_' + re.sub(r'[^A-Za-z0-9_]', '_', sym)


class Module:
    def __init__(self, name, path, base, simd='asm'):
        self.name = name
        self.nso = Nso(path)
        self.base = base
        n = self.nso
        self.lo = base + n.text[0]
        self.hi = base + n.text[0] + n.text[1]
        self.words = struct.unpack(f'<{n.text[1] // 4}I', bytes(n.image[n.text[0]:n.text[0] + n.text[1]]))
        self.lifter = Lifter(base, simd)
        self.cache = {}
        self.stats = Counter()
        self.mu = None

    def in_text(self, a):
        return self.lo <= a < self.hi and a % 4 == 0

    def word(self, a):
        return self.words[(a - self.lo) // 4]

    def lift(self, a):
        ins = self.cache.get(a)
        if ins is None:
            try:
                ins = self.lifter.lift(self.word(a), a)
            except Unsupported as e:
                self.stats['unsupported'] += 1
                ins = Insn('trap', f'rt_udf(c, {hx(a)}); /* {e} */')
            self.cache[a] = ins
        return ins

    def boundaries(self):
        n, b = self.nso, self.base
        starts = set()
        for a in eh_frame_functions(n):
            starts.add(b + a)
        for name, info, shndx, value, size in n.syms:
            if shndx and (info & 0xF) == 2:
                starts.add(b + value)
        for off, typ, sym, add in n.relocs:
            if typ == R_AARCH64_RELATIVE:
                starts.add(b + add)
        starts = sorted(a for a in starts if self.in_text(a))
        self.orig_bounds = starts
        return starts

    def orig_range(self, entry, end):
        if not getattr(self, 'orig_bounds', None):
            return entry, end
        idx = bisect.bisect_right(self.orig_bounds, entry) - 1
        if idx < 0:
            return entry, end
        lo = self.orig_bounds[idx]
        hi = self.orig_bounds[idx + 1] if idx + 1 < len(self.orig_bounds) else self.hi
        return lo, hi

    def detect_freetype_setjmp(self):
        """Locate FreeType gray_convert_glyph setjmp/longjmp recovery sites, version-independently."""
        n, b = self.nso, self.base
        got_slots = set()
        for off, typ, symidx, add in n.relocs:
            if symidx < len(n.syms) and n.syms[symidx][0] == 'setjmp':
                got_slots.add(b + off)
        plt_stubs = set()
        for i in range(max(0, len(self.words) - 3)):
            w0, w2, w3 = self.words[i], self.words[i + 2], self.words[i + 3]
            if (w0 & 0x9F00001F) == 0x90000010 and (w2 & 0xFFC003FF) == 0x91000210 and w3 == 0xD61F0220:
                pc = self.lo + i * 4
                imm = ((w0 >> 5) & 0x7FFFF) << 2 | ((w0 >> 29) & 3)
                if imm & (1 << 20):
                    imm -= 1 << 21
                target = (pc & ~0xFFF) + (imm << 12) + ((w2 >> 10) & 0xFFF)
                if target in got_slots:
                    plt_stubs.add(pc)
        found = {}
        pre_sig = (0xF81E0FF3, 0xA9017BFD, 0x910043FD, 0xAA0003F3, 0xB81FC3BF)
        post_sig = (0x340000E0, 0x321A03E8, 0xB81FC3A8)
        for i in range(len(pre_sig), len(self.words) - len(post_sig)):
            w = self.words[i]
            if (w & 0xFC000000) != 0x94000000:
                continue
            bl_pc = self.lo + i * 4
            callee = bl_pc + sext26(w) * 4
            if plt_stubs and callee not in plt_stubs:
                continue
            if self.words[i - len(pre_sig):i] == pre_sig and self.words[i + 1:i + 1 + len(post_sig)] == post_sig:
                entry = bl_pc - len(pre_sig) * 4
                ret_pc = bl_pc + 4
                ovf_pc = bl_pc + 8
                found[entry] = (bl_pc, ret_pc, ovf_pc)
        return found

    # ------------------------------------------------------------ jump tables

    def emu(self, fill_byte=0):
        emus = getattr(self, '_emus', None)
        if emus is None:
            emus = {}
            self._emus = emus
        if fill_byte not in emus:
            mu = uc.Uc(uc.UC_ARCH_ARM64, uc.UC_MODE_ARM)
            n = self.nso
            ro_end = n.data[0] & ~0xFFF
            if ro_end > 0:
                mu.mem_map(self.base, ro_end, uc.UC_PROT_READ | uc.UC_PROT_EXEC)
                mu.mem_write(self.base, bytes(n.image[:ro_end]))
            if len(n.image) > ro_end:
                mu.mem_map(self.base + ro_end, len(n.image) - ro_end, uc.UC_PROT_READ | uc.UC_PROT_WRITE)
                mu.mem_write(self.base + ro_end, bytes(n.image[ro_end:]))
            mu.mem_map(SCRATCH_MEM, 0x100000)
            mu.mem_write(SCRATCH_MEM, bytes([fill_byte]) * 0x100000)
            mu.reg_write(A.UC_ARM64_REG_CPACR_EL1, 0x300000)
            dirty = []
            mu.hook_add(uc.UC_HOOK_MEM_WRITE,
                        lambda _mu, _acc, addr, size, _val, _ud, d=dirty: d.append((addr, size)),
                        begin=SCRATCH_MEM, end=SCRATCH_MEM + 0x100000 - 1)
            if len(n.image) > ro_end:
                mu.hook_add(uc.UC_HOOK_MEM_WRITE,
                            lambda _mu, _acc, addr, size, _val, _ud, d=dirty: d.append((addr, size)),
                            begin=self.base + ro_end, end=self.base + len(n.image) - 1)
            emus[fill_byte] = (mu, dirty, ro_end)
            if fill_byte == 0 and self.mu is None:
                self.mu = mu
        return emus[fill_byte][0]

    def jump_table(self, br_pc, entry, end, orig_lo=None, orig_hi=None):
        """Resolve `br xN` targets from a bounds-checked jump table, or None."""
        if orig_lo is None or orig_hi is None:
            orig_lo, orig_hi = self.orig_range(entry, end)
        r = self._jump_table_fallthrough(br_pc, entry, end, orig_lo, orig_hi)
        if r is None:
            r = self._jump_table_taken(br_pc, entry, end, orig_lo, orig_hi)
        return r

    def _index_regs(self, start, at, idx):
        """The index register plus registers holding the same value in start..at: sources it was
        copied/masked from, and copies of those (the table lookup sometimes recomputes the index
        from another copy after the bounds check)."""
        def copy_src(w):
            if (w & 0x7F800000) == 0x12000000:  # AND (imm)
                return bits(w, 5, 5)
            if (w & 0x7FE0FFE0) == 0x2A0003E0:  # MOV (ORR with zr)
                return bits(w, 16, 5)
            if (w & 0x7F800000) in (0x53000000, 0x13000000):  # UBFM/SBFM (uxtb, uxth, sxtw...)
                return bits(w, 5, 5)
            return None
        regs = {idx}
        changed = True
        while changed:
            changed = False
            for a in range(start, at, 4):
                w = self.word(a)
                rd, src = w & 31, copy_src(w)
                if src is None or src >= 31 or rd >= 31:
                    continue
                for x, y in ((rd, src), (src, rd)):
                    if x in regs and y not in regs:
                        regs.add(y)
                        changed = True
        return sorted(regs)

    def _reg_id(self, r):
        return A.UC_ARM64_REG_X0 + r if r < 29 else (A.UC_ARM64_REG_X29 if r == 29 else A.UC_ARM64_REG_X30)

    def _set_index(self, mu, regs, i):
        for r in regs:
            mu.reg_write(self._reg_id(r), i)

    def _reset_scratch(self, fill_byte):
        mu = self.emu(fill_byte)
        _, dirty, ro_end = self._emus[fill_byte]
        if dirty:
            fill_pat = bytes([fill_byte])
            img = self.nso.image
            for addr, size in dirty:
                if addr >= SCRATCH_MEM:
                    lo = max(SCRATCH_MEM, addr)
                    hi = min(SCRATCH_MEM + 0x100000, addr + size)
                    if hi > lo:
                        mu.mem_write(lo, fill_pat * (hi - lo))
                else:
                    lo = max(self.base + ro_end, addr)
                    hi = min(self.base + len(img), addr + size)
                    if hi > lo:
                        mu.mem_write(lo, bytes(img[lo - self.base:hi - self.base]))
            dirty.clear()
        return mu

    def _fill_regs(self, mu, reg_base, sp_val, step_reg):
        for r in range(31):
            mu.reg_write(self._reg_id(r), reg_base + r * step_reg)
        mu.reg_write(A.UC_ARM64_REG_SP, sp_val)

    def _verify_dual(self, run_once):
        seq_a = run_once(0x00, SCRATCH_MEM + 0x80000, SCRATCH_MEM + 0x80000, 0)
        if seq_a is None:
            return None
        seq_b = run_once(0x55, SCRATCH_MEM + 0x2f708, SCRATCH_MEM + 0x60110, 8)
        if seq_b != seq_a:
            return None
        seq_c = run_once(0xAA, SCRATCH_MEM + 0x3b318, SCRATCH_MEM + 0x70220, 16)
        if seq_c != seq_a:
            return None
        return sorted(set(seq_a))

    def _emulate_table(self, br_pc, entry, end, start, idx, count, set_at, orig_lo=None, orig_hi=None):
        """Run start..br for each index value (set at address set_at), collect targets."""
        if orig_lo is None or orig_hi is None:
            orig_lo, orig_hi = entry, end
        rn_reg = self._reg_id(bits(self.word(br_pc), 5, 5))

        def step_to(mu, frm, to):
            # single-step: Unicorn's `until` is unreliable mid translation-block
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        def run_profile(fill_byte, reg_base, sp_val, step_reg):
            seq = []
            try:
                for i in range(count):
                    mu = self._reset_scratch(fill_byte)
                    self._fill_regs(mu, reg_base, sp_val, step_reg)
                    a2 = entry
                    while a2 < start:
                        w2 = self.word(a2)
                        if (w2 & 0x1F000000) == 0x10000000 or (w2 & 0xFF800000) == 0x91000000:
                            mu.emu_start(a2, 0, count=1)
                        a2 += 4
                    if set_at != start and not step_to(mu, start, set_at):
                        return None
                    self._set_index(mu, [idx], i)
                    if not step_to(mu, set_at, br_pc):
                        return None
                    t = mu.reg_read(rn_reg)
                    if not (orig_lo <= t < orig_hi and self.in_text(t) and t % 4 == 0):
                        return None
                    seq.append(t)
            except uc.UcError:
                return None
            return seq

        return self._verify_dual(run_profile)

    def _jump_table_taken(self, br_pc, entry, end, orig_lo=None, orig_hi=None):
        """Form: cmp idx, #N ; b.ls/b.lo table ... table: <address math> ; br"""
        if orig_lo is None or orig_hi is None:
            orig_lo, orig_hi = entry, end
        # start of the block holding the br
        s = br_pc
        while s - 4 >= entry and br_pc - (s - 4) <= 4 * 12 and self.lift(s - 4).kind in ('op', 'asm'):
            s -= 4
        for g in range(entry, end, 4):
            w = self.word(g)
            if (w & 0xFF000010) != 0x54000000 or bits(w, 0, 4) not in (9, 3):
                continue
            t = g + sext(bits(w, 5, 19), 19) * 4
            if t != s:
                continue
            cw = self.word(g - 4) if g - 4 >= entry else 0
            if (cw & 0x7F80001F) != 0x7100001F or bits(cw, 22, 1):
                continue
            limit = bits(cw, 10, 12)
            count = limit + 1 if bits(w, 0, 4) == 9 else limit
            if count <= 0 or count > 4096:
                continue
            return self._emulate_table(br_pc, entry, end, s, bits(cw, 5, 5), count, s, orig_lo, orig_hi)
        return None

    def _jump_table_fallthrough(self, br_pc, entry, end, orig_lo=None, orig_hi=None):
        if orig_lo is None or orig_hi is None:
            orig_lo, orig_hi = entry, end
        rn_reg = self._reg_id(bits(self.word(br_pc), 5, 5))
        # find the guarding b.hi / b.hs and its cmp, scanning backwards
        a = br_pc - 4
        guard = cmp = None
        while a >= entry and br_pc - a <= 4 * 16:
            w = self.word(a)
            if (w & 0xFF000010) == 0x54000000 and bits(w, 0, 4) in (8, 2):
                guard = a
                break
            if self.lift(a).kind in ('b', 'bl', 'blr', 'br', 'ret', 'trap'):
                return None
            a -= 4
        if guard is None:
            return None
        a = guard - 4
        while a >= entry and guard - a <= 4 * 8:
            w = self.word(a)
            # SUBS (imm) with Rd = zr, i.e. CMP #imm
            if (w & 0x7F80001F) == 0x7100001F:
                cmp = a
                break
            if self.lift(a).kind != 'op':
                return None
            a -= 4
        if cmp is None:
            return None
        w = self.word(cmp)
        if bits(w, 22, 1):
            return None
        sf = bits(w, 31, 1)
        limit = bits(w, 10, 12)
        count = limit + 1 if bits(self.word(guard), 0, 4) == 8 else limit
        if count <= 0 or count > 4096:
            return None
        idx = bits(w, 5, 5)
        idx_reg_id = self._reg_id(idx)
        # window start: back from cmp to the first control-flow instruction
        start = cmp
        while start - 4 >= entry and cmp - (start - 4) <= 4 * 12 and self.lift(start - 4).kind in ('op', 'asm'):
            start -= 4
        orig_start = start
        idx_regs = self._index_regs(orig_start, cmp, idx)

        def step_to(mu, frm, to):
            # single-step: Unicorn's `until` is unreliable mid translation-block
            pc = frm
            for _ in range(64):
                if pc == to:
                    return True
                mu.emu_start(pc, 0, count=1)
                pc = mu.reg_read(A.UC_ARM64_REG_PC)
            return False

        def replay_adr(mu, upto):
            a2 = entry
            while a2 < upto:
                w2 = self.word(a2)
                if (w2 & 0x1F000000) == 0x10000000 or (w2 & 0xFF800000) == 0x91000000:
                    mu.emu_start(a2, 0, count=1)
                a2 += 4

        def run_mode(mode, fill_byte, reg_base, sp_val, step_reg):
            if mode == 'pre' and orig_start == cmp:
                return None
            seq = []
            try:
                for i in range(count):
                    mu = self._reset_scratch(fill_byte)
                    self._fill_regs(mu, reg_base, sp_val, step_reg)
                    if mode == 'normal':
                        replay_adr(mu, orig_start)
                        if not step_to(mu, orig_start, cmp):
                            return None
                        self._set_index(mu, idx_regs, i)
                    elif mode == 'pre':
                        replay_adr(mu, orig_start)
                        self._set_index(mu, idx_regs, i)
                        if not step_to(mu, orig_start, cmp):
                            return None
                        arrived = mu.reg_read(idx_reg_id)
                        if not sf:
                            arrived &= 0xFFFFFFFF
                        if arrived != i:
                            return None
                    else:
                        replay_adr(mu, cmp)
                        self._set_index(mu, idx_regs, i)
                    if not step_to(mu, cmp, br_pc):
                        return None
                    t = mu.reg_read(rn_reg)
                    if not (orig_lo <= t < orig_hi and self.in_text(t) and t % 4 == 0):
                        return None
                    seq.append(t)
            except uc.UcError:
                return None
            return seq

        for mode in ('normal', 'pre', 'cmp'):
            res = self._verify_dual(lambda fb, rb, spv, sr, m=mode: run_mode(m, fb, rb, spv, sr))
            if res is not None:
                return res
        return None

    # ------------------------------------------------------------ functions

    def computed_target(self, br_pc, entry):
        """True if the br register comes from address arithmetic (a table) rather than a load."""
        rn = bits(self.word(br_pc), 5, 5)
        a = br_pc - 4
        while a >= entry and br_pc - a <= 4 * 16:
            w = self.word(a)
            if self.lift(a).kind not in ('op', 'asm'):
                return False
            if (w & 31) == rn:
                if (w & 0x1F000000) == 0x0B000000:  # ADD/SUB (shifted or extended register)
                    return True
                if (w & 0x9F000000) == 0x10000000:  # ADR
                    return True
                return False
            a -= 4
        return False

    def scan(self, entry, end):
        """Collect labels and external entries referenced by a function."""
        labels, ext, tables = {entry}, set(), {}
        orig_lo, orig_hi = self.orig_range(entry, end)
        terminated = False
        for a in range(entry, end, 4):
            ins = self.lift(a)
            if ins.kind in ('b', 'bcond'):
                t = ins.target
                if entry <= t < end:
                    labels.add(t)
                elif self.in_text(t):
                    ext.add(t)
            elif ins.kind == 'bl':
                if self.in_text(ins.target):
                    ext.add(ins.target)
            elif ins.kind == 'br':
                jt = self.jump_table(a, entry, end, orig_lo, orig_hi)
                if jt:
                    tables[a] = jt
                    for t in jt:
                        if entry <= t < end:
                            labels.add(t)
                        elif self.in_text(t):
                            ext.add(t)
                    self.stats['jumptables'] += 1
                elif self.computed_target(a, entry):
                    # unresolved table jump: allow jumping to any instruction of the function
                    tables[a] = 'all'
                    self.stats['br_all_labels'] += 1
                else:
                    self.stats['br_dispatch'] += 1
            terminated = ins.kind in ('b', 'br', 'ret', 'trap')
        if not terminated and end < self.hi:
            ext.add(end)
        return labels, ext, tables

    def render_single(self, ins, a, jump, refs, generic_mode=False):
        """C for one lifted instruction (no batching); used for patch variants."""
        k = ins.kind
        if k == 'asm':
            return [emit_asm_batch([ins.asm])]
        if k in ('op', 'trap'):
            return [ins.code]
        if k == 'b':
            return [jump(ins.target)]
        if k == 'bcond':
            return [f'if ({ins.cond}) {{ {jump(ins.target)} }}']
        if k == 'bl':
            t = ins.target
            if self.in_text(t):
                refs.add(t)
                pfx = 'c->pc = 0; ' if generic_mode else ''
                return [f'{pfx}c->x[30] = {hx(a + 4)}; {fname(t)}(c);']
            return [f'c->x[30] = {hx(a + 4)}; c->pc = {hx(t)}; rt_dispatch(c);']
        if k == 'blr':
            return [f'c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; rt_dispatch(c);']
        if k == 'br':
            return [f'c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);']
        if k == 'ret':
            return ['return;']
        raise Unsupported(k)

    def emit_function(self, entry, end, labels, tables, hle, aliases=(), generic_mode=False, ft_unwind=None):
        out = [f'void {fname(entry)}(Cpu *c) {{']
        if entry in hle:
            out.append(f'    {sanitize(hle[entry])}(c);')
            out.append('}')
            return out
        out.append(f'    FN_ENTER({hx(entry)});')
        if generic_mode:
            out.append('    uint64_t _sp0 = c->sp;  // FRAME_INVARIANT')
            out.append('    (void)_sp0;')
        if ft_unwind:
            bl_pc, ret_pc, ovf_pc = ft_unwind
            labels = set(labels)
            labels.add(ovf_pc)
            out.append('    jmp_buf _ft_jb;')
            out.append('    void *_prev_ft_jb = rt_get_ft_jmp();')
            out.append('    uint64_t _prev_ft_ret = rt_get_ft_jmp_ret();')
            out.append(f'    rt_push_ft_jmp_at(&_ft_jb, {hx(ret_pc)});')
            out.append('    if (setjmp(_ft_jb) != 0) {')
            out.append('        rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret);')
            out.append(f'        goto L_{ovf_pc:x};')
            out.append('    }')
        if aliases:  # (merged byte-table span: thunks re-enter at these internal labels. The entry pc is
            out.append('    { uint64_t _pc = c->pc; c->pc = 0;')  # captured and cleared: a stale pc left by
            for b in aliases:                                      # an earlier dispatch must not re-trigger them
                out.append(f'    if (_pc == {hx(b)}) goto L_{b:x};')  # when the walk re-enters its own function)
            out.append('    }')
        batch = []
        refs = set()

        def flush():
            if batch:
                out.append('    ' + emit_asm_batch(batch))
                batch.clear()

        def jump(t):
            if entry <= t < end:
                return f'goto L_{t:x};'
            if not self.in_text(t):
                pfx = 'rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret); ' if ft_unwind else ''
                return f'{pfx}c->pc = {hx(t)}; __attribute__((musttail)) return rt_dispatch(c);'
            refs.add(t)
            pfx = 'rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret); ' if ft_unwind else ''
            if generic_mode:
                return f'{pfx}c->pc = 0; __attribute__((musttail)) return {fname(t)}(c);'
            return f'{pfx}__attribute__((musttail)) return {fname(t)}(c);'

        # Only enable all-instruction L_dispatch when at least one br has an unresolved 'all' table.
        # Resolved bounds-checked jump tables (jt) already include all targets in `labels` and fall back to rt_dispatch.
        all_labels = any(v == 'all' for v in tables.values())
        if all_labels:
            labels = set(range(entry, end, 4))
        rando_here = [a for a in range(entry, end, 4) if a - self.base in RANDO_CODE]
        if rando_here:
            labels = set(labels)
            for a in rando_here:
                for w in RANDO_CODE[a - self.base]:
                    v = self.lifter.lift(w, a)
                    if v.kind in ('b', 'bcond') and isinstance(v.target, int) and entry <= v.target < end:
                        labels.add(v.target)
        hooks_here = [a for a in range(entry, end, 4) if a in HOOKS]
        if hooks_here:  # (a hook's jump targets need their labels)
            labels = set(labels)
            for a in hooks_here:
                for t in HOOKS[a][1]:
                    if not entry <= t < end:
                        raise ValueError(f'hook {a:#x}: target {t:#x} is outside its function')
                    labels.add(t)
        # Sub-chunk gigantic generic functions (> 4000 insns) into 2000-insn static parts chained via
        # __attribute__((musttail)) so neither -O1 compile time nor -O0 RegAllocFast stack frames explode.
        if ((end - entry) // 4 > 4000 and generic_mode and not all_labels
                and not aliases and not ft_unwind and not rando_here and not hooks_here):
            return self._emit_split_function(entry, end, tables, refs)

        last = None
        for a in range(entry, end, 4):
            ins = self.lift(a)
            if a in labels:
                flush()
                out.append(f'L_{a:x}:;')
            if a - self.base in RANDO_CODE:  # randomizer patch site: the active seed picks the instruction
                flush()
                out.append(f'    {{ extern uint32_t rando_word(uint64_t); uint32_t rw = rando_word({hx(a)});')
                first = True
                for w in RANDO_CODE[a - self.base]:
                    v = self.lifter.lift(w, a)
                    out.append(f'    {"if" if first else "else if"} (rw == {hx(w)}) {{ ' + ' '.join(self.render_single(v, a, jump, refs)) + ' }')
                    first = False
                out.append('    else { ' + ' '.join(self.render_single(ins, a, jump, refs)) + ' } }')
                last = 'op'
                continue
            if a in HOOKS:  # hook site (tools/patches.py HOOKS): the runtime decides, the instruction stays as it is
                flush()
                word, targets, _ = HOOKS[a]
                w = struct.unpack_from('<I', self.nso.image, a - self.base)[0]
                if w != word:
                    raise ValueError(f'hook {a:#x}: instruction {w:#010x}, expected {word:#010x}')
                body = ' '.join(self.render_single(ins, a, jump, refs))
                jumps = ' '.join(f'else if (h_ == {k + 2}) goto L_{t:x};' for k, t in enumerate(targets))
                out.append(f'    {{ int h_ = rt_hook(c, {hx(a)}); if (h_ == 0) {{ {body} }} {jumps} }}'
                           f'  /* {a:#x}: hook (runtime/hle/hooks.c) */')
                last = 'op'
                continue
            if a in PATCHES:  # hand-written replacement for one instruction (tools/patches.py)
                flush()
                out.append('    ' + PATCHES[a](self, a))
                last = 'op'
                continue
            k = ins.kind
            if k == 'asm':
                if len(batch) >= 24:
                    flush()
                batch.append(ins.asm)
                try:
                    emit_asm_batch(batch)
                except Unsupported:
                    batch.pop()
                    flush()
                    batch.append(ins.asm)
                last = k
                continue
            flush()
            if k == 'op' or k == 'trap':
                out.append('    ' + ins.code)
            elif k == 'b':
                out.append('    ' + jump(ins.target))
            elif k == 'bcond':
                out.append(f'    if ({ins.cond}) {{ {jump(ins.target)} }}')
            elif k == 'bl':
                t = ins.target
                if self.in_text(t):
                    refs.add(t)
                    pfx = 'c->pc = 0; ' if generic_mode else ''
                    out.append(f'    {pfx}c->x[30] = {hx(a + 4)}; {fname(t)}(c);')
                else:
                    out.append(f'    c->x[30] = {hx(a + 4)}; c->pc = {hx(t)}; rt_dispatch(c);')
            elif k == 'blr':
                out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; rt_dispatch(c);')
            elif k == 'br':
                jt = tables.get(a)
                if jt == 'all':
                    out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    continue
                if jt:
                    cases = []
                    for t in jt:
                        if entry <= t < end:
                            cases.append(f'case {hx(t)}: goto L_{t:x};')
                        else:
                            refs.add(t)
                            pfx = 'rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret); ' if ft_unwind else ''
                            pc0 = 'c->pc = 0; ' if generic_mode else ''
                            cases.append(f'case {hx(t)}: {pfx}{pc0}__attribute__((musttail)) return {fname(t)}(c);')
                    out.append(f'    switch ({ins.target}) {{ {" ".join(cases)} default: break; }}')
                    if all_labels:
                        out.append(f'    c->pc = {ins.target}; goto L_dispatch;')
                    else:
                        if ft_unwind:
                            out.append('    rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret);')
                        out.append(f'    c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
                    continue
                if ft_unwind:
                    out.append('    rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret);')
                out.append(f'    c->pc = {ins.target}; __attribute__((musttail)) return rt_dispatch(c);')
            elif k == 'ret':
                if generic_mode:
                    out.append(f'    if (c->sp != _sp0) rt_frame_bad({hx(entry)}, {hx(a)}, _sp0, c->sp);')
                if ft_unwind:
                    out.append('    rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret);')
                out.append('    return;')
            last = k
        flush()
        if last not in ('b', 'br', 'ret', 'trap'):
            out.append('    ' + jump(end) if end < self.hi else f'    rt_udf(c, {hx(end)});')
        if all_labels:
            out.append('L_dispatch:')
            cases = ' '.join(f'case {hx(t)}: goto L_{t:x};' for t in range(entry, end, 4))
            out.append(f'    switch (c->pc) {{ {cases} default: break; }}')
            if ft_unwind:
                out.append('    rt_pop_ft_jmp_at(_prev_ft_jb, _prev_ft_ret);')
            out.append('    __attribute__((musttail)) return rt_dispatch(c);')
        # Hoist reused temporary locals once at function top so -O0 builds never explode host stack.
        hoist_idx = 2
        body_str = '\n'.join(out[hoist_idx:])
        u64_tmps = [v for v in ('_ea', '_v', '_a', '_b') if re.search(rf'\b{v}\b', body_str)]
        u32_tmps = [v for v in ('_st',) if re.search(rf'\b{v}\b', body_str)]
        u128_tmps = [v for v in ('_va', '_vb') if re.search(rf'\b{v}\b', body_str)]
        decls = []
        if u64_tmps:
            decls.append('    uint64_t ' + ', '.join(f'{v} = 0' for v in u64_tmps) + ';')
        if u32_tmps:
            decls.append('    uint32_t ' + ', '.join(f'{v} = 0' for v in u32_tmps) + ';')
        if u128_tmps:
            decls.append('    u128 ' + ', '.join(f'{v} = 0' for v in u128_tmps) + ';')
        if decls:
            out[hoist_idx:hoist_idx] = decls
        out.append('}')
        self._refs = refs
        return out

    def _emit_split_function(self, entry, end, tables, refs):
        step_bytes = 2000 * 4
        parts = [(lo, min(lo + step_bytes, end)) for lo in range(entry, end, step_bytes)]
        nparts = len(parts)
        fn_base = fname(entry)

        def part_of(t):
            return (t - entry) // step_bytes

        entry_targets = [set() for _ in range(nparts)]
        need_labels = [set() for _ in range(nparts)]
        entry_targets[0].add(entry)
        need_labels[0].add(entry)
        for pi, (p_lo, p_hi) in enumerate(parts):
            last_k = None
            for a in range(p_lo, p_hi, 4):
                ins = self.lift(a)
                last_k = ins.kind
                if ins.kind in ('b', 'bcond'):
                    t = ins.target
                    if entry <= t < end:
                        pj = part_of(t)
                        if pj != pi:
                            entry_targets[pj].add(t)
                        need_labels[pj].add(t)
                elif ins.kind == 'br':
                    jt = tables.get(a)
                    if jt and jt != 'all':
                        for t in jt:
                            if entry <= t < end:
                                pj = part_of(t)
                                if pj != pi:
                                    entry_targets[pj].add(t)
                                need_labels[pj].add(t)
            if last_k not in ('b', 'br', 'ret', 'trap') and pi + 1 < nparts:
                entry_targets[pi + 1].add(p_hi)
                need_labels[pi + 1].add(p_hi)

        out = [f'static void {fn_base}_p{pi}(Cpu *c, uint64_t _sp0);' for pi in range(nparts)]

        for pi, (p_lo, p_hi) in enumerate(parts):
            p_out = [f'static void {fn_base}_p{pi}(Cpu *c, uint64_t _sp0) {{', '    (void)_sp0;']
            batch = []

            def flush():
                if batch:
                    p_out.append('    ' + emit_asm_batch(batch))
                    batch.clear()

            def jump(t):
                if entry <= t < end:
                    pj = part_of(t)
                    if pj == pi:
                        return f'goto L_{t:x};'
                    return f'c->pc = {hx(t)}; __attribute__((musttail)) return {fn_base}_p{pj}(c, _sp0);'
                if not self.in_text(t):
                    return f'c->pc = {hx(t)}; rt_dispatch(c); return;'
                refs.add(t)
                return f'c->pc = 0; {fname(t)}(c); return;'

            targs = sorted(entry_targets[pi])
            if len(targs) > 1:
                cases = ' '.join(f'case {hx(t)}: goto L_{t:x};' for t in targs)
                p_out.append(f'    switch (c->pc) {{ {cases} default: rt_udf(c, c->pc); }}')
            elif len(targs) == 1 and targs[0] != p_lo:
                p_out.append(f'    goto L_{targs[0]:x};')

            last = None
            for a in range(p_lo, p_hi, 4):
                ins = self.lift(a)
                if a in need_labels[pi]:
                    flush()
                    p_out.append(f'L_{a:x}:;')
                if a in PATCHES:
                    flush()
                    p_out.append('    ' + PATCHES[a](self, a))
                    last = 'op'
                    continue
                k = ins.kind
                if k == 'asm':
                    if len(batch) >= 24:
                        flush()
                    batch.append(ins.asm)
                    try:
                        emit_asm_batch(batch)
                    except Unsupported:
                        batch.pop()
                        flush()
                        batch.append(ins.asm)
                    last = k
                    continue
                flush()
                if k == 'op' or k == 'trap':
                    p_out.append('    ' + ins.code)
                elif k == 'b':
                    p_out.append('    ' + jump(ins.target))
                elif k == 'bcond':
                    p_out.append(f'    if ({ins.cond}) {{ {jump(ins.target)} }}')
                elif k == 'bl':
                    t = ins.target
                    if self.in_text(t):
                        refs.add(t)
                        p_out.append(f'    c->pc = 0; c->x[30] = {hx(a + 4)}; {fname(t)}(c);')
                    else:
                        p_out.append(f'    c->x[30] = {hx(a + 4)}; c->pc = {hx(t)}; rt_dispatch(c);')
                elif k == 'blr':
                    p_out.append(f'    c->pc = {ins.target}; c->x[30] = {hx(a + 4)}; rt_dispatch(c);')
                elif k == 'br':
                    jt = tables.get(a)
                    if jt and jt != 'all':
                        cases = []
                        for t in jt:
                            if entry <= t < end:
                                pj = part_of(t)
                                if pj == pi:
                                    cases.append(f'case {hx(t)}: goto L_{t:x};')
                                else:
                                    cases.append(
                                        f'case {hx(t)}: c->pc = {hx(t)}; __attribute__((musttail)) return {fn_base}_p{pj}(c, _sp0);'
                                    )
                            else:
                                refs.add(t)
                                cases.append(f'case {hx(t)}: c->pc = 0; {fname(t)}(c); return;')
                        p_out.append(f'    switch ({ins.target}) {{ {" ".join(cases)} default: break; }}')
                    p_out.append(f'    c->pc = {ins.target}; rt_dispatch(c); return;')
                elif k == 'ret':
                    p_out.append(f'    if (c->sp != _sp0) rt_frame_bad({hx(entry)}, {hx(a)}, _sp0, c->sp);')
                    p_out.append('    return;')
                last = k
            flush()
            if last not in ('b', 'br', 'ret', 'trap'):
                nxt = p_hi
                if pi + 1 < nparts:
                    p_out.append(f'    c->pc = {hx(nxt)}; __attribute__((musttail)) return {fn_base}_p{pi + 1}(c, _sp0);')
                else:
                    p_out.append('    ' + (jump(end) if end < self.hi else f'rt_udf(c, {hx(end)});'))

            hoist_idx = 2
            body_str = '\n'.join(p_out[hoist_idx:])
            u64_tmps = [v for v in ('_ea', '_v', '_a', '_b') if re.search(rf'\b{v}\b', body_str)]
            u32_tmps = [v for v in ('_st',) if re.search(rf'\b{v}\b', body_str)]
            u128_tmps = [v for v in ('_va', '_vb') if re.search(rf'\b{v}\b', body_str)]
            decls = []
            if u64_tmps:
                decls.append('    uint64_t ' + ', '.join(f'{v} = 0' for v in u64_tmps) + ';')
            if u32_tmps:
                decls.append('    uint32_t ' + ', '.join(f'{v} = 0' for v in u32_tmps) + ';')
            if u128_tmps:
                decls.append('    u128 ' + ', '.join(f'{v} = 0' for v in u128_tmps) + ';')
            if decls:
                p_out[hoist_idx:hoist_idx] = decls
            p_out.append('}')
            out.extend(p_out)

        out.append(f'void {fn_base}(Cpu *c) {{')
        out.append(f'    FN_ENTER({hx(entry)});')
        out.append('    uint64_t _sp0 = c->sp;  // FRAME_INVARIANT')
        if len(entry_targets[0]) > 1:
            out.append(f'    c->pc = {hx(entry)};')
        out.append(f'    {fn_base}_p0(c, _sp0);')
        out.append('}')
        self._refs = refs
        return out


# tools/patches.py, for the main module only (its sites are main addresses): loaded in main() once the game version
# is known from the NSO's build ID (tools/game_addresses.py; an unknown build ID stops the generator)
HOOKS, PATCHES = {}, {}

import json as _json
import os as _os
_rv = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), 'rando', 'patch_variants.json')
RANDO_CODE = {}
if _os.path.exists(_rv):
    for _a, _ws in _json.load(open(_rv))['code'].items():
        RANDO_CODE[int(_a, 16)] = [int(_w, 16) for _w in _ws]


def write_if_changed(path, text):
    """Leaves unchanged chunks untouched so make only recompiles what a patch affected."""
    try:
        with open(path) as f:
            if f.read() == text:
                return
    except FileNotFoundError:
        pass
    with open(path, 'w') as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('nso')
    ap.add_argument('base', type=lambda s: int(s, 0))
    ap.add_argument('outdir')
    ap.add_argument('--hle')
    ap.add_argument('--chunk', type=int, default=1500)  # functions per file at most
    ap.add_argument('--chunk-bytes', type=int, default=1500000)  # and about this much C (even compile times)
    ap.add_argument('--big-fn', type=int, default=100000)  # bytes of C from which a function gets its own file
    ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--generic', action='store_true', help='Route A generic codegen (auto-enabled for v13_*)')
    ap.add_argument('--simd', choices=('asm', 'c'),
                    default='asm' if platform.machine().lower() in ('arm64', 'aarch64') else 'c')
    args = ap.parse_args()

    t0 = time.time()
    m = Module(args.name, args.nso, args.base, args.simd)
    version = None
    generic_mode = args.generic or (args.name != 'main')
    if args.name == 'main':
        global HOOKS, PATCHES
        import game_addresses
        try:
            version = game_addresses.select_build_id(m.nso.build_id)
        except game_addresses.UnknownGameVersion as e:
            if not args.generic:
                sys.exit(f'{args.name}: {e}')
        if version and version.startswith('v13_'):
            generic_mode = True
        if generic_mode:
            HOOKS, PATCHES = {}, {}
            lbl = game_addresses.VERSIONS[version][0] if version else 'generic'
            print(f'{args.name}: generic Route A generation for {lbl} (build {m.nso.build_id[:16]})', file=sys.stderr)
        else:
            import patches
            HOOKS, PATCHES = patches.HOOKS, patches.PATCHES
            label, build_id = game_addresses.VERSIONS[version]
            print(f'{args.name}: game version {label} (build {build_id[:16]}), address table {version}', file=sys.stderr)
    elif generic_mode:
        print(f'{args.name}: generic Route A generation (build {m.nso.build_id[:16]})', file=sys.stderr)

    ft_unwinds = m.detect_freetype_setjmp() if (args.name == 'main' and generic_mode) else {}
    for _e, (_bl, _ret, _ovf) in sorted(ft_unwinds.items()):
        print(f'{args.name}: freetype longjmp unwind @ {hx(_e)} (setjmp ret {hx(_ret)})', file=sys.stderr)

    starts = m.boundaries()
    hle = {}
    if args.hle and os.path.exists(args.hle):
        want = {l.strip() for l in open(args.hle) if l.strip() and not l.startswith('#')}
        for name, info, shndx, value, size in m.nso.syms:
            if shndx and name in want:
                hle[args.base + value] = name

    # Partition text into functions; any branch/call target outside its function becomes a new
    # boundary (splitting the containing function) until a fixpoint is reached.
    start_set = set(starts) | set(ft_unwinds.keys())
    for i, w in enumerate(m.words):
        if (w & 0xFC000000) == 0x94000000:
            t = m.lo + i * 4 + sext26(w) * 4
            if m.in_text(t):
                start_set.add(t)

    adrp_regs = {}
    table_spans = []
    if generic_mode:
        # Code addresses materialized in registers: ADRP followed shortly by ADD on the same register.
        # Route A: plain ADR is intra-function relative addressing and is never a separate entry boundary.
        for i, w in enumerate(m.words):
            pc = m.lo + i * 4
            if (w & 0x9F000000) == 0x90000000:  # ADRP
                imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
                if imm & (1 << 20):
                    imm -= 1 << 21
                adrp_regs[w & 31] = ((pc & ~0xFFF) + (imm << 12), i)
            elif (w & 0xFFC00000) == 0x91000000:  # ADD (imm, 64-bit, no shift)
                rn, rd = (w >> 5) & 31, w & 31
                if rn in adrp_regs and i - adrp_regs[rn][1] <= 8:
                    t = adrp_regs[rn][0] + ((w >> 10) & 0xFFF)
                    if m.in_text(t):
                        start_set.add(t)

        # Prologue linter: function entries preceded by a terminator that set up a stack frame
        def _is_term(x):
            return (x == 0xD65F03C0 or (x & 0xFC000000) == 0x14000000
                    or (x & 0xFFFFFC1F) in (0xD61F0000, 0xD63F0000) or x == 0xD4200000)

        def _frame_setup(x):
            if (x & 0xFFC00000) == 0xA9800000 and ((x >> 5) & 31) == 31:
                return True
            if (x & 0xFFE00C00) == 0xF8000C00 and ((x >> 5) & 31) == 31:
                return True
            if (x & 0xFF8003FF) == 0xD10003FF:
                return True
            return False

        prologue_added = []
        for i in range(1, len(m.words)):
            pc = m.lo + i * 4
            if pc not in start_set and _frame_setup(m.words[i]) and _is_term(m.words[i - 1]):
                start_set.add(pc)
                prologue_added.append(pc)
        if prologue_added:
            print(f'{args.name}: boundary linter: {len(prologue_added)} function entries found by prologue shape'
                  f' (e.g. {", ".join(hx(a) for a in prologue_added[:6])})', file=sys.stderr)
    else:
        for i, w in enumerate(m.words):
            pc = m.lo + i * 4
            if (w & 0x9F000000) == 0x10000000:  # ADR
                imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
                if imm & (1 << 20):
                    imm -= 1 << 21
                if m.in_text(pc + imm):
                    start_set.add(pc + imm)
            elif (w & 0x9F000000) == 0x90000000:  # ADRP
                imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
                if imm & (1 << 20):
                    imm -= 1 << 21
                adrp_regs[w & 31] = ((pc & ~0xFFF) + (imm << 12), i)
            elif (w & 0xFFC00000) == 0x91000000:  # ADD (imm, 64-bit, no shift)
                rn, rd = (w >> 5) & 31, w & 31
                if rn in adrp_regs and i - adrp_regs[rn][1] <= 8:
                    t = adrp_regs[rn][0] + ((w >> 10) & 0xFFF)
                    if m.in_text(t):
                        start_set.add(t)

        def table_count(at, words):
            for k in range(at - 1, max(at - 5, -1), -1):
                w = words[k]
                if (w & 0xFFC0001F) == 0x7100001F:  # cmp wI, #imm
                    imm = (w >> 10) & 0xFFF
                    if k + 1 < len(words) and (words[k + 1] & 0xFF000010) == 0x54000000:
                        if words[k + 1] & 0xF in (2, 8):
                            return imm + 1
                        return imm
            return 64

        def table_addr(at, words, k):
            for j in range(at - 1, max(at - 9, -1), -1):
                w = words[j]
                if (w & 0xFFC00000) == 0x91000000 and (w & 31) == k and ((w >> 5) & 31) == k:
                    imm = ((w >> 10) & 0xFFF)
                    w2 = words[j - 1]
                    if (w2 & 0x9F000000) == 0x90000000 and (w2 & 31) == k:
                        pimm = ((w2 >> 5) & 0x7FFFF) << 2 | ((w2 >> 29) & 3)
                        if pimm & (1 << 20):
                            pimm -= 1 << 21
                        pc = m.lo + (j - 1) * 4
                        return (pc & ~0xFFF) + (pimm << 12) + imm
            return None

        for i, w in enumerate(m.words):
            pc = m.lo + i * 4
            if (w & 0x9F000000) != 0x10000000:
                continue
            n = w & 31
            if n >= 29:
                continue
            imm = ((w >> 5) & 0x7FFFF) << 2 | ((w >> 29) & 3)
            if imm & (1 << 20):
                imm -= 1 << 21
            base = pc + imm
            if not m.in_text(base) or i + 3 >= len(m.words):
                continue
            j = None
            scale = 0
            for jj in range(i + 1, min(i + 6, len(m.words))):
                w1 = m.words[jj]
                if (w1 & 0xFFE00C00) == 0x38600800:
                    j, scale = jj, 0
                    break
                if (w1 & 0xFFE00C00) == 0x78600800:
                    j, scale = jj, 1
                    break
                if (w1 & 0xFFE00C00) == 0xB8600800:
                    j, scale = jj, 2
                    break
            if j is None or j + 1 >= len(m.words):
                continue
            w1 = m.words[j]
            w2 = m.words[j + 1]
            if (w2 & 0xFFC00000) != 0x8B000000:
                continue
            if (w2 & 31) != n or ((w2 >> 5) & 31) != n or ((w2 >> 10) & 0x3F) != 2 or ((w2 >> 16) & 31) != (w1 & 31):
                continue
            m2 = None
            for mm in range(j + 2, min(j + 7, len(m.words))):
                w3 = m.words[mm]
                if (w3 & 0xFFFFFC1F) == 0xD61F0000 and ((w3 >> 5) & 31) == n:
                    m2 = mm
                    break
            if m2 is None:
                continue
            span_end = base + 0x400
            if scale:
                ta = table_addr(i, m.words, (w1 >> 5) & 31)
                cnt = table_count(i, m.words)
                if ta is not None and cnt <= 256:
                    lo = 1 << 31
                    hi = 0
                    import struct as _st
                    for e in range(cnt):
                        img_off = ta - m.base + e * (2 if scale == 1 else 4)
                        v = _st.unpack_from('<H' if scale == 1 else '<I', m.nso.image, img_off)[0]
                        s = base + 4 * (v & 0xFFFF if scale == 1 else v)
                        if m.in_text(s):
                            lo = min(lo, s)
                            hi = max(hi, s + 4)
                    if hi > lo:
                        span_end = max(min(hi, base + 0x2000), base + 0x10)
            table_spans.append((pc, base, min(max(span_end, pc + 4), m.hi)))

    scanned = {}
    rounds = 0
    while True:
        rounds += 1
        order = sorted(start_set)
        new = set()
        for i, a in enumerate(order):
            end = order[i + 1] if i + 1 < len(order) else m.hi
            key = (a, end)
            if key not in scanned:
                scanned[key] = m.scan(a, end)
            new |= scanned[key][1] - start_set
        if not new:
            break
        start_set |= new

    aliases = {}
    merged_aliases = {}
    if not generic_mode:
        spans = sorted(table_spans)
        merged_spans = []
        for br_pc, base, e in spans:
            prev = [a for a in order if a <= br_pc]
            s = prev[-1] if prev else br_pc
            if merged_spans and s <= merged_spans[-1][1]:
                merged_spans[-1] = (merged_spans[-1][0], max(merged_spans[-1][1], e))
            else:
                merged_spans.append((s, e))
        for s, e in merged_spans:
            for b in order:
                if s < b < e:
                    aliases[b] = s
        for b, a in aliases.items():
            merged_aliases.setdefault(a, []).append(b)
        order = [a for a in order if a not in aliases]
    else:
        order = sorted(start_set)

    entries = {}
    for i, a in enumerate(order):
        end = order[i + 1] if i + 1 < len(order) else m.hi
        key = (a, end)
        if key not in scanned:
            scanned[key] = m.scan(a, end)
        labels, ext, tables = scanned[key]
        if a in merged_aliases:
            labels = set(labels) | set(merged_aliases[a])
        entries[a] = (end, labels, tables)
    print(f'{args.name}: {len(starts)} boundaries, {len(entries)} functions (+{len(aliases)} thunks), {rounds} rounds, '
          f'{dict(m.stats)}, {time.time() - t0:.0f}s', file=sys.stderr)

    os.makedirs(args.outdir, exist_ok=True)
    order = sorted(entries)
    if args.limit:
        order = order[:args.limit]
    nchunks = 0
    ci = 0
    pending = None  # (address, lines, refs) of a big function that starts the next file
    while ci < len(order) or pending:
        body, refs, size, fns = [], set(), 0, []
        while (pending or ci < len(order)) and len(fns) < args.chunk and size < args.chunk_bytes:
            if pending:
                a, lines, r = pending
                pending = None
            else:
                a = order[ci]
                ci += 1
                end, labels, tables = entries[a]
                lines = m.emit_function(a, end, labels, tables, hle,
                                        aliases=merged_aliases.get(a, ()),
                                        generic_mode=generic_mode,
                                        ft_unwind=ft_unwinds.get(a))
                r = getattr(m, '_refs', set())
                m._refs = set()
            n = sum(len(l) + 1 for l in lines)
            if n > args.big_fn and fns:  # (a big function gets a file of its own: they compile in parallel)
                pending = (a, lines, r)
                break
            body += lines
            size += n
            fns.append(a)
            refs |= r
            if n > args.big_fn:
                break
        path = os.path.join(args.outdir, f'{args.name}_{nchunks:03d}.c')
        text = ['#include "cpu.h"\n' + ('#include "simd.h"\n' if m.lifter.sc else '')]
        text += [f'void {fname(r)}(Cpu *c);\n' for r in sorted(refs)]
        text += [f'void {sanitize(hle[a])}(Cpu *c);\n' for a in sorted(set(hle) & set(fns))]
        text += ['\n'.join(body), '\n']
        write_if_changed(path, ''.join(text))
        nchunks += 1
        # (the start screen's setup reads these for its progress bar: runtime/gamebuild.c)
        print(f'{args.name}: progress {ci}/{len(order)} files {nchunks}', file=sys.stderr, flush=True)
    for fn in os.listdir(args.outdir):  # an earlier run's extra chunks
        mm = re.fullmatch(re.escape(args.name) + r'_(\d{3})\.c', fn)
        if mm and int(mm.group(1)) >= nchunks:
            os.remove(os.path.join(args.outdir, fn))
    import io
    thunk_path = os.path.join(args.outdir, f'{args.name}_thunk.c')
    if aliases:  # a thunk per merged-span boundary: re-enters the merged function at its label
        thunk_lines = ['#include "cpu.h"\n']
        thunk_lines += [f'void {fname(a)}(Cpu *c);\n' for a in sorted(set(aliases.values()))]
        thunk_lines.append('\n')
        for b in sorted(aliases):
            thunk_lines.append(f'void {fname(b)}(Cpu *c) {{\n'
                               f'    c->pc = {hx(b)};\n'
                               f'    __attribute__((musttail)) return {fname(aliases[b])}(c);\n'
                               f'}}\n')
        write_if_changed(thunk_path, ''.join(thunk_lines))
    elif os.path.exists(thunk_path):
        write_if_changed(thunk_path, '#include "cpu.h"\n// Route A: No thunks needed.\n')
    all_addrs = sorted(set(order) | set(aliases))  # the table keeps every original boundary: the
    with io.StringIO() as f:                       #  removed ones map to their thunk
        f.write('#include "cpu.h"\n')
        for a in all_addrs:
            f.write(f'void {fname(a)}(Cpu *c);\n')
        f.write(f'const uint64_t {args.name}_fn_addrs[] = {{\n')
        f.write(',\n'.join(hx(a) for a in all_addrs))
        f.write('};\n')
        f.write(f'const GuestFn {args.name}_fn_ptrs[] = {{\n')
        f.write(',\n'.join(fname(a) for a in all_addrs))
        f.write('};\n')
        f.write(f'const uint64_t {args.name}_fn_count = {len(all_addrs)};\n')
        write_if_changed(os.path.join(args.outdir, f'{args.name}_table.c'), f.getvalue())
    if args.name == 'main':  # (what this code was generated from: the Makefile's game code library id, GAMELIB_BUILD_ID)
        bid = game_addresses.VERSIONS[version][1] if version else m.nso.build_id.upper()
        write_if_changed(os.path.join(args.outdir, 'main_build_id.txt'), bid + '\n')
    print(f'{args.name}: wrote {nchunks} chunks, {time.time() - t0:.0f}s total', file=sys.stderr)


if __name__ == '__main__':
    main()
