
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_rect_pack.h>

struct vg_font :public font_family_t {
	hb_buffer_t* hb_buffer;
	hb_language_t hb_language;
	hb_raster_draw_t* rdr;
	hb_raster_paint_t* pnt;
};

struct FontStyle {
	std::string family;
	std::set<std::string> alias;
	std::string style;
	std::string file;
	vg_font font;
	int weight;
	int slant;
	int index;
	bool slnt_applied = false;       // 是否已设置变量 slnt
};
void hb_res_init(vg_font* hp, hb_font_t* font);
void free_hb_res(vg_font* hp);
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
				if (it->font.font)
					free_hb_res(&it->font);
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
				if (it->font.font)
					free_hb_res(&it->font);
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
vg_font* font_cache_cx::get_font(const char* family, const char* style, int weight, int slant)
{
	bool mb = mk_font(&_familys_name, family, style, weight, slant);
	if (_temp.size()) {
		return &_temp[0]->font;
	}
	bool ab = mk_font(&_familys, family, style, weight, slant);
	if (_temp.size()) {
		return &_temp[0]->font;
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
		if (!it->font.font)
		{
			it->font.font = load_font(it->file.c_str(), it->index);
		}
		if (it->font.font)
		{
			hb_res_init(&it->font, 0);
			if (font_supports_slnt_axis(it->font.font)) {
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
struct draw_ctx_f {
	ovg_ctx_cb* cb = 0;
	rvg_t* cr = 0;
};

void hb_ovg_move_to(hb_draw_funcs_t* dfuncs, void* draw_data, hb_draw_state_t* st, float to_x, float to_y, void* user_data)
{
	auto ctx = (draw_ctx_f*)draw_data;
	ctx->cb->move_to(ctx->cr, (double)to_x, (double)to_y);
}

void hb_ovg_line_to(hb_draw_funcs_t* dfuncs, void* draw_data, hb_draw_state_t* st, float to_x, float to_y, void* user_data)
{
	auto ctx = (draw_ctx_f*)draw_data;
	ctx->cb->line_to(ctx->cr, (double)to_x, (double)to_y);
}

void hb_ovg_quadratic_to(hb_draw_funcs_t*, void* data, hb_draw_state_t*, float cx1, float cy1, float to_x, float to_y, void*) {
	auto* c = static_cast<draw_ctx_f*>(data);
	c->cb->quadratic_to(c->cr, cx1, cy1, to_x, to_y);
}
void hb_ovg_cubic_to(hb_draw_funcs_t* dfuncs, void* draw_data, hb_draw_state_t* st, float control1_x, float control1_y, float control2_x, float control2_y, float to_x, float to_y, void* user_data)
{
	auto ctx = (draw_ctx_f*)draw_data;
	ctx->cb->curve_to(ctx->cr, (double)control1_x, (double)control1_y, (double)control2_x, (double)control2_y, (double)to_x, (double)to_y);
}

void hb_ovg_close_path(hb_draw_funcs_t* dfuncs, void* draw_data, hb_draw_state_t* st, void* user_data)
{
	auto ctx = (draw_ctx_f*)draw_data;
	ctx->cb->close_path(ctx->cr);
}
// 文本渲染

hb_draw_funcs_t* create_ovg_draw_funcs() {
	hb_draw_funcs_t* funcs = hb_draw_funcs_create();
	hb_draw_funcs_set_move_to_func(funcs, hb_ovg_move_to, nullptr, nullptr);
	hb_draw_funcs_set_line_to_func(funcs, hb_ovg_line_to, nullptr, nullptr);
	hb_draw_funcs_set_quadratic_to_func(funcs, hb_ovg_quadratic_to, nullptr, nullptr);
	hb_draw_funcs_set_cubic_to_func(funcs, hb_ovg_cubic_to, nullptr, nullptr);
	hb_draw_funcs_set_close_path_func(funcs, hb_ovg_close_path, nullptr, nullptr);
	return funcs;
}

const font_family_t* resolve_family(const font_familys_t* ffs, uint32_t cp)
{
	for (int i = 0; i < ffs->count; i++) {
		if (hb_set_has(ffs->familys[i]->coverage, cp))
			return ffs->familys[i];
	}
	return ffs->familys[0]; // fallback
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
void render_text_shaped(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_ctx_cb* ovg, rvg_t* vg, const glm::uvec3& color) {
	if (!ffs || !str8 || !len || !ovg || ffs->count == 0) return;
	static std::vector<uint32_t> utf32;
	utf32.clear();
	utf8_to_utf32(str8, len, &utf32);
	hb_font_t* primary = ffs->familys[0]->font; // 主字体（可按 script 选）
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
		if (!ff) ff = ffs->familys[0];
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
			draw_ctx_f ctx = { ovg, vg };
			ovg->identity_matrix(vg);
			ovg->translate(vg, pen_x, pen_y);
			ovg->scale(vg, 1.0, -1.0);
			ovg->set_source_color(vg, color.x);
			hb_font_draw_glyph(ff->font, info[i].codepoint, df, &ctx);
			pen_x += floorf(pos[i].x_advance); // ✅ 像素对齐
			ovg->set_source_color(vg, color.x);
			ovg->fill_preserve(vg);
			ovg->set_source_color(vg, color.y);
			ovg->stroke(vg);
		}
		hb_buffer_destroy(buf);
		run_start = run_end;
	}
	hb_buffer_destroy(buf);
	hb_draw_funcs_destroy(df);
	ovg->set_source_color(vg, color.x);
	ovg->fill_preserve(vg);
	ovg->set_source_color(vg, color.y);
	ovg->stroke(vg);
	ovg->identity_matrix(vg);

}


void hb_res_init(vg_font* hp, hb_font_t* font) {
	if (!hp)return;
	if (font)
		hp->font = font;
	if (!hp->hb_language)
		hp->hb_language = hb_language_from_string("", -1);
	if (!hp->hb_buffer)
		hp->hb_buffer = hb_buffer_create();
	auto face = hb_font_get_face(hp->font);
	bool has_color = hb_ot_color_has_paint(face) || hb_ot_color_has_layers(face) || hb_ot_color_has_svg(face);
	auto bp = hb_ot_color_has_png(face);
	if (!hp->rdr)
		hp->rdr = hb_raster_draw_create_or_fail();
	if (!hp->pnt)
		hp->pnt = has_color ? hb_raster_paint_create_or_fail() : nullptr;

	hb_font_extents_t extents;
	hb_font_get_extents_for_direction(hp->font, HB_DIRECTION_LTR, &extents);
	hp->ascent = extents.ascender;
	hp->coverage = hb_set_create();
	hb_face_collect_unicodes(hb_font_get_face(hp->font), hp->coverage);
	hp->upem = hb_face_get_upem(face);
}
void free_hb_res(vg_font* hp) {
	if (hp->hb_buffer)
		hb_buffer_destroy(hp->hb_buffer);

	if (hp->pnt)
		hb_raster_paint_destroy(hp->pnt);
	if (hp->rdr)
		hb_raster_draw_destroy(hp->rdr);
	hb_set_destroy(hp->coverage);
	if (hp->font) {
		//hb_face_t* face = hb_font_get_face(hp->hb_font);
		//if (face)hb_face_destroy(face);
		hb_font_destroy(hp->font);
	}
	hp->font = 0;
	hp->coverage = 0;
	hp->hb_language = 0;
	hp->hb_buffer = 0;
	hp->rdr = 0;
	hp->pnt = 0;
}

void* build_glyph_image_hb(vg_font* hp, uint32_t gid, int font_size, glm::ivec4* ot)
{
	hb_raster_image_t* img = nullptr;
	hb_glyph_extents_t gext = {};
	hb_font_extents_t extents[2] = {};
	auto rdr = hp->rdr;
	auto font = hp->font;
	auto pnt = hp->pnt;
	hb_font_set_scale(font, font_size, font_size);
	hb_raster_draw_reset(rdr);
	hb_bool_t bhe = hb_font_get_h_extents(hp->font, &extents[0]);
	hb_bool_t bve = hb_font_get_v_extents(hp->font, &extents[1]);
	do {
		bool bext = hb_font_get_glyph_extents(font, gid, &gext);
		int pad = abs(font_size + gext.height);
		gext.x_bearing -= pad;
		gext.y_bearing += pad;       // 顶部扩大
		gext.width += 2 * pad;
		gext.height -= 2 * pad;      // height 为负，向下也扩大
		if (pnt)
		{
			hb_raster_paint_set_foreground(pnt, HB_COLOR(255, 255, 255, 255));
			if (bext && hb_raster_paint_set_glyph_extents(pnt, &gext))
			{
				hb_raster_paint_set_transform(pnt, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f);
				hb_raster_paint_glyph(pnt, font, gid);	// 光栅化颜色字形
				img = hb_raster_paint_render(pnt);
			}
			if (img)
			{
				hb_raster_paint_recycle_image(pnt, img);
				break;
			}
		}
		{
			hb_raster_draw_set_glyph_extents(rdr, &gext);
			hb_raster_draw_set_transform(rdr, 1.f, 0.f, 0.f, 1.f, 0.0f, 0.0f);
			hb_raster_draw_glyph(rdr, font, gid);	// 单色
			img = hb_raster_draw_render(rdr);
			if (img) { hb_raster_draw_recycle_image(rdr, img); }
		}
	} while (0);
	hb_raster_extents_t ext = {};
	if (img) {
		hb_raster_image_get_extents(img, &ext);
		if (!ext.width || !ext.height) { img = 0; return img; }
		if (ot)
		{
			ot->x = ext.x_origin;
			ot->y = -(ext.height + ext.y_origin);
			ot->z = ext.width;
			ot->w = ext.height;
		}
	}
	return img;
}
// 按颜色复制
void rgba_copy2gray(ovg_image_s* dst, int x, int y, int w, int h, uint32_t c, const uint8_t* dt, int stride, bool fy)
{
	if (stride < 1)stride = w;
	if (!dst || !dt || w <= 0 || h <= 0 || stride < w) return;
	// 计算实际绘制区域（处理边界裁剪）
	int dst_x = std::max(0, x);
	int dst_y = std::max(0, y);
	int src_x = dst_x - x;
	int src_y = dst_y - y;
	int copy_w = std::min(w - src_x, dst->width - dst_x);
	int copy_h = std::min(h - src_y, dst->height - dst_y);
	c = (c & 0x00FFFFFF);
	for (int iy = 0; iy < copy_h; ++iy) {
		const uint8_t* src_row = fy ? dt + (h - 1 - (src_y + iy)) * stride : dt + (src_y + iy) * stride + src_x;
		uint32_t* dst_row = dst->data + ((dst_y + iy) * dst->width + dst_x);
		for (int ix = 0; ix < copy_w; ++ix) {
			uint8_t gray = src_row[ix];
			if (gray > 0)
			{
				uint32_t cc = c | (gray << 24);
				dst_row[ix] = cc;
			}
		}
	}
}
// 预乘白色复制
void rgba_copy2gray_mul(ovg_image_s* dst, int x, int y, int w, int h, const uint8_t* dt, int stride, bool fy)
{
	if (stride < 1)stride = w;
	if (!dst || !dt || w <= 0 || h <= 0 || stride < w) return;
	// 计算实际绘制区域（处理边界裁剪）
	int dst_x = std::max(0, x);
	int dst_y = std::max(0, y);
	int src_x = dst_x - x;
	int src_y = dst_y - y;
	int copy_w = std::min(w - src_x, dst->width - dst_x);
	int copy_h = std::min(h - src_y, dst->height - dst_y);
	for (int iy = 0; iy < copy_h; ++iy) {
		const uint8_t* src_row = fy ? dt + (h - 1 - (src_y + iy)) * stride : dt + (src_y + iy) * stride + src_x;
		uint32_t* dst_row = dst->data + ((dst_y + iy) * dst->width + dst_x);
		for (int ix = 0; ix < copy_w; ++ix) {
			uint8_t gray = src_row[ix];
			if (gray > 0)
			{
				auto cc = (uint32_t)(gray << 24) | (gray << 16) | (gray << 8) | gray;
				dst_row[ix] = cc;
			}
		}
	}
}

void rgba_copy_bgra(ovg_image_s* dst, int x, int y, int w, int h, const uint32_t* dt, int stride, bool fy)
{
	if (!dst || !dt || w <= 0 || h <= 0) return;
	if (stride < 1)
		stride = w * sizeof(int);
	int dst_x = std::max(0, x), dst_y = std::max(0, y);
	int src_x = dst_x - x;
	int src_y = dst_y - y;
	int src_w = w - (dst_x - x), src_h = h - (dst_y - y);
	int copy_w = std::min(src_w, dst->width - dst_x);
	int copy_h = std::min(src_h, dst->height - dst_y);
	auto t = (uint8_t*)dt;
	for (int iy = 0; iy < copy_h; ++iy) {
		const uint8_t* src_row = fy ? t + (h - 1 - (src_y + iy)) * stride : t + (src_y + iy) * stride + src_x;
		auto dstp = dst->data + ((dst_y + iy) * dst->width + dst_x);
		memcpy(dstp, src_row, copy_w * sizeof(uint32_t));
	}
}

void rgba_copy_bgra2rgba(ovg_image_s* dst, int x, int y, int w, int h, const uint32_t* dt, int stride, bool fy)
{
	if (!dst || !dt || w <= 0 || h <= 0) return;
	if (stride < 1)
		stride = w * sizeof(int);
	int dst_x = std::max(0, x), dst_y = std::max(0, y);
	int src_x = dst_x - x;
	int src_y = dst_y - y;
	int src_w = w - (dst_x - x), src_h = h - (dst_y - y);
	int copy_w = std::min(src_w, dst->width - dst_x);
	int copy_h = std::min(src_h, dst->height - dst_y);
	auto t = (uint8_t*)dt;
	for (int iy = 0; iy < copy_h; ++iy) {
		const uint8_t* src_row = fy ? t + (h - 1 - (src_y + iy)) * stride : t + (src_y + iy) * stride + src_x;
		auto dstp = dst->data + ((dst_y + iy) * dst->width + dst_x);
		memcpy(dstp, src_row, copy_w * sizeof(uint32_t));
		for (size_t i = 0; i < copy_w; i++)
		{
			auto it = (uint8_t*)&dstp[i];
			std::swap(it[0], it[2]);
		}
	}
}
bool gfont_copy_image(ovg_image_s* dst, int rx, int ry, uint32_t color, hb_raster_image_t* img_src, bool origin)
{
	auto img = img_src;
	bool has_color = false;
	hb_raster_extents_t ext = {};
	hb_raster_image_get_extents(img_src, &ext);
	hb_raster_format_t fmt = hb_raster_image_get_format(img_src);
	dst->multiply = 1;
	if (origin)
	{
		rx += ext.x_origin;
		ry += -(ext.height + ext.y_origin);
	}
	if (fmt == HB_RASTER_FORMAT_A8)
	{
		rgba_copy2gray(dst, rx, ry, ext.width, ext.height, color, hb_raster_image_get_buffer(img_src), ext.stride, true);
	}
	else if (fmt == HB_RASTER_FORMAT_BGRA32)
	{
		has_color = true;
		if (dst->format == 1)
			rgba_copy_bgra(dst, rx, ry, ext.width, ext.height, (uint32_t*)hb_raster_image_get_buffer(img_src), ext.stride, true);
		else
			rgba_copy_bgra2rgba(dst, rx, ry, ext.width, ext.height, (uint32_t*)hb_raster_image_get_buffer(img_src), ext.stride, true);
	}
	return has_color;
}
void set_hb_fontsize(vg_font* hp, int font_size)
{
	assert(hp);
	if (!hp)return;
	auto font = hp->font;
	hb_font_set_scale(font, font_size, font_size);
}


// todo packer


packer_base::packer_base()
{}
packer_base::~packer_base()
{}

void packer_base::init_target(int width, int height, int heuristic) {}
void packer_base::clear() {}
size_t packer_base::push_rect(glm::ivec4* rc, int n, size_t stride) { return 0; }
bool packer_base::push_rect(const glm::ivec2& rc, glm::ivec2* pos) { return false; }

class image_packer :public packer_base
{
public:
	stbrp_context _ctx = {};
	std::vector<stbrp_node> _rpns;
public:
	image_packer() {}
	~image_packer() {}
	// BL = 0 “从下向左塞”（快速降低高度）
	// BF = 1 “精打细算”（最小化空间浪费）
	void init_target(int width, int height, int heuristic) {
		assert(!(width < 10 || height < 10));
		if (width < 10 || height < 10)return;
		this->width = width;
		this->height = height;
		_rpns.resize(width);
		memset(_rpns.data(), 0, _rpns.size() * sizeof(stbrp_node));
		stbrp_init_target(&_ctx, width, height, _rpns.data(), _rpns.size());
		stbrp_setup_heuristic(&_ctx, heuristic);
		stbrp_setup_allow_out_of_mem(&_ctx, 0);
	}
	void clear() {
		init_target(_ctx.width, _ctx.height, 0);
	}
	size_t push_rect(glm::ivec4* rc, int n, size_t stride)
	{
		if (!rc || n < 1)return false;
		std::vector<stbrp_rect> rct(n);
		auto r = rc;
		auto t = (char*)rc;
		if (stride < sizeof(glm::ivec4))
			stride = sizeof(glm::ivec4);
		for (auto& it : rct)
		{
			r = (glm::ivec4*)t;
			it.w = r->z; it.h = r->w;
			t += stride;
		}
		t = (char*)rc;
		int ret = stbrp_pack_rects(&_ctx, rct.data(), n);
		size_t cx = 0;
		for (auto& it : rct)
		{
			r = (glm::ivec4*)t;
			r->x = it.x; r->y = it.y;
			if (!it.was_packed)
			{
				cx++;
			}
			t += stride;
		}
		return cx;
	}
	bool push_rect(const glm::ivec2& rc, glm::ivec2* pos)
	{
		stbrp_rect rct[2] = {};
		rct->w = rc.x;
		rct->h = rc.y;
		int ret = stbrp_pack_rects(&_ctx, rct, 1);
		if (pos)
		{
			*pos = { rct->x,rct->y };
		}
		return ret;
	}
public:
	// todo stb结构
	int pack_rects(stbrp_rect* rects, int num_rects)
	{
		return stbrp_pack_rects(&_ctx, rects, num_rects);
	}
	void setup_allow_out_of_mem(int allow_out_of_mem)
	{
		stbrp_setup_allow_out_of_mem(&_ctx, allow_out_of_mem);
	}
	//可以选择库应该使用哪个打包启发式方法。不同启发式方法将为不同的数据集生成更好/更差的结果。 如果再次调用init，将重置为默认值。	
	void setup_heuristic(int heuristic = 1)
	{
		stbrp_setup_heuristic(&_ctx, heuristic);
	}
private:

};
packer_base* new_packer(int width, int height)
{
	auto p = new image_packer();
	if (p)
	{
		p->init_target(width, height, 0);
	}
	return p;
}

void free_packer(packer_base* p)
{
	if (p)
	{
		delete p;
	}
}

class stb_packer :public packer_base
{
public:
	stbrp_context _ctx = {};
	ovg_image_s img = {};
	std::vector<uint32_t> ptr;
	std::vector<stbrp_node> _rpns;
public:
	stb_packer() {}
	~stb_packer() {}
	ovg_image_s* get() {
		return (ovg_image_s*)&img;
	}
	// BL = 0 “从下向左塞”（快速降低高度）
	// BF = 1 “精打细算”（最小化空间浪费）
	void init_target(int width, int height, int heuristic) {
		assert(!(width < 10 || height < 10));
		if (width < 10 || height < 10)return;
		ptr.resize(width * height);
		auto img = get();
		img->width = width;
		img->height = height;
		img->valid = 1;
		img->multiply = true;
		this->width = width;
		this->height = height;
		img->data = ptr.data();
		_rpns.resize(width);
		memset(_rpns.data(), 0, _rpns.size() * sizeof(stbrp_node));
		stbrp_init_target(&_ctx, width, height, _rpns.data(), _rpns.size());
		stbrp_setup_heuristic(&_ctx, heuristic);
		stbrp_setup_allow_out_of_mem(&_ctx, 0);
	}
	void clear() {
		init_target(_ctx.width, _ctx.height, 0);
	}
	size_t push_rect(glm::ivec4* rc, int n, size_t stride)
	{
		if (!rc || n < 1)return false;
		std::vector<stbrp_rect> rct(n);
		auto r = rc;
		auto t = (char*)rc;
		if (stride < sizeof(glm::ivec4))
			stride = sizeof(glm::ivec4);
		for (auto& it : rct)
		{
			r = (glm::ivec4*)t;
			it.w = r->z; it.h = r->w;
			t += stride;
		}
		int ret = stbrp_pack_rects(&_ctx, rct.data(), n);
		for (auto& it : rct)
		{
			r = (glm::ivec4*)t;
			r->x = it.x; r->y = it.y;
			t += stride;
		}
		auto img = get();
		img->valid = 1;
		return ret;
	}
	bool push_rect(const glm::ivec2& rc, glm::ivec2* pos)
	{
		stbrp_rect rct[2] = {};
		rct->w = rc.x;
		rct->h = rc.y;
		int ret = stbrp_pack_rects(&_ctx, rct, 1);
		if (pos)
		{
			*pos = { rct->x,rct->y };
		}
		auto img = get();
		img->valid = 1;
		return ret;
	}
public:
	int pack_rects(stbrp_rect* rects, int num_rects)
	{
		return stbrp_pack_rects(&_ctx, rects, num_rects);
	}
	void setup_allow_out_of_mem(int allow_out_of_mem)
	{
		stbrp_setup_allow_out_of_mem(&_ctx, allow_out_of_mem);
	}
	//可以选择库应该使用哪个打包启发式方法。不同启发式方法将为不同的数据集生成更好/更差的结果。 如果再次调用init，将重置为默认值。	
	void setup_heuristic(int heuristic = 1)
	{
		stbrp_setup_heuristic(&_ctx, heuristic);
	}
private:

};



image_cache_cx::image_cache_cx()
{
	resize(width, height);
}

image_cache_cx::~image_cache_cx()
{
	clear();
}

void image_cache_cx::resize(int w, int h)
{
	if (w < 10 || h < 10 || (w == width && h == height))return;
	width = w;
	height = h;
	clear();
}

glm::ivec2 image_cache_cx::fill_color(int w, int h, uint32_t color)
{
	glm::ivec2 pos = {};
	auto pt = get_last_packer(false);
	if (!pt || w < 1 || h < 1)return pos;
	auto ret = pt->push_rect({ w, h }, &pos);
	auto ptr = pt->get();
	auto px = ((uint32_t*)ptr->data) + pos.x;
	px += pos.y * width;
	for (size_t i = 0; i < h; i++)
	{
		for (size_t x = 0; x < w; x++)
		{
			px[x] = color;
		}
		px += width;
	}
	return pos;
}

ovg_image_s* image_cache_cx::push_cache_size(const glm::ivec2& ss, glm::ivec2* pos, int linegap)
{
	int width = align_up(ss.x + linegap, 2), height = align_up(ss.y + linegap, 2);
	glm::ivec4 rc4 = { 0, 0, ss.x,ss.y };
	auto pt = get_last_packer(false);
	if (!pt)return 0;
	auto ret = pt->push_rect({ width, height }, pos);
	if (!ret)
	{
		pt = get_last_packer(true);
		ret = pt->push_rect({ width, height }, pos);
	}
	if (!ret)
	{
		return 0;
	}
	return pt->get();
}

ovg_image_s* image_cache_cx::push_cache_bitmap(hb_raster_image_t* img, glm::ivec2* pos, int linegap)
{
	hb_raster_extents_t ext = {};
	hb_raster_image_get_extents(img, &ext);
	int width = align_up(ext.width + linegap, 2), height = align_up(ext.height + linegap, 2);
	glm::ivec4 rc4 = { 0, 0, ext.width, ext.height };
	auto pt = get_last_packer(false);
	if (!pt)return 0;
	auto ret = pt->push_rect({ width, height }, pos);
	if (!ret)
	{
		pt = get_last_packer(true);
		ret = pt->push_rect({ width, height }, pos);
	}
	if (ret)
	{
		rc4.x = pos->x;
		rc4.y = pos->y;

		auto dst = pt->get();

		hb_raster_format_t fmt = hb_raster_image_get_format(img);
		int rx = pos->x; int ry = pos->y;
		bool origin = false;
		if (origin)
		{
			rx += ext.x_origin;
			ry += -(ext.height + ext.y_origin);
		}
		auto imgd = hb_raster_image_get_buffer(img);
		if (fmt == HB_RASTER_FORMAT_A8)
		{
			rgba_copy2gray(dst, rx, ry, ext.width, ext.height, -1, imgd, ext.stride, true);
		}
		else if (fmt == HB_RASTER_FORMAT_BGRA32)
		{
			if (dst->format == 1)
				rgba_copy_bgra(dst, rx, ry, ext.width, ext.height, (uint32_t*)imgd, ext.stride, true);
			else
				rgba_copy_bgra2rgba(dst, rx, ry, ext.width, ext.height, (uint32_t*)imgd, ext.stride, true);
		}

		dst->valid = 1; // 更新缓存标志
	}
	return pt->get();
}


void image_cache_cx::clear()
{
	for (auto it : _packer)
	{
		if (it)delete it;
	}
	_packer.clear();
	_data.clear();
}

stb_packer* image_cache_cx::get_last_packer(bool isnew)
{
	if (_packer.empty() || isnew)
	{
		auto p = new stb_packer();
		if (!p)return 0;
		_packer.push_back(p);
		p->init_target(width, height, 0);
		_data.push_back(p->get());
	}
	return *_packer.rbegin();
}
