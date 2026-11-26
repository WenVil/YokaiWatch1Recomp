"""Read the supported game-version list shared with the C/C++ runtime.

runtime/game_addresses/versions.h lists the supported game versions by the main program's build ID.
An unknown build ID is refused: there is no fallback table.
"""
from pathlib import Path
import re
import sys
from types import SimpleNamespace


class UnknownGameVersion(Exception):
    pass


def load_versions(path):
    """{key: (label, build ID as 40 upper-case hex digits)} from versions.h, in file order."""
    versions = {}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.startswith('GA_VERSION'):
            continue
        match = re.fullmatch(r'GA_VERSION\((v[A-Za-z0-9_]+),\s+"([^"]+)",\s+"([0-9A-F]{40})"\)', line)
        if not match or match[1] in versions:
            raise ValueError(f'Invalid or duplicate game version: {line}')
        versions[match[1]] = (match[2], match[3])
    if not versions:
        raise ValueError('No game versions')
    return versions


_root = Path(sys._MEIPASS) if hasattr(sys, '_MEIPASS') else Path(__file__).resolve().parent.parent
TABLE_DIR = _root / 'runtime/game_addresses'
VERSIONS = load_versions(TABLE_DIR / 'versions.h')


def version_for_build_id(build_id):
    """The version key for a main build ID (hex, any case; Nso.build_id strips trailing zero bytes)."""
    want = build_id.upper()
    padded = want.ljust(64, '0')  # (the header's 32-byte field: a 20-byte ID, zero-filled)
    for key, (label, bid) in VERSIONS.items():
        if padded[:40] == bid and not padded[40:].strip('0'):
            return key
    supported = ', '.join(f'{label} ({bid[:16]})' for label, bid in VERSIONS.values())
    raise UnknownGameVersion(f'These game files are an unsupported version of the game (main build ID '
                             f'{want[:16] or "missing"}). Supported: {supported}.')


ADDRESS = SimpleNamespace()
SELECTED = None


def select_version(key):
    """Select version key (Yo-kai Watch 1 uses Route A generic codegen)."""
    global SELECTED
    if key not in VERSIONS:
        raise UnknownGameVersion(f'Unknown game version {key}')
    ADDRESS.__dict__.clear()
    SELECTED = key
    return key


def select_build_id(build_id):
    return select_version(version_for_build_id(build_id))


YW1_TITLE_ID = 0x0100C0000CEEA000


def npdm_title_id(npdm_path):
    """Read ACI0 Program/Title ID (little-endian uint64) from exefs/main.npdm, or 0 on error."""
    try:
        with open(npdm_path, 'rb') as f:
            hdr = f.read(0x80)
            if len(hdr) < 0x80 or hdr[:4] != b'META':
                return 0
            aci0_off = int.from_bytes(hdr[0x70:0x74], 'little')
            if aci0_off < 0x80 or aci0_off > 0x100000:
                return 0
            f.seek(aci0_off)
            aci0 = f.read(0x20)
            if len(aci0) < 0x18 or aci0[:4] != b'ACI0':
                return 0
            return int.from_bytes(aci0[0x10:0x18], 'little')
    except OSError:
        return 0


def select_for_nso(build_id, nso_path=None):
    """Select version by build ID, or universal Yo-kai Watch 1 (Title ID 0100C0000CEEA000)."""
    global SELECTED
    try:
        return select_build_id(build_id)
    except UnknownGameVersion:
        if nso_path:
            npdm = Path(nso_path).resolve().parent / 'main.npdm'
            if npdm_title_id(npdm) == YW1_TITLE_ID:
                ADDRESS.__dict__.clear()
                SELECTED = 'vyw1_universal'
                return SELECTED
        raise


def addr_text(code):
    """Expand @NAME@ as hex digits inside generated-C addresses, symbols or labels."""
    return re.sub(r'@([A-Z][A-Z0-9_]*)@', lambda m: f'{getattr(ADDRESS, m[1]):x}', code)
