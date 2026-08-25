
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

static void ovg_quadratic_to(hb_draw_funcs_t*, void* data,
	hb_draw_state_t*,
	float cx1, float cy1,
	float to_x, float to_y, void*) {
	auto* c = static_cast<draw_ctx*>(data);
	c->ovg->quadratic_to(c->vg,
		c->x + cx1 * c->scale, c->y + (c->ascent - cy1 * c->scale),
		c->x + to_x * c->scale, c->y + (c->ascent - to_y * c->scale));
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
	hb_draw_funcs_set_quadratic_to_func(funcs, ovg_quadratic_to, nullptr, nullptr);
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

		unsigned int n;
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
	ovg_ctx_cb* cb = 0;
	rvg_t* cr = 0;
	ovg_canvas_cb* can = 0;
	hb_draw_funcs_t* draw_fill = 0;
	std::stack<glm::mat3x2> _skmat;
	std::stack<glm::ivec4> _skclip;
	std::stack<ovg_path_t*> _skclip_p;
	std::stack<rvg_t*> _vg;
	std::pmr::vector<rvg_t*> _rvgs;
	glm::ivec4 clip = { 0,0,-1,-1 };
	rvg_t* cvg = 0;
	ovg_path_t* clip_vg = 0;
	float scale = 1.0;
	glm::vec2 pos = {};
	glm::mat3x2 mat = glm::mat3x2(1.0);
	// 测试
	std::vector<glm::vec4> debugs;
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
	paint_text_cx* ctx = 0;
	ovg_canvas_cb* ovg = 0;
	ovg_path_t* vg = 0;
	glm::mat3x2 mat = glm::mat3x2(1.0);
};
glm::vec2 transform_point(const glm::mat3x2& mat, float x, float y)
{
	glm::vec3 tp = { x,y ,1.0 };
	return mat * tp;
}
glm::vec2 transform_point(const glm::mat3x2& mat, glm::vec2* ps)
{
	glm::vec3 tp = { *ps,1.0 };
	auto v = mat * tp;
	*ps = v;
	return v;
}

static void povg_move_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	glm::vec2 p = { to_x,to_y };
	transform_point(c->ctx->mat, &p);
	c->ovg->move_to(c->vg, p.x, -p.y);
}

static void povg_line_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	glm::vec2 p = { to_x,to_y };
	transform_point(c->ctx->mat, &p);
	c->ovg->line_to(c->vg, p.x, -p.y);
}

static void povg_quadratic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx1, float cy1, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	glm::vec2 p = { to_x,to_y };
	glm::vec2 cp = { cx1,cy1 };
	transform_point(c->ctx->mat, &p);
	transform_point(c->ctx->mat, &cp);
	c->ovg->quadratic_to(c->vg, cp.x, -cp.y, p.x, -p.y);
}

static void povg_cubic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx1, float cy1, float cx2, float cy2, float to_x, float to_y, void*) {
	auto* c = static_cast<pdraw_ctx*>(data);
	glm::vec2 p = { to_x,to_y };
	glm::vec2 cp = { cx1,cy1 };
	glm::vec2 cp2 = { cx2,cy2 };
	transform_point(c->ctx->mat, &p);
	transform_point(c->ctx->mat, &cp);
	transform_point(c->ctx->mat, &cp2);
	c->ovg->curve_to(c->vg, cp.x, -cp.y, cp2.x, -cp2.y, p.x, -p.y);
}

static void povg_close_path(hb_draw_funcs_t*, void* data, hb_draw_state_t*, void*) {
	auto* ctx = static_cast<pdraw_ctx*>(data);
	ctx->ovg->close_path(ctx->vg);
}

hb_draw_funcs_t* paint_text_cx::new_draw_funcs() {
	hb_draw_funcs_t* funcs = hb_draw_funcs_create();
	hb_draw_funcs_set_move_to_func(funcs, povg_move_to, nullptr, nullptr);
	hb_draw_funcs_set_line_to_func(funcs, povg_line_to, nullptr, nullptr);
	hb_draw_funcs_set_quadratic_to_func(funcs, povg_quadratic_to, nullptr, nullptr);
	hb_draw_funcs_set_cubic_to_func(funcs, povg_cubic_to, nullptr, nullptr);
	hb_draw_funcs_set_close_path_func(funcs, povg_close_path, nullptr, nullptr);
	draw_fill = funcs;
	return funcs;
}

void txt_paint_push_transform(hb_paint_funcs_t* funcs, void* paint_data, float xx, float yx, float xy, float yy, float dx, float dy, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	glm::mat3 m = glm::mat3x2(xx, yx, xy, yy, dx, dy);
	glm::mat3x3 oldm = ctx->mat;
	ctx->_skmat.push(oldm);
	ctx->mat = oldm * m;
	//printf("push %lld\t%.03f %.03f %.03f %.03f %.03f %.03f\n", ctx->_skmat.size(), xx, yx, xy, yy, dx, dy);
	//printf("\tcur  %.03f %.03f %.03f %.03f %.03f %.03f\n", oldm[0].x, oldm[0].y, oldm[1].x, oldm[1].y, oldm[2].x, oldm[2].y);
}
void txt_paint_pop_transform(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (!ctx->_skmat.empty()) {
		auto& m = ctx->_skmat.top();
		ctx->mat = m;
		//printf("pop %lld scale{%.03f %.03f}\n", ctx->_skmat.size(), m[0].x, m[1].y);
		ctx->_skmat.pop();
	}
}
void txt_paint_fill_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, hb_bool_t is_foreground, hb_color_t color, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (ctx && ctx->draw_fill) {
		ctx->can->set_source_color(ctx->cvg->st, color);
		pdraw_ctx pc = { };
		pc.ctx = ctx; pc.ovg = ctx->can; pc.vg = ctx->cvg->path;
		hb_font_draw_glyph(font, glyph, ctx->draw_fill, &pc);
		ctx->can->fill(ctx->cvg);
	}
}
void txt_paint_push_clip_glyph(hb_paint_funcs_t* funcs, void* paint_data, hb_codepoint_t glyph, hb_font_t* font, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	pdraw_ctx pc = { };
	pc.ctx = ctx; pc.ovg = ctx->can; pc.vg = ctx->cvg->path;
	hb_font_draw_glyph(font, glyph, ctx->draw_fill, &pc);
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
	glm::mat3x2 m = ctx->mat;
	glm::vec2 v[4] = { glm::vec2(xmin, ymin),glm::vec2(xmax, ymin)
		,glm::vec2(xmax, ymax),glm::vec2(xmin, ymax) };
	for (int i = 0; i < 4; i++)
	{
		transform_point(ctx->mat, &v[i]);
		v[i].y *= -1;
	}

	float fmin_x = v[0].x, fmin_y = v[0].y, fmax_x = v[0].x, fmax_y = v[0].y;
	for (unsigned i = 1; i < 4; i++)
	{
		fmin_x = std::min(fmin_x, v[i].x); fmin_y = std::min(fmin_y, v[i].y);
		fmax_x = std::max(fmax_x, v[i].x); fmax_y = std::max(fmax_y, v[i].y);
	}

	int px0 = floorf(fmin_x);
	int py0 = floorf(fmin_y);
	int px1 = ceilf(fmax_x);
	int py1 = ceilf(fmax_y);

	glm::ivec4 c = { px0, py0, px1 - px0, py1 - py0 };
	ctx->_skclip.push(ctx->clip);
	ctx->clip = c;
	ctx->can->set_clip_rect(ctx->cvg, &c);
}
void txt_paint_pop_clip(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (ctx->_skclip.size()) {
		ctx->clip = ctx->_skclip.top();
		ctx->_skclip.pop();
		ctx->can->set_clip_rect(ctx->cvg, &ctx->clip);
	}
}
void txt_paint_color(hb_paint_funcs_t* funcs, void* paint_data, hb_bool_t is_foreground, hb_color_t color, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	ctx->can->set_source_color(ctx->cvg->st, color);
	ctx->cvg->st->pattern = 0;
	ctx->can->fill(ctx->cvg);
}

void reduce_linear_anchors(float x0, float y0, float x1, float y1, float x2, float y2, glm::vec2* p0, glm::vec2* p1)
{
	float q2x = x2 - x0, q2y = y2 - y0;
	float s = q2x * q2x + q2y * q2y;
	if (s < 1e-6f)
	{
		p0->x = x0; p0->y = y0;
		p1->x = x1; p1->y = y1;
		return;
	}
	float q1x = x1 - x0, q1y = y1 - y0;
	float k = (q2x * q1x + q2y * q1y) / s;
	p0->x = x0;
	p0->y = y0;
	p1->x = x1 - k * q2x;
	p1->y = y1 - k * q2y;
}
void paint_normalize_color_line(hb_color_stop_t* stops,
	unsigned int     len,
	float* min,
	float* max)
{
	if ((!len))
	{
		*min = *max = 0.f;
		return;
	}

	qsort(stops, len, sizeof(hb_color_stop_t), [](const void* aa, const void* bb) {
		auto a = (hb_color_stop_t*)aa; auto b = (hb_color_stop_t*)bb;
		return (a->offset > b->offset) - (a->offset < b->offset);
		});

	float mn = stops[0].offset, mx = stops[0].offset;
	for (unsigned i = 1; i < len; i++)
	{
		mn = std::min(mn, stops[i].offset);
		mx = std::max(mx, stops[i].offset);
	}
	if (mn != mx)
		for (unsigned i = 0; i < len; i++)
			stops[i].offset = (stops[i].offset - mn) / (mx - mn);

	*min = mn;
	*max = mx;
}

void txt_paint_linear_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line
	, float x0, float y0, float x1, float y1, float x2, float y2, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (!ctx || !ctx->can || !ctx->can->ac)return;
	glm::vec2 p[] = { glm::vec2(x0, y0) , glm::vec2(x1, y1) ,glm::vec2(x2, y2) };
	glm::vec2 p0, p1;
	reduce_linear_anchors(p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, &p0, &p1);

	glm::vec4 cp[2] = { {p0,p1},{} };
	transform_point(ctx->mat, &p0);
	transform_point(ctx->mat, &p1);
	float gx0 = 0.0f;
	float gy0 = 0.0f;
	float gx1 = 0.0f;
	float gy1 = 0.0f;
	uint32_t n1 = 0;
	vg_pattern_t* pat = 0;
	unsigned int n = hb_color_line_get_color_stops(color_line, 0, &n1, NULL);
	if (n > 0) {
		hb_color_stop_t stops[32];
		hb_color_line_get_color_stops(color_line, 0, &n, stops);
		float mn, mx;
		paint_normalize_color_line(stops, n, &mn, &mx);
		auto l0 = p0;
		auto l1 = p1;
		/* Apply normalization to endpoints */
		gx0 = l0.x + mn * (l1.x - l0.x);
		gy0 = l0.y + mn * (l1.y - l0.y);
		gx1 = l0.x + mx * (l1.x - l0.x);
		gy1 = l0.y + mx * (l1.y - l0.y);
		pat = ctx->can->new_pattern_linear(ctx->can->ac, gx0, -gy0, gx1, -gy1);
		if (!pat) return;
		//ctx->can->pattern_set_matrix(pat, &ctx->mat);

		for (unsigned int i = 0; i < n; ++i) {
			hb_color_t c = stops[i].color;
			ctx->can->pattern_add_color_stop(
				pat,
				stops[i].offset,
				hb_color_get_red(c) / 255.0f,
				hb_color_get_green(c) / 255.0f,
				hb_color_get_blue(c) / 255.0f,
				hb_color_get_alpha(c) / 255.0f
			);
		}

		/* 4. extend mode */
		hb_paint_extend_t extend = hb_color_line_get_extend(color_line);
		int vg_extend =
			(extend == HB_PAINT_EXTEND_REPEAT) ? 2 : // 按 Cairo 惯例
			(extend == HB_PAINT_EXTEND_REFLECT) ? 1 :
			0; // PAD
		ctx->can->pattern_set_extend(pat, vg_extend);
		ctx->cvg->st->pattern = pat;
	}
	else {
		ctx->cvg->st->pattern = 0;
	}
	ctx->can->fill(ctx->cvg);
	ctx->can->pattern_destroy(pat);
#ifdef _DEBUG
	ctx->debugs.push_back({ p0.x, -p0.y, abs(p1.x - p0.x), abs(p1.y - p0.y) });

#endif // _DEBUG

	//printf("fill linear\n");
}
void txt_paint_radial_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float r0, float x1, float y1, float r1, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	glm::vec3 p0 = { x0,y0,r0 * ctx->scale }, p1 = { x1,y1,r1 * ctx->scale };
	glm::vec2 p[3] = { p0,p1, glm::vec2(r0 * ctx->scale, r1 * ctx->scale) };
	glm::vec4 cp[2] = { {x0,y0,r0,0 },{x1,y1,r1 ,0} };
	transform_point(ctx->mat, &p[0]);
	transform_point(ctx->mat, &p[1]);
	vg_pattern_t* pat = ctx->can->new_pattern_radial(ctx->can->ac, p[0].x, -p[0].y, p[2].x, p[1].x, -p[1].y, p[2].y, false);
	if (!pat) return;
	//ctx->can->pattern_set_matrix(pat, &ctx->mat);
	/* color stops */
	unsigned int n = hb_color_line_get_color_stops(color_line, 0, 0, NULL);
	if (n > 0) {
		hb_color_stop_t stops[32];
		hb_color_line_get_color_stops(color_line, 0, &n, stops);
		float mn, mx;
		paint_normalize_color_line(stops, n, &mn, &mx);
		float cx0 = x0 + mn * (x1 - x0);
		float cy0 = y0 + mn * (y1 - y0);
		float cr0 = r0 + mn * (r1 - r0);
		float cx1 = x0 + mx * (x1 - x0);
		float cy1 = y0 + mx * (y1 - y0);
		float cr1 = r0 + mx * (r1 - r0);

		for (unsigned int i = 0; i < n; ++i) {
			hb_color_t c = stops[i].color;
			ctx->can->pattern_add_color_stop(
				pat,
				stops[i].offset,
				hb_color_get_red(c) / 255.0f,
				hb_color_get_green(c) / 255.0f,
				hb_color_get_blue(c) / 255.0f,
				hb_color_get_alpha(c) / 255.0f
			);
		}
		/* extend */
		hb_paint_extend_t extend = hb_color_line_get_extend(color_line);
		int vg_extend =
			(extend == HB_PAINT_EXTEND_REPEAT) ? 2 :
			(extend == HB_PAINT_EXTEND_REFLECT) ? 1 :
			0;
		ctx->can->pattern_set_extend(pat, vg_extend);
		ctx->cvg->st->pattern = pat;
	}
	else {
		ctx->cvg->st->pattern = 0;
	}
	ctx->can->fill(ctx->cvg);
	ctx->can->pattern_destroy(pat);
	//printf("fill radial\n");
	ctx->debugs.push_back({ p[0].x, -p[0].y,0,0 });
	ctx->debugs.push_back({ p[1].x, -p[1].y,0,0 });
}
void txt_paint_sweep_gradient(hb_paint_funcs_t* funcs, void* paint_data, hb_color_line_t* color_line, float x0, float y0, float start_angle, float end_angle, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	glm::vec2 p = glm::vec2(x0, y0) * ctx->scale;
	vg_pattern_t* pat = ctx->can->new_pattern_sweep(ctx->can->ac, p.x, -p.y, start_angle, end_angle);
	if (!pat) return;
	ctx->can->pattern_set_matrix(pat, &ctx->mat);
	/* color stops */
	unsigned int n = hb_color_line_get_color_stops(color_line, 0, 0, NULL);
	if (n > 0) {
		hb_color_stop_t stops[32];
		hb_color_line_get_color_stops(color_line, 0, &n, stops);
		float mn, mx;
		paint_normalize_color_line(stops, n, &mn, &mx);
		float a0 = start_angle + mn * (end_angle - start_angle);
		float a1 = start_angle + mx * (end_angle - start_angle);
		float angle_range = a1 - a0;
		for (unsigned int i = 0; i < n; ++i) {
			hb_color_t c = stops[i].color;
			ctx->can->pattern_add_color_stop(
				pat,
				stops[i].offset,
				hb_color_get_red(c) / 255.0f,
				hb_color_get_green(c) / 255.0f,
				hb_color_get_blue(c) / 255.0f,
				hb_color_get_alpha(c) / 255.0f
			);
		}
	}
	hb_paint_extend_t extend = hb_color_line_get_extend(color_line);
	int vg_extend =
		(extend == HB_PAINT_EXTEND_REPEAT) ? 2 :
		(extend == HB_PAINT_EXTEND_REFLECT) ? 1 :
		0;
	ctx->can->pattern_set_extend(pat, vg_extend);
	ctx->cvg->st->pattern = pat;
	ctx->can->fill(ctx->cvg);
	ctx->can->pattern_destroy(pat);
	//printf("fill sweep\n");
}
void txt_paint_push_group(hb_paint_funcs_t* funcs, void* paint_data, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	auto vg = ctx->can->new_rvg(ctx->can->ac);
	auto path = ctx->can->new_path(ctx->can->ac);
	auto st = ctx->can->new_state(ctx->can->ac);
	ctx->_rvgs.push_back(vg);
	ctx->can->set_path(vg, path, st);
	ctx->_vg.push(ctx->cvg);
	ctx->cvg = vg;
}
void txt_paint_push_group_for(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	return;
}
void txt_paint_pop_group(hb_paint_funcs_t* funcs, void* paint_data, hb_paint_composite_mode_t mode, void* user_data) {
	auto ctx = (paint_text_cx*)paint_data;
	if (ctx->_vg.size())
	{
		auto vg = ctx->_vg.top();// todo 叠加rvg_t渲染
		ctx->_vg.pop();
		ctx->cvg = vg;
	}
}
hb_paint_funcs_t* new_hbpaint_cb() {
	hb_paint_funcs_t* cb = hb_paint_funcs_create();
	hb_paint_funcs_set_push_transform_func(cb, txt_paint_push_transform, 0, 0);
	hb_paint_funcs_set_pop_transform_func(cb, txt_paint_pop_transform, 0, 0);
	hb_paint_funcs_set_fill_glyph_func(cb, txt_paint_fill_glyph, 0, 0);
	hb_paint_funcs_set_push_clip_glyph_func(cb, txt_paint_push_clip_glyph, 0, 0);
	//hb_paint_funcs_set_push_clip_rectangle_func(cb, txt_paint_push_clip_rectangle, 0, 0);
	hb_paint_funcs_set_pop_clip_func(cb, txt_paint_pop_clip, 0, 0);
	hb_paint_funcs_set_color_func(cb, txt_paint_color, 0, 0);
	hb_paint_funcs_set_linear_gradient_func(cb, txt_paint_linear_gradient, 0, 0);
	hb_paint_funcs_set_radial_gradient_func(cb, txt_paint_radial_gradient, 0, 0);
	hb_paint_funcs_set_sweep_gradient_func(cb, txt_paint_sweep_gradient, 0, 0);
	hb_paint_funcs_set_push_group_func(cb, txt_paint_push_group, 0, 0);
	hb_paint_funcs_set_pop_group_func(cb, txt_paint_pop_group, 0, 0);
	return cb;
}
glm::mat3x2 flipY(float surfaceHeight)
{
	return glm::mat3x2(
		1.0f, 0.0f,   // col 0: (sx, shx)
		0.0f, -1.0f,   // col 1: (shy, sy)
		0.0f, surfaceHeight // col 2: (tx, ty)
	);
}
glm::mat3 orthoYDown(float w, float h)
{
	auto r = glm::mat3x2(
		2.0f / w, 0.0f,
		0.0f, -2.0f / h,
		-1.0f, 1.0f
	);
	return r;
}
int hb_ovg_render_glyph(hb_font_t* font, unsigned long glyph, paint_text_cx* ctx, vg_text_extents_t* extents);
int hb_ovg_render_color_glyph(hb_font_t* font, uint32_t glyph, paint_text_cx* ctx, vg_text_extents_t* extents);

void render_text_print(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_canvas_cb* ovg, ovg_ctx_cb* ctx, rvg_t* vg, const glm::uvec3& color)
{
	if (!ffs || !str8 || !len || !ovg || ffs->count == 0) return;
	static std::vector<uint32_t> utf32;
	utf32.clear();
	utf8_to_utf32(str8, len, &utf32);
	hb_font_t* primary = ffs->familys[0].font; // 主字体（可按 script 选）
	auto pactx = new paint_text_cx();
	pactx->can = ovg;
	pactx->cvg = vg;
	pactx->cb = ctx;
	pactx->cr = vg;
	hb_paint_funcs_t* df = new_hbpaint_cb();
	hb_buffer_t* buf = hb_buffer_create();
	hb_buffer_add_utf32(buf, utf32.data(), utf32.size(), 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_shape(primary, buf, nullptr, 0);

	uint32_t n;
	hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
	hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

	float pen_x = x, pen_y = y;
	size_t run_start = 0;
	double h = color.z;
	while (run_start < utf32.size()) {
		uint32_t cp = utf32[run_start];
		const font_family_t* ff = resolve_family(ffs, cp);
		if (!ff) ff = &ffs->familys[0];
		float sc1 = 1.0;
		double upem = hb_face_get_upem(hb_font_get_face(ff->font));
		if (h > 0)
		{
			hb_font_set_scale(ff->font, h, h);
		}
		glm::ivec2 sss = {};
		hb_font_get_scale(ff->font, &sss.x, &sss.y);
		sc1 = h;
		float fixed_scale = h / upem;
		pactx->scale = sc1 / upem;
		auto sc = pactx->scale;
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
		static bool is_draw_debug = false;
		unsigned int n;
		hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
		hb_glyph_position_t* pos =
			hb_buffer_get_glyph_positions(buf, nullptr);
		glm::mat3x2 m = {};
		int subpixel_bits = 6;
		float sx = scalbnf(1.f, -(int)subpixel_bits);
		float sy = scalbnf(1.f, -(int)subpixel_bits);
		float scale_factor = scalbnf(1.f, (int)subpixel_bits);
		glm::mat3 flip = flipY(vg->height);
		glm::mat3x2 yd = flip * orthoYDown(vg->width, vg->height);
		for (unsigned i = 0; i < n; i++) {
			pactx->pos = { };
			ovg->identity_matrix(vg->st);
			ovg->translate(vg->st, pen_x, pen_y);
			ovg->get_matrix(vg->st, &m);
			ovg->set_source_color(vg->st, color.x);
			//hb_font_paint_glyph(ff->font, info[i].codepoint, df, pactx, 0, color.x);
			hb_ovg_render_color_glyph(ff->font, info[i].codepoint, pactx, 0);
			//hb_ovg_render_glyph(ff->font, info[i].codepoint, pactx, 0);

			pen_x += floorf(pos[i].x_advance); // ✅ 像素对齐

			if (is_draw_debug) {
				ovg->set_line_width(vg->st, 2);
				for (auto& c : pactx->debugs)
				{
					if (c.z > 0)
					{
					}
					else
					{
						ovg->circle(vg->path, c.x, c.y, 6);
					}
				}
				ovg->set_source_rgb(vg->st, 0, 0, 0);
				ovg->stroke(vg);
				for (auto& c : pactx->debugs)
				{
					if (c.z > 0)
					{
						ovg->rectangle(vg->path, c.x, c.y, std::max(4.0f, c.z), std::max(4.0f, c.w));
					}
				}
				ovg->set_source_rgb(vg->st, 1, 0, 0);
				ovg->stroke(vg);
			}
			pactx->debugs.clear();
		}
		hb_buffer_destroy(buf);
		run_start = run_end;
	}
	delete pactx;
	hb_buffer_destroy(buf);
	hb_paint_funcs_destroy(df);
	ovg->set_source_color(vg->st, color.x);
	ovg->fill_preserve(vg);
	ovg->set_source_color(vg->st, color.y);
	ovg->stroke(vg);
}



#endif // 1


#ifndef NOT_HAVE_OVG

struct ovg_t {
	ovg_ctx_cb* cb;
	rvg_t* cr;
};
static void
hb_ovg_move_to(hb_draw_funcs_t* dfuncs,
	void* draw_data,
	hb_draw_state_t* st,
	float to_x, float to_y,
	void* user_data)
{
	auto ctx = (ovg_t*)draw_data;
	ctx->cb->move_to(ctx->cr, (double)to_x, (double)to_y);
}

static void
hb_ovg_line_to(hb_draw_funcs_t* dfuncs,
	void* draw_data,
	hb_draw_state_t* st,
	float to_x, float to_y,
	void* user_data)
{
	auto ctx = (ovg_t*)draw_data;

	ctx->cb->line_to(ctx->cr, (double)to_x, (double)to_y);
}

static void
hb_ovg_cubic_to(hb_draw_funcs_t* dfuncs,
	void* draw_data,
	hb_draw_state_t* st,
	float control1_x, float control1_y,
	float control2_x, float control2_y,
	float to_x, float to_y,
	void* user_data)
{
	auto ctx = (ovg_t*)draw_data;

	ctx->cb->curve_to(ctx->cr,
		(double)control1_x, (double)control1_y,
		(double)control2_x, (double)control2_y,
		(double)to_x, (double)to_y);
}

static void
hb_ovg_close_path(hb_draw_funcs_t* dfuncs,
	void* draw_data,
	hb_draw_state_t* st,
	void* user_data)
{
	auto ctx = (ovg_t*)draw_data;

	ctx->cb->close_path(ctx->cr);
}

static hb_draw_funcs_t* create_df()
{
	hb_draw_funcs_t* funcs = hb_draw_funcs_create();

	hb_draw_funcs_set_move_to_func(funcs, hb_ovg_move_to, nullptr, nullptr);
	hb_draw_funcs_set_line_to_func(funcs, hb_ovg_line_to, nullptr, nullptr);
	hb_draw_funcs_set_cubic_to_func(funcs, hb_ovg_cubic_to, nullptr, nullptr);
	hb_draw_funcs_set_close_path_func(funcs, hb_ovg_close_path, nullptr, nullptr);

	hb_draw_funcs_make_immutable(funcs);


	return funcs;
}


static hb_draw_funcs_t*
hb_ovg_draw_get_funcs()
{
	static hb_draw_funcs_t* df = create_df();
	return df;
}


#ifndef NOT_HAVE_OVG_USER_FONT_FACE_SET_RENDER_COLOR_GLYPH_FUNC

static void
hb_ovg_push_transform(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	float xx, float yx,
	float xy, float yy,
	float dx, float dy,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	glm::mat3x2 m;
	ctx->cb->save(ctx->cr);
	ctx->cb->matrix_init(&m, (double)xx, (double)yx,
		(double)xy, (double)yy,
		(double)dx, (double)dy);
	ctx->cb->transform(ctx->cr, &m);
}

static void
hb_ovg_pop_transform(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;


	ctx->cb->restore(ctx->cr);
}

static void
hb_ovg_fill_glyph(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_codepoint_t glyph,
	hb_font_t* font,
	hb_bool_t use_foreground,
	hb_color_t color,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;


	ovg_t o = {};
	o.cb = ctx->cb;
	o.cr = ctx->cr;
	ctx->cb->save(ctx->cr);

	ctx->cb->new_path(ctx->cr);
	hb_font_draw_glyph(font, glyph, hb_ovg_draw_get_funcs(), &o);
	ctx->cb->close_path(ctx->cr);
	if (use_foreground) {}
	else {
		ctx->cb->set_source_rgba(ctx->cr,
			hb_color_get_red(color) / 255.,
			hb_color_get_green(color) / 255.,
			hb_color_get_blue(color) / 255.,
			hb_color_get_alpha(color) / 255.);
	}
	ctx->cb->fill(ctx->cr);

	ctx->cb->restore(ctx->cr);
}

static hb_bool_t
hb_ovg_paint_color_glyph(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_codepoint_t glyph,
	hb_font_t* font,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;


	ctx->cb->save(ctx->cr);

	hb_position_t x_scale, y_scale;
	hb_font_get_scale(font, &x_scale, &y_scale);
	ctx->cb->scale(ctx->cr, x_scale, -y_scale);

	//cairo_glyph_t cairo_glyph = { glyph, 0, 0 };
	//ctx->cb->set_scaled_font(ctx->cr, c->scaled_font);
	//ctx->cb->set_font_size(ctx->cr, 1);
	//ctx->cb->show_glyphs(ctx->cr, &cairo_glyph, 1);

	ctx->cb->restore(ctx->cr);

	return true;
}

static void
hb_ovg_push_clip_glyph(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_codepoint_t glyph,
	hb_font_t* font,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;


	ctx->cb->save(ctx->cr);
	ctx->cb->new_path(ctx->cr);
	ovg_t o = {};
	o.cb = ctx->cb;
	o.cr = ctx->cr;
	hb_font_draw_glyph(font, glyph, hb_ovg_draw_get_funcs(), &o);

	ctx->cb->close_path(ctx->cr);
	//ctx->cb->clip(ctx->cr);
}

static void
hb_ovg_push_clip_rectangle(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	float xmin, float ymin, float xmax, float ymax,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;

	ctx->cb->save(ctx->cr);
	ctx->cb->rectangle(ctx->cr,
		(double)xmin, (double)ymin,
		(double)(xmax - xmin), (double)(ymax - ymin));
	ctx->cb->clip(ctx->cr);
}

static hb_draw_funcs_t*
hb_ovg_push_clip_path_start(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	void** draw_data,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;


	ctx->cb->save(ctx->cr);
	ctx->cb->new_path(ctx->cr);
	*draw_data = ctx->cr;
	return hb_ovg_draw_get_funcs();
}

static void
hb_ovg_push_clip_path_end(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	ctx->cb->close_path(ctx->cr);
	ctx->cb->clip(ctx->cr);
}

static void
hb_ovg_pop_clip(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	ctx->cb->restore(ctx->cr);
}

static void
hb_ovg_push_group(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	ctx->cb->save(ctx->cr);
	//ctx->cb->push_group(ctx->cr);
}

static void
hb_ovg_pop_group(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_paint_composite_mode_t mode,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	//ctx->cb->pop_group_to_source(ctx->cr);
	//ctx->cb->set_operator(ctx->cr, _hb_paint_composite_mode_to_cairo(mode));
	//ctx->cb->paint(ctx->cr);

	ctx->cb->restore(ctx->cr);
}

static void
hb_ovg_paint_color(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_bool_t use_foreground,
	hb_color_t color,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	//_hb_ovg_set_source_color(c, use_foreground, color);
	ctx->cb->paint(ctx->cr);
}

static hb_bool_t
hb_ovg_paint_image(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_blob_t* blob,
	unsigned width,
	unsigned height,
	hb_tag_t format,
	float slant,
	hb_glyph_extents_t* extents,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;
	return 0;
	//return _hb_ovg_paint_glyph_image(c, blob, width, height, format, slant, extents);
}
#ifndef likely
#define likely(expr) (expr)
#define unlikely(expr) (expr)
#endif

static inline void*
hb_malloc2vg(size_t nmemb, size_t size)
{
	if (size && nmemb > SIZE_MAX / size) return nullptr;
	return hb_malloc(nmemb * size);
}
static inline void*
hb_realloc2vg(void* ptr, size_t nmemb, size_t size)
{
	if (size && nmemb > SIZE_MAX / size) return nullptr;
	return hb_realloc(ptr, nmemb * size);
}

static bool
_hb_ovg_get_color_stops(paint_text_cx* c,
	hb_color_line_t* color_line,
	unsigned* count,
	hb_color_stop_t** stops)
{
	unsigned len = hb_color_line_get_color_stops(color_line, 0, nullptr, nullptr);
	bool allocated = false;
	if (unlikely(!len))
		return false;
	if (len > *count)
	{
		*stops = (hb_color_stop_t*)hb_malloc2vg(len, sizeof(hb_color_stop_t));
		if (unlikely(!*stops))
			return false;
		allocated = true;
	}
	hb_color_line_get_color_stops(color_line, 0, &len, *stops);
	if (unlikely(!len))
	{
		if (allocated)
		{
			hb_free(*stops);
			*stops = nullptr;
		}
		return false;
	}
	for (unsigned i = 0; i < len; i++)
		if ((*stops)[i].is_foreground)
		{
#ifdef HAVE_OVG_USER_SCALED_FONT_GET_FOREGROUND_SOURCE
			double r, g, b, a;
			vg_pattern_t* foreground = cairo_user_scaled_font_get_foreground_source(c->scaled_font);
			if (cairo_pattern_get_rgba(foreground, &r, &g, &b, &a) == CAIRO_STATUS_SUCCESS)
				(*stops)[i].color = HB_COLOR(round(b * 255.), round(g * 255.), round(r * 255.),
					round(a * hb_color_get_alpha((*stops)[i].color)));
			else
#endif
				(*stops)[i].color = HB_COLOR(0, 0, 0, hb_color_get_alpha((*stops)[i].color));
		}

	*count = len;
	return true;
}
int hb_ovg_extend(hb_paint_extend_t extend) {
	int vg_extend =
		(extend == HB_PAINT_EXTEND_REPEAT) ? 2 : // 按 Cairo 惯例
		(extend == HB_PAINT_EXTEND_REFLECT) ? 1 :
		0; // PAD
	return vg_extend;
}

void
_hb_ovg_paint_linear_gradient(paint_text_cx* c,
	hb_color_line_t* color_line,
	float x0, float y0,
	float x1, float y1,
	float x2, float y2)
{
	auto cr = c->cr;

	unsigned int len = 32;
	hb_color_stop_t stops_[32];
	hb_color_stop_t* stops = stops_;
	float xx0, yy0, xx1, yy1;
	float xxx0, yyy0, xxx1, yyy1;
	float min, max;
	vg_pattern_t* pattern = 0;

	if (unlikely(!_hb_ovg_get_color_stops(c, color_line, &len, &stops)))
		return;
	hb_paint_normalize_color_line(stops, len, &min, &max);

	hb_paint_reduce_linear_anchors(x0, y0, x1, y1, x2, y2, &xx0, &yy0, &xx1, &yy1);

	xxx0 = xx0 + min * (xx1 - xx0);
	yyy0 = yy0 + min * (yy1 - yy0);
	xxx1 = xx0 + max * (xx1 - xx0);
	yyy1 = yy0 + max * (yy1 - yy0);

	pattern = c->cb->new_pattern_linear(cr, (double)xxx0, (double)yyy0, (double)xxx1, (double)yyy1);
	c->cb->pattern_set_extend(pattern, hb_ovg_extend(hb_color_line_get_extend(color_line)));
	for (unsigned int i = 0; i < len; i++)
	{
		double r, g, b, a;
		r = hb_color_get_red(stops[i].color) / 255.;
		g = hb_color_get_green(stops[i].color) / 255.;
		b = hb_color_get_blue(stops[i].color) / 255.;
		a = hb_color_get_alpha(stops[i].color) / 255.;
		c->cb->pattern_add_color_stop(pattern, (double)stops[i].offset, r, g, b, a);
	}

	c->cb->set_source(cr, pattern);
	c->cb->set_source_rgb(cr, 1, 1, 0);
	c->cb->paint(cr);

	//c->cb->pattern_destroy(pattern);

	if (stops != stops_)
		hb_free(stops);
}

void
_hb_ovg_paint_radial_gradient(paint_text_cx* c,
	hb_color_line_t* color_line,
	float x0, float y0, float r0,
	float x1, float y1, float r1)
{
	auto cr = c->cr;

	unsigned int len = 32;
	hb_color_stop_t stops_[32];
	hb_color_stop_t* stops = stops_;
	float min, max;
	float xx0, yy0, xx1, yy1;
	float rr0, rr1;
	vg_pattern_t* pattern;

	if (unlikely(!_hb_ovg_get_color_stops(c, color_line, &len, &stops)))
		return;
	hb_paint_normalize_color_line(stops, len, &min, &max);

	xx0 = x0 + min * (x1 - x0);
	yy0 = y0 + min * (y1 - y0);
	xx1 = x0 + max * (x1 - x0);
	yy1 = y0 + max * (y1 - y0);
	rr0 = r0 + min * (r1 - r0);
	rr1 = r0 + max * (r1 - r0);

	pattern = c->cb->new_pattern_radial(cr, (double)xx0, (double)yy0, (double)rr0, (double)xx1, (double)yy1, (double)rr1, false);
	c->cb->pattern_set_extend(pattern, hb_ovg_extend(hb_color_line_get_extend(color_line)));

	for (unsigned int i = 0; i < len; i++)
	{
		double r, g, b, a;
		r = hb_color_get_red(stops[i].color) / 255.;
		g = hb_color_get_green(stops[i].color) / 255.;
		b = hb_color_get_blue(stops[i].color) / 255.;
		a = hb_color_get_alpha(stops[i].color) / 255.;
		c->cb->pattern_add_color_stop(pattern, (double)stops[i].offset, r, g, b, a);
	}

	c->cb->set_source(cr, pattern);
	c->cb->set_source_rgb(cr, 1, 0, 0);
	c->cb->paint(cr);

	//c->cb->pattern_destroy(pattern);

	if (stops != stops_)
		hb_free(stops);
}

void
_hb_ovg_paint_sweep_gradient(paint_text_cx* c,
	hb_color_line_t* color_line,
	float cx, float cy,
	float start_angle,
	float end_angle)
{
	auto cr = c->cr;

	unsigned int len = 32;
	hb_color_stop_t stops_[32];
	hb_color_stop_t* stops = stops_;
	vg_extend_t extend;
	double x1, y1, x2, y2;
	float max_x, max_y, radius;
	vg_pattern_t* pattern;

	if (unlikely(!_hb_ovg_get_color_stops(c, color_line, &len, &stops)))
		return;

	//hb_array_t<hb_color_stop_t>(stops, len)
	//	.qsort([](const hb_color_stop_t& a, const hb_color_stop_t& b) {
	//	return (a.offset > b.offset) - (a.offset < b.offset);
	//		});
	qsort(stops, len, sizeof(hb_color_stop_t), [](const void* aa, const void* bb) {
		auto a = (hb_color_stop_t*)aa; auto b = (hb_color_stop_t*)bb;
		return (a->offset > b->offset) - (a->offset < b->offset);
		});
	int crt[4] = {};
	c->cb->get_clip_rect(cr, crt);
	//c->cb->clip_extents(cr, &x1, &y1, &x2, &y2);
	max_x = (float)std::max((x1 - (double)cx) * (x1 - (double)cx), (x2 - (double)cx) * (x2 - (double)cx));
	max_y = (float)std::max((y1 - (double)cy) * (y1 - (double)cy), (y2 - (double)cy) * (y2 - (double)cy));
	radius = sqrtf(max_x + max_y);

	extend = (vg_extend_t)hb_ovg_extend(hb_color_line_get_extend(color_line));
	//pattern = cairo_pattern_create_mesh();
	//_hb_cairo_add_sweep_gradient_patches(stops, len, extend, cx, cy, radius, start_angle, end_angle, pattern);
	c->cb->set_source(cr, pattern);
	c->cb->paint(cr);

	//c->cb->pattern_destroy(pattern);

	if (stops != stops_)
		hb_free(stops);
}

static void
hb_ovg_paint_linear_gradient(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_color_line_t* color_line,
	float x0, float y0,
	float x1, float y1,
	float x2, float y2,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;

	_hb_ovg_paint_linear_gradient(ctx, color_line, x0, y0, x1, y1, x2, y2);
}

static void
hb_ovg_paint_radial_gradient(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_color_line_t* color_line,
	float x0, float y0, float r0,
	float x1, float y1, float r1,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;

	_hb_ovg_paint_radial_gradient(ctx, color_line, x0, y0, r0, x1, y1, r1);
}

static void
hb_ovg_paint_sweep_gradient(hb_paint_funcs_t* pfuncs,
	void* paint_data,
	hb_color_line_t* color_line,
	float x0, float y0,
	float start_angle, float end_angle,
	void* user_data)
{
	paint_text_cx* ctx = (paint_text_cx*)paint_data;

	_hb_ovg_paint_sweep_gradient(ctx, color_line, x0, y0, start_angle, end_angle);
}

//static const cairo_user_data_key_t color_cache_key = { 0 };

static void
_hb_ovg_destroy_map(void* p)
{
	hb_map_destroy((hb_map_t*)p);
}

static hb_bool_t
hb_ovg_paint_custom_palette_color(hb_paint_funcs_t* funcs,
	void* paint_data,
	unsigned int color_index,
	hb_color_t* color,
	void* user_data)
{
	return false;
}

static hb_paint_funcs_t* hb_ovg_paint_get_funcs()
{
	static hb_paint_funcs_t* funcs = hb_paint_funcs_create();

	hb_paint_funcs_set_push_transform_func(funcs, hb_ovg_push_transform, nullptr, nullptr);
	hb_paint_funcs_set_pop_transform_func(funcs, hb_ovg_pop_transform, nullptr, nullptr);
	hb_paint_funcs_set_fill_glyph_func(funcs, hb_ovg_fill_glyph, nullptr, nullptr);
	hb_paint_funcs_set_color_glyph_func(funcs, hb_ovg_paint_color_glyph, nullptr, nullptr);
	hb_paint_funcs_set_push_clip_glyph_func(funcs, hb_ovg_push_clip_glyph, nullptr, nullptr);
	//hb_paint_funcs_set_push_clip_rectangle_func(funcs, hb_ovg_push_clip_rectangle, nullptr, nullptr);
	hb_paint_funcs_set_push_clip_path_start_func(funcs, hb_ovg_push_clip_path_start, nullptr, nullptr);
	hb_paint_funcs_set_push_clip_path_end_func(funcs, hb_ovg_push_clip_path_end, nullptr, nullptr);
	hb_paint_funcs_set_pop_clip_func(funcs, hb_ovg_pop_clip, nullptr, nullptr);
	hb_paint_funcs_set_push_group_func(funcs, hb_ovg_push_group, nullptr, nullptr);
	hb_paint_funcs_set_pop_group_func(funcs, hb_ovg_pop_group, nullptr, nullptr);
	hb_paint_funcs_set_color_func(funcs, hb_ovg_paint_color, nullptr, nullptr);
	hb_paint_funcs_set_image_func(funcs, hb_ovg_paint_image, nullptr, nullptr);
	hb_paint_funcs_set_linear_gradient_func(funcs, hb_ovg_paint_linear_gradient, nullptr, nullptr);
	hb_paint_funcs_set_radial_gradient_func(funcs, hb_ovg_paint_radial_gradient, nullptr, nullptr);
	hb_paint_funcs_set_sweep_gradient_func(funcs, hb_ovg_paint_sweep_gradient, nullptr, nullptr);
	hb_paint_funcs_set_custom_palette_color_func(funcs, hb_ovg_paint_custom_palette_color, nullptr, nullptr);

	hb_paint_funcs_make_immutable(funcs);

	//hb_atexit(free_static_cairo_paint_funcs);

	return funcs;
}
#endif

static int hb_ovg_render_glyph(hb_font_t* font, unsigned long glyph, paint_text_cx* ctx, vg_text_extents_t* extents)
{
	hb_position_t x_scale = 1, y_scale = 1;
	//hb_font_get_scale(font, &x_scale, &y_scale);

	ovg_t o = {};
	o.cb = ctx->cb;
	o.cr = ctx->cr;
	ctx->cb->scale(ctx->cr, +1. / (x_scale ? x_scale : 1), -1. / (y_scale ? y_scale : 1));
	if (hb_font_draw_glyph_or_fail(font, glyph, hb_ovg_draw_get_funcs(), &o))
		ctx->cb->fill(ctx->cr);

	return 0;
}

#ifndef NOT_USER_FONT_FACE_SET_RENDER_COLOR_GLYPH_FUNC

int hb_ovg_render_color_glyph(hb_font_t* font, uint32_t glyph, paint_text_cx* ctx, vg_text_extents_t* extents)
{
	unsigned int palette = 0;
	hb_color_t color = HB_COLOR(0, 0, 0, 255);
	hb_position_t x_scale = 0, y_scale = 0;
	//hb_font_get_scale(font, &x_scale, &y_scale);
	double sx = 0.05, sy = 0.05;
	ctx->cb->scale(ctx->cr, +1. / (sx > 0. ? sx : 1), -1. / (sy > 0. ? sy : 1));

	hb_font_paint_glyph(font, glyph, hb_ovg_paint_get_funcs(), ctx, palette, color);

	return 0;
}


#endif 

#endif 
