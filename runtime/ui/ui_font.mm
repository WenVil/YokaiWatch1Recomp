#include "ui_font.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imgui.h"
#include "ui_i18n.h"

extern "C" {
#include "../paths.h"
#include "../platform.h"
#include "../settings.h"
}

namespace {

static inline uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static inline uint32_t rd_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

struct FontCmap {
    const uint8_t *data = nullptr;
    size_t size = 0;
    std::vector<size_t> fmt4_subs;
    std::vector<size_t> fmt12_subs;
};

static bool parse_font_cmap(const std::vector<unsigned char> &bytes, int font_no, FontCmap &out) {
    out.data = bytes.data();
    out.size = bytes.size();
    out.fmt4_subs.clear();
    out.fmt12_subs.clear();
    if (out.size < 16) return false;

    size_t sfnt_off = 0;
    if (memcmp(out.data, "ttcf", 4) == 0) {
        uint32_t nf = rd_u32(out.data + 8);
        if ((uint32_t)font_no >= nf || 12 + (size_t)font_no * 4 + 4 > out.size) return false;
        sfnt_off = rd_u32(out.data + 12 + font_no * 4);
    }
    if (sfnt_off + 12 > out.size) return false;
    uint16_t num_tables = rd_u16(out.data + sfnt_off + 4);
    if (sfnt_off + 12 + (size_t)num_tables * 16 > out.size) return false;

    size_t cmap_off = 0;
    for (uint16_t i = 0; i < num_tables; i++) {
        const uint8_t *rec = out.data + sfnt_off + 12 + (size_t)i * 16;
        if (memcmp(rec, "cmap", 4) == 0) {
            cmap_off = rd_u32(rec + 8);
            break;
        }
    }
    if (!cmap_off || cmap_off + 4 > out.size) return false;

    uint16_t nsub = rd_u16(out.data + cmap_off + 2);
    if (cmap_off + 4 + (size_t)nsub * 8 > out.size) return false;

    for (uint16_t i = 0; i < nsub; i++) {
        const uint8_t *rec = out.data + cmap_off + 4 + (size_t)i * 8;
        uint16_t pid = rd_u16(rec);
        uint16_t eid = rd_u16(rec + 2);
        uint32_t sub_rel = rd_u32(rec + 4);
        // Accept Unicode (pid=0) and Windows Unicode BMP/Full (pid=3, eid=1 or 10)
        if (!(pid == 0 || (pid == 3 && (eid == 1 || eid == 10)))) continue;
        size_t st = cmap_off + sub_rel;
        if (st + 4 > out.size) continue;
        uint16_t fmt = rd_u16(out.data + st);
        if (fmt == 4 && st + 16 <= out.size) out.fmt4_subs.push_back(st);
        else if (fmt == 12 && st + 16 <= out.size) out.fmt12_subs.push_back(st);
    }
    return !out.fmt4_subs.empty() || !out.fmt12_subs.empty();
}

static bool cmap_fmt4_has(const FontCmap &cm, size_t st, uint32_t cp) {
    if (cp > 0xFFFFu) return false;
    uint16_t seg_count = rd_u16(cm.data + st + 6) / 2;
    size_t end_off = st + 14;
    size_t start_off = end_off + (size_t)seg_count * 2 + 2;
    size_t delta_off = start_off + (size_t)seg_count * 2;
    size_t roff_off = delta_off + (size_t)seg_count * 2;
    if (roff_off + (size_t)seg_count * 2 > cm.size) return false;

    uint16_t lo = 0, hi = seg_count;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        uint16_t ec = rd_u16(cm.data + end_off + mid * 2);
        if (ec < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= seg_count) return false;
    uint16_t sc = rd_u16(cm.data + start_off + lo * 2);
    uint16_t ec = rd_u16(cm.data + end_off + lo * 2);
    if (cp < sc || cp > ec || sc == 0xFFFFu) return false;

    uint16_t roff = rd_u16(cm.data + roff_off + lo * 2);
    if (roff == 0) {
        uint16_t delta = rd_u16(cm.data + delta_off + lo * 2);
        return ((cp + delta) & 0xFFFFu) != 0;
    }
    size_t g_pos = roff_off + lo * 2 + roff + (cp - sc) * 2;
    if (g_pos + 2 > cm.size) return false;
    uint16_t gid = rd_u16(cm.data + g_pos);
    return gid != 0;
}

static bool cmap_fmt12_has(const FontCmap &cm, size_t st, uint32_t cp) {
    uint32_t ngrp = rd_u32(cm.data + st + 12);
    size_t grp_off = st + 16;
    if (grp_off + (size_t)ngrp * 12 > cm.size) return false;
    uint32_t lo = 0, hi = ngrp;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *g = cm.data + grp_off + (size_t)mid * 12;
        uint32_t ec = rd_u32(g + 4);
        if (ec < cp) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= ngrp) return false;
    const uint8_t *g = cm.data + grp_off + (size_t)lo * 12;
    uint32_t sc = rd_u32(g);
    uint32_t ec = rd_u32(g + 4);
    uint32_t sid = rd_u32(g + 8);
    if (cp < sc || cp > ec) return false;
    return (sid + (cp - sc)) != 0;
}

static bool cmap_has_cp(const FontCmap &cm, uint32_t cp) {
    for (size_t st : cm.fmt12_subs)
        if (cmap_fmt12_has(cm, st, cp)) return true;
    for (size_t st : cm.fmt4_subs)
        if (cmap_fmt4_has(cm, st, cp)) return true;
    return false;
}

static bool ends_with_ci(const std::string &s, const char *suf) {
    size_t n = strlen(suf);
    if (s.size() < n) return false;
    const char *p = s.c_str() + s.size() - n;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)suf[i])) return false;
    return true;
}

static std::string norm_key(const std::string &p) {
    std::string k = p;
    for (char &c : k) c = (char)(c == '\\' ? '/' : tolower((unsigned char)c));
    return k;
}

static bool read_file_bytes(const char *path, std::vector<unsigned char> &out) {
    if (!path || !*path) return false;
    FILE *f = plat_fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 1024 || sz > (64L << 20) || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)sz);
    size_t got = fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        out.clear();
        return false;
    }
    return true;
}

template <typename Fn>
static void for_each_utf8_cp(const char *utf8, Fn &&fn) {
    if (!utf8) return;
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t cp = 0;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0 && p[1]) {
            cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            cp = ((*p & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 4;
        } else {
            p++;
            continue;
        }
        if (cp >= 0x20 && cp <= 0xFFFF) fn(cp);
    }
}

static void collect_needed_codepoints(const char *extra_text, std::vector<uint32_t> &out_cps) {
    std::set<uint32_t> s;
    // Basic Latin + Latin-1 Supplement
    for (uint32_t cp = 0x0020; cp <= 0x00FF; cp++) {
        if (cp >= 0x007F && cp <= 0x009F) continue;
        s.insert(cp);
    }
    // Required UI & navigation symbols
    static const uint32_t k_syms[] = {
        0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
        0x2039, 0x203A, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194,
        0x25B6, 0x25B8, 0x25C0, 0x2713
    };
    for (uint32_t cp : k_syms) s.insert(cp);

    auto add_str = [&](const char *str) {
        for_each_utf8_cp(str, [&](uint32_t cp) { s.insert(cp); });
    };
    add_str(ui_i18n_all_languages_corpus());
    if (g_settings.game_dir[0]) add_str(g_settings.game_dir);
    if (const char *dr = rt_data_root(); dr && *dr) add_str(dr);
    if (extra_text && *extra_text) add_str(extra_text);

    out_cps.assign(s.begin(), s.end());
}

static void collect_candidates(std::vector<std::string> &paths, int ui_lang, const char *env_override) {
    std::unordered_set<std::string> seen;
    auto push = [&](const std::string &p) {
        if (p.empty()) return;
        std::string k = norm_key(p);
        if (seen.insert(k).second) paths.push_back(p);
    };

    if (env_override && *env_override) push(env_override);
    if (const char *e = getenv("LARECOMP_MENU_FONT"); e && *e) push(e);

    if (PlatDir *d = plat_opendir("fonts")) {
        char name[512];
        bool is_d = false;
        while (plat_readdir(d, name, sizeof name, &is_d, nullptr)) {
            if (is_d || name[0] == '.') continue;
            std::string n = name;
            if (ends_with_ci(n, ".ttc") || ends_with_ci(n, ".ttf") || ends_with_ci(n, ".otf"))
                push("fonts/" + n);
        }
        plat_closedir(d);
    }

#ifdef _WIN32
    // Primary font for active UI language first
    switch (ui_lang) {
    case UI_LANG_ZH_HANT:
        push("C:/Windows/Fonts/msjh.ttc");
        push("C:/Windows/Fonts/msyh.ttc");
        break;
    case UI_LANG_JA:
        push("C:/Windows/Fonts/YuGothM.ttc");
        push("C:/Windows/Fonts/meiryo.ttc");
        push("C:/Windows/Fonts/msgothic.ttc");
        break;
    case UI_LANG_KO:
        push("C:/Windows/Fonts/malgun.ttf");
        break;
    case UI_LANG_EN:
        push("C:/Windows/Fonts/segoeui.ttf");
        break;
    case UI_LANG_ZH_HANS:
    default:
        push("C:/Windows/Fonts/msyh.ttc");
        push("C:/Windows/Fonts/simhei.ttf");
        break;
    }
    // Cross-language fallback chain (each contributes only missing codepoints via MergeMode=true)
    push("C:/Windows/Fonts/msyh.ttc");
    push("C:/Windows/Fonts/msjh.ttc");
    push("C:/Windows/Fonts/YuGothM.ttc");
    push("C:/Windows/Fonts/meiryo.ttc");
    push("C:/Windows/Fonts/msgothic.ttc");
    push("C:/Windows/Fonts/malgun.ttf");
    push("C:/Windows/Fonts/simhei.ttf");
    push("C:/Windows/Fonts/Deng.ttf");
    push("C:/Windows/Fonts/seguisym.ttf");
    push("C:/Windows/Fonts/segoeui.ttf");
#elif defined(__APPLE__)
    switch (ui_lang) {
    case UI_LANG_KO:
        push("/System/Library/Fonts/AppleSDGothicNeo.ttc");
        break;
    case UI_LANG_JA:
        push("/System/Library/Fonts/Hiragino Sans GB.ttc");
        break;
    default:
        break;
    }
    push("/System/Library/Fonts/PingFang.ttc");
    push("/System/Library/Fonts/Hiragino Sans GB.ttc");
    push("/System/Library/Fonts/STHeiti Medium.ttc");
    push("/System/Library/Fonts/AppleSDGothicNeo.ttc");
#else
    push("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    push("/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");
    push("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc");
#endif
    char res[1024];
    if (rt_resource("DejaVuSans.ttf", "third_party/fonts/DejaVu/DejaVuSans.ttf", res, sizeof res)) push(res);
    if (rt_resource("Baloo2-SemiBold.ttf", "third_party/fonts/Baloo2/Baloo2-SemiBold.ttf", res, sizeof res)) push(res);
}

struct FontPiece {
    std::string path;
    std::vector<unsigned char> bytes;
    const ImWchar *ranges = nullptr;
};

static ImFont *add_piece(ImFontAtlas *atlas, const FontPiece &piece, float size_px, bool merge_mode) {
    float px = floorf(size_px + 0.5f);
    if (piece.bytes.empty() || !piece.ranges || !piece.ranges[0] || px < 8.0f) return nullptr;
    void *copy = IM_ALLOC(piece.bytes.size());
    if (!copy) return nullptr;
    memcpy(copy, piece.bytes.data(), piece.bytes.size());
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    cfg.FontNo = 0;
    cfg.MergeMode = merge_mode;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    ImFont *f = atlas->AddFontFromMemoryTTF(copy, (int)piece.bytes.size(), px, &cfg, piece.ranges);
    if (!f) IM_FREE(copy);
    return f;
}

static int verify_font_coverage(ImFont *font, const char *extra_text) {
    if (!font) return -1;
    std::set<uint32_t> checked;
    std::set<uint32_t> missing;
    auto check_str = [&](const char *str) {
        for_each_utf8_cp(str, [&](uint32_t cp) {
            if (!checked.insert(cp).second) return;
            if (!font->FindGlyphNoFallback((ImWchar)cp)) missing.insert(cp);
        });
    };

    struct CheckCtx {
        decltype(&check_str) fn;
    };
    ui_i18n_for_each_text([](int, const char *utf8, void *user) {
        auto *f = (decltype(check_str) *)user;
        (*f)(utf8);
    }, &check_str);

    if (g_settings.game_dir[0]) check_str(g_settings.game_dir);
    if (const char *dr = rt_data_root(); dr && *dr) check_str(dr);
    if (extra_text && *extra_text) check_str(extra_text);

    for (uint32_t cp : missing) {
        fprintf(stderr, "[ui-font] MISSING glyph U+%04X\n", cp);
    }
    return (int)missing.size();
}

}  // namespace

void ui_load_i18n_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                        float sz_small, float sz_menu, float sz_big,
                        int ui_lang, const char *extra_text, const char *env_override) {
    ImGuiIO &io = ImGui::GetIO();
    io.FontDefault = nullptr;
    io.Fonts->Clear();
    io.Fonts->TexDesiredWidth = 2048;

    static std::unordered_map<ImFontAtlas *, std::deque<ImVector<ImWchar>>> s_keep_ranges_map;
    std::deque<ImVector<ImWchar>> &s_keep_ranges = s_keep_ranges_map[io.Fonts];
    s_keep_ranges.clear();

    std::vector<uint32_t> remaining;
    collect_needed_codepoints(extra_text, remaining);

    std::vector<std::string> cands;
    collect_candidates(cands, ui_lang, env_override);

    std::vector<FontPiece> pieces;
    for (const std::string &p : cands) {
        if (remaining.empty()) break;
        std::vector<unsigned char> bytes;
        if (!read_file_bytes(p.c_str(), bytes)) continue;
        FontCmap cm;
        if (!parse_font_cmap(bytes, 0, cm)) continue;

        std::vector<uint32_t> matched, unmatched;
        matched.reserve(remaining.size());
        unmatched.reserve(remaining.size());
        for (uint32_t cp : remaining) {
            if (cmap_has_cp(cm, cp)) matched.push_back(cp);
            else unmatched.push_back(cp);
        }
        if (matched.empty()) continue;

        // First font becomes Primary: require it to cover printable ASCII letters so numbers/Latin use Primary
        if (pieces.empty() && !cmap_has_cp(cm, 'A')) continue;

        ImFontGlyphRangesBuilder b;
        for (uint32_t cp : matched) b.AddChar((ImWchar)cp);
        s_keep_ranges.emplace_back();
        b.BuildRanges(&s_keep_ranges.back());

        FontPiece piece;
        piece.path = p;
        piece.bytes = std::move(bytes);
        piece.ranges = s_keep_ranges.back().Data;
        pieces.push_back(std::move(piece));
        remaining.swap(unmatched);
    }

    auto build_size = [&](float px) -> ImFont * {
        if (pieces.empty()) return nullptr;
        ImFont *base = add_piece(io.Fonts, pieces[0], px, false);
        if (!base) return nullptr;
        for (size_t i = 1; i < pieces.size(); i++) add_piece(io.Fonts, pieces[i], px, true);
        return base;
    };

    ImFont *f_small = build_size(sz_small);
    ImFont *f_menu = (fabsf(sz_menu - sz_small) < 0.5f) ? f_small : build_size(sz_menu);
    ImFont *f_big = (fabsf(sz_big - sz_menu) < 0.5f) ? f_menu
                  : (fabsf(sz_big - sz_small) < 0.5f) ? f_small
                                                      : build_size(sz_big);

    if (!f_small) {
        fprintf(stderr, "[ui-font] warning: no usable system font found, falling back to ImGui default\n");
        f_small = io.Fonts->AddFontDefault();
        f_menu = f_small;
        f_big = f_small;
    }
    if (!f_menu) f_menu = f_small;
    if (!f_big) f_big = f_menu;

    io.Fonts->Build();
    int miss_cnt = verify_font_coverage(f_small, extra_text);
    fprintf(stderr, "[ui-font] lang=%s atlas=%dx%d glyphs=%d pieces=%zu primary=%s missing=%d\n",
            ui_i18n_lang_name(ui_lang), io.Fonts->TexWidth, io.Fonts->TexHeight,
            f_small->Glyphs.Size, pieces.size(),
            pieces.empty() ? "default" : pieces[0].path.c_str(), miss_cnt);

    if (out_small) *out_small = f_small;
    if (out_menu) *out_menu = f_menu;
    if (out_big) *out_big = f_big;
}

void ui_load_cjk_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                       float sz_small, float sz_menu, float sz_big,
                       const char *env_override) {
    ui_load_i18n_fonts(out_small, out_menu, out_big, sz_small, sz_menu, sz_big,
                       ui_resolve_lang(), nullptr, env_override);
}

bool ui_font_text_missing(ImFont *font, const char *utf8) {
    if (!font || !utf8 || !*utf8) return false;
    bool miss = false;
    for_each_utf8_cp(utf8, [&](uint32_t cp) {
        if (!miss && !font->FindGlyphNoFallback((ImWchar)cp)) miss = true;
    });
    return miss;
}

void ui_apply_sentinel_theme(float style_scale) {
    ImGuiStyle &st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 10.0f;
    st.ChildRounding = 10.0f;
    st.FrameRounding = 7.0f;
    st.PopupRounding = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.GrabRounding = 6.0f;
    st.TabRounding = 7.0f;
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 1.0f;
    st.WindowPadding = ImVec2(20, 18);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(10, 8);
    st.CellPadding = ImVec2(8, 6);
    st.ScrollbarSize = 13.0f;

    ImVec4 *c = st.Colors;
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.95f, 0.98f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.64f, 0.72f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.082f, 0.120f, 0.97f);
    c[ImGuiCol_ChildBg] = ImVec4(0.082f, 0.118f, 0.168f, 0.94f);
    c[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.105f, 0.155f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.34f, 0.45f, 0.65f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.165f, 0.235f, 0.95f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.24f, 0.34f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.19f, 0.31f, 0.44f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.08f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.13f, 0.19f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.32f, 0.90f, 0.96f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.76f, 0.88f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.78f, 0.34f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.13f, 0.21f, 0.30f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.32f, 0.44f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.42f, 0.56f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.16f, 0.28f, 0.38f, 0.90f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.36f, 0.48f, 0.95f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.44f, 0.58f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.36f, 0.48f, 0.65f);
    c[ImGuiCol_Tab] = ImVec4(0.10f, 0.15f, 0.22f, 0.95f);
    c[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.32f, 0.44f, 1.00f);
    c[ImGuiCol_TabSelected] = ImVec4(0.16f, 0.36f, 0.48f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0.30f, 0.88f, 0.96f, 1.00f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.11f, 0.17f, 0.24f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.32f, 0.42f, 0.45f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.07f, 0.10f, 0.15f, 0.35f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.10f, 0.14f, 0.20f, 0.35f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.08f, 0.12f, 0.60f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.34f, 0.44f, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.48f, 0.62f, 1.00f);
    c[ImGuiCol_NavCursor] = ImVec4(0.28f, 0.88f, 0.96f, 1.00f);

    if (style_scale > 0.0f && fabsf(style_scale - 1.0f) > 0.01f) {
        st.ScaleAllSizes(style_scale);
    }
}

void ui_draw_loading_visual(double elapsed_sec, int stage, const char *status_text,
                            float sc, ImFont *font_title, ImFont *font_body) {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 ds = io.DisplaySize;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();

    dl->AddRectFilledMultiColor(ImVec2(0, 0), ds,
                                IM_COL32(8, 14, 24, 255), IM_COL32(14, 28, 42, 255),
                                IM_COL32(8, 13, 20, 255), IM_COL32(5, 8, 14, 255));

    float cx = ds.x * 0.5f;
    float cy = ds.y * 0.42f;
    float r_outer = 96.0f * sc;
    float r_mid = 74.0f * sc;
    float r_inner = 50.0f * sc;

    float pulse = 0.5f + 0.5f * sinf((float)elapsed_sec * 3.0f);
    dl->AddCircleFilled(ImVec2(cx, cy), r_outer * (1.18f + 0.06f * pulse),
                        IM_COL32(36, 175, 210, (int)(16 + 14 * pulse)), 48);

    float a0 = (float)elapsed_sec * 1.45f;
    for (int i = 0; i < 3; i++) {
        float sa = a0 + i * (6.2831853f / 3.0f);
        dl->PathArcTo(ImVec2(cx, cy), r_outer, sa, sa + 1.35f, 28);
        dl->PathStroke(IM_COL32(72, 224, 246, 220), 0, 3.5f * sc);
    }

    float a1 = -(float)elapsed_sec * 2.15f;
    for (int i = 0; i < 2; i++) {
        float sa = a1 + i * 3.1415926f;
        dl->PathArcTo(ImVec2(cx, cy), r_mid, sa, sa + 2.1f, 28);
        dl->PathStroke(IM_COL32(242, 198, 86, 210), 0, 2.8f * sc);
    }

    float ah = (float)elapsed_sec * 0.65f;
    ImVec2 hex[6];
    for (int i = 0; i < 6; i++) {
        float ang = ah + i * (6.2831853f / 6.0f);
        hex[i] = ImVec2(cx + cosf(ang) * r_inner, cy + sinf(ang) * r_inner);
    }
    dl->AddPolyline(hex, 6, IM_COL32(110, 232, 248, (int)(150 + 80 * pulse)), ImDrawFlags_Closed, 2.2f * sc);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##loading_ui", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);

    if (font_body) ImGui::PushFont(font_body);

    const char *title = ui_tr(STR_LOAD_TITLE);
    if (font_title) ImGui::PushFont(font_title);
    ImVec2 ts = ImGui::CalcTextSize(title);
    ImGui::SetCursorPos(ImVec2((ds.x - ts.x) * 0.5f, cy + r_outer + 34.0f * sc));
    ImGui::TextColored(ImVec4(0.95f, 0.79f, 0.36f, 1.0f), "%s", title);
    if (font_title) ImGui::PopFont();

    float target_frac = stage <= 0 ? 0.18f : stage == 1 ? 0.42f : stage == 2 ? 0.68f : stage == 3 ? 0.88f : 0.96f;
    static float smooth_frac = 0.08f;
    static int prev_stage = -1;
    if (stage < prev_stage || (prev_stage < 0 && elapsed_sec < 0.05)) smooth_frac = 0.08f;
    prev_stage = stage;
    smooth_frac += (target_frac - smooth_frac) * 0.08f;
    float shimmer = 0.02f * (0.5f + 0.5f * sinf((float)elapsed_sec * 4.0f));
    float bar_frac = fminf(0.98f, smooth_frac + shimmer);

    float bar_w = fminf(ds.x * 0.52f, 560.0f * sc);
    float bar_h = 10.0f * sc;
    float bar_x = (ds.x - bar_w) * 0.5f;
    float bar_y = cy + r_outer + 92.0f * sc;
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                      IM_COL32(20, 32, 46, 230), bar_h * 0.5f);
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w * bar_frac, bar_y + bar_h),
                      IM_COL32(56, 212, 236, 245), bar_h * 0.5f);
    dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h),
                IM_COL32(80, 160, 195, 160), bar_h * 0.5f);

    const char *st_lbl = (status_text && *status_text) ? status_text
                       : stage <= 0 ? ui_tr(STR_LOAD_STAGE_0)
                       : stage == 1 ? ui_tr(STR_LOAD_STAGE_1)
                       : stage == 2 ? ui_tr(STR_LOAD_STAGE_2)
                       : stage == 3 ? ui_tr(STR_LOAD_STAGE_3)
                       : ui_tr(STR_LOAD_STAGE_4);
    ImVec2 ss = ImGui::CalcTextSize(st_lbl);
    ImGui::SetCursorPos(ImVec2((ds.x - ss.x) * 0.5f, bar_y + bar_h + 20.0f * sc));
    ImGui::TextColored(ImVec4(0.84f, 0.92f, 0.96f, 1.0f), "%s", st_lbl);

    const char *sub = ui_tr(STR_LOAD_PLEASE_WAIT);
    ImVec2 bs = ImGui::CalcTextSize(sub);
    ImGui::SetCursorPos(ImVec2((ds.x - bs.x) * 0.5f, ds.y - 52.0f * sc));
    ImGui::TextDisabled("%s", sub);

    if (font_body) ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleColor();
}
