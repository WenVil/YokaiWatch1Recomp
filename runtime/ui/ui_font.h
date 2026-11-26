#pragma once

struct ImFont;

// Load/rebuild crisp HiDPI fonts into the active ImGui context's FontAtlas using
// on-demand multi-language range partitioning + MergeMode fallback chain.
// Call before gl_imgui_launcher_init() / gl_imgui_overlay_init() or follow with gl_imgui_fonts_rebuild().
void ui_load_i18n_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                        float sz_small, float sz_menu, float sz_big,
                        int ui_lang, const char *extra_text, const char *env_override);

// Backward-compatible wrapper using resolved UI language.
void ui_load_cjk_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                       float sz_small, float sz_menu, float sz_big,
                       const char *env_override);

// Returns true if any printable UTF-8 codepoint in `utf8` is missing from `font` (FindGlyphNoFallback == nullptr).
bool ui_font_text_missing(ImFont *font, const char *utf8);

// Returns the number of missing UI glyph codepoints recorded during the latest ui_load_i18n_fonts call.
int ui_font_last_missing_count(void);

// Apply unified modern dark Yo-kai Watch 1 theme (colors, rounding, padding, focus highlight).
void ui_apply_yw1_theme(float style_scale);

// Draw shared animated loading screen onto the full active ImGui viewport.
void ui_draw_loading_visual(double elapsed_sec, int stage, const char *status_text,
                            float sc, ImFont *font_title, ImFont *font_body);
