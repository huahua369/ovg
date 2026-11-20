
/*
字体处理管理

处理顺序：bidi -> 断行 -> 选择字体 -> shape -> 渲染
2026/8/19 创建

*/

#include <map>
#include <string>
#include <set>
#include <stack>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
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
//#include "mapView.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ICU */
#include <unicode/utypes.h>
#include <unicode/ucsdet.h>
#include <unicode/unistr.h>
#include <unicode/ubrk.h>
#include <unicode/ubidi.h>
#include <unicode/uscript.h>
#include <unicode/brkiter.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>

/* HarfBuzz */
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h> 
#include <harfbuzz/hb-raster.h> 

#include "ovg.h"
#include "ovg_fonts.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_rect_pack.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#include <shlwapi.h>
#include <Shlobj.h>
#else
#include <unistd.h>  
#include <sys/mman.h>
#include <sys/types.h>  
#include <sys/stat.h> 
#include <fcntl.h>
#endif
struct icu_lib_t;
icu_lib_t* get_icu(int v);

#ifdef max
#undef max
#undef min
#endif




// icu
#if 1

struct icu_lib_t
{
	void (*_ubidi_setPara)(UBiDi* pBiDi, const UChar* text, int32_t length, UBiDiLevel paraLevel, UBiDiLevel* embeddingLevels, UErrorCode* pErrorCode);
	int32_t(*_ubidi_countRuns) (UBiDi* pBiDi, UErrorCode* pErrorCode);
	UBiDi* (*_ubidi_open)(void);
	void (*_ubidi_close)(UBiDi* pBiDi);
	UBiDiDirection(*_ubidi_getVisualRun)(UBiDi* pBiDi, int32_t runIndex, int32_t* pLogicalStart, int32_t* pLength);
	int32_t(*_ubidi_getVisualIndex)(UBiDi* pBiDi, int32_t logicalIndex, UErrorCode* pErrorCode);
	int32_t(*_ubidi_getLogicalIndex)(UBiDi* pBiDi, int32_t visualIndex, UErrorCode* pErrorCode);
	int32_t(*_ubidi_getLength)(const UBiDi* pBiDi);
	void (*_ubidi_getVisualMap)(UBiDi* pBiDi, int32_t* indexMap, UErrorCode* pErrorCode);
	void (*_ubidi_setClassCallback)(UBiDi* pBiDi, UBiDiClassCallback* newFn,
		const void* newContext, UBiDiClassCallback** oldFn,
		const void** oldContext, UErrorCode* pErrorCode);
	void (*_ubidi_getClassCallback)(UBiDi* pBiDi, UBiDiClassCallback** fn, const void** context);


	int32_t(*_ucnv_convert)(const char* toConverterName, const char* fromConverterName, char* target, int32_t targetCapacity, const char* source, int32_t sourceLength, UErrorCode* pErrorCode);
	UScriptCode(*_uscript_getScript)(UChar32 codepoint, UErrorCode* err);
	UBool(*_uscript_hasScript)(UChar32 c, UScriptCode sc);
	const char* (*_uscript_getName)(UScriptCode scriptCode);
	const char* (*_uscript_getShortName)(UScriptCode scriptCode);

	UCharsetDetector* (*_ucsdet_open)(UErrorCode* status);
	void (*_ucsdet_close)(UCharsetDetector* ucsd);
	void (*_ucsdet_setText)(UCharsetDetector* ucsd, const char* textIn, int32_t len, UErrorCode* status);
	void (*_ucsdet_setDeclaredEncoding)(UCharsetDetector* ucsd, const char* encoding, int32_t length, UErrorCode* status);
	const UCharsetMatch* (*_ucsdet_detect)(UCharsetDetector* ucsd, UErrorCode* status);
	const char* (*_ucsdet_getName)(const UCharsetMatch* ucsm, UErrorCode* status);

	UBreakIterator* (*_ubrk_open)(UBreakIteratorType type, const char* locale, const UChar* text, int32_t textLength, UErrorCode* status);
	UBreakIterator* (*_ubrk_openRules)(const UChar* rules, int32_t rulesLength, const UChar* text, int32_t textLength, UParseError* parseErr, UErrorCode* status);
	UBreakIterator* (*_ubrk_openBinaryRules)(const uint8_t* binaryRules, int32_t rulesLength, const UChar* text, int32_t textLength, UErrorCode* status);
	UBreakIterator* (*_ubrk_safeClone)(const UBreakIterator* bi, void* stackBuffer, int32_t* pBufferSize, UErrorCode* status);
	UBreakIterator* (*_ubrk_clone)(const UBreakIterator* bi, UErrorCode* status);
	void (*_ubrk_close)(UBreakIterator* bi);
	void (*_ubrk_setText)(UBreakIterator* bi, const UChar* text, int32_t textLength, UErrorCode* status);
	void (*_ubrk_setUText)(UBreakIterator* bi, UText* text, UErrorCode* status);
	int32_t(*_ubrk_current)(const UBreakIterator* bi);
	int32_t(*_ubrk_next)(UBreakIterator* bi);
	int32_t(*_ubrk_previous)(UBreakIterator* bi);
	int32_t(*_ubrk_first)(UBreakIterator* bi);
	int32_t(*_ubrk_last)(UBreakIterator* bi);
	int32_t(*_ubrk_preceding)(UBreakIterator* bi, int32_t offset);
	int32_t(*_ubrk_following)(UBreakIterator* bi, int32_t offset);
	const char* (*_ubrk_getAvailable)(int32_t index);
	int32_t(*_ubrk_countAvailable)(void);
	UBool(*_ubrk_isBoundary)(UBreakIterator* bi, int32_t offset);
	int32_t(*_ubrk_getRuleStatus)(UBreakIterator* bi);
	int32_t(*_ubrk_getRuleStatusVec)(UBreakIterator* bi, int32_t* fillInVec, int32_t capacity, UErrorCode* status);
	const char* (*_ubrk_getLocaleByType)(const UBreakIterator* bi, ULocDataLocaleType type, UErrorCode* status);
	void (*_ubrk_refreshUText)(UBreakIterator* bi, UText* text, UErrorCode* status);
	int32_t(*_ubrk_getBinaryRules)(UBreakIterator* bi, uint8_t* binaryRules, int32_t rulesCapacity, UErrorCode* status);

	void* _handle;
};

static std::vector<std::string> icu_funstr = { "ubidi_setPara","ubidi_countRuns","ubidi_open","ubidi_close","ubidi_getVisualRun",
"ubidi_getVisualIndex",
"ubidi_getLogicalIndex",
"ubidi_getLength",
"ubidi_getVisualMap",
"ubidi_setClassCallback","ubidi_getClassCallback"
,"ucnv_convert" , "uscript_getScript","uscript_hasScript","uscript_getName","uscript_getShortName",
"ucsdet_open",
"ucsdet_close",
"ucsdet_setText",
"ucsdet_setDeclaredEncoding",
"ucsdet_detect",
"ucsdet_getName",
"ubrk_open",
"ubrk_openRules",
"ubrk_openBinaryRules",
"ubrk_safeClone",
"ubrk_clone",
"ubrk_close",
"ubrk_setText",
"ubrk_setUText",
"ubrk_current",
"ubrk_next",
"ubrk_previous",
"ubrk_first",
"ubrk_last",
"ubrk_preceding",
"ubrk_following",
"ubrk_getAvailable",
"ubrk_countAvailable",
"ubrk_isBoundary",
"ubrk_getRuleStatus",
"ubrk_getRuleStatusVec",
"ubrk_getLocaleByType",
"ubrk_refreshUText",
"ubrk_getBinaryRules",
};

#ifdef _WIN32
const char* exts = ".dll";
#else
const char* exts = ".so";
#endif // _WIN32

static icu_lib_t* icub = 0;

#ifdef _WIN32
#define _DL_OPEN(d) LoadLibraryExA(d,0,LOAD_WITH_ALTERED_SEARCH_PATH)
const char* sysdirstr = nullptr;
const char* sys64 = nullptr;
#else
#define _DL_OPEN(d) dlopen(d,RTLD_NOW)
const char* sysdirstr = "/usr/local/lib/";
const char* sys64 = "/system/lib64/";
#endif
void* loadso(const char* name)
{
	std::string dlln = name;
	dlln += exts;
	void* so = _DL_OPEN(dlln.c_str());
	if (!so)
	{
		std::string dlln1 = sysdirstr;
		dlln1 += dlln;
		so = _DL_OPEN(dlln1.c_str());
	}
	if (!so)
	{
		std::string dlln2 = sys64;
		dlln2 += dlln;
		so = _DL_OPEN(dlln2.c_str());
	}
	return so;
}
void destroy_so(void* so)
{
	if (so)
	{
#ifdef _WIN32
		FreeLibrary((HMODULE)so);
#else
		dlclose(so);
#endif
	}
}
void* _dlsym(void* so, const char* funcname)
{
#if defined(_WIN32)
#define __dlsym GetProcAddress
#define LIBPTR HMODULE
#else
#define __dlsym dlsym
#define LIBPTR void*
#endif
	void* func = (void*)__dlsym((LIBPTR)so, funcname);
	//if (!func)
	//	func = (void*)__dlsym((LIBPTR)so, funcname);
	return func;
#undef __dlsym
#undef LIBPTR
}
void dlsyms(void* so, const std::vector<std::string>& funs, void** outbuf)
{
	auto n = funs.size();
	for (size_t i = 0; i < n; i++)
	{
		auto fcn = funs[i];
		if (fcn.size())
		{
			auto it = _dlsym(so, fcn.c_str());
			if (it)
			{
				outbuf[i] = it;
			}
		}
	}
}


icu_lib_t* get_icu(int v)
{
#define mxv 1000
	if (!icub)
	{
		do {
			std::string dlln = "libicuuc";
			std::string dlln0 = "icudt";
			int v1 = v;
			auto so = loadso("icu");
			if (!so) {
				so = loadso("libicu");
				if (!so)
					break;
			}
			std::string n;
			void* uc = 0;

#if 1 
			int nc = 0;
			do
			{
				for (int i = v1; i > 0; i--)
				{
					auto n1 = std::to_string(i);
					auto k = "ubidi_close_" + n1;
					auto fb = _dlsym(so, k.c_str());
					if (fb)
					{
						n = n1;
						uc = fb;
						break;
					}
				}
				if (n.empty() && nc == 0) { v1 = mxv; nc++; continue; }

			} while (0);
#endif
			if (n.size())
			{
				for (auto& it : icu_funstr)
				{
					it += "_" + n;
				}
			}
			icu_lib_t tb = {};
			dlsyms(so, icu_funstr, (void**)&tb);
			if (tb._ubidi_close)
			{
				icub = new icu_lib_t();
				*icub = tb;
				icub->_handle = so;
			}
			else {
				destroy_so(so);
			}
		} while (0);
	}
	return icub;
}

void un_icu()
{
	if (icub)
	{

		if (icub->_handle)
		{
			destroy_so(icub->_handle);
		}
		delete icub;
		icub = 0;
	}
}

void init_icu()
{
	if (!icub)
		get_icu(U_ICU_VERSION_MAJOR_NUM);
}

#endif // 1








struct DWGlyphBitmap {
	int width = 0;
	int height = 0;
	int stride = 0;            // bytes per row
	std::vector<uint8_t> data; // RGBA8888

	float left = 0;          // x offset (y-down)
	float top = 0;          // y offset (y-down)
};


struct vg_font :public font_family_t {
	hb_buffer_t* hb_buffer;
	hb_language_t hb_language;
	hb_raster_draw_t* rdr;
	hb_raster_paint_t* pnt;
};

struct FontAxis {
	float min = 0, max = 0, value = 0; // value includes named-instance coordinates
	bool present = false;
};

struct FontStyle {
	uint32_t id = 0;
	std::string family;
	std::set<std::string> alias;
	std::string style, instance_style, file;
	vg_font font{};
	int weight = VG_FONT_WEIGHT_REGULAR;
	int slant = VG_FONT_SLANT_NORMAL;
	int index = 0;
	FontAxis wght, slnt_axis, ital;
	bool variable = false;
	bool no_synthetic = false;
};
struct raster_image_t {
	int      x_origin, y_origin;
	unsigned int width, height;
	unsigned int stride;
	hb_raster_format_t fmt;
	uint8_t* data;
	std::vector<uint8_t>* buf;
};
void hb_res_init(vg_font* hp, hb_font_t* font);
void free_hb_res(vg_font* hp);
bool gfont_copy_image(ovg_image_data* dst, int rx, int ry, uint32_t color, raster_image_t* img_src, const glm::ivec4& ow, bool origin, bool type, SubpixelLayout pixelLayout);
namespace ovg {
	uint64_t align_up(uint64_t val, uint64_t alignment)
	{
		return (val + alignment - (uint64_t)1) & ~(alignment - (uint64_t)1);
	}
	// align val to the previous multiple of alignment
	uint64_t align_down(uint64_t val, uint64_t alignment)
	{
		return val & ~(alignment - (uint64_t)1);
	}

	int utf8_to_utf16(const char* utf8, int32_t slen, UChar* out16, int cap16)
	{
		UErrorCode st = U_ZERO_ERROR;
		int32_t len = 0;
		u_strFromUTF8(out16, cap16, &len, utf8, slen, &st);
		return st ? 0 : len;
	}
	void utf16_slice_to_utf8(const UChar* u16, int32_t start, int32_t len, char* out, int cap)
	{
		UErrorCode st = U_ZERO_ERROR;
		u_strToUTF8(out, cap, NULL, u16 + start, len, &st);
	}
}
namespace {
constexpr uint32_t font_id_limit = 0xffff;
constexpr size_t variant_limit = 256;
constexpr hb_tag_t wght_tag = HB_TAG('w', 'g', 'h', 't');
constexpr hb_tag_t slnt_tag = HB_TAG('s', 'l', 'n', 't');
constexpr hb_tag_t ital_tag = HB_TAG('i', 't', 'a', 'l');
hb_user_data_key_t fontStyleKey{};
hb_user_data_key_t g_font_cache_key{};

std::string font_name(hb_face_t* face, hb_ot_name_id_t id)
{
	auto lang = hb_language_get_default();
	unsigned len = hb_ot_name_get_utf8(face, id, lang, nullptr, nullptr);
	if (!len) {
		lang = HB_LANGUAGE_INVALID; // English when available.
		len = hb_ot_name_get_utf8(face, id, lang, nullptr, nullptr);
	}
	if (!len) {
		unsigned count = 0;
		auto names = hb_ot_name_list_names(face, &count);
		for (unsigned i = 0; i < count && !len; ++i) {
			if (names[i].name_id != id) continue;
			lang = names[i].language;
			len = hb_ot_name_get_utf8(face, id, lang, nullptr, nullptr);
		}
	}
	if (!len) return {};
	std::string value(len + 1, '\0');
	unsigned cap = len + 1;
	hb_ot_name_get_utf8(face, id, lang, &cap, value.data());
	value.resize(cap);
	return value;
}

static bool face_axis(hb_font_t* font, hb_tag_t tag, FontAxis& axis)
{
	hb_ot_var_axis_info_t info{};
	if (!hb_ot_var_find_axis_info(hb_font_get_face(font), tag, &info)) return false;
	unsigned count = 0;
	const float* coords = hb_font_get_var_coords_design(font, &count);
	axis = {info.min_value, info.max_value,
		info.axis_index < count ? coords[info.axis_index] : info.default_value, true};
	return true;
}

int effective_weight(float value)
{
	return value > 0 ? static_cast<int>(std::lround(std::clamp(value, 100.0f, 900.0f))) : VG_FONT_WEIGHT_REGULAR;
}

void init_face(FontStyle* fs, font_cache_cx* cache, hb_font_t* font)
{
	hb_font_set_user_data(font, &fontStyleKey, fs, nullptr, true);
	hb_font_set_user_data(font, &g_font_cache_key, cache, nullptr, true);
	hb_res_init(&fs->font, font);
}

void read_capabilities(FontStyle& fs, hb_font_t* font)
{
	auto face = hb_font_get_face(font);
	face_axis(font, wght_tag, fs.wght);
	face_axis(font, slnt_tag, fs.slnt_axis);
	face_axis(font, ital_tag, fs.ital);
	fs.variable = hb_ot_var_has_data(face);
	fs.no_synthetic |= hb_ot_color_has_paint(face) || hb_ot_color_has_layers(face)
		|| hb_ot_color_has_svg(face) || hb_ot_color_has_png(face);
	for (auto tag : {HB_TAG('E','B','D','T'), HB_TAG('C','B','D','T'), HB_TAG('b','d','a','t'), HB_TAG('s','b','i','x')}) {
		auto table = hb_face_reference_table(face, tag);
		fs.no_synthetic |= hb_blob_get_length(table) != 0;
		hb_blob_destroy(table);
	}
}

hb_font_t* load_face_from_blob(hb_blob_t* blob, unsigned index)
{
	if (hb_blob_get_length(blob) == 0) return nullptr;
	// Keep FC_INDEX intact: its high bits select a named instance in HB.
	auto face = hb_face_create(blob, index);
	auto font = hb_face_get_glyph_count(face) ? hb_font_create(face) : nullptr;
	hb_face_destroy(face);
	return font == hb_font_get_empty() ? nullptr : font;
}

void delete_face(FontStyle* fs)
{
	if (fs->font.font) free_hb_res(&fs->font);
	delete fs;
}

int slant_rank(int actual, int requested)
{
	if (actual == requested) return 0;
	if (requested == VG_FONT_SLANT_NORMAL) return actual == VG_FONT_SLANT_OBLIQUE ? 1 : 2;
	return actual == VG_FONT_SLANT_NORMAL ? 2 : 1;
}

int weight_group(int actual, int requested)
{
	if (actual == requested) return 0;
	if (requested < 400) return actual < requested ? 1 : 2;
	if (requested > 500) return actual > requested ? 1 : 2;
	if (actual >= 400 && actual <= 500) return 1;
	return actual > 500 ? 2 : 3;
}

struct face_coordinates {
	float wght, slnt, ital;
	int weight, slant;
};

face_coordinates resolve_coordinates(const FontStyle& fs, int weight, int slant)
{
	face_coordinates c{fs.wght.value, fs.slnt_axis.value, fs.ital.value, fs.weight, fs.slant};
	if (weight && fs.wght.present) {
		c.wght = std::clamp(static_cast<float>(weight), fs.wght.min, fs.wght.max);
		c.weight = effective_weight(c.wght);
	}
	if (slant && (fs.slnt_axis.present || fs.ital.present)) {
		const bool roman = slant == VG_FONT_SLANT_NORMAL;
		const float angle = roman ? 0.0f : slant == VG_FONT_SLANT_ITALIC ? -12.0f : -10.0f;
		if (fs.ital.present) c.ital = std::clamp(roman ? 0.0f : 1.0f, fs.ital.min, fs.ital.max);
		if (fs.slnt_axis.present) c.slnt = std::clamp(angle, fs.slnt_axis.min, fs.slnt_axis.max);
		if (fs.ital.present && c.ital >= 0.5f)
			c.slant = slant == VG_FONT_SLANT_OBLIQUE && c.slnt < -1.0f ? slant : VG_FONT_SLANT_ITALIC;
		else if (fs.slnt_axis.present && std::abs(c.slnt) > 1.0f)
			c.slant = !roman && c.slnt < 0.0f ? slant : VG_FONT_SLANT_OBLIQUE;
		else if (fs.ital.present || fs.slnt_axis.value != 0.0f || fs.slant != VG_FONT_SLANT_ITALIC)
			c.slant = VG_FONT_SLANT_NORMAL;
	}
	return c;
}
} // namespace

font_cache_cx::font_cache_cx()
{
	get_sys_family();
}

font_cache_cx::~font_cache_cx()
{
	clear_load();
	clear_sys();
	if (funcs) hb_draw_funcs_destroy(funcs);
	if (cfg) FcConfigDestroy(cfg);
}

void font_cache_cx::set_alloc_ptr(vg_alloc_cx* p)
{
	if (p) ac = p;
}

void font_cache_cx::forget_family(const font_familys_t* family)
{
	auto width = text_width_cache.lower_bound({family, (std::numeric_limits<int>::min)(), {}});
	while (width != text_width_cache.end() && width->first.font == family)
		width = text_width_cache.erase(width);
	auto advance = glyph_advance_cache.lower_bound({family, (std::numeric_limits<int>::min)(), 0});
	while (advance != glyph_advance_cache.end() && advance->first.font == family)
		advance = glyph_advance_cache.erase(advance);
	auto metrics = font_metrics_cache.lower_bound({family, (std::numeric_limits<int>::min)()});
	while (metrics != font_metrics_cache.end() && metrics->first.first == family)
		metrics = font_metrics_cache.erase(metrics);
}

void font_cache_cx::clear_glyphs_and_measurements()
{
	++glyph_generation;
	// Explicit resets invalidate cached draw data; existing managed lists remain
	// safe to delete or derive from, but have no resolved faces until recreated.
	for (auto list : live_families) {
		list->familys = nullptr;
		list->count = 0;
	}
	for (auto& [key, entry] : glyph_cache) {
		// Raster entries borrow the vector entry's path.
		if (entry.type == glyph_atlas_entry::VECTOR && entry.path_data)
			(ac ? ac : vg_get_default_allocator())->dealloc(entry.path_data, entry.path_size * sizeof(float));
	}
	glyph_cache.clear();
	_sub_glyph_cache.clear();
	image_cache.clear();
	text_width_cache.clear();
	glyph_advance_cache.clear();
	font_metrics_cache.clear();
}

void font_cache_cx::clear_variants()
{
	for (auto& [key, fs] : variants) delete_face(fs);
	variants.clear();
}

void font_cache_cx::clear_sys()
{
	clear_glyphs_and_measurements();
	clear_variants();
	_familys.clear();
	for (auto& [key, fs] : system_faces) delete_face(fs);
	system_faces.clear();
}

void font_cache_cx::clear_load()
{
	clear_glyphs_and_measurements();
	clear_variants();
	for (auto& [key, faces] : _familys_name)
		for (auto fs : faces) delete_face(fs);
	_familys_name.clear();
}

void font_cache_cx::get_sys_family()
{
	if (!cfg) cfg = FcInitLoadConfigAndFonts();
	if (!cfg) return;
	auto pat = FcPatternCreate();
	auto os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, FC_FILE,
		FC_FULLNAME, FC_INDEX, nullptr);
	auto fs = pat && os ? FcFontList(cfg, pat, os) : nullptr;
	for (int i = 0; fs && i < fs->nfont; ++i) {
		auto p = fs->fonts[i];
		FcChar8 *family = nullptr, *style = nullptr, *file = nullptr;
		int w = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN, index = 0;
		FcPatternGetString(p, FC_FAMILY, 0, &family);
		FcPatternGetString(p, FC_STYLE, 0, &style);
		FcPatternGetString(p, FC_FILE, 0, &file);
		FcPatternGetInteger(p, FC_WEIGHT, 0, &w);
		FcPatternGetInteger(p, FC_SLANT, 0, &slant);
		FcPatternGetInteger(p, FC_INDEX, 0, &index);
		if (!file || !family || !*file || !*family) continue;
		const auto key = std::make_pair(std::string(reinterpret_cast<const char*>(file)), index);
		auto pos = system_faces.find(key);
		FontStyle* it = nullptr;
		if (pos == system_faces.end()) {
			if (next_font_id >= font_id_limit) continue;
			it = new FontStyle();
			it->id = next_font_id++;
			it->family = reinterpret_cast<const char*>(family);
			if (style) it->style = reinterpret_cast<const char*>(style);
			it->file = key.first;
			it->index = index;
			it->weight = effective_weight(static_cast<float>(FcWeightToOpenType(w)));
			it->slant = slant == FC_SLANT_ITALIC ? VG_FONT_SLANT_ITALIC :
				slant == FC_SLANT_OBLIQUE ? VG_FONT_SLANT_OBLIQUE : VG_FONT_SLANT_NORMAL;
			system_faces.emplace(key, it);
			_familys[it->family].push_back(it);
		} else {
			it = pos->second;
		}
		for (const char* field : {FC_FAMILY, FC_FULLNAME}) {
			FcChar8* alias = nullptr;
			for (int j = 0; FcPatternGetString(p, field, j, &alias) == FcResultMatch; ++j)
				if (alias && *alias) it->alias.insert(reinterpret_cast<const char*>(alias));
		}
	}
	if (fs) FcFontSetDestroy(fs);
	if (os) FcObjectSetDestroy(os);
	if (pat) FcPatternDestroy(pat);
}

bool font_cache_cx::load_face(FontStyle* fs)
{
	if (fs->font.font) return true;
	auto blob = hb_blob_create_from_file(fs->file.c_str());
	auto font = load_face_from_blob(blob, static_cast<unsigned>(fs->index));
	hb_blob_destroy(blob);
	if (!font) return false;
	read_capabilities(*fs, font);
	if (fs->wght.present) fs->weight = effective_weight(fs->wght.value);
	if (fs->ital.present && fs->ital.value >= 0.5f) fs->slant = VG_FONT_SLANT_ITALIC;
	else if (fs->slnt_axis.present && std::abs(fs->slnt_axis.value) > 1.0f) fs->slant = VG_FONT_SLANT_OBLIQUE;
	const unsigned instance = static_cast<unsigned>(fs->index) >> 16;
	if (instance) {
		auto face = hb_font_get_face(font);
		fs->instance_style = font_name(face, hb_ot_var_named_instance_get_subfamily_name_id(face, instance - 1));
	}
	init_face(fs, this, font);
	return true;
}

FontStyle* font_cache_cx::resolve_face(const char* family, const char* style, int weight, int slant)
{
	if (!family || !*family) return nullptr;
	const bool explicit_style = style && *style;
	if (!weight && !slant) {
		// Preserve the legacy direct/alias and custom/system lookup order. Custom
		// faces historically had no subfamily name in this lookup path.
		for (auto faces : {&_familys_name, &_familys}) {
			if (explicit_style && faces == &_familys_name) continue;
			auto matches = [&](FontStyle* fs) { return !explicit_style || fs->style == style; };
			auto found = faces->find(family);
			if (found != faces->end())
				for (auto fs : found->second) if (matches(fs) && load_face(fs)) return fs;
			for (auto& [key, candidates] : *faces)
				for (auto fs : candidates)
					if (fs->alias.count(family) && matches(fs) && load_face(fs)) return fs;
		}
		return nullptr;
	}

	std::vector<FontStyle*> candidates;
	for (auto faces : {&_familys_name, &_familys}) {
		auto found = faces->find(family);
		if (found != faces->end()) candidates.insert(candidates.end(), found->second.begin(), found->second.end());
		for (auto& [key, list] : *faces) {
			if (key == family) continue;
			for (auto fs : list) if (fs->alias.count(family)) candidates.push_back(fs);
		}
	}

	const int target_weight = weight ? weight : VG_FONT_WEIGHT_REGULAR;
	const int target_slant = slant ? slant : VG_FONT_SLANT_NORMAL;
	FontStyle* best = nullptr;
	std::tuple<int, int, int, int, int, int, int> best_score{};
	for (auto fs : candidates) {
		if (!load_face(fs)) continue;
		const bool exact_style = explicit_style &&
			(fs->instance_style.empty() ? fs->style == style : fs->instance_style == style || fs->style == style);
		const auto c = resolve_coordinates(*fs, weight, slant);
		const int hits = (weight && c.weight == weight) + (slant && c.slant == slant);
		const bool static_exact = !fs->variable && hits == (!!weight + !!slant);
		const auto score = std::make_tuple(int(!exact_style), -hits, int(!static_exact),
			weight_group(c.weight, target_weight), std::abs(c.weight - target_weight),
			slant_rank(c.slant, target_slant), -c.weight);
		if (!best || score < best_score) {
			best = fs;
			best_score = score;
		}
	}
	return best;
}

FontStyle* font_cache_cx::materialize(FontStyle* base, int weight, int slant)
{
	const auto key = std::make_tuple(base->id, weight, slant);
	auto found = variants.find(key);
	if (found != variants.end()) return found->second;
	const auto c = resolve_coordinates(*base, weight, slant);
	const bool synthetic_bold = !base->no_synthetic && !base->wght.present && weight > c.weight;
	const bool synthetic_slant = !base->no_synthetic && c.slant == VG_FONT_SLANT_NORMAL &&
		(slant == VG_FONT_SLANT_ITALIC || slant == VG_FONT_SLANT_OBLIQUE);
	if (c.wght == base->wght.value && c.slnt == base->slnt_axis.value && c.ital == base->ital.value &&
		!synthetic_bold && !synthetic_slant) return base;
	// Do not evict live faces: lists, cached draw runs, and atlas entries borrow
	// them. At capacity use the selected base until the next explicit reset.
	if (variants.size() >= variant_limit || next_font_id >= font_id_limit) return base;
	auto font = hb_font_create(hb_font_get_face(base->font.font));
	if (font == hb_font_get_empty()) return base;
	if (c.wght != base->wght.value) hb_font_set_variation(font, wght_tag, c.wght);
	if (c.slnt != base->slnt_axis.value) hb_font_set_variation(font, slnt_tag, c.slnt);
	if (c.ital != base->ital.value) hb_font_set_variation(font, ital_tag, c.ital);
	if (synthetic_bold) {
		const float amount = std::clamp((weight - c.weight) * 0.00009f, 0.001f, 0.05f);
		hb_font_set_synthetic_bold(font, amount, amount, false);
	}
	if (synthetic_slant)
		hb_font_set_synthetic_slant(font, slant == VG_FONT_SLANT_ITALIC ? 0.21255656f : 0.17632698f);

	auto it = new FontStyle();
	it->id = next_font_id++;
	it->weight = synthetic_bold ? weight : c.weight;
	it->slant = synthetic_slant ? slant : c.slant;
	init_face(it, this, font);
	variants.emplace(key, it);
	return it;
}

font_family_t* font_cache_cx::get_font(const char* family, const char* style, int weight, int slant)
{
	auto base = resolve_face(family, style, weight, slant);
	if (!base) return nullptr;
	return &(weight || slant ? materialize(base, weight, slant) : base)->font;
}

bool font_cache_cx::load_font_from_path(const char* path, const char* name)
{
	if (!cfg || !path || !*path || !FcConfigAppFontAddFile(cfg, (const FcChar8*)path)) return false;
	if (!name || !*name) return true;
	auto blob = hb_blob_create_from_file(path);
	unsigned len = 0;
	auto data = hb_blob_get_data(blob, &len);
	const bool ok = len <= static_cast<unsigned>((std::numeric_limits<long>::max)()) &&
		register_font_memory(reinterpret_cast<unsigned char*>(const_cast<char*>(data)), static_cast<long>(len), name);
	hb_blob_destroy(blob);
	return ok;
}

bool font_cache_cx::add_font_dir(const char* dir)
{
	return cfg && dir && *dir && FcConfigAppFontAddDir(cfg, (const FcChar8*)dir);
}

bool font_cache_cx::load_font_from_memory(unsigned char* data, long size, const char* name)
{
	return register_font_memory(data, size, name);
}

bool font_cache_cx::register_font_memory(unsigned char* data, long size, const char* name)
{
	if (!data || size <= 0 || static_cast<unsigned long>(size) > (std::numeric_limits<unsigned>::max)() ||
		next_font_id >= font_id_limit) return false;
	// The caller may release or reuse the input buffer immediately.
	auto blob = hb_blob_create(reinterpret_cast<const char*>(data), static_cast<unsigned>(size),
		HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
	auto font = load_face_from_blob(blob, 0);
	hb_blob_destroy(blob);
	if (!font) return false;
	auto face = hb_font_get_face(font);
	auto internal = font_name(face, HB_OT_NAME_ID_FONT_FAMILY); // Preserve legacy registration key.
	if (internal.empty()) internal = font_name(face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
	const std::string key = name && *name ? name : internal;
	if (key.empty()) {
		hb_font_destroy(font);
		return false;
	}

	auto it = new FontStyle();
	it->id = next_font_id++;
	it->family = key;
	if (!internal.empty()) it->alias.insert(internal);
	it->style = font_name(face, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
	if (it->style.empty()) it->style = font_name(face, HB_OT_NAME_ID_FONT_SUBFAMILY);
	if (it->style.empty()) it->style = font_name(face, HB_OT_NAME_ID_WWS_SUBFAMILY);
	for (auto id : {HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY, HB_OT_NAME_ID_FULL_NAME}) {
		auto alias = font_name(face, id);
		if (!alias.empty()) it->alias.insert(std::move(alias));
	}
	it->weight = effective_weight(hb_style_get_value(font, HB_STYLE_TAG_WEIGHT));
	it->slant = hb_style_get_value(font, HB_STYLE_TAG_ITALIC) >= 0.5f ? VG_FONT_SLANT_ITALIC :
		hb_style_get_value(font, HB_STYLE_TAG_SLANT_ANGLE) < -1.0f ? VG_FONT_SLANT_OBLIQUE : VG_FONT_SLANT_NORMAL;
	read_capabilities(*it, font);
	init_face(it, this, font);
	_familys_name[key].push_back(it);
	return true;
}

void font_family_get_style(const font_family_t* face, int* weight, int* slant)
{
	auto fs = face && face->font ? static_cast<const FontStyle*>(hb_font_get_user_data(face->font, &fontStyleKey)) : nullptr;
	if (weight) *weight = fs ? fs->weight : 0;
	if (slant) *slant = fs ? fs->slant : 0;
}

void font_familys_get_style(const font_familys_t* ffs, int* weight, int* slant)
{
	font_family_get_style(ffs && ffs->familys && ffs->count > 0 ? ffs->familys[0] : nullptr, weight, slant);
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

	auto icu = get_icu(U_ICU_VERSION_MAJOR_NUM);
	if (!icu)return;

	/* 1) ICU Bidi：设置段落方向 */
	UBiDi* bidi = icu->_ubidi_open();
	icu->_ubidi_setPara(bidi, line_u16, line_len, base_dir, NULL, &st);
	if (U_FAILURE(st)) {
		//fprintf(stderr, "ubidi_setPara failed: %s\n", icu->_u_errorName(st));
		icu->_ubidi_close(bidi);
		return;
	}

	/* 2) 获取视觉顺序的 run 列表 */
	int32_t run_count = icu->_ubidi_countRuns(bidi, &st);
	if (U_FAILURE(st)) {
		icu->_ubidi_close(bidi);
		return;
	}

	/* 3) 按视觉顺序拼接逻辑 UTF-16 */
	UChar visual_u16[512];
	int32_t vis_len = 0;

	for (int32_t r = 0; r < run_count; r++) {
		int32_t run_start, run_len;
		UBiDiDirection run_dir = icu->_ubidi_getVisualRun(bidi, r, &run_start, &run_len);

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
	icu->_ubidi_close(bidi);
}
//UBiDiDirection get_dir(UChar32 c)
//{ 
//	UBiDiDirection dir = (UBiDiDirection)UBIDI_DEFAULT_LTR;
//	if (icu->_u_charDirection(c) == U_RIGHT_TO_LEFT || icu->_u_charDirection(c) == U_ARABIC_NUMBER) {
//		dir = UBIDI_RTL;
//	}
//	return dir;
//}
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
	case VG_FONT_WEIGHT_THIN:        return "Thin";
	case VG_FONT_WEIGHT_EXTRA_LIGHT: return "ExtraLight";
	case VG_FONT_WEIGHT_LIGHT:       return "Light";
	case VG_FONT_WEIGHT_SEMI_LIGHT:  return "SemiLight";
	case VG_FONT_WEIGHT_REGULAR:     return "Regular";
	case VG_FONT_WEIGHT_MEDIUM:      return "Medium";
	case VG_FONT_WEIGHT_SEMI_BOLD:   return "SemiBold";
	case VG_FONT_WEIGHT_BOLD:        return "Bold";
	case VG_FONT_WEIGHT_EXTRA_BOLD:  return "ExtraBold";
	case VG_FONT_WEIGHT_BLACK:       return "Black";
	default:                        return "Default";
	}
}

const char* font_cache_cx::slant_to_string(int s) {
	switch (s) {
	case VG_FONT_SLANT_ITALIC:  return "Italic";
	case VG_FONT_SLANT_OBLIQUE: return "Oblique";
	case VG_FONT_SLANT_NORMAL:  return "Roman";
	default:                   return "Default";
	}
}

void path_builder::clear()
{
	data.clear();
}
void path_builder::move_to(float x, float y) {
	data.push_back((float)path_type_et::e_vmove);  // MOVE
	data.push_back(x);
	data.push_back(-y); // ← y 翻转
	pen_x = x; pen_y = -y;
}
void path_builder::line_to(float x, float y) {
	data.push_back((float)path_type_et::e_vline);  // LINE
	data.push_back(x);
	data.push_back(-y); // ← y 翻转
	pen_x = x; pen_y = -y;
}
void path_builder::quad_to(float cx, float cy, float x, float y) {
	data.push_back((float)path_type_et::e_quadratic);  // QUAD
	data.push_back(cx);
	data.push_back(-cy); // ← y 翻转
	data.push_back(x);
	data.push_back(-y);  // ← y 翻转
	pen_x = x; pen_y = -y;
}
void path_builder::close() {
	data.push_back((float)path_type_et::e_close);  // CLOSE
	// close 不存坐标，ovg_path_t 内部补 终点→起点 直线
}
static void _hb_move_to(hb_draw_funcs_t* funcs, void* draw_data, hb_draw_state_t* st, float to_x, float to_y, void* user_data)
{
	auto* b = (path_builder*)draw_data;
	b->move_to(to_x, to_y);
}

static void _hb_line_to(hb_draw_funcs_t* funcs, void* draw_data, hb_draw_state_t* st, float to_x, float to_y, void* user_data)
{
	auto* b = (path_builder*)draw_data;
	b->line_to(to_x, to_y);
}

static void _hb_quad_to(hb_draw_funcs_t* funcs, void* draw_data, hb_draw_state_t* st, float cx, float cy, float to_x, float to_y, void* user_data)
{
	auto* b = (path_builder*)draw_data;
	b->quad_to(cx, cy, to_x, to_y);
}

static void _hb_close_path(hb_draw_funcs_t* funcs, void* draw_data, hb_draw_state_t* st, void* user_data)
{
	auto* b = (path_builder*)draw_data;
	b->close();
}

// Return the bitmap to its rasterizer only after the atlas or CPU compositor
// has finished reading it. HarfBuzz takes ownership on recycle.
struct glyph_raster_recycler
{
	vg_font* font;
	bool painted = false;
	void operator()(hb_raster_image_t* image) const
	{
		if (painted) hb_raster_paint_recycle_image(font->pnt, image);
		else hb_raster_draw_recycle_image(font->rdr, image);
	}
};
using glyph_raster = std::unique_ptr<hb_raster_image_t, glyph_raster_recycler>;

static glyph_raster build_glyph_image_hb(vg_font* hp, uint32_t gid, int font_size, glm::ivec4* ot, const glm::vec2& scale);

glyph_atlas_entry* font_cache_cx::get_cache_lookup_glyph(hb_font_t* font, uint32_t glyph_id, int fontsize)
{
	glyph_atlas_entry* ret = 0;
	auto font_ptr = (FontStyle*)hb_font_get_user_data(font, (hb_user_data_key_t*)&fontStyleKey);
	uint32_t font_id = font_ptr->id;
	hb_font_set_scale(font, fontsize, fontsize);
	hb_glyph_extents_t extents;
	hb_font_get_glyph_extents(font, glyph_id, &extents);

	int em = hb_face_get_upem(hb_font_get_face(font));
	float scale = fontsize / (float)em;

	auto px_w = (extents.width);
	auto px_h = (-extents.height);

	bool use_raster = (px_w <= max_raster_size && px_h <= max_raster_size && px_w > 0 && px_h > 0);

	glyph_key gk;
	gk.s.k.x = (uint16_t)font_id;
	gk.s.glyph_index = glyph_id;

	{
		gk.s.k.y = 0;
		auto it = glyph_cache.find(gk.v);
		if (it == glyph_cache.end()) {
			if (!funcs)
			{
				funcs = hb_draw_funcs_create();
				hb_draw_funcs_set_move_to_func(funcs, _hb_move_to, nullptr, nullptr);
				hb_draw_funcs_set_line_to_func(funcs, _hb_line_to, nullptr, nullptr);
				hb_draw_funcs_set_quadratic_to_func(funcs, _hb_quad_to, nullptr, nullptr);
				hb_draw_funcs_set_close_path_func(funcs, _hb_close_path, nullptr, nullptr);
			}
			temp_path.clear();
			hb_font_set_scale(font, em, em);
			hb_font_draw_glyph(font, glyph_id, funcs, &temp_path);

			// Only the vector entry owns this allocation. Whitespace has no path.
			size_t bytes = temp_path.data.size() * sizeof(float);
			auto allocator = ac ? ac : vg_get_default_allocator();
			float* path_copy = bytes ? static_cast<float*>(allocator->alloc(bytes, 0)) : nullptr;
			if (path_copy) memcpy(path_copy, temp_path.data.data(), bytes);

			glyph_atlas_entry entry{};
			entry.type = glyph_atlas_entry::VECTOR;
			entry.atlas_img = nullptr;
			entry.uv_rect = glm::ivec4(0);
			entry.offset = glm::ivec2(0);
			entry.advance = 0;
			entry.path_data = path_copy;
			entry.path_size = temp_path.data.size();
			entry.em_units = em;
			entry.font = font;
			auto [it2, ok] = glyph_cache.emplace(gk.v, entry);
			ret = &it2->second;
		}
		else {
			ret = &it->second;
		}
		if (!use_raster)return ret;
	}

	gk.s.k.y = (uint16_t)use_raster ? fontsize : 0;// 大号字符统一绑定0字号
	glm::ivec2 fsc = { 1,1 };
	if (fontsize <= min_subpixel && !font_ptr->font.pnt)
	{
		fsc.x = 3;
	}
	auto gca = fsc.x > 1 ? &_sub_glyph_cache : &glyph_cache;
	auto it = gca->find(gk.v);
	if (it != gca->end())
		return &it->second;
	assert(ret);
	// ═══════════════════════════════════════════
	// 位图路径
	// ═══════════════════════════════════════════
	if (use_raster) {

		// ── 缓存未命中：光栅化 ──
		glm::ivec4 rc = {};
		glm::ivec2 pos = {};
		// 装箱进 image_cache_cx
		auto* hp = &font_ptr->font;
		const auto img = build_glyph_image_hb(hp, glyph_id, fontsize, &rc, fsc);
		if (!img) return ret;
		raster_image_t img_ds = {};
		hb_raster_image_get_extents(img.get(), (hb_raster_extents_t*)&img_ds);
		img_ds.fmt = hb_raster_image_get_format(img.get());
		img_ds.data = (uint8_t*)hb_raster_image_get_buffer(img.get());

		const bool exact_bounds = fsc.x == 1 && !hp->pnt;
		int ow = rc.z / fsc.x;
		if (!exact_bounds && ow > fontsize) ++ow;
		// A tightly bounded mask needs an empty texel on all sides when sampled.
		const int gutter = exact_bounds ? 1 : 0;
		const int stored_h = std::max(rc.w, (int)img_ds.height);
		ovg_image_data* img_data = image_cache.push_cache_size({ow + 2 * gutter, stored_h + 2 * gutter}, &pos);
		if (!img_data) return ret;
		pos += glm::ivec2(gutter);
		gfont_copy_image(img_data, pos.x, pos.y, -1, &img_ds, rc, false, fsc.x > 1, SubpixelLayout::RGB);

		glyph_atlas_entry entry{};
		entry.atlas_img = img_data;  // ovg_image_data 继承自 vg_image_t
		entry.uv_rect = glm::ivec4(pos.x, pos.y, ow, rc.w);
		entry.offset = glm::ivec2(rc.x, rc.y);
		entry.advance = 0;
		entry.path_data = ret->path_data;
		entry.path_size = ret->path_size;
		entry.em_units = em;
		entry.font = font;
		entry.has_color = font_ptr->font.pnt ? true : false;
		auto [it2, ok] = gca->emplace(gk.v, entry);
		ret = &it2->second;
	}

	return ret;
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

static uint32_t utf8_next(uint8_t*& p, const uint8_t* end) {
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
// 从 *p 读取一个 UTF-32 codepoint，并推进指针
// 返回 0xFFFFFFFF 表示遇到非法序列或越界
inline uint32_t utf16_next(uint16_t*& p, const uint16_t* end) {
	if (p >= end) return 0xFFFFFFFF;

	uint16_t lead = *p++;

	// 不在 surrogate 范围内，直接返回（BMP 字符）
	if (lead < 0xD800 || lead > 0xDBFF)
		return lead;

	// lead 是高代理，需要 trail
	if (p >= end)
		return 0xFFFD;   // 孤立高代理，返回替换字符

	uint16_t trail = *p;
	if (trail < 0xDC00 || trail > 0xDFFF) {
		// 高代理后面不是低代理，非法序列
		// 不推进 p，让 trail 留给下次调用（或者也可以选择推进）
		return 0xFFFD;
	}

	// 合法的 surrogate pair
	p++;
	return 0x10000 + (((lead - 0xD800) << 10) | (trail - 0xDC00));
}
void vg_utf8_to_utf32(const void* str8, size_t len, std::vector<uint32_t>* ot)
{
	if (!str8 || !ot || len == 0) return;
	uint8_t* p = (uint8_t*)str8;
	const uint8_t* end = p + len;
	ot->reserve(len + ot->size());
	while (p < end)
		ot->push_back(utf8_next(p, end));
}

void vg_utf8_to_utf16(const void* str8, size_t len, std::vector<uint16_t>* ot)
{
	if (!str8 || !ot || len == 0) return;
	uint8_t* p = (uint8_t*)str8;
	const uint8_t* end = p + len;
	ot->resize(ot->size() + len);  // 保守够用 
	int n = ovg::utf8_to_utf16((const char*)str8, len, (UChar*)ot->data(), ot->size());
	if (n > 0)
	{
		ot->resize(n);
	}
}

void vg_utf8_to_utf16(const void* str8, size_t len, std::u16string* ot)
{
	if (!str8 || !ot || len == 0) return;
	uint8_t* p = (uint8_t*)str8;
	const uint8_t* end = p + len;
	ot->resize(ot->size() + len);  // 保守够用 
	int n = ovg::utf8_to_utf16((const char*)str8, len, (UChar*)ot->data(), ot->size());
	if (n > 0)
	{
		ot->resize(n);
	}
}

uint8_t* vg_utf8_to_utf16(const void* str8, size_t len, uint16_t* ot, size_t* ot_len)
{
	if (!str8 || !ot || len == 0) return 0;
	size_t count = ovg::utf8_to_utf16((const char*)str8, len, (UChar*)ot, len);
	if (ot_len)*ot_len = count;
	return 0;
}

bool write_png_bgra(const char* path, const uint8_t* bgra, int w, int h) {
	std::vector<uint8_t> rgb(size_t(w) * h * 4);
	for (int y = 0; y < h; ++y) {
		const uint8_t* src = bgra + y * w * 4;
		uint8_t* dst = rgb.data() + y * w * 4;
		for (int x = 0; x < w; ++x) {
			dst[x * 4 + 0] = src[x * 4 + 2]; // R
			dst[x * 4 + 1] = src[x * 4 + 1]; // G
			dst[x * 4 + 2] = src[x * 4 + 0]; // B
			auto a = src[x * 4 + 3];
			dst[x * 4 + 3] = a;// 0xff; // A
		}
	}
	return stbi_write_png(path, w, h, 4, rgb.data(), w * 4) != 0;
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
	hp->ascender = extents.ascender;
	hp->descender = extents.descender;
	hp->line_gap = extents.line_gap;
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


glm::vec3 get_font_extents(hb_font_t* font, int height, bool vert)
{
	glm::vec3 r = {};
#ifdef HB_RASTER_H 
	hb_font_extents_t extents[2] = {};
	int h = abs(height);
	if (h == 0)
		h = hb_face_get_upem(hb_font_get_face(font));
	hb_font_set_scale(font, h, h);
	hb_bool_t bhe = hb_font_get_h_extents(font, &extents[0]);
	hb_bool_t bve = hb_font_get_v_extents(font, &extents[1]);
	if (bhe)
	{
		r = { extents->ascender , extents->descender , extents->line_gap };
	}
	if (bve && vert)
	{
		r = { extents[1].ascender , extents[1].descender , extents[1].line_gap };
	}
#endif
	return r;
}

// ── UI text measurement ──────────────────────────────────────────────────
// This used to live in gui/vgui.cpp and called HarfBuzz directly.  It keeps
// the exact same algorithm (per-codepoint nominal advances, coverage-based
// fallback selection, '\n' splits lines) so moving it does not change layout.
// Caches are owned by font_cache_cx, which shares the lifetime of the font
// objects and is reached through the same user-data key as vg_text_run_cx.
static uint32_t vg_measure_u8_decode(const char* text)
{
	if (!text || !*text) return 0;
	const unsigned char* s = (const unsigned char*)text;
	if (s[0] < 0x80) return s[0];
	if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
		return ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
	if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
		return ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
	if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
		return ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
		((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
	return s[0];
}

static font_cache_cx* vg_measure_cache(const font_familys_t* ffs)
{
	if (!ffs || ffs->count < 1 || !ffs->familys || !ffs->familys[0] || !ffs->familys[0]->font)
		return nullptr;
	return (font_cache_cx*)hb_font_get_user_data(ffs->familys[0]->font, &g_font_cache_key);
}

float vg_measure_text_width(const font_familys_t* ffs, int fontsize, const char* text, int len)
{
	if (!ffs || ffs->count < 1 || !text || len <= 0) return 0;
	if (fontsize < 1) fontsize = 1;

	font_cache_cx* cache = vg_measure_cache(ffs);

	// Fast path for complete strings: most labels are measured on every draw
	// with exactly the same font, size and contents.
	vg_measure_text_key text_key;
	bool text_cached = cache != nullptr;
	if (text_cached)
	{
		text_key.font = ffs;
		text_key.fontsize = fontsize;
		text_key.text.assign(text, (size_t)len);
		auto it = cache->text_width_cache.find(text_key);
		if (it != cache->text_width_cache.end())
			return it->second;
	}

	float best = 0, cur = 0;
	for (int i = 0; i < len;)
	{
		unsigned char c = (unsigned char)text[i];
		if (c == '\n')
		{
			best = std::max(best, cur);
			cur = 0;
			i++;
			continue;
		}
		uint32_t cp = vg_measure_u8_decode(text + i);
		int n = 1;
		if (c >= 0xF0) n = 4;
		else if (c >= 0xE0) n = 3;
		else if (c >= 0xC0) n = 2;
		i += n;

		float advance = 0;
		bool advance_cached = false;
		vg_measure_glyph_key glyph_key;
		if (cache)
		{
			glyph_key.font = ffs;
			glyph_key.fontsize = fontsize;
			glyph_key.codepoint = cp;
			auto it = cache->glyph_advance_cache.find(glyph_key);
			if (it != cache->glyph_advance_cache.end())
			{
				advance = it->second;
				advance_cached = true;
			}
		}

		if (!advance_cached)
		{
			// Same family selection as ovg rendering: first family whose
			// coverage has the codepoint, otherwise the primary family.
			const font_family_t* fam = ffs->familys[0];
			for (int f = 0; f < ffs->count; f++)
			{
				const font_family_t* cand = ffs->familys[f];
				if (cand && cand->font && cand->coverage && hb_set_has(cand->coverage, cp))
				{
					fam = cand;
					break;
				}
			}
			if (!fam || !fam->font)
			{
				advance = fontsize * 0.5f;
			}
			else
			{
				hb_font_set_scale(fam->font, fontsize, fontsize);
				hb_codepoint_t gid = 0;
				hb_font_get_nominal_glyph(fam->font, cp, &gid);
				advance = (float)hb_font_get_glyph_h_advance(fam->font, gid);
			}
			if (cache)
			{
				if (cache->glyph_advance_cache.size() >= 8192)
					cache->glyph_advance_cache.clear();
				cache->glyph_advance_cache.emplace(glyph_key, advance);
			}
		}
		cur += advance;
	}

	float width = std::max(best, cur);
	if (text_cached)
	{
		if (cache->text_width_cache.size() >= 4096)
			cache->text_width_cache.clear();
		cache->text_width_cache.emplace(std::move(text_key), width);
	}
	return width;
}

vg_font_metrics_t vg_get_font_metrics(const font_familys_t* ffs, float font_size)
{
	vg_font_metrics_t out = {};
	int fontsize = (int)(std::isfinite(font_size) ? std::clamp(font_size, 1.0f, 32768.0f) + 0.5f : 1.0f);

	font_cache_cx* cache = vg_measure_cache(ffs);
	if (cache)
	{
		auto key = std::make_pair(ffs, fontsize);
		auto it = cache->font_metrics_cache.find(key);
		if (it != cache->font_metrics_cache.end())
			return it->second;
	}

	// Mirrors the old font_get_lineheight(first=true) + vgui_text_ascent:
	// both come from the primary family at the rounded pixel size, with the
	// same fallbacks when extents are unavailable.
	int lh = 0;
	float ascent = 0;
	if (ffs && ffs->count > 0 && ffs->familys && ffs->familys[0] && ffs->familys[0]->font)
	{
		auto ex = get_font_extents(ffs->familys[0]->font, fontsize, false);
		lh = (int)(ex.x - ex.y + ex.z);
		ascent = ex.x;
	}
	if (lh <= 0)
		lh = (int)(font_size * 1.35f + 0.5f);
	if (lh < 1) lh = 1;
	if (ascent <= 0) ascent = font_size * 0.8f;

	out.ascent = ascent;
	out.line_height = (float)lh;
	const float px = (float)fontsize;
	out.x_height = px * 0.5f;
	out.cap_height = px * 0.7f;
	out.underline_thickness = std::max(1.0f, std::round(px / 14.0f));
	out.underline_offset = std::max(1.0f, std::round(px * 0.11f));
	out.strikeout_thickness = out.underline_thickness;
	out.strikeout_offset = -out.x_height * 0.55f;
	if (ffs && ffs->count > 0 && ffs->familys && ffs->familys[0] && ffs->familys[0]->font)
	{
		auto* font = ffs->familys[0]->font;
		// Use fractional pixel precision: small positive design thicknesses
		// would otherwise round to zero at common UI font sizes.
		hb_font_set_scale(font, fontsize * 64, fontsize * 64);
		auto metric = [&](hb_ot_metrics_tag_t tag, float fallback, bool with_fallback = false)
		{
			hb_position_t value = 0;
			if (with_fallback)
				hb_ot_metrics_get_position_with_fallback(font, tag, &value);
			else if (!hb_ot_metrics_get_position(font, tag, &value))
				return fallback;
			return value != 0 ? value / 64.0f : fallback;
		};
		out.x_height = metric(HB_OT_METRICS_TAG_X_HEIGHT, out.x_height, true);
		out.cap_height = metric(HB_OT_METRICS_TAG_CAP_HEIGHT, out.cap_height, true);
		out.underline_offset = -metric(HB_OT_METRICS_TAG_UNDERLINE_OFFSET, -out.underline_offset);
		const float ut = metric(HB_OT_METRICS_TAG_UNDERLINE_SIZE, out.underline_thickness);
		if (ut > 0) out.underline_thickness = std::max(1.0f, ut);
		out.strikeout_offset = -metric(HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, out.x_height * 0.55f);
		const float st = metric(HB_OT_METRICS_TAG_STRIKEOUT_SIZE, out.underline_thickness);
		out.strikeout_thickness = st > 0 ? std::max(1.0f, st) : out.underline_thickness;
		hb_font_set_scale(font, fontsize, fontsize);
	}

	if (cache)
		cache->font_metrics_cache.emplace(std::make_pair(ffs, fontsize), out);
	return out;
}


static glyph_raster build_glyph_image_hb(vg_font* hp, uint32_t gid, int font_size, glm::ivec4* ot, const glm::vec2& scale)
{
	glyph_raster img(nullptr, {hp});
	auto* rdr = hp->rdr;
	auto* font = hp->font;
	auto* pnt = hp->pnt;
	hb_raster_draw_reset(rdr);
	hb_font_set_scale(font, font_size, font_size);
	const bool exact_bounds = scale.x == 1 && scale.y == 1 && !pnt;
	if (exact_bounds)
	{
		// At small sizes integer font extents can round inward, and an
		// explicit raster box then drops coverage before the text is drawn.
		// Let HarfBuzz bound the actual outline in the flipped pixel space.
		hb_raster_draw_set_transform(rdr, 1.f, 0.f, 0.f, -1.f, 0.f, 0.f);
		hb_raster_draw_glyph(rdr, font, gid);
		img.reset(hb_raster_draw_render(rdr));
	}
	else
	{
		// Keep the existing color paint and LCD buffer geometry.
		constexpr int margin = 2;
		hb_glyph_extents_t regular = {}, scaled = {};
		hb_font_get_glyph_extents(font, gid, &regular);
		hb_font_set_scale(font, font_size * scale.x, font_size * scale.y);
		const bool has_extents = hb_font_get_glyph_extents(font, gid, &scaled);
		hb_raster_extents_t box = {};
		box.x_origin = scaled.x_bearing;
		box.y_origin = -(scaled.y_bearing + margin);
		box.width = ovg::align_up(abs(scaled.x_bearing) + scaled.width + margin * 2, 2);
		box.height = ovg::align_up(abs(scaled.height) + abs(scaled.y_bearing), 2);
		if (pnt && has_extents)
		{
			hb_raster_paint_set_extents(pnt, &box);
			hb_raster_paint_set_foreground(pnt, HB_COLOR(255, 255, 255, 255));
			hb_raster_paint_set_transform(pnt, 1.f, 0.f, 0.f, -1.f, 0.f, 0.f);
			hb_raster_paint_glyph(pnt, font, gid);
			img.reset(hb_raster_paint_render(pnt));
			img.get_deleter().painted = !!img;
		}
		if (!img)
		{
			hb_raster_draw_set_extents(rdr, &box);
			hb_raster_draw_set_transform(rdr, 1.f, 0.f, 0.f, -1.f, 0.f, 0.f);
			hb_raster_draw_glyph(rdr, font, gid);
			img.reset(hb_raster_draw_render(rdr));
		}
		ot->x = regular.x_bearing;
		ot->y = regular.y_bearing + margin;
		ot->w = ovg::align_up(abs(regular.height) + margin * 2, 4);
	}
	if (img)
	{
		hb_raster_extents_t ext = {};
		hb_raster_image_get_extents(img.get(), &ext);
		if (!ext.width || !ext.height) img.reset();
		else if (exact_bounds)
			*ot = {ext.x_origin, -ext.y_origin, (int)ext.width, (int)ext.height};
		else
			ot->z = ext.width;
	}
	return img;
}
#if 1
// 像素写入器：编译期选择，可被完全内联（原函数指针版本无法内联，逐像素一次间接调用）
// put 返回 false 表示跳过不写入；row_copy=true 且无翻转/无 x 偏移时整行 memcpy
struct write_gray_to_color {
	static constexpr bool row_copy = false;
	static bool put(const uint8_t* src, uint32_t& out) {
		uint32_t color = -1;
		uint8_t gray = src[0];
		if (gray == 0) return false;
		out = (color & 0x00FFFFFF) | (uint32_t(gray) << 24);
		return true;
	}
};

struct write_gray_mul {
	static constexpr bool row_copy = false;
	static bool put(const uint8_t* src, uint32_t& out) {
		uint8_t gray = src[0];
		if (gray == 0) return false;
		out = (uint32_t(gray) << 24) | (uint32_t(gray) << 16) | (uint32_t(gray) << 8) | gray;
		return true;
	}
};

struct write_bgra_raw {
	static constexpr bool row_copy = true;   // 直接拷贝，可整行 memcpy
	static bool put(const uint8_t* src, uint32_t& out) {
		out = *(const uint32_t*)src;
		return true;
	}
};

struct write_bgra_to_rgba {
	static constexpr bool row_copy = false;
	static bool put(const uint8_t* src, uint32_t& out) {
		uint32_t pixel = *(const uint32_t*)src;
		uint8_t b = pixel & 0xFF;
		uint8_t g = (pixel >> 8) & 0xFF;
		uint8_t r = (pixel >> 16) & 0xFF;
		uint8_t a = (pixel >> 24) & 0xFF;
		out = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
		return true;
	}
};

template <typename Writer>
static void rgba_blit(
	ovg_image_data* dst,
	int dst_x, int dst_y,
	int src_x, int src_y,
	int src_w, int src_h,
	const uint8_t* src_data,
	int src_stride,        // 字节
	int src_bpp,           // 每像素字节数
	bool flip_y
)
{
	if (!dst || !src_data || src_w <= 0 || src_h <= 0 || src_stride < src_w * src_bpp)
		return;

	// 裁剪到 dst 边界
	int d_x = std::max(0, dst_x);
	int d_y = std::max(0, dst_y);
	int s_x = src_x;
	int s_y = src_y;

	int copy_w = std::min(src_w - s_x, dst->width - d_x);
	int copy_h = std::min(src_h - s_y, dst->height - d_y);

	if (copy_w <= 0 || copy_h <= 0)
		return;

	// 无翻转且从行首开始时可整行 memcpy（仅对可直接拷贝的写入器）
	const bool row_copy = Writer::row_copy && !flip_y && s_x == 0;
	const size_t row_bytes = (size_t)copy_w * src_bpp;

	for (int iy = 0; iy < copy_h; ++iy) {
		int src_row_y = flip_y ? (src_h - 1 - (s_y + iy)) : (s_y + iy);
		const uint8_t* src_row = src_data + src_row_y * src_stride + s_x * src_bpp;
		uint32_t* dst_row = dst->data + (d_y + iy) * dst->width + d_x;

		if (row_copy) {
			memcpy(dst_row, src_row, row_bytes);
			continue;
		}
		for (int ix = 0; ix < copy_w; ++ix) {
			uint32_t pixel;
			if (Writer::put(src_row + ix * src_bpp, pixel)) {
				dst_row[ix] = pixel;
			}
		}
	}
}

void rgba_copy2gray(ovg_image_data* dst, int x, int y, int w, int h,
	uint32_t c, const uint8_t* dt, int stride, bool fy)
{
	if (stride < 1) stride = w;

	rgba_blit<write_gray_to_color>(dst, x, y, 0, 0, w, h, dt, stride, 1, fy);
}

void rgba_copy2gray_mul(ovg_image_data* dst, int x, int y, int w, int h,
	const uint8_t* dt, int stride, bool fy)
{
	if (stride < 1) stride = w;
	rgba_blit<write_gray_mul>(dst, x, y, 0, 0, w, h, dt, stride, 1, fy);
}

void rgba_copy_bgra(ovg_image_data* dst, int x, int y, int w, int h,
	const uint32_t* dt, int stride, bool fy)
{
	if (stride < 1) stride = w * 4;
	rgba_blit<write_bgra_raw>(dst, x, y, 0, 0, w, h, (const uint8_t*)dt, stride, 4, fy);
}

void rgba_copy_bgra2rgba(ovg_image_data* dst, int x, int y, int w, int h,
	const uint32_t* dt, int stride, bool fy)
{
	if (stride < 1) stride = w * 4;
	rgba_blit<write_bgra_to_rgba>(dst, x, y, 0, 0, w, h, (const uint8_t*)dt, stride, 4, fy);
}
#endif // 1


struct A8Buf {
	int      w, h;
	uint8_t* rows; // 行主序，pitch = w
	std::vector<uint8_t> _data;
	A8Buf(int w_, int h_) : w(w_), h(h_) {
		_data.resize(w_ * h_); rows = _data.data();
		memset(rows, 0, w * h);
	}
	~A8Buf() { rows = 0; }
	uint8_t* row(int y) { return rows + y * w; }
	const uint8_t* row(int y) const { return rows + y * w; }
};
struct A8Glyph {
	uint8_t* coverage = nullptr;   // 长度 = width * height
	int      width = 0;         // 像素宽度（已含 3x 水平放大）
	int      height = 0;
	int      dx = 0;         // 相对字形原点的水平偏移（像素）
	int      dy = 0;         // 相对字形原点的垂直偏移（像素）
	std::vector<uint8_t> _data;

	A8Glyph() {}
	A8Glyph(int w, int h) {
		if (w > 0 && h > 0) {
			width = w; height = h; _data.resize(w * h);
			coverage = _data.data();
			memset(coverage, 0, w * h);
		}
	}
	~A8Glyph() { coverage = 0; }

};
/* ==================== 子像素排列（subpixel layout）====================
* LCD 亚像素渲染必须知道显示器的子像素物理排列，否则彩色边缘会反向/偏色。
*   RGB  / BGR   ：水平排列，可做三通道着色（最常见）
*   VRGB / VBGR  ：垂直排列，水平方向无额外分辨率 → 退化为灰阶 AA
*   NONE         ：非标准/未知排列（PenTile、旋转屏、高 DPI）→ 强制灰阶
*
* 取值应与操作系统报告一致：
*   Windows : GetDeviceCaps(hdc, COLORMGM) / DWM
*   X11     : xrandr --prop  →  "Subpixel: rgb"
*   Wayland : wl_output.subpixel
*   macOS   : 系统内部决定，不暴露，通常按 RGB 处理或默认灰阶 */

/* ==================== 2. 5-tap FIR 低通（FT_LcdFilter 经典系数）====================
	对每个颜色分量用相邻 3 个「通道」平滑，抑制 LCD 色边条纹。
	系数 {0x08, 0x4D, 0x56, 0x4D, 0x08} 与 FreeType 默认一致。
	水平 5-tap，作用于「3x 宽」的 A8 缓冲（每像素 3 个采样点）。 */
void fir_horizontal(A8Buf& src, A8Buf& dst) {
	static const uint8_t FIR[5] = { 0x08, 0x4D, 0x56, 0x4D, 0x08 };
	static const uint16_t FIR_SUM = 0x08 + 0x4D + 0x56 + 0x4D + 0x08; // = 0x140
	for (int y = 0; y < src.h; ++y) {
		uint8_t* s = src.row(y);
		uint8_t* d = dst.row(y);
		for (int x = 0; x < src.w; ++x) {
			int v = 0;
			for (int k = -2; k <= 2; ++k) {
				int xi = x + k;
				if (xi < 0) xi = 0;
				if (xi >= src.w) xi = src.w - 1;
				v += FIR[k + 2] * s[xi];
			}
			d[x] = uint8_t(v / FIR_SUM); // 归一化
		}
	}
}
inline void blend_lcd_bgra_premul(
	uint8_t* dst,        // 目标 BGRA（dst RGB 也必须是预乘格式）
	const uint8_t* src)  // a8_to_lcd 输出的 BGRA（预乘）
{
	uint8_t src_a = src[3];
	if (src_a == 0) return;

	if (src_a == 255) {
		*(uint32_t*)dst = *(const uint32_t*)src;
		return;
	}
	uint8_t dst_a = dst[3];
	uint8_t ia = 255 - src_a;
	dst[0] = (src[0] + (uint16_t)dst[0] * ia / 255);
	dst[1] = (src[1] + (uint16_t)dst[1] * ia / 255);
	dst[2] = (src[2] + (uint16_t)dst[2] * ia / 255);
	dst[3] = src_a + (uint16_t)dst_a * ia / 255;
}
void a8_to_lcd(const A8Buf& filtered, int height, uint8_t* rgba_out, int out_w, int out_stride, SubpixelLayout layout, uint32_t c) {
	glm::vec4 c4 = *(glm::u8vec4*)&c;
	c4 *= c4.w / 255.0;
	glm::u8vec4 color = c4;
	/* gamma 校正：覆盖度是线性面积，需转 sRGB 观感。
	 * 原实现每像素调用 std::pow（双精度），LCD 路径每字形数千次——改为 256 项查表。 */
	struct gamma_lut_t {
		uint8_t v[256];
		gamma_lut_t() {
			for (int i = 0; i < 256; ++i) {
				double n = i / 255.0;
				n = std::pow(n, 1.0 / 1.8); // 1.8 接近典型 LCD gamma
				v[i] = uint8_t(glm::clamp(n * 255.0, 0.0, 255.0));
			}
		}
	};
	static const gamma_lut_t gamma_lut;
	auto gamma = [](int v) -> uint8_t {
		return gamma_lut.v[glm::clamp(v, 0, 255)];
		};

	/* 是否为水平排列（可做真正的三通道 LCD 着色） */
	const bool horizontal = (layout == SubpixelLayout::RGB || layout == SubpixelLayout::BGR);
	int ow = std::min(filtered.w / 3, out_w);
	for (int y = 0; y < height; ++y) {
		const uint8_t* a = filtered.row(y);
		uint8_t* out = rgba_out + y * out_stride;
		for (int px = 0; px < ow; ++px) {
			int c0 = a[px * 3 + 0]; // 左通道
			int c1 = a[px * 3 + 1]; // 中通道
			int c2 = a[px * 3 + 2]; // 右通道

			uint8_t r, g, b;
			uint8_t gray0 = gamma((c0 + c1 + c2) / 3);
			uint8_t gray = gamma(gray0);
			if (horizontal) {
				/* 水平排列：三个采样点 → R/G/B，BGR 时翻转。
				 * 各通道独立乘颜色 → 产生 LCD 彩色边缘。 */
				r = gamma(c0); g = gamma(c1); b = gamma(c2);
				if (layout == SubpixelLayout::BGR) std::swap(r, b);
				r = uint8_t((r * color.x) >> 8);
				g = uint8_t((g * color.y) >> 8);
				b = uint8_t((b * color.z) >> 8);
			}
			else {
				/* 垂直 / 未知排列：退化为灰阶 AA（不产生彩色 fringe）。
				 * 用三个采样的平均覆盖度，乘上「颜色的亮度」——
				 * 关键是三通道用同一个系数，保证 r==g==b 仍是灰阶。
				 * 亮度系数采用 BT.601 (0.299R+0.587G+0.114B)，接近人眼感知。 */
				uint8_t lum = uint8_t((color.x * 77 + color.y * 150 + color.z * 29) >> 8);
				r = g = b = gray;// uint8_t((gray * lum) >> 8);
				r = uint8_t((r * color.x) >> 8);
				g = uint8_t((g * color.y) >> 8);
				b = uint8_t((b * color.z) >> 8);
			}

			{
				uint32_t cc = 0;
				auto dc = (uint8_t*)&cc;
				dc[0] = b;
				dc[1] = g;
				dc[2] = r;
				dc[3] = gray0;
				*(uint32_t*)(out + px * 4) = cc;
			}
		}
	}
}

void rgba_copy2gray(A8Glyph* dst, const glm::ivec2& pos, const glm::ivec4& src, const uint8_t* dt, int stride, bool fy)
{
	if (stride < 1)stride = src.z;
	if (!dst || !dt || src.z <= 0 || src.w <= 0 || stride < src.z) return;
	// 计算实际绘制区域（处理边界裁剪）
	int dst_x = std::max(0, pos.x);
	int dst_y = std::max(0, pos.y);
	int src_x = src.x;
	int src_y = src.y;
	int copy_w = std::min(src.z - src_x, dst->width - dst_x);
	int copy_h = std::min(src.w - src_y, dst->height - dst_y);
	for (int iy = 0; iy < copy_h; ++iy) {
		const uint8_t* src_row = fy ? dt + (src.w - 1 - (src_y + iy)) * stride : dt + (src_y + iy) * stride + src_x;
		auto dst_row = dst->coverage + ((dst_y + iy) * dst->width + dst_x);
		for (int ix = 0; ix < copy_w; ++ix) {
			uint8_t gray = src_row[ix];
			if (gray > 0)
			{
				dst_row[ix] = gray;
			}
		}
	}
}

void subpixel_lcd(ovg_image_data* dst, int px, int py, raster_image_t* img_src, const glm::ivec2& size, SubpixelLayout pixelLayout, bool flipY)
{
	auto& ext = *img_src;
	glm::ivec2 ws = { ovg::align_up(ext.width,2), ovg::align_up(ext.height,2) };

	A8Buf src(ws.x, ws.y);
	A8Buf filtered(ws.x, ws.y);
	auto coverage = img_src->data;
#if 1
	const uint8_t* src_row = coverage + (flipY ? (ext.height - 1) * ext.stride : 0);
	int row_step = flipY ? -ext.stride : ext.stride;
	for (size_t i = 0; i < ext.height; i++)
	{
		std::memcpy(src.rows + src.w * i, src_row, ext.width);
		src_row += row_step;
	}
#else
	A8Glyph g(ws.x, ws.y);
	rgba_copy2gray(&g, { 0, 0 }, { 0,0,ext.width, ext.height }, coverage, ext.stride, true);
	std::memcpy(src.rows, g.coverage, g.width * g.height);
#endif
	uint32_t c = -1;
	/* ② 5-tap FIR 低通 */
	fir_horizontal(src, filtered);
	/* ③ A8(3x) → LCD 三通道 + gamma */
	if (py < 0)
		py = 0;
	if (px < 0)px = 0;
	a8_to_lcd(filtered, ext.height, (uint8_t*)(dst->data + px + py * dst->width), size.x, dst->stride, pixelLayout, c);
}

bool gfont_copy_image(ovg_image_data* dst, int rx, int ry, uint32_t color, raster_image_t* img_src, const glm::ivec4& orc, bool origin, bool type, SubpixelLayout pixelLayout)
{
	auto img = img_src;
	bool has_color = false;
	dst->multiply = 1;
	bool fy = false;
	auto& ext = *img_src;
	if (origin)
	{
		rx += orc.x;
		ry += orc.y;
	}
	if (img_src->fmt == HB_RASTER_FORMAT_A8)
	{
		if (type)
			subpixel_lcd(dst, rx, ry, img, { orc.z,orc.w }, pixelLayout, fy);
		else
			rgba_copy2gray_mul(dst, rx, ry, ext.width, ext.height, img_src->data, ext.stride, fy);
	}
	else if (img_src->fmt == HB_RASTER_FORMAT_BGRA32)
	{
		has_color = true;
		if (dst->format == 1)
			rgba_copy_bgra(dst, rx, ry, ext.width, ext.height, (uint32_t*)img_src->data, ext.stride, fy);
		else
			rgba_copy_bgra2rgba(dst, rx, ry, ext.width, ext.height, (uint32_t*)img_src->data, ext.stride, fy);
	}
	return has_color;
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
	std::vector<stbrp_rect> _scratch;   // 复用缓冲，避免每次 push_rect 堆分配
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
		if (_scratch.size() < (size_t)n)_scratch.resize(n);
		auto rct = _scratch.data();
		auto r = rc;
		auto t = (char*)rc;
		if (stride < sizeof(glm::ivec4))
			stride = sizeof(glm::ivec4);
		for (int i = 0; i < n; ++i)
		{
			r = (glm::ivec4*)t;
			rct[i].id = 0; rct[i].w = r->z; rct[i].h = r->w;
			t += stride;
		}
		t = (char*)rc;
		int ret = stbrp_pack_rects(&_ctx, rct, n);
		size_t cx = 0;
		for (int i = 0; i < n; ++i)
		{
			r = (glm::ivec4*)t;
			r->x = rct[i].x; r->y = rct[i].y;
			if (!rct[i].was_packed)
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
	ovg_image_data img = {};
	std::vector<uint32_t> ptr;
	std::vector<stbrp_node> _rpns;
	std::vector<stbrp_rect> _scratch;   // 复用缓冲，避免每次 push_rect 堆分配
public:
	stb_packer() {}
	~stb_packer() {}
	ovg_image_data* get() {
		return &img;
	}
	// BL = 0 “从下向左塞”（快速降低高度）
	// BF = 1 “精打细算”（最小化空间浪费）
	void init_target(int width, int height, int heuristic) {
		assert(!(width < 10 || height < 10));
		if (width < 10 || height < 10)return;
		ptr.resize(width * height);
		auto img = get();
		img->format = 1;
		img->width = width;
		img->height = height;
		img->valid = 1;
		img->multiply = true;
		img->stride = width * sizeof(uint32_t);
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
		if (_scratch.size() < (size_t)n)_scratch.resize(n);
		auto rct = _scratch.data();
		auto r = rc;
		auto t = (char*)rc;
		if (stride < sizeof(glm::ivec4))
			stride = sizeof(glm::ivec4);
		for (int i = 0; i < n; ++i)
		{
			r = (glm::ivec4*)t;
			rct[i].id = 0; rct[i].w = r->z; rct[i].h = r->w;
			t += stride;
		}
		int ret = stbrp_pack_rects(&_ctx, rct, n);
		for (int i = 0; i < n; ++i)
		{
			r = (glm::ivec4*)t;
			r->x = rct[i].x; r->y = rct[i].y;
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

ovg_image_data* image_cache_cx::push_cache_size(const glm::ivec2& ss, glm::ivec2* pos, int linegap)
{
	int width = ovg::align_up(ss.x + linegap, 10), height = ovg::align_up(ss.y + linegap, 2);
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

ovg_image_data* image_cache_cx::push_cache_bitmap(hb_raster_image_t* img, glm::ivec2* pos, int linegap)
{
	hb_raster_extents_t ext = {};
	hb_raster_image_get_extents(img, &ext);
	int width = ovg::align_up(ext.width + linegap, 2), height = ovg::align_up(ext.height + linegap, 2);
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

void text_draw_list::clear() { cmds.clear(); }
void text_draw_list::push_raster(glyph_atlas_entry* e, float x, float y, float w, float h, const glm::vec4& uv, uint32_t c) {
	cmds.push_back({ glyph_draw_cmd::RASTER, e, {x,y}, {w,h}, uv, c ,fontsize });
}
void text_draw_list::push_vector(glyph_atlas_entry* e, float x, float y, uint32_t c) {
	cmds.push_back({ glyph_draw_cmd::VECTOR, e, {x,y}, {}, {}, c ,fontsize });
}
vg_text_run_cx::vg_text_run_cx() {
	_buf = hb_buffer_create();
}

vg_text_run_cx::~vg_text_run_cx() {
	free_buffer();

}

void vg_text_run_cx::clear()
{
	_ffs = nullptr;
	_fontsize = 16;
	_utf16.clear();
	_extents = {};
	_glyphs.clear();
	_glyph_count = 0;
	_cache = nullptr;
	visual_runs.clear();
	//_buf = 0;			// 可复用对象不用清除
	//_bidi = 0;
	//_line_brk = 0;
	_layout = {};
	_shaped.clear();
	_indexs.clear();
	drawable.clear();
}


void vg_text_run_cx::free_buffer() {
	if (_buf) {
		hb_buffer_destroy(_buf);
		_buf = nullptr;
	}
	auto icu = get_icu(U_ICU_VERSION_MAJOR_NUM);
	if (icu)
	{
		if (_bidi) {
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
		}
		if (_line_brk)
		{
			icu->_ubrk_close(_line_brk);
			_line_brk = nullptr;
		}
	}
}


void vg_text_run_cx::set_font_families(const font_familys_t* ffs, int fontsize) {
	_ffs = ffs;
	_fontsize = fontsize;
	if (ffs && ffs->count > 0 && ffs->familys[0]) {
		auto font = ffs->familys[0]->font;
		if (font) {
			_cache = (font_cache_cx*)hb_font_get_user_data(font, &g_font_cache_key);
		}
		else {
			_cache = nullptr;
		}
	}
}

void vg_text_run_cx::set_text(const void* str8, size_t len) {
	_utf16.clear();
	if (!str8 || len == 0) return;
	if (len == (size_t)-1) len = strlen((const char*)str8);
	vg_utf8_to_utf16(str8, len, &_utf16);
}

void vg_text_run_cx::set_layout_mode(const text_box_rt* box, float max_width, bool enable_bidi, uint8_t para_dir, const char* locale)
{
	if (box)
		_box = *box;
	if (locale && *locale)
	{
		_layout._locale = locale;
	}
	else {
		_layout._locale.clear();
	}
	_layout.enable_bidi = enable_bidi;
	_layout.max_width = max_width;
	_layout.para_dir = para_dir;
}


void vg_text_run_cx::clear_glyphs() {
	_glyphs.clear();
	_glyph_count = 0;
	_extents = {};

}

// 找到 y == 1 的连续段起点/终点执行置反
void reverse1(std::vector<glm::ivec2>& v)
{
	size_t i = 0;
	const size_t n = v.size();
	while (i < n)
	{
		if (v[i].y != 1)
		{
			++i;
			continue;
		}
		size_t start = i;
		while (i < n && v[i].y == 1)
			++i;
		size_t end = i;
		std::reverse(v.begin() + start, v.begin() + end);
	}
}
bool find_br(const char16_t* p, int n)
{
	if (!p || !p[0] || n < 1)return false;
	for (int i = 0; i < n; i++) {
		if (p[i] == '\n')
		{
			return true;
		}
	}
	return false;
}
void vg_text_run_cx::shape() {
	clear_glyphs();
	if (!_ffs || _ffs->count < 1 || _utf16.empty()) return;

	uint16_t* p = (uint16_t*)_utf16.data();
	uint16_t* end = p + _utf16.size();
	size_t len = _utf16.size();
	auto icu = get_icu(U_ICU_VERSION_MAJOR_NUM);
	UErrorCode err = U_ZERO_ERROR;
	visual_runs.clear();
	std::vector<int> visualMap;
	if (_layout.enable_bidi)
	{
		UErrorCode st = {};
		if (!icu)return;
		if (!_bidi)
		{
			_bidi = icu->_ubidi_open();
		}
		icu->_ubidi_setPara(_bidi, (UChar*)p, len, _layout.para_dir ? UBIDI_DEFAULT_RTL : UBIDI_DEFAULT_LTR, NULL, &st);
		if (U_FAILURE(st)) {
			fprintf(stderr, "ubidi_setPara failed: %s\n", u_errorName(st));
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
			return;
		}
		int32_t run_count = icu->_ubidi_countRuns(_bidi, &st);
		if (U_FAILURE(st)) {
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
			return;
		}
		visual_runs.reserve(len);
		for (int32_t r = 0; r < run_count; r++) {
			int32_t run_start, run_len;
			UBiDiDirection run_dir = icu->_ubidi_getVisualRun(_bidi, r, &run_start, &run_len);
			visual_runs.push_back(glm::uvec3(run_start, run_len, run_dir));
		}
		int length = icu->_ubidi_getLength(_bidi);
		visualMap.resize(length);
		icu->_ubidi_getVisualMap(_bidi, visualMap.data(), &err);
	}
	else {
		visual_runs.push_back(glm::uvec3(0, len, (UBiDiDirection)_layout.para_dir));
	}

	std::vector<int> breaks;
	if (_box.word_wrap >= 0)
	{
		if (_line_brk && current_word_wrap != _box.word_wrap) {
			icu->_ubrk_close(_line_brk); _line_brk = nullptr;
		}
		if (!_line_brk) {
			current_word_wrap = _box.word_wrap;
			_line_brk = icu->_ubrk_open(
				(UBreakIteratorType)_box.word_wrap,
				_layout._locale.size() ? _layout._locale.c_str() : nullptr,
				(UChar*)p, len, &err);
		}
		else {
			// 复用：换文本
			err = U_ZERO_ERROR;
			icu->_ubrk_setText(_line_brk, (UChar*)p, len, &err);
		}

		if (U_FAILURE(err)) {
			//fprintf(stderr, "ubrk_open/setText failed: %s\n", u_errorName(err));
			if (_line_brk) { icu->_ubrk_close(_line_brk); _line_brk = nullptr; }
		}
		else {
			// 收集断点（UTF-16 索引）
			breaks.push_back({});
			int32_t pos = icu->_ubrk_first(_line_brk);
			while (pos != UBRK_DONE) {
				if (pos > 0 && pos < (int32_t)len)
				{
					auto rs = icu->_ubrk_getRuleStatus(_line_brk);
					bool br = rs >= UBRK_LINE_HARD;

					breaks.push_back(pos);
				}
				pos = icu->_ubrk_next(_line_brk);
			}
			breaks.push_back((int32_t)len);
		}
	}

	// ─── 交叉 visual runs × 断点 → segments ───
	// 每个 segment: [u16_start, u16_len, direction]
	std::vector<glm::uvec3> segments;

	if (breaks.empty()) {
		// 无断行：直接用 visual runs
		segments = visual_runs;
	}
	else {
		// 有断行：每个 visual run 按断点切
		for (const auto& run : visual_runs) {
			int32_t run_start = (int32_t)run.x;
			int32_t run_end = run_start + (int32_t)run.y;
			UBiDiDirection dir = (UBiDiDirection)run.z;
			// 找和这个 run 重叠的断点区间
			for (size_t k = 0; k < breaks.size() - 1; ++k) {
				auto b0 = breaks[k];
				auto b1 = breaks[k + 1];
				int32_t seg_start = std::max(b0, run_start);
				int32_t seg_end = std::min(b1, run_end);
				if (seg_start < seg_end) {
					segments.push_back(glm::uvec3(
						(uint32_t)seg_start,
						(uint32_t)(seg_end - seg_start),
						(uint32_t)dir));
				}
			}
		}
	}

	struct subseg_t {
		int32_t        start;
		int32_t        len;
		UBiDiDirection dir;
		hb_font_t* font;
		int            fontsize;
		char16_t* str;
	};
	std::vector<subseg_t> subsegs;
	uint16_t* base = (uint16_t*)_utf16.data();

	_indexs.clear();
	_indexs.reserve(512);
	int cidx = 0;
	for (const auto& seg : segments) {
		uint16_t* p = base + seg.x;
		uint16_t* end = p + seg.y;

		while (p < end) {
			const uint16_t* sub_start = p;

			// 解码第一个 codepoint → 决定字体
			uint16_t* look = p;
			uint32_t cp = utf16_next(look, end);
			const font_family_t* ff = resolve_family(_ffs, cp);
			if (!ff) ff = _ffs->familys[0];
			hb_font_t* font = ff->font;
			int scale = _fontsize > 0 ? _fontsize : ff->upem;

			// 找同字体连续区间
			uint16_t* sub_end = look;
			while (sub_end < end) {
				uint16_t* nlook = sub_end;
				uint32_t ncp = utf16_next(nlook, end);
				const font_family_t* nff = resolve_family(_ffs, ncp);
				if (!nff) nff = _ffs->familys[0];
				if (nff->font != font) break;
				sub_end = nlook;
			}
			auto first = (sub_start - base);
			char16_t* str = _utf16.data() + first;
			subsegs.push_back({ (int32_t)first, (int32_t)(sub_end - sub_start), (UBiDiDirection)seg.z, font, scale,str });

			_indexs.push_back({ cidx++ ,seg.z });
			p = sub_end;
		}
	}
	reverse1(_indexs);
	// ═══════════════════════════════════════════════════════════
	//  Step 5: Pass 1 — shape 每个 subsegment
	// ═══════════════════════════════════════════════════════════
	_shaped.clear();
	_shaped.reserve(subsegs.size());
	_glyphs.clear();
	std::vector<int> vmsize;
	int tdir = 0;
	vmsize.push_back(0);
	for (const auto& it : _indexs)
	{
		auto& ss = subsegs[it.x];
		shaped_segment_t out;
		shape_segment(ss.start, ss.len, ss.dir, ss.font, ss.fontsize, out);
		if (tdir == out.dir) {
			vmsize.back() += out.width_px;
		}
		else {
			vmsize.push_back(out.width_px);
			tdir = out.dir;
		}
		char16_t* ch = (char16_t*)base + ss.start;
		auto yb = find_br(ch, ss.len);
		if (yb)
			yb = yb;
		out.new_line = yb;
		for (const auto& g : out.glyphs) {
			_glyphs.push_back(g);
		}
		_shaped.push_back(std::move(out));
	}
	// ─── 计算整体 extents ───
	// 与 populate_draw_list 的排版规则保持一致：按 segment 顺序累计 advance，
	// 遇到 new_line 换行；宽度取最长行，高度 = 行数 × 行高。
	int fs = _fontsize > 0 ? _fontsize : 1;
	hb_font_t* primary = _ffs->familys[0] ? _ffs->familys[0]->font : nullptr;
	hb_font_extents_t fextents = {};
	if (primary) {
		hb_font_set_scale(primary, fs, fs);
		hb_font_get_extents_for_direction(primary, HB_DIRECTION_LTR, &fextents);
	}
	float line_height = _st.lineheight;
	if (line_height <= 0.0f && primary)
		line_height = (float)(fextents.ascender - fextents.descender + fextents.line_gap);
	if (line_height <= 0.0f)
		line_height = fs * 1.2f;

	_extents = {};
	float cur_width = 0.0f;
	float max_width = 0.0f;
	int line_count = 1;
	for (const auto& seg : _shaped) {
		for (const auto& g : seg.glyphs)
			cur_width += std::fabs(g.x_advance);
		if (seg.new_line) {
			max_width = std::max(max_width, cur_width);
			cur_width = 0.0f;
			line_count++;
		}
	}
	max_width = std::max(max_width, cur_width);
	_extents.width = max_width;
	_extents.height = line_height * line_count;
	_extents.x_advance = max_width;
	_extents.y_advance = _extents.height;
	// bearing：以首字形相对基线的偏移 + 首行 ascent 给出
	if (!_shaped.empty() && !_shaped[0].glyphs.empty()) {
		_extents.x_bearing = _shaped[0].glyphs[0].x_offset;
		_extents.y_bearing = fextents.ascender > 0 ? -(float)fextents.ascender : _shaped[0].glyphs[0].y_offset;
	}
	_glyph_count = (uint32_t)_glyphs.size();

}
void vg_text_run_cx::shape_segment(int u16_start, int u16_len, int  dir0, hb_font_t* font, int fontsize, shaped_segment_t& out)
{
	out.dir = dir0;
	out.glyphs.clear();
	out.width_px = 0.0f;
	if (!font || u16_len <= 0) return;
	UBiDiDirection dir = (UBiDiDirection)dir0;
	if (_cache) _cache->min_subpixel = _st.min_subpixel;
	hb_font_set_scale(font, fontsize, fontsize);

	auto buf = _buf;
	hb_buffer_reset(buf);

	hb_buffer_set_direction(buf, dir == UBIDI_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_add_utf16(buf, (uint16_t*)_utf16.data() + u16_start, u16_len, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_feature_t features[] = { {HB_TAG('k','e','r','n'), 0, 0, ~0u} };
	hb_shape(font, buf, features, 1);

	unsigned int count = 0;
	hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &count);
	hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

	out.glyphs.reserve(count);
	const float scale = 1.0f;
	float x = 0.0f;
	for (unsigned int i = 0; i < count; ++i) {
		vg_glyph_info_t g;
		g.glyph_id = info[i].codepoint;
		g.x_offset = (float)pos[i].x_offset * scale;
		g.y_offset = (float)-pos[i].y_offset * scale;
		g.x_advance = (float)pos[i].x_advance * scale;
		g.y_advance = (float)pos[i].y_advance * scale;
		// 注：hb_font_get_glyph_advance_for_direction 的结果从未被使用，省去每字形一次 HB 调用
		g.cache_entry = _cache ? _cache->get_cache_lookup_glyph(font, info[i].codepoint, fontsize) : nullptr;
		out.glyphs.push_back(g);
		x += g.x_advance;
	}
	out.width_px = x;
}

void vg_text_run_cx::populate_draw_list(text_draw_list& list, float origin_x, float origin_y, uint32_t color, render_mode mode)
{
	float pen_x = origin_x;
	float pen_y = origin_y;
	list.fontsize = _fontsize;
	for (auto& seg : _shaped) {
		for (const auto& g : seg.glyphs) {
			if (!g.cache_entry || !g.glyph_id) { pen_x += g.x_advance; continue; }

			auto* e = g.cache_entry;
			float x = pen_x + g.x_offset;
			float y = pen_y + g.y_offset;
			if (mode == VECTOR_ONLY || !e->atlas_img) {
				if (e->path_data) {
					list.push_vector(e, x, y, color);
				}
			}
			else if (e->atlas_img) {
				float w = (float)e->uv_rect.z;
				float h = (float)e->uv_rect.w;
				float atlas_w = (float)e->atlas_img->width;
				float atlas_h = (float)e->atlas_img->height;
				glm::vec4 uv(
					(float)e->uv_rect.x / atlas_w,
					(float)e->uv_rect.y / atlas_h,
					(float)(e->uv_rect.x + e->uv_rect.z) / atlas_w,
					(float)(e->uv_rect.y + e->uv_rect.w) / atlas_h
				);
				list.push_raster(e, x, y, w, h, uv, color);
			}
			pen_x += g.x_advance;
		}
		if (seg.new_line)
		{
			pen_y += _st.lineheight; pen_x = origin_x;
		}
	}
	list.extents = _extents;
}

vg_text_run_cx* new_text_run(vg_text_run_cx* ptr, text_st_t* p, text_style_t* ts, text_box_rt* box)
{
	if (!p || !p->text || !ts || !ts->family) return 0;

	int fontsize = ts->fontsize > 0 ? (int)ts->fontsize : 1;
	// ── 1. shape ──
	vg_text_run_cx* run = ptr ? ptr : new vg_text_run_cx();
	if (!run)
		return 0;
	run->clear();
	run->_st = *ts;
	if (box)
		run->_box = *box;
	run->_tt = *p;
	run->set_font_families(ts->family, fontsize);
	int lh = run->_st.lineheight;
	if (!lh)
	{
		hb_font_extents_t extents;
		for (int i = 0; i < ts->family->count; i++) {
			auto font = ts->family->familys[i]->font;
			hb_font_set_scale(font, fontsize, fontsize);
			hb_font_get_extents_for_direction(font, HB_DIRECTION_LTR, &extents);
			lh = std::max(lh, extents.ascender - extents.descender + extents.line_gap);
			break;
		}
		run->_st.lineheight = lh;
	}
	run->set_text(p->text, p->text_len);
	run->shape();  // 内部按 fallback 切 run，lookup 缓存

	return run;
}
void vg_text_run_cx::visit_ink(float origin_x, float origin_y, vg_text_ink_fn emit, void* user) const
{
	float x = origin_x, y = origin_y;
	hb_font_t* font = nullptr;
	constexpr float unit = 1.0f / 64.0f;
	for (const auto& seg : _shaped) {
		for (const auto& glyph : seg.glyphs) {
			if (glyph.glyph_id && glyph.cache_entry && glyph.cache_entry->font) {
				auto* next = glyph.cache_entry->font;
				if (font != next) {
					if (font) hb_font_set_scale(font, _fontsize, _fontsize);
					font = next;
					hb_font_set_scale(font, _fontsize * 64, _fontsize * 64);
				}
				hb_glyph_extents_t e{};
				if (hb_font_get_glyph_extents(font, glyph.glyph_id, &e) && e.width && e.height) {
					const float w = e.width * unit, h = -e.height * unit;
					emit(user, x + glyph.x_offset + e.x_bearing * unit + std::min(0.0f, w),
						y + glyph.y_offset - e.y_bearing * unit + std::min(0.0f, h), std::abs(w), std::abs(h));
				}
			}
			x += glyph.x_advance;
		}
		if (seg.new_line) { x = origin_x; y += _st.lineheight; }
	}
	if (font) hb_font_set_scale(font, _fontsize, _fontsize);
}

void vg_visit_text_ink(const text_st_t* text, const text_style_t* style,
	float baseline_x, float baseline_y, vg_text_ink_fn emit, void* user)
{
	if (!text || !text->text || !style || !style->family || style->family->count <= 0 ||
		!style->family->familys || !style->family->familys[0] || !style->family->familys[0]->font ||
		!emit || !std::isfinite(style->fontsize) || style->fontsize > 4096 ||
		!std::isfinite(baseline_x) || !std::isfinite(baseline_y)) return;
	text_st_t input = *text;
	text_style_t st = *style;
	vg_text_run_cx run;
	if (new_text_run(&run, &input, &st, nullptr)) run.visit_ink(baseline_x, baseline_y, emit, user);
}

// Composite premultiplied RGBA over premultiplied RGBA, with integer rounding.
static void rgba8_over(uint8_t* d, unsigned r, unsigned g, unsigned b, unsigned a)
{
    unsigned inv = 255 - a;
    d[0] = (uint8_t)std::min(255u, r + (d[0] * inv + 127) / 255);
    d[1] = (uint8_t)std::min(255u, g + (d[1] * inv + 127) / 255);
    d[2] = (uint8_t)std::min(255u, b + (d[2] * inv + 127) / 255);
    d[3] = (uint8_t)std::min(255u, a + (d[3] * inv + 127) / 255);
}

struct vg_text_rgba_layout {
    font_cache_cx* cache = nullptr;
    uint64_t generation = 0;
    text_draw_list draw{};
    std::unordered_map<const glyph_atlas_entry*, uint32_t> vector_ids;
    ~vg_text_rgba_layout() { free_font_cache(cache); }
};

static font_cache_cx* rgba8_text_cache(const text_st_t* text, const text_style_t* style,
    const text_box_rt* box)
{
    if (!text || !text->text || !style ||
        !std::isfinite(style->fontsize) || style->fontsize > 4096 ||
        !std::isfinite(style->lineheight) || style->lineheight < 0 ||
        (double)style->lineheight > INT32_MAX ||
        !std::isfinite(text->pos.x) || !std::isfinite(text->pos.y) ||
        (box && (!std::isfinite(box->text_align.x) || !std::isfinite(box->text_align.y))))
        return nullptr;
    return vg_measure_cache(style->family);
}

struct rgba8_clip {
    int64_t left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const { return left >= right || top >= bottom; }
};

static bool rgba8_destination(uint8_t* pixels, int width, int height, size_t stride,
    const ivec4* clip, rgba8_clip& bounds)
{
    if (!pixels || width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / 4 ||
        stride < (size_t)width * 4 ||
        (size_t)(height - 1) > (SIZE_MAX - (size_t)width * 4) / stride)
        return false;
    bounds = {0, 0, width, height};
    if (clip) {
        bounds.left = std::max<int64_t>(0, clip->x);
        bounds.top = std::max<int64_t>(0, clip->y);
        bounds.right = std::min<int64_t>(width, (int64_t)clip->x + std::max(0, clip->z));
        bounds.bottom = std::min<int64_t>(height, (int64_t)clip->y + std::max(0, clip->w));
    }
    return true;
}

static bool prepare_rgba8_layout(vg_text_rgba_layout& layout, font_cache_cx* cache,
    const text_st_t& text, const text_style_t& style, const text_box_rt* box)
{
    // LCD atlas data is unsuitable for a transparent RGBA destination.
    text_style_t st = style;
    st.min_subpixel = 0;
    text_st_t input = text;
    text_box_rt box_copy = box ? *box : text_box_rt{};
    vg_text_run_cx run;
    if (!new_text_run(&run, &input, &st, box ? &box_copy : nullptr)) return false;
    const auto& ex = run.extents();
    const auto& bx = run._box;
    const float bw = bx.rc.z > 0 ? (float)bx.rc.z : ex.width;
    const float bh = bx.rc.w > 0 ? (float)bx.rc.w : ex.height;
    const double ox = (double)text.pos.x + (bw - ex.width) * bx.text_align.x;
    const double oy = (double)text.pos.y + (bh - ex.height) * bx.text_align.y;
    // add_text truncates the aligned origin to int before adding baseline.
    if (!std::isfinite(ox) || !std::isfinite(oy) ||
        ox < INT32_MIN || ox > INT32_MAX || oy < INT32_MIN || oy > INT32_MAX)
        return false;
    run.populate_draw_list(layout.draw, (float)(int)ox,
        (float)((double)(int)oy + bx.baseline), 0, vg_text_run_cx::RASTER_FIRST);
    for (const auto& cmd : layout.draw.cmds)
        if (cmd.type == glyph_draw_cmd::VECTOR) layout.vector_ids.try_emplace(cmd.entry, 0);
    if (!layout.vector_ids.empty()) {
        // Vector entries do not carry glyph IDs. Resolve only the entries used
        // by this layout once, not on every replay.
        for (const auto& item : cache->glyph_cache) {
            const auto it = layout.vector_ids.find(&item.second);
            if (it == layout.vector_ids.end()) continue;
            font_cache_cx::glyph_key key; key.v = item.first;
            if (key.s.k.y == 0) it->second = key.s.glyph_index;
        }
        for (const auto& item : layout.vector_ids)
            if (item.second == 0) return false;
    }
    ++cache->references;
    layout.cache = cache;
    layout.generation = cache->glyph_generation;
    return true;
}

static bool blend_rgba8_layout(const vg_text_rgba_layout& layout, int tx, int ty,
    uint32_t tint, uint8_t* pixels, size_t stride, const rgba8_clip& bounds)
{
    const auto [left, top, right, bottom] = bounds;
    try {
        for (const auto& cmd : layout.draw.cmds) {
            const auto* e = cmd.entry;
            const uint8_t* src = nullptr;
            int sw = 0, sh = 0, pitch = 0, sx = 0, sy = 0;
            int bearing_x = 0, bearing_y = 0;
            bool color = e->has_color, bgra = true, a8 = false;
            glyph_raster image(nullptr, {});
            if (cmd.type == glyph_draw_cmd::RASTER) {
                const auto* atlas = (const ovg_image_data*)e->atlas_img;
                if (!atlas || !atlas->data) return false;
                src = (const uint8_t*)atlas->data;
                pitch = atlas->stride; sx = e->uv_rect.x; sy = e->uv_rect.y;
                sw = e->uv_rect.z; sh = e->uv_rect.w;
                bearing_x = e->offset.x; bearing_y = e->offset.y;
                bgra = atlas->format == 1;
            } else {
                const auto found = layout.vector_ids.find(e);
                if (found == layout.vector_ids.end()) return false;
                const uint32_t gid = found->second;
                auto* fs = (FontStyle*)hb_font_get_user_data(e->font, &fontStyleKey);
                if (!gid || !fs) return false;
                glm::ivec4 rc = {};
                image = build_glyph_image_hb(&fs->font, gid, cmd.fontsize, &rc, {1, 1});
                if (!image) return false;
                hb_raster_extents_t ext = {};
                hb_raster_image_get_extents(image.get(), &ext);
                auto fmt = hb_raster_image_get_format(image.get());
                if (fmt != HB_RASTER_FORMAT_A8 && fmt != HB_RASTER_FORMAT_BGRA32) return false;
                src = (const uint8_t*)hb_raster_image_get_buffer(image.get());
                pitch = ext.stride; sw = std::min((int)ext.width, rc.z);
                sh = std::min((int)ext.height, rc.w);
                bearing_x = rc.x; bearing_y = rc.y;
                a8 = fmt == HB_RASTER_FORMAT_A8; color = !a8;
            }
            if (!src || pitch <= 0 || sw < 0 || sh < 0) return false;
            double dx = std::floor((double)cmd.pos.x + bearing_x + 0.5) + tx;
            double dy = std::floor((double)cmd.pos.y - bearing_y + 0.5) + ty;
            if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
            if (dx >= right || dy >= bottom || dx + sw <= left || dy + sh <= top) continue;
            int64_t x0 = (int64_t)dx, y0 = (int64_t)dy;
            for (int64_t y = std::max(top, y0); y < std::min(bottom, y0 + sh); ++y) {
                const uint8_t* row = src + (size_t)(sy + y - y0) * pitch;
                uint8_t* dst = pixels + (size_t)y * stride;
                for (int64_t x = std::max(left, x0); x < std::min(right, x0 + sw); ++x) {
                    const uint8_t* p = row + (size_t)(sx + x - x0) * (a8 ? 1 : 4);
                    unsigned r, g, b, a;
                    if (color) {
                        a = p[3]; r = p[bgra ? 2 : 0]; g = p[1]; b = p[bgra ? 0 : 2];
                    } else {
                        unsigned coverage = a8 ? p[0] : p[3];
                        a = (coverage * ((tint >> 24) & 255) + 127) / 255;
                        r = ((tint & 255) * a + 127) / 255;
                        g = (((tint >> 8) & 255) * a + 127) / 255;
                        b = (((tint >> 16) & 255) * a + 127) / 255;
                    }
                    rgba8_over(dst + (size_t)x * 4, r, g, b, a);
                }
            }
        }
        return true;
    } catch (const std::bad_alloc&) { return false; }
}

bool vg_render_text_rgba8(const text_st_t* text, const text_style_t* style,
    const text_box_rt* box, uint8_t* pixels, int width, int height,
    size_t stride, const ivec4* clip)
{
    auto* cache = rgba8_text_cache(text, style, box);
    rgba8_clip bounds;
    if (!cache || !rgba8_destination(pixels, width, height, stride, clip, bounds)) return false;
    if (bounds.empty()) return true;
    try {
        vg_text_rgba_layout layout;
        return prepare_rgba8_layout(layout, cache, *text, *style, box) &&
            blend_rgba8_layout(layout, 0, 0, style->color, pixels, stride, bounds);
    } catch (const std::bad_alloc&) { return false; }
}

vg_text_rgba_layout* vg_create_text_rgba_layout(const text_st_t* text,
    const text_style_t* style, const text_box_rt* box)
{
    auto* cache = rgba8_text_cache(text, style, box);
    if (!cache) return nullptr;
    try {
        auto layout = std::make_unique<vg_text_rgba_layout>();
        if (!prepare_rgba8_layout(*layout, cache, *text, *style, box)) return nullptr;
        return layout.release();
    } catch (const std::bad_alloc&) { return nullptr; }
}

void vg_free_text_rgba_layout(vg_text_rgba_layout* layout)
{
    delete layout;
}

bool vg_render_text_layout_rgba8(const vg_text_rgba_layout* layout,
    int dx, int dy, uint32_t color, uint8_t* pixels, int width, int height,
    size_t stride, const ivec4* clip)
{
    rgba8_clip bounds;
    if (!layout || layout->generation != layout->cache->glyph_generation ||
        !rgba8_destination(pixels, width, height, stride, clip, bounds)) return false;
    return bounds.empty() || blend_rgba8_layout(*layout, dx, dy, color, pixels, stride, bounds);
}

void free_text_run(vg_text_run_cx* ptr)
{
	if (ptr)
	{
		delete ptr;
	}
}

#if 1

text_run_dst_cx::text_run_dst_cx()
{
	_buf = hb_buffer_create();
}

text_run_dst_cx::~text_run_dst_cx()
{
	if (_buf) {
		hb_buffer_destroy(_buf);
		_buf = nullptr;
	}
	auto icu = get_icu(U_ICU_VERSION_MAJOR_NUM);
	if (icu)
	{
		if (_bidi) {
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
		}
		if (_line_brk)
		{
			icu->_ubrk_close(_line_brk);
			_line_brk = nullptr;
		}
	}
}
void text_run_dst_cx::set_layout_mode(const text_style_t* ts, const text_box_rt* box, bool enable_bidi, uint8_t para_dir, const char* locale)
{
	if (ts)_st = *ts;
	if (box)_box = *box;
	_layout.enable_bidi = enable_bidi;
	if (locale && *locale)
	{
		_layout._locale = locale;
	}
	else {
		_layout._locale.clear();
	}
	_layout.para_dir = para_dir;

	if (_st.family && _st.family->count > 0 && _st.family->familys[0]) {
		auto font = _st.family->familys[0]->font;
		if (font) {
			_cache = (font_cache_cx*)hb_font_get_user_data(font, &g_font_cache_key);
		}
		else {
			_cache = nullptr;
		}
	}
}
void text_run_dst_cx::text_shape_segment(int u16_start, int u16_len, int  dir0, hb_font_t* font, int fontsize, shaped_segment_t& out)
{
	out.dir = dir0;
	out.glyphs.clear();
	out.width_px = 0.0f;
	if (!font || u16_len <= 0) return;
	UBiDiDirection dir = (UBiDiDirection)dir0;
	if (_cache) _cache->min_subpixel = _st.min_subpixel;
	hb_font_set_scale(font, fontsize, fontsize);

	auto buf = _buf;
	hb_buffer_reset(buf);

	hb_buffer_set_direction(buf, dir == UBIDI_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_add_utf16(buf, (uint16_t*)_utf16.data() + u16_start, u16_len, 0, -1);
	hb_buffer_guess_segment_properties(buf);
	hb_feature_t features[] = { {HB_TAG('k','e','r','n'), 0, 0, ~0u} };
	hb_shape(font, buf, features, 1);

	unsigned int count = 0;
	hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &count);
	hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, nullptr);

	out.glyphs.reserve(count);
	const float scale = 1.0f;
	float x = 0.0f;
	for (unsigned int i = 0; i < count; ++i) {
		vg_glyph_info_t g;
		g.glyph_id = info[i].codepoint;
		g.x_offset = (float)pos[i].x_offset * scale;
		g.y_offset = (float)-pos[i].y_offset * scale;
		g.x_advance = (float)pos[i].x_advance * scale;
		g.y_advance = (float)pos[i].y_advance * scale;
		// 注：hb_font_get_glyph_advance_for_direction 的结果从未被使用，省去每字形一次 HB 调用
		g.cache_entry = _cache ? _cache->get_cache_lookup_glyph(font, info[i].codepoint, fontsize) : nullptr;
		out.glyphs.push_back(g);
		x += g.x_advance;
	}
	out.width_px = x;
}

void text_run_dst_cx::text_shape(text_st_t* pt)
{
	_utf16.clear();
	if (!pt->text || pt->text_len == 0) return;
	if (pt->text_len == (size_t)-1) pt->text_len = strlen((const char*)pt->text);
	vg_utf8_to_utf16(pt->text, pt->text_len, &_utf16);
	auto ts = &_st;
	if (!ts->family || ts->family->count < 1 || _utf16.empty()) return;

	uint16_t* p = (uint16_t*)_utf16.data();
	uint16_t* end = p + _utf16.size();
	size_t len = _utf16.size();
	auto icu = get_icu(U_ICU_VERSION_MAJOR_NUM);
	UErrorCode err = U_ZERO_ERROR;
	visual_runs.clear();
	std::vector<int> visualMap;
	if (_layout.enable_bidi)
	{
		UErrorCode st = {};
		if (!icu)return;
		if (!_bidi)
		{
			_bidi = icu->_ubidi_open();
		}
		icu->_ubidi_setPara(_bidi, (UChar*)p, len, _layout.para_dir ? UBIDI_DEFAULT_RTL : UBIDI_DEFAULT_LTR, NULL, &st);
		if (U_FAILURE(st)) {
			fprintf(stderr, "ubidi_setPara failed: %s\n", u_errorName(st));
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
			return;
		}
		int32_t run_count = icu->_ubidi_countRuns(_bidi, &st);
		if (U_FAILURE(st)) {
			icu->_ubidi_close(_bidi);
			_bidi = nullptr;
			return;
		}
		visual_runs.reserve(len);
		for (int32_t r = 0; r < run_count; r++) {
			int32_t run_start, run_len;
			UBiDiDirection run_dir = icu->_ubidi_getVisualRun(_bidi, r, &run_start, &run_len);
			visual_runs.push_back(glm::uvec3(run_start, run_len, run_dir));
		}
		int length = icu->_ubidi_getLength(_bidi);
		visualMap.resize(length);
		icu->_ubidi_getVisualMap(_bidi, visualMap.data(), &err);
	}
	else {
		visual_runs.push_back(glm::uvec3(0, len, (UBiDiDirection)_layout.para_dir));
	}

	std::vector<int> breaks;
	if (_layout.max_width > 0)
	{
		if (_line_brk && current_word_wrap != _box.word_wrap) {
			icu->_ubrk_close(_line_brk); _line_brk = nullptr;
		}
		if (!_line_brk) {
			current_word_wrap = _box.word_wrap;
			_line_brk = icu->_ubrk_open(
				(UBreakIteratorType)_box.word_wrap,
				_layout._locale.size() ? _layout._locale.c_str() : nullptr,
				(UChar*)p, len, &err);
		}
		else {
			// 复用：换文本
			err = U_ZERO_ERROR;
			icu->_ubrk_setText(_line_brk, (UChar*)p, len, &err);
		}

		if (U_FAILURE(err)) {
			//fprintf(stderr, "ubrk_open/setText failed: %s\n", u_errorName(err));
			if (_line_brk) { icu->_ubrk_close(_line_brk); _line_brk = nullptr; }
		}
		else {
			// 收集断点（UTF-16 索引）
			breaks.push_back({});
			int32_t pos = icu->_ubrk_first(_line_brk);
			while (pos != UBRK_DONE) {
				if (pos > 0 && pos < (int32_t)len)
				{
					auto rs = icu->_ubrk_getRuleStatus(_line_brk);
					bool br = rs >= UBRK_LINE_HARD;
					breaks.push_back(pos);
				}
				pos = icu->_ubrk_next(_line_brk);
			}
			breaks.push_back((int32_t)len);
		}
	}

	std::vector<glm::uvec3> segments;
	if (breaks.empty()) {
		// 无断行：直接用 visual runs
		segments = visual_runs;
	}
	else {
		// 有断行：每个 visual run 按断点切
		for (const auto& run : visual_runs) {
			int32_t run_start = (int32_t)run.x;
			int32_t run_end = run_start + (int32_t)run.y;
			UBiDiDirection dir = (UBiDiDirection)run.z;
			// 找和这个 run 重叠的断点区间
			for (size_t k = 0; k < breaks.size() - 1; ++k) {
				auto b0 = breaks[k];
				auto b1 = breaks[k + 1];
				int32_t seg_start = std::max(b0, run_start);
				int32_t seg_end = std::min(b1, run_end);
				if (seg_start < seg_end) {
					segments.push_back(glm::uvec3(
						(uint32_t)seg_start,
						(uint32_t)(seg_end - seg_start),
						(uint32_t)dir));
				}
			}
		}
	}

	struct subseg_t {
		int32_t        start;
		int32_t        len;
		UBiDiDirection dir;
		hb_font_t* font;
		int            fontsize;
	};
	std::vector<subseg_t> subsegs;
	uint16_t* base = (uint16_t*)_utf16.data();

	_indexs.clear();
	_indexs.reserve(512);
	int cidx = 0;
	for (const auto& seg : segments) {
		uint16_t* p = base + seg.x;
		uint16_t* end = p + seg.y;

		while (p < end) {
			const uint16_t* sub_start = p;

			// 解码第一个 codepoint → 决定字体
			uint16_t* look = p;
			uint32_t cp = utf16_next(look, end);
			const font_family_t* ff = resolve_family(ts->family, cp);
			if (!ff) ff = ts->family->familys[0];
			hb_font_t* font = ff->font;
			int scale = ts->fontsize > 0 ? ts->fontsize : ff->upem;

			// 找同字体连续区间
			uint16_t* sub_end = look;
			while (sub_end < end) {
				uint16_t* nlook = sub_end;
				uint32_t ncp = utf16_next(nlook, end);
				const font_family_t* nff = resolve_family(ts->family, ncp);
				if (!nff) nff = ts->family->familys[0];
				if (nff->font != font) break;
				sub_end = nlook;
			}

			subsegs.push_back({ (int32_t)(sub_start - base), (int32_t)(sub_end - sub_start), (UBiDiDirection)seg.z, font, scale });

			_indexs.push_back({ cidx++ ,seg.z });
			p = sub_end;
		}
	}
	reverse1(_indexs);
	// ═══════════════════════════════════════════════════════════
	//  Step 5: Pass 1 — shape 每个 subsegment
	// ═══════════════════════════════════════════════════════════
	_shaped.clear();
	_shaped.reserve(subsegs.size());
	_glyphs.clear();
	std::vector<int> vmsize;
	int tdir = 0;
	vmsize.push_back(0);
	for (const auto& it : _indexs)
	{
		auto& ss = subsegs[it.x];
		shaped_segment_t out;
		text_shape_segment(ss.start, ss.len, ss.dir, ss.font, ss.fontsize, out);
		if (tdir == out.dir) {
			vmsize.back() += out.width_px;
		}
		else {
			vmsize.push_back(out.width_px);
			tdir = out.dir;
		}
		char16_t* ch = (char16_t*)base + ss.start;
		auto yb = find_br(ch, ss.len);
		out.new_line = yb;
		for (const auto& g : out.glyphs) {
			_glyphs.push_back(g);
		}
		_shaped.push_back(std::move(out));
	}


}
#endif // 1

