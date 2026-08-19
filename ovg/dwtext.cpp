

#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb.h>
#include <string>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <vector>
#include <harfbuzz/hb.h> 
#include <harfbuzz/hb-raster.h> 
 

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct FontConfigProvider {
    FontConfigProvider() { FcInit(); }
    ~FontConfigProvider() { FcFini(); }

    std::string findFont(const std::string& family, const std::string& lang = "en") {
        FcPattern* pat = FcPatternCreate();
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)family.c_str());
        if (!lang.empty()) FcPatternAddString(pat, FC_LANG, (const FcChar8*)lang.c_str());
        FcConfigSubstitute(nullptr, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        FcResult r;
        FcPattern* m = FcFontMatch(nullptr, pat, &r);
        FcPatternDestroy(pat);
        if (!m) return {};
        FcChar8* file = nullptr;
        if (FcPatternGetString(m, FC_FILE, 0, &file) != FcResultMatch) { FcPatternDestroy(m); return {}; }
        std::string p((const char*)file);
        FcPatternDestroy(m);
        return p;
    }

    std::string findForCodepoint(uint32_t cp) {
        if (auto it = cache.find(cp); it != cache.end()) return it->second;
        hb_script_t s = hb_unicode_script(hb_unicode_funcs_get_default(), cp);
        std::string fam = "sans-serif", lang = "en";
        bool emoji = (cp >= 0x1F300 && cp <= 0x1FAFF) || cp == 0x2764 || cp == 0xFE0F;
        if (emoji) fam = "Noto Color Emoji";
        else if (s == HB_SCRIPT_ARABIC) lang = "ar";
        else if (s == HB_SCRIPT_HAN) lang = "zh";
        else if (s == HB_SCRIPT_HIRAGANA) lang = "ja";
        else if (s == HB_SCRIPT_HANGUL) lang = "ko";
        std::string p = findFont(fam, lang);
        if (p.empty()) p = findFont("sans-serif", "en");
        if (!p.empty()) cache[cp] = p;
        return p;
    }
private:
    std::unordered_map<uint32_t, std::string> cache;
}; 

class TextAtlas {
public:
    TextAtlas(SDL_Renderer* r, int w = 1024, int h = 256) : m_w(w), m_h(h) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(r, tex);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        SDL_SetRenderTarget(r, nullptr);
    }
    ~TextAtlas() { SDL_DestroyTexture(tex); }

    int width() const { return m_w; }
    int height() const { return m_h; }
    SDL_Texture* texture() { return tex; }

    void clear() { m_x = m_y = 0; m_row = 0; }

    SDL_FRect upload(const uint8_t* px, int w, int h) {
        if (m_x + w > m_w) { m_x = 0; m_y += m_row + 2; m_row = 0; }
        if (h > m_row) m_row = h;
        SDL_Rect dst{ m_x, m_y, w, h };
        SDL_UpdateTexture(tex, &dst, px, w * 4);
        SDL_FRect uv{ (float)m_x / m_w, (float)m_y / m_h, (float)w / m_w, (float)h / m_h };
        m_x += w + 1;
        return uv;
    }
private:
    SDL_Texture* tex;
    int m_w, m_h, m_x = 0, m_y = 0, m_row = 0;
};

struct ShapedGlyph {
    hb_font_t* font;
    hb_codepoint_t gid;
    float x, y;      // 相对行起点（已含 offset）
    float adv;
    bool color;
};

struct AtlasSlot {
    SDL_FRect uv;
    float bearingX, bearingY, adv;
};

// ---- 单色 A8 → 染前景色成 RGBA32
static std::vector<uint8_t> a8_to_rgba(const uint8_t* a8, int w, int h, SDL_Color c) {
    std::vector<uint8_t> out((size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        uint8_t a = a8[i];
        out[i * 4 + 0] = c.r; out[i * 4 + 1] = c.g; out[i * 4 + 2] = c.b;
        out[i * 4 + 3] = (uint8_t)(a * c.a / 255);
    }
    return out;
}
// ---- BGRA32 → RGBA32（字节交换）
static std::vector<uint8_t> bgra_to_rgba(const uint8_t* src, int w, int h) {
    std::vector<uint8_t> out((size_t)w * h * 4);
    for (int i = 0; i < w * h; ++i) {
        out[i * 4 + 0] = src[i * 4 + 2];
        out[i * 4 + 1] = src[i * 4 + 1];
        out[i * 4 + 2] = src[i * 4 + 0];
        out[i * 4 + 3] = src[i * 4 + 3];
    }
    return out;
}

int main(int argc, char** argv) {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* win = SDL_CreateWindow("raqm+hb-raster+SDL3", 900, 300, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);

    FontConfigProvider fc;
    TextAtlas atlas(ren, 1024, 256);

    // ---- 文本
    const std::string text = "Hello 你好 سلام 🌍❤️🎉";
    float fontSize = 48;
    SDL_Color fg{ 235,235,235,255 };

    // ---- 1. 收集文本里出现的所有 codepoint → 用 FC 选字体 → 建 hb_font
    std::unordered_map<std::string, hb_font_t*> fontCache;
    std::vector<hb_font_t*> fonts;
    hb_font_t* defaultFont = nullptr;

    // 简单按 UTF-8 遍历
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = 0;
        hb_codepoint_t c = text[i];
        if (c < 0x80) { cp = c; i += 1; }
        else if ((c & 0xE0) == 0xC0) { cp = (c & 0x1F) << 6 | (text[i + 1] & 0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0) { cp = (c & 0x0F) << 12 | (text[i + 1] & 0x3F) << 6 | (text[i + 2] & 0x3F); i += 3; }
        else { cp = (c & 0x07) << 18 | (text[i + 1] & 0x3F) << 12 | (text[i + 2] & 0x3F) << 6 | (text[i + 3] & 0x3F); i += 4; }
        std::string path = fc.findForCodepoint(cp);
        if (path.empty()) continue;
        if (!fontCache.count(path)) {
            hb_blob_t* blob = hb_blob_create_from_file(path.c_str());
            hb_face_t* face = hb_face_create(blob, 0);
            hb_font_t* f = hb_font_create(face);
            hb_font_set_scale(f, (int)(fontSize * 64), (int)(fontSize * 64));
            fontCache[path] = f; fonts.push_back(f);
            hb_face_destroy(face); hb_blob_destroy(blob);
            if (!defaultFont) defaultFont = f;
        }
    }
    if (!defaultFont) return 1;

    // ---- 2. libraqm layout
    raqm_t* rq = raqm_create();
    raqm_set_text_utf8(rq, text.c_str(), text.size());
    raqm_set_par_direction(rq, RAQM_DIRECTION_DEFAULT);
    for (auto f : fonts) raqm_add_font(rq, f);
    raqm_layout(rq);

    size_t n = 0;
    raqm_glyph_t* g = raqm_get_glyphs(rq, &n);

    std::vector<ShapedGlyph> shaped;
    float cx = 0;
    for (size_t i = 0; i < n; ++i) {
        hb_font_t* f = g[i].ftface ? hb_ft_font_create_cached((FT_Face)g[i].ftface, nullptr) : nullptr;
        // 上面 ftface 在无 FT 场景为 NULL，我们改用 raqm 内部绑定的 hb_font？
        // 注意：raqm_add_font 传的是 hb_font_t*，但 raqm_glyph_t 只回传 FT_Face。
        // 无 FT 时：我们在 raqm_add_font 之前把 hb_font 和“顺序”绑定，
        // 这里采用更简单做法：shape 时只用 defaultFont 做 fallback 演示，
        // 真实多字体 fallback 需用 hb_buffer 自己 shape。下面用简化策略：
        (void)f;
        bool color = false;
        hb_font_t* useFont = defaultFont;
        // 判断彩色（用 defaultFont 近似，演示用）
        if (hb_ot_color_has_paint(useFont, g[i].index)) color = true;
        else if (hb_ot_color_has_palette(useFont) && hb_ot_color_glyph_get_layers(useFont, g[i].index, 0, nullptr, nullptr)) color = true;

        ShapedGlyph sg;
        sg.font = useFont;
        sg.gid = g[i].index;
        sg.x = cx + g[i].x_offset / 64.f;
        sg.y = -g[i].y_offset / 64.f;
        sg.adv = g[i].x_advance / 64.f;
        sg.color = color;
        shaped.push_back(sg);
        cx += sg.adv;
    }
    float lineWidth = cx;

    // ---- 3. 光栅化进 atlas
    atlas.clear();
    std::vector<AtlasSlot> slots;
    for (auto& sg : shaped) {
        AtlasSlot slot{};
        slot.adv = sg.adv;
        if (sg.color) {
            hb_raster_paint_t* p = hb_raster_paint_create_or_fail();
            hb_raster_paint_set_scale_factor(p, 1.f, 1.f);
            hb_raster_paint_set_foreground(p, HB_COLOR(fg.a, fg.r, fg.g, fg.b));
            hb_glyph_extents_t ext; hb_font_get_glyph_extents(sg.font, sg.gid, &ext);
            hb_raster_paint_set_glyph_extents(p, &ext);
            hb_raster_paint_glyph(p, sg.font, sg.gid, 0, 0);
            hb_raster_image_t* img = hb_raster_paint_render(p);
            if (img) {
                const uint8_t* buf = hb_raster_image_get_buffer(img);
                hb_glyph_extents_t e; hb_raster_image_get_extents(img, &e);
                int w = (e.x2 - e.x1) / 64 + 1, h = (e.y2 - e.y1) / 64 + 1;
                auto rgba = bgra_to_rgba(buf, w, h);
                slot.uv = atlas.upload(rgba.data(), w, h);
                slot.bearingX = e.x1 / 64.f; slot.bearingY = e.y1 / 64.f;
                hb_raster_paint_recycle_image(p, img);
            }
            hb_raster_paint_destroy(p);
        }
        else {
            hb_raster_draw_t* d = hb_raster_draw_create_or_fail();
            hb_raster_draw_set_scale_factor(d, 1.f, 1.f);
            hb_glyph_extents_t ext; hb_font_get_glyph_extents(sg.font, sg.gid, &ext);
            _hb_raster_draw_set_glyph_extents(d, &ext);
            hb_raster_draw_glyph(d, sg.font, sg.gid, 0, 0);
            hb_raster_image_t* img = hb_raster_draw_render(d);
            if (img) {
                const uint8_t* buf = hb_raster_image_get_buffer(img);
                hb_glyph_extents_t e; hb_raster_image_get_extents(img, &e);
                int w = (e.x2 - e.x1) / 64 + 1, h = (e.y2 - e.y1) / 64 + 1;
                auto rgba = a8_to_rgba(buf, w, h, fg);
                slot.uv = atlas.upload(rgba.data(), w, h);
                slot.bearingX = e.x1 / 64.f; slot.bearingY = e.y1 / 64.f;
                hb_raster_draw_recycle_image(d, img);
            }
            hb_raster_draw_destroy(d);
        }
        slots.push_back(slot);
    }

    // ---- 4. 渲染循环
    bool run = true; SDL_Event ev;
    while (run) {
        while (SDL_PollEvent(&ev)) if (ev.type == SDL_EVENT_QUIT) run = false;
        SDL_SetRenderDrawColor(ren, 28, 28, 36, 255);
        SDL_RenderClear(ren);
        float baseX = (900 - lineWidth) / 2.f;
        float baseY = 150;
        float penX = baseX;
        for (size_t i = 0; i < shaped.size(); ++i) {
            auto& sg = shaped[i]; auto& sl = slots[i];
            SDL_FRect dst{
                penX + sg.x + sl.bearingX,
                baseY + sg.y - sl.bearingY,
                sl.uv.w * atlas.width(),
                sl.uv.h * atlas.height()
            };
            SDL_RenderTexture(ren, atlas.texture(), &sl.uv, &dst);
            penX += sg.adv;
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    // ---- cleanup
    raqm_destroy(rq);
    for (auto& kv : fontCache) hb_font_destroy(kv.second);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}