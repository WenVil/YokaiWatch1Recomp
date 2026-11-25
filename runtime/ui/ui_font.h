#pragma once

struct ImFont;

// Load/rebuild crisp HiDPI fonts into the active ImGui context's FontAtlas using
// on-demand multi-language range partitioning + MergeMode fallback chain.
// Call before vk_imgui_backend_init() or while GPU/backend font texture rebuild is safe.
void ui_load_i18n_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                        float sz_small, float sz_menu, float sz_big,
                        int ui_lang, const char *extra_text, const char *env_override);

// Backward-compatible wrapper using resolved UI language.
void ui_load_cjk_fonts(ImFont **out_small, ImFont **out_menu, ImFont **out_big,
                       float sz_small, float sz_menu, float sz_big,
                       const char *env_override);

// Returns true if any printable UTF-8 codepoint in `utf8` is missing from `font` (FindGlyphNoFallback == nullptr).
bool ui_font_text_missing(ImFont *font, const char *utf8);

// Apply unified modern dark Sentinel theme (colors, rounding, padding, focus highlight).
void ui_apply_sentinel_theme(float style_scale);

// Draw shared animated Sentinel sci-fi loading screen onto the full active ImGui viewport.
void ui_draw_loading_visual(double elapsed_sec, int stage, const char *status_text,
                            float sc, ImFont *font_title, ImFont *font_body);
