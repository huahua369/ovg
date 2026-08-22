
/*
字体处理管理


2026/8/19 创建

*/

#include <map>
#include <string>
#include <set>
#include <stack>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <vector>

#include <locale.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#define VG_USE_FONTCONFIG

#include <fontconfig/fontconfig.h> 

#ifndef GLM_FORCE_XYZW_ONLY 
#define GLM_ENABLE_EXPERIMENTAL 
#define GLM_FORCE_XYZW_ONLY
#include <glm/glm.hpp>  
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/closest_point.hpp>
#include <glm/gtc/type_ptr.hpp> 
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp> 
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/euler_angles.hpp>
#endif
#include "mapView.h"

#include "ovg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ICU */
#include <unicode/ubrk.h>
#include <unicode/ubidi.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>

/* HarfBuzz */
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h> 
#include <harfbuzz/hb-raster.h> 
#include "ovg_fonts.h"

static int utf8_to_utf16(const char* utf8, UChar* out16, int cap16, UErrorCode* st)
{
	int32_t len = 0;
	u_strFromUTF8(out16, cap16, &len, utf8, -1, st);
	return len;
}
static void utf16_slice_to_utf8(const UChar* u16, int32_t start, int32_t len, char* out, int cap)
{
	UErrorCode st = U_ZERO_ERROR;
	u_strToUTF8(out, cap, NULL, u16 + start, len, &st);
}
static hb_font_t* load_font(const char* family, const char* style)
{
	/* Fontconfig：只负责“找文件” */
	FcConfig* cfg = FcInitLoadConfigAndFonts();
	FcPattern* pat = FcPatternCreate();

	FcPatternAddString(pat, FC_FAMILY, (FcChar8*)family);
	if (style)
		FcPatternAddString(pat, FC_STYLE, (FcChar8*)style);

	FcConfigSubstitute(cfg, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	FcResult result;
	FcPattern* match = FcFontMatch(cfg, pat, &result);
	if (!match) {
		fprintf(stderr, "Fontconfig: no match for %s\n", family);
		FcPatternDestroy(pat);
		FcConfigDestroy(cfg);
		return NULL;
	}

	FcChar8* file = NULL;
	int idx = 0;
	FcPatternGetString(match, FC_FILE, 0, &file);
	FcPatternGetInteger(match, FC_INDEX, 0, &idx);

	/* HarfBuzz：从文件创建 face */
	hb_blob_t* blob = hb_blob_create_from_file((const char*)file);
	if (hb_blob_get_length(blob) == 0) {
		fprintf(stderr, "HarfBuzz: failed to load %s\n", file);
		hb_blob_destroy(blob);
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		FcConfigDestroy(cfg);
		return NULL;
	}

	hb_face_t* hb_face = hb_face_create(blob, idx);
	hb_blob_destroy(blob);   /* face 已引用 blob */

	if (hb_face_get_glyph_count(hb_face) == 0) {
		fprintf(stderr, "HarfBuzz: invalid face %s\n", file);
		hb_face_destroy(hb_face);
		FcPatternDestroy(match);
		FcPatternDestroy(pat);
		FcConfigDestroy(cfg);
		return NULL;
	}

	/* 创建 font（带 scale） */
	hb_font_t* hb_font = hb_font_create(hb_face);
	hb_font_set_scale(hb_font, 16 * 64, 16 * 64); // 16px
	hb_ot_font_set_funcs(hb_font);                 // ✅ 用 OT 回调

	hb_face_destroy(hb_face);  /* font 已引用 face */

	FcPatternDestroy(match);
	FcPatternDestroy(pat);
	FcConfigDestroy(cfg);
	return hb_font;
}
font_cache_cx::font_cache_cx()
{
	get_sys_family();
}

font_cache_cx::~font_cache_cx()
{
	clear_load();
	clear_sys();
	if (cfg)
		FcConfigDestroy(cfg);
	cfg = 0;
	_emojis.clear();
}

void font_cache_cx::clear_sys()
{
	_temp.clear();
	for (auto& [k, v] : _familys) {
		for (auto it : v) {
			if (it) {
				if (it->font)
					hb_font_destroy(it->font);
				delete it;
			}
		}
	}
	_familys.clear();
}

void font_cache_cx::clear_load()
{
	_temp.clear();
	for (auto& [k, v] : _familys_name) {
		for (auto it : v) {
			if (it) {
				if (it->font)
					hb_font_destroy(it->font);
				delete it;
			}
		}
	}
	_familys_name.clear();
}

hb_font_t* load_font(const char* file, int idx) {
	hb_font_t* font = 0; hb_face_t* face = 0;
	hb_blob_t* blob = hb_blob_create_from_file((const char*)file);
	do {
		if (hb_blob_get_length(blob) == 0) {
			break;
		}
		face = hb_face_create(blob, idx);
		if (!face)break;
		auto gcont = hb_face_get_glyph_count(face);
		if (!gcont) {
			break;
		}
		font = hb_font_create(face);
	} while (0);
	if (face)
		hb_face_destroy(face);
	if (blob)
		hb_blob_destroy(blob);
	return font;
}
std::string get_pat_str(FcPattern* font, const char* o, int n)
{
	FcChar8* s = nullptr;
	std::string r;
	if (o && ::FcPatternGetString(font, o, n, &s) == FcResultMatch)
	{
		if (s)
		{
			r = (char*)s;
		}
	}
	return r;
}
std::set<std::string> get_pat_strs(FcPattern* font, const char* o)
{
	std::set<std::string> rv;
	FcChar8* s = nullptr;
	int n = 0;
	do {
		if (o && ::FcPatternGetString(font, o, n, &s) == FcResultMatch)
		{
			if (s)
			{
				rv.insert((char*)s);
			}
		}
		else { break; }
		n++;
	} while (1);
	return rv;
}
void get_pat_strs(FcPattern* font, const char* o, std::set<std::string>& rv)
{
	FcChar8* s = nullptr;
	int n = 0;
	do {
		if (o && ::FcPatternGetString(font, o, n, &s) == FcResultMatch)
		{
			if (s)
			{
				rv.insert((char*)s);
			}
		}
		else { break; }
		n++;
	} while (1);
	return;
}

inline float font_get_slant_angle(hb_font_t* font) {
	return hb_style_get_value(font, HB_STYLE_TAG_SLANT_ANGLE);
}

inline float font_get_italic_value(hb_font_t* font) {
	return hb_style_get_value(font, HB_STYLE_TAG_ITALIC);
}
// Fontconfig slant → 期望的 slnt 角度（右倾为负）
inline float fc_slant_to_slant_angle(int fc_slant) noexcept {
	switch (fc_slant) {
	case FC_SLANT_ITALIC:
		return -12.0f;   // 典型 italic 倾斜
	case FC_SLANT_OBLIQUE:
		return -10.0f;   // oblique 通常略小于 italic
	case FC_SLANT_ROMAN:
	default:
		return 0.0f;
	}
}
//SLANT_ANGLE：OpenType slnt 轴值
//ITALIC：0 = Roman，1 = Italic（COLRv1 / STAT 表）
//四、关键逻辑：是否设置 slnt（变量字体才设）
bool font_supports_slnt_axis(hb_font_t* font) {
	hb_face_t* face = hb_font_get_face(font);
	unsigned int axis_count = hb_ot_var_get_axis_count(face);
	auto axc = axis_count;
	for (unsigned i = 0; i < axis_count; i++) {
		hb_ot_var_axis_info_t info;
		hb_ot_var_get_axis_infos(face, i, &axc, &info);
		if (info.tag == HB_STYLE_TAG_SLANT_ANGLE)
			return true;
	}
	return false;
}
void font_cache_cx::get_sys_family()
{
	if (!cfg)
		cfg = FcInitLoadConfigAndFonts();
	FcPattern* pat = FcPatternCreate();
	FcObjectSet* os = FcObjectSetBuild(
		FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, FC_FILE, FC_FULLNAME, FC_INDEX, NULL);
	FcFontSet* fs = FcFontList(cfg, pat, os);

	for (int i = 0; i < fs->nfont; i++) {
		FcPattern* p = fs->fonts[i];
		FcChar8* family = NULL, * style = NULL;
		int weight = 0, slant = 0;
		char* file = NULL;
		int index = 0;
		FcPatternGetString(p, FC_FAMILY, 0, &family);
		FcPatternGetString(p, FC_STYLE, 0, &style);
		FcPatternGetInteger(p, FC_WEIGHT, 0, &weight);
		FcPatternGetInteger(p, FC_SLANT, 0, &slant);
		FcPatternGetString(p, FC_FILE, 0, (FcChar8**)&file);
		FcPatternGetInteger(p, FC_INDEX, 0, &index);
		if (!style)style = (FcChar8*)"";
		if (!file || !family || !(*file) || !(*family))continue;
		hb_font_t* font = 0;
		auto it = new FontStyle();
		if (it)
		{
			get_pat_strs(p, FC_FAMILY, it->alias);
			get_pat_strs(p, FC_FULLNAME, it->alias);
			it->family = (char*)family;
			std::string fname = it->family;
			if (fname.find("moji") != std::string::npos)
			{
				_emojis.push_back(it);
			}
			it->style = (char*)style;
			it->file = file;
			it->weight = weight;
			it->slant = slant;
			it->index = index;

			_familys[(char*)family].push_back(it);
		}
	}
	FcFontSetDestroy(fs);
	FcObjectSetDestroy(os);
	FcPatternDestroy(pat);

}
hb_font_t* font_cache_cx::get_font(const char* family, const char* style, int weight, int slant)
{
	bool mb = mk_font(&_familys_name, family, style, weight, slant);
	if (_temp.size()) {
		return _temp[0]->font;
	}
	bool ab = mk_font(&_familys, family, style, weight, slant);
	if (_temp.size()) {
		return _temp[0]->font;
	}
	return nullptr;
}

void font_cache_cx::select_font_face(const char* family, const char* style, int weight, int slant)
{}

bool font_cache_cx::load_font_from_path(const char* path, const char* name)
{
	if (!path || !*path)return false;
	bool hr = FcConfigAppFontAddFile(cfg, (FcChar8*)path);
	if (hr)
	{

	}
	return hr;
}

bool font_cache_cx::add_font_dir(const char* dir)
{
	if (!dir || !*dir)return false;
	return FcConfigAppFontAddDir(cfg, (FcChar8*)dir);
}

bool font_cache_cx::load_font_from_memory(unsigned char* fontBuffer, long fontBufferByteSize, const char* name)
{
	return false;
}


size_t font_cache_cx::mk_font(std::map<std::string, std::vector<FontStyle*>>* pt, const char* family, const char* style, int weight, int slant)
{
	size_t n = 0;
	_temp.clear();
	if (!family || !(*family) || !pt)return n;
	{
		auto it = pt->find(family);
		if (it != pt->end()) {
			auto& v = it->second;
			for (auto& vt : v)
			{
				bool bst = !style || !(*style);
				if (!bst) {
					bst = (style && *style && vt->style == style);
				}
				bool bw = vt->weight == weight || weight < 1;
				bool bsl = vt->slant == slant || slant < 1;
				if (bst && bw && bsl) {
					_temp.push_back(vt);
				}
			}
		}
	}
	if (_temp.empty()) {
		for (auto& [k, v] : *pt) {
			for (auto vt : v) {
				if (vt) {
					if (vt->alias.find(family) != vt->alias.end())
					{
						bool bst = !style || !(*style);
						if (!bst) {
							bst = (style && *style && vt->style == style);
						}
						bool bw = vt->weight == weight || weight < 1;
						bool bsl = vt->slant == slant || slant < 1;
						if (bst && bw && bsl) {
							_temp.push_back(vt);
						}
					}
				}
			}
		}
	}
	for (auto it : _temp) {
		if (!it->font)
		{
			it->font = load_font(it->file.c_str(), it->index);
		}
		if (it->font)
		{
			if (font_supports_slnt_axis(it->font)) {
				//float desired = fc_slant_to_slant_angle(slant);
				//hb_font_set_variation(font, HB_OT_TAG_VAR_AXIS_SLANT, desired);
				it->slnt_applied = true;
			}
			n++;
		}
	}
	return n;
}

/* ─────────────────────────────────────────────
 * 对一行逻辑文本做 Bidi 重排 + HarfBuzz 整形
 * ───────────────────────────────────────────── */
static void
shape_line(hb_font_t* hb_font,
	const UChar* line_u16,
	int32_t       line_len,
	UBiDiDirection base_dir)
{
	UErrorCode st = U_ZERO_ERROR;

	/* 1) ICU Bidi：设置段落方向 */
	UBiDi* bidi = ubidi_open();
	ubidi_setPara(bidi, line_u16, line_len, base_dir, NULL, &st);
	if (U_FAILURE(st)) {
		fprintf(stderr, "ubidi_setPara failed: %s\n", u_errorName(st));
		ubidi_close(bidi);
		return;
	}

	/* 2) 获取视觉顺序的 run 列表 */
	int32_t run_count = ubidi_countRuns(bidi, &st);
	if (U_FAILURE(st)) {
		ubidi_close(bidi);
		return;
	}

	/* 3) 按视觉顺序拼接逻辑 UTF-16 */
	UChar visual_u16[512];
	int32_t vis_len = 0;

	for (int32_t r = 0; r < run_count; r++) {
		int32_t run_start, run_len;
		UBiDiDirection run_dir = ubidi_getVisualRun(bidi, r, &run_start, &run_len);
		run_start = ubidi_getVisualRun(bidi, r, &run_start, &run_len); /* 返回逻辑起点 */

		if (run_dir == UBIDI_LTR) {
			for (int32_t i = 0; i < run_len; i++)
				visual_u16[vis_len++] = line_u16[run_start + i];
		}
		else {
			for (int32_t i = run_len - 1; i >= 0; i--)
				visual_u16[vis_len++] = line_u16[run_start + i];
		}
	}

	/* 4) UTF-16 → UTF-8（给 HarfBuzz） */
	char utf8_buf[1024];
	u_strToUTF8(utf8_buf, sizeof(utf8_buf), NULL,
		visual_u16, vis_len, &st);

	/* 5) HarfBuzz 整形 */
	hb_buffer_t* buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, utf8_buf, -1, 0, -1);

	/* 设置 script / direction（用 Bidi 推断的段落方向） */
	hb_buffer_set_script(buf, HB_SCRIPT_COMMON); /* 让 HB 自动检测 */
	hb_buffer_set_direction(buf,
		(base_dir == UBIDI_RTL) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_language(buf, hb_language_from_string("en", -1));
	hb_buffer_guess_segment_properties(buf);

	hb_shape(hb_font, buf, NULL, 0);

	/* 6) 打印 glyph 结果 */
	unsigned int glyph_count;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &glyph_count);
	hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

	printf("  ├─ glyphs (%u): ", glyph_count);
	for (unsigned int i = 0; i < glyph_count; i++) {
		printf("[gid=%u x=%d y=%d dx=%d dy=%d] ",
			infos[i].codepoint,
			pos[i].x_offset, pos[i].y_offset,
			pos[i].x_advance, pos[i].y_advance);
	}
	printf("\n");

	hb_buffer_destroy(buf);
	ubidi_close(bidi);
}
UBiDiDirection get_dir(UChar32 c)
{
	UBiDiDirection dir = (UBiDiDirection)UBIDI_DEFAULT_LTR;
	if (u_charDirection(c) == U_RIGHT_TO_LEFT || u_charDirection(c) == U_ARABIC_NUMBER) {
		dir = UBIDI_RTL;
	}
	return dir;
}
hb_font_t* find_face_for_codepoint(hb_font_t* font, uint32_t cp, uint32_t variation_selector)
{
	hb_codepoint_t glyph{};

	if (variation_selector != 0) {
		if (hb_font_get_variation_glyph(font,
			cp,
			variation_selector,
			&glyph) ||
			hb_font_get_nominal_glyph(font, cp, &glyph)) {
			return font;
		}
	}
	else {
		if (hb_font_get_nominal_glyph(font, cp, &glyph)) {
			return font;
		}
	}
	return 0;
}

const char* font_cache_cx::weight_to_string(int w) {
	switch (w) {
	case FC_WEIGHT_THIN:		return "Thin";
	case FC_WEIGHT_EXTRALIGHT:	return "ExtraLight";
	case FC_WEIGHT_LIGHT:		return "Light";
	case FC_WEIGHT_DEMILIGHT:	return "DemiLight";
	case FC_WEIGHT_BOOK:		return "Book";
	case FC_WEIGHT_REGULAR:		return "Regular";
	case FC_WEIGHT_MEDIUM:		return "Medium";
	case FC_WEIGHT_DEMIBOLD:	return "DemiBold";
	case FC_WEIGHT_BOLD:		return "Bold";
	case FC_WEIGHT_EXTRABOLD:	return "ExtraBold";
	case FC_WEIGHT_BLACK:		return "Black";
	case FC_WEIGHT_EXTRABLACK:	return "ExtraBlack";
	}
	return "Black";
}

const char* font_cache_cx::slant_to_string(int s) {
	switch (s) {
	case FC_SLANT_ITALIC:  return "Italic";
	case FC_SLANT_OBLIQUE: return "Oblique";
	default:               return "Roman";
	}
}
struct draw_ctx {
	ovg_ctx_cb* ovg = 0;
	rvg_t* vg = 0;
	float x = 0.0f, y = 0.0f;
	float scale = 1.0f;      // 字体像素大小
	float ascent = 0.0f;     // 用于 baseline
};
static void ovg_move_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<draw_ctx*>(data);
	c->ovg->move_to(c->vg,
		c->x + to_x * c->scale,
		c->y + (c->ascent - to_y * c->scale));
}

static void ovg_line_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<draw_ctx*>(data);
	c->ovg->line_to(c->vg,
		c->x + to_x * c->scale,
		c->y + (c->ascent - to_y * c->scale));
}

static void ovg_cubic_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*,
	float cx1, float cy1, float cx2, float cy2,
	float to_x, float to_y, void*) {
	auto* c = static_cast<draw_ctx*>(data);
	c->ovg->curve_to(c->vg,
		c->x + cx1 * c->scale, c->y + (c->ascent - cy1 * c->scale),
		c->x + cx2 * c->scale, c->y + (c->ascent - cy2 * c->scale),
		c->x + to_x * c->scale, c->y + (c->ascent - to_y * c->scale));
}


static void ovg_close_path(hb_draw_funcs_t*,
	void* data,
	hb_draw_state_t*,
	void*) {
	auto* ctx = static_cast<draw_ctx*>(data);
	ctx->ovg->close_path(ctx->vg);
}
// 文本渲染
#if 1
hb_draw_funcs_t* create_ovg_draw_funcs() {
	hb_draw_funcs_t* funcs = hb_draw_funcs_create();
	hb_draw_funcs_set_move_to_func(funcs, ovg_move_to, nullptr, nullptr);
	hb_draw_funcs_set_line_to_func(funcs, ovg_line_to, nullptr, nullptr);
	hb_draw_funcs_set_cubic_to_func(funcs, ovg_cubic_to, nullptr, nullptr);
	hb_draw_funcs_set_close_path_func(funcs, ovg_close_path, nullptr, nullptr);
	return funcs;
}

const font_family_t* resolve_family(
	const font_familys_t* ffs,
	uint32_t cp)
{
	for (int i = 0; i < ffs->count; i++) {
		if (hb_set_has(ffs->familys[i].coverage, cp))
			return &ffs->familys[i];
	}
	return &ffs->familys[0]; // fallback
}

static uint32_t utf8_next(const uint8_t*& p, const uint8_t* end) {
	if (p >= end) return 0;
	uint8_t c = *p++;
	uint32_t r = 0;
	if (c < 0x80) return c;
	if ((c & 0xE0) == 0xC0 && p + 1 <= end)
		r = ((c & 0x1F) << 6) | (*p++ & 0x3F);
	if ((c & 0xF0) == 0xE0 && p + 2 <= end)
		r = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F), p += 2;
	if ((c & 0xF8) == 0xF0 && p + 3 <= end)
		r = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F), p += 3;
	return r ? r : 0xFFFD;
}
struct font_family_ta {
	hb_font_t* font;
	float ascent;        // 从 hb_font_extents
	hb_set_t* coverage;  // hb_face_collect_unicodes
};
struct font_familys_ta {
	font_family_t* familys;
	int count;
};
void utf8_to_utf32(const void* str8, size_t len, std::vector<uint32_t>* ot)
{
	const uint8_t* p = (const uint8_t*)str8;
	const uint8_t* end = p + len;
	ot->reserve(len + ot->size());
	while (p < end)
		ot->push_back(utf8_next(p, end));
}
void render_text_shaped(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_ctx_cb* ovg, rvg_t* ovg_ctx, const glm::uvec3& color) {
	if (!ffs || !str8 || !len || !ovg || ffs->count == 0) return;
	static std::vector<uint32_t> utf32;
	utf32.clear();
	utf8_to_utf32(str8, len, &utf32);
	hb_font_t* primary = ffs->familys[0].font; // 主字体（可按 script 选）
	hb_draw_funcs_t* df = create_ovg_draw_funcs();
	hb_buffer_t* buf = hb_buffer_create();
	hb_buffer_add_utf32(buf, utf32.data(), utf32.size(), 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(primary, buf, nullptr, 0);

	uint32_t n;
	hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
	hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

	float pen_x = x, pen_y = y;
	size_t run_start = 0;
	int h = color.z;
	while (run_start < utf32.size()) {
		uint32_t cp = utf32[run_start];
		const font_family_t* ff = resolve_family(ffs, cp);
		if (!ff) ff = &ffs->familys[0];
		float sc = 1.0;
		if (h > 0)
		{
			hb_font_set_scale(ff->font, h, h);
		}
		/* 扩展 run */
		size_t run_end = run_start + 1;
		while (run_end < utf32.size() &&
			resolve_family(ffs, utf32[run_end]) == ff)
			run_end++;

		/* shape run */
		hb_buffer_t* buf = hb_buffer_create();
		hb_buffer_add_utf32(buf,
			utf32.data() + run_start,
			run_end - run_start,
			0, -1);
		hb_buffer_guess_segment_properties(buf);

		/* ✅ UI：可选关闭 kerning */
		hb_feature_t features[] = {
			{HB_TAG('k','e','r','n'), 0, 0, ~0u}
		};
		hb_shape(ff->font, buf, features, 1);

		unsigned n;
		hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
		hb_glyph_position_t* pos =
			hb_buffer_get_glyph_positions(buf, nullptr);

		/* 画 run */
		for (unsigned i = 0; i < n; i++) {
			draw_ctx ctx{
				ovg, ovg_ctx,
				pen_x, pen_y, sc,
				ff->ascent / ff->scale.x
			};
			hb_font_draw_glyph(ff->font, info[i].codepoint, df, &ctx);
			pen_x += floorf(pos[i].x_advance); // ✅ 像素对齐
		}
		hb_buffer_destroy(buf);
		run_start = run_end;
	}
	hb_buffer_destroy(buf);
	hb_draw_funcs_destroy(df);
	ovg->set_source_color(ovg_ctx, color.x);
	ovg->fill_preserve(ovg_ctx);
	ovg->set_source_color(ovg_ctx, color.y);
	ovg->stroke(ovg_ctx);
}
// todo 渐变色
struct ovg_paint;
void txt_paint_push_transform(hb_paint_funcs_t* funcs, void* paint_data, float xx, float yx, float xy, float yy, float dx, float dy, void* user_data);
void txt_paint_pop_transform(hb_paint_funcs_t* funcs, void* paint_data, void* user_data);
void txt_paint_fill_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, hb_bool_t is_foreground, hb_color_t color, void* user_data);

void txt_paint_push_clip_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, void* user_data);
void txt_paint_push_clip_rectangle(hb_paint_funcs_t* funcs, void* paint_data, float xmin, float ymin, float xmax, float ymax, void* user_data);
hb_draw_funcs_t* txt_paint_push_clip_path_start(hb_paint_funcs_t* funcs, void* paint_data, void** draw_data, void* user_data);
void txt_paint_push_clip_path_end(hb_paint_funcs_t* funcs, void* paint_data, void* user_data);
void txt_paint_pop_clip(hb_paint_funcs_t* funcs, void* paint_data, void* user_data);
void txt_paint_color(hb_paint_funcs_t* funcs, void* paint_data, hb_bool_t is_foreground, hb_color_t color, void* user_data);
void txt_paint_linear_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float x1, float y1, float x2, float y2, void* user_data);
void txt_paint_radial_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float r0, float x1, float y1, float r1, void* user_data);
void txt_paint_sweep_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float start_angle, float end_angle, void* user_data);
void txt_paint_push_group(hb_paint_funcs_t* funcs, void* paint_data, void* user_data);
void txt_paint_push_group_for(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data);
void txt_paint_pop_group(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data);


class paint_text_cx
{
public:
	ovg_canvas_cb* can = 0;
	hb_draw_funcs_t* draw_fill = 0;
	std::stack<glm::mat3x2> _skmat;
	std::stack<glm::ivec4> _skclip;
	std::stack<rvg_t*> _vg;
	glm::ivec4 clip = {};
	vg_state_save_t* st = 0;
	rvg_t* mvg = 0;
	ovg_path_t* cvg = 0;
public:
	paint_text_cx();
	~paint_text_cx();

	hb_draw_funcs_t* new_draw_funcs();
private:

};

paint_text_cx::paint_text_cx()
{
	new_draw_funcs();
}

paint_text_cx::~paint_text_cx()
{}

struct pdraw_ctx {
	ovg_canvas_cb* ovg = 0;
	ovg_path_t* vg = 0;
	float x = 0.0f, y = 0.0f;
	float scale = 1.0f;      // 字体像素大小
	float ascent = 0.0f;     // 用于 baseline
};
static void povg_move_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	c->ovg->move_to(c->vg,
		c->x + to_x * c->scale,
		c->y + (c->ascent - to_y * c->scale));
}

static void povg_line_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	c->ovg->line_to(c->vg,
		c->x + to_x * c->scale,
		c->y + (c->ascent - to_y * c->scale));
}

static void povg_cubic_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*,
	float cx1, float cy1, float cx2, float cy2,
	float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	c->ovg->curve_to(c->vg,
		c->x + cx1 * c->scale, c->y + (c->ascent - cy1 * c->scale),
		c->x + cx2 * c->scale, c->y + (c->ascent - cy2 * c->scale),
		c->x + to_x * c->scale, c->y + (c->ascent - to_y * c->scale));
}


static void povg_close_path(hb_draw_funcs_t*,
	void* data,
	hb_draw_state_t*,
	void*) {
	auto* ctx = static_cast<pdraw_ctx*>(data);
	ctx->ovg->close_path(ctx->vg);
}

hb_draw_funcs_t* paint_text_cx::new_draw_funcs() {
	hb_draw_funcs_t* funcs = hb_draw_funcs_create();
	hb_draw_funcs_set_move_to_func(funcs, povg_move_to, nullptr, nullptr);
	hb_draw_funcs_set_line_to_func(funcs, povg_line_to, nullptr, nullptr);
	hb_draw_funcs_set_cubic_to_func(funcs, povg_cubic_to, nullptr, nullptr);
	hb_draw_funcs_set_close_path_func(funcs, povg_close_path, nullptr, nullptr);
	draw_fill = funcs;
	return funcs;
}

void txt_paint_push_transform(hb_paint_funcs_t* funcs, void* paint_data, float xx, float yx, float xy, float yy, float dx, float dy, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	ctx->_skmat.push(ctx->st->pushConsts.mat);
	ctx->st->pushConsts.mat = glm::mat3x2(xx, yx, xy, yy, dx, dy);
}
void txt_paint_pop_transform(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto& m = ctx->_skmat.top();
	ctx->st->pushConsts.mat = m;
	ctx->_skmat.pop();
}
void txt_paint_fill_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, hb_bool_t is_foreground, hb_color_t color, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (ctx && ctx->draw_fill) {
		ctx->can->set_source_color(ctx->st, color); ctx->can->set_source_color(ctx->st, color);
		ctx->can->clear_path(ctx->cvg);

		rvg_t* vg = 0;
		float x = 0.0f, y = 0.0f;
		float scale = 1.0f;      // 字体像素大小
		float ascent = 0.0f;     // 用于 baseline

		pdraw_ctx pc{
				ctx->can,ctx->cvg,
				x, y, scale,
				//ff->ascent / ff->scale.x
		};
		hb_font_draw_glyph(font, glyph, ctx->draw_fill, &pc);
		ctx->can->fill(ctx->mvg);
	}
}
void txt_paint_push_clip_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
}
hb_draw_funcs_t* txt_paint_push_clip_path_start(hb_paint_funcs_t* funcs, void* paint_data, void** draw_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	return 0;
}
void txt_paint_push_clip_path_end(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
}
void txt_paint_push_clip_rectangle(hb_paint_funcs_t* funcs, void* paint_data, float xmin, float ymin, float xmax, float ymax, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	glm::ivec4 c = { xmin, ymin, xmin + xmax, ymin + ymax };
	ctx->_skclip.push(c);
	ctx->can->set_clip_rect(ctx->mvg, &c);
}
void txt_paint_pop_clip(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	ctx->clip = ctx->_skclip.top();
	ctx->_skclip.pop();
	ctx->can->set_clip_rect(ctx->mvg, &ctx->clip);
}
void txt_paint_color(hb_paint_funcs_t* funcs, void* paint_data, hb_bool_t is_foreground, hb_color_t color, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	ctx->can->set_source_color(ctx->st, color);
}
#define COLOR_STOP_RGBA(color) ((color) >> 24) / 255.0f, (((color) >> 16) & 0xff) / 255.0f, (((color) >> 8) & 0xff) / 255.0f, ((color) & 0xff) / 255.0f
void set_color_stop(ovg_canvas_cb* cb, vg_pattern_t* pat, hb_color_line_t* color_line)
{
	if (pat) {
		auto len = hb_color_line_get_color_stops(color_line, 0, NULL, NULL);
		hb_color_stop_t stops[32] = {};
		assert(len < 33);
		hb_color_line_get_color_stops(color_line, 0, &len, stops);
		for (size_t i = 0; i < len; i++)
		{
			cb->pattern_add_color_stop(pat, stops[i].offset, COLOR_STOP_RGBA(stops[i].color));
		}
	}
}
void txt_paint_linear_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float x1, float y1, float x2, float y2, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (!ctx || !ctx->can || !ctx->can->ac)return;
	auto pat = ctx->can->new_pattern_linear(ctx->can->ac, x0, y0, x2, y2);
	set_color_stop(ctx->can, pat, color_line);
}
void txt_paint_radial_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float r0, float x1, float y1, float r1, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto pat = ctx->can->new_pattern_radial(ctx->can->ac, x0, y0, r0, x1, y1, r1, false);
	set_color_stop(ctx->can, pat, color_line);
}
void txt_paint_sweep_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float start_angle, float end_angle, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto pat = ctx->can->new_pattern_sweep(ctx->can->ac, x0, y0, start_angle, end_angle);
	set_color_stop(ctx->can, pat, color_line);
}
void txt_paint_push_group(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto vg = ctx->can->new_rvg(ctx->can->ac);
	ctx->_vg.push(vg);
}
void txt_paint_push_group_for(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
}
void txt_paint_pop_group(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto vg = ctx->_vg.top();// todo 叠加rvg_t渲染
	ctx->_vg.pop();
}
hb_paint_funcs_t* new_hbpaint_cb() {
	hb_paint_funcs_t* cb = hb_paint_funcs_create();
	hb_paint_funcs_set_push_transform_func(cb, txt_paint_push_transform, 0, 0);
	hb_paint_funcs_set_pop_transform_func(cb, txt_paint_pop_transform, 0, 0);
	hb_paint_funcs_set_fill_glyph_func(cb, txt_paint_fill_glyph, 0, 0);
	//hb_paint_funcs_set_push_clip_glyph_func(cb, txt_paint_push_clip_glyph, 0, 0);
	hb_paint_funcs_set_push_clip_rectangle_func(cb, txt_paint_push_clip_rectangle, 0, 0);
	//hb_paint_funcs_set_push_clip_path_end_func(cb, txt_paint_push_clip_path_end, 0, 0);
	hb_paint_funcs_set_pop_clip_func(cb, txt_paint_pop_clip, 0, 0);
	hb_paint_funcs_set_color_func(cb, txt_paint_color, 0, 0);
	hb_paint_funcs_set_linear_gradient_func(cb, txt_paint_linear_gradient, 0, 0);
	hb_paint_funcs_set_radial_gradient_func(cb, txt_paint_radial_gradient, 0, 0);
	hb_paint_funcs_set_sweep_gradient_func(cb, txt_paint_sweep_gradient, 0, 0);
	hb_paint_funcs_set_push_group_func(cb, txt_paint_push_group, 0, 0);
	//hb_paint_funcs_set_push_group_for_func(cb, txt_paint_push_group_for, 0, 0);
	hb_paint_funcs_set_pop_group_func(cb, txt_paint_pop_group, 0, 0);
	return cb;
}
#endif // 1
