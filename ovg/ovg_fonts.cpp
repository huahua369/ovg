

#include <fontconfig/fontconfig.h> 
#include <map>
#include <string>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <vector>
#include <harfbuzz/hb.h> 
#include <harfbuzz/hb-raster.h> 

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
#if 0
#define FONT_PAGE_SIZE          1024
#define FONT_CACHE_INIT_LAYERS  1
#define FONT_FILE_NAME_MAX_SIZE 1024
#define FONT_NAME_MAX_SIZE      128


struct _char_ref {
	glm::vec4    bounds;
	glm::i16vec2 bmpDiff;
	uint8_t pageIdx;
	glm::vec2 advance;
};

struct _tex_ref_t {
	uint8_t pageIdx;
	int     penX;
	int     penY;
	int     height;
};

struct _vg_font_t {
	hb_face_t* hb_face;
	uint32_t           charSize;
	float              scale;
	int                ascent;
	int                descent;
	int                lineGap;

	hb_font_t* hb_font;

	_char_ref** charLookup;

	_tex_ref_t curLine;
};

/* Font identification structure */
struct _vg_font_identity_t {
	char** names; /* Resolved Input names to this font by fontConfig or custom name set by @ref vg_load_from_path*/
	uint32_t namesCount;        /* Count of resolved names by fontConfig */
	unsigned char* fontBuffer;  /* stb_truetype in memory buffer */
	long           fontBufSize; /* */
	char* fontFile;    /* Font file full path*/

	uint32_t      sizeCount; /* available font size loaded */
	_vg_font_t* sizes;     /* loaded font size array */
};

// Font cache global structure, entry point for all font related operations.
struct _font_cache_t {
	FcConfig* config; /* Font config, used to find font files by font names*/


	int      stagingX; /* x pen in host buffer */
	uint8_t* hostBuff; /* host memory where bitmaps are first loaded */

	void* cmd;          /* vulkan command buffer for font textures upload */

	void* texture;      /* 2d array texture used by contexts to draw characteres */
	uint32_t        texFormat;    /* Format of the fonts texture array */
	uint8_t         texPixelSize; /* Size in byte of a single pixel in a font texture */
	uint8_t texLength;   /* layer count of 2d array texture, starts with FONT_CACHE_INIT_LAYERS count and increased when
							needed */
	int* pensY;       /* array of current y pen positions for each texture in cache 2d array */

	_vg_font_identity_t* fonts;      /* Loaded fonts structure array */
	int32_t                fontsCount; /* Loaded fonts array count*/
};

struct vg_font_extents_t {
	float ascent;        /*!< the distance that the font extends above the baseline. */
	float descent;       /*!< the distance that the font extends below the baseline.*/
	float height;        /*!< the recommended vertical distance between baselines. */
	float max_x_advance; /*!< the maximum distance in the X direction that the origin is advanced for any glyph in the
							font.*/
	float max_y_advance; /*!< the maximum distance in the Y direction that the origin is advanced for any glyph in the
							font. This will be zero for normal fonts used for horizontal writing.*/
};
struct vg_text_extents_t {
	float x_bearing; /*!< the horizontal distance from the origin to the leftmost part of the glyphs as drawn. Positive
						if the glyphs lie entirely to the right of the origin. */
	float y_bearing; /*!< the vertical distance from the origin to the topmost part of the glyphs as drawn. Positive
						only if the glyphs lie completely below the origin; will usually be negative.*/
	float width;     /*!< width of the glyphs as drawn*/
	float height;    /*!< height of the glyphs as drawn*/
	float x_advance; /*!< distance to advance in the X direction after drawing these glyphs*/
	float y_advance; /*!< distance to advance in the Y direction after drawing these glyphs. Will typically be zero
						except for vertical text layout as found in East-Asian languages.*/
};
typedef struct _glyph_info_t {
	int32_t x_advance;
	int32_t y_advance;
	int32_t x_offset;
	int32_t y_offset;
	/* private */
	uint32_t codepoint; // should be named glyphIndex, but for harfbuzz compatibility...
} vg_glyph_info_t;
typedef void* vgDevice;
// Precompute everything necessary to measure and draw one line of text, usefull to draw the same text multiple times.
struct vg_text_run_t {
	_vg_font_identity_t* fontId;      /* vkvg font structure pointer */
	_vg_font_t* font;        /* vkvg font structure pointer */
	vgDevice             dev;         /* vkvg device associated with this text run */
	vg_text_extents_t    extents;     /* store computed text extends */
	const char* text;        /* utf8 char array of text*/
	unsigned int           glyph_count; /* Total glyph count */

	hb_buffer_t* hbBuf;  /* HarfBuzz buffer of text */
	hb_glyph_position_t* glyphs; /* HarfBuzz computed glyph positions array */

};
typedef vg_font_context* vgContext;
struct vg_font_context {
	int status;
	uint32_t      references; // reference count

	vgDevice  dev;

	long selectedCharSize; /* Font size*/
	char selectedFontName[FONT_NAME_MAX_SIZE];
	//_vkvg_font_t		  selectedFont;		//hold current face and size before cache addition
	_vg_font_identity_t* currentFont;     // font pointing to cached fonts identity
	_vg_font_t* currentFontSize; // font structure by size ready for lookup
	//vg_direction_t       textDirection;
	_font_cache_t* fontCache;
};
typedef struct vg_text_run_t* vgText;

// Create font cache.
void _fonts_cache_create(vgContext ctx);
// Release all ressources of font cache.
void                   _font_cache_destroy(vgContext ctx);
_vg_font_identity_t* _font_cache_add_font_identity(vgContext ctx, const char* fontFile, const char* name);
bool                   _font_cache_load_font_file_in_memory(_vg_font_identity_t* fontId);
// Draw text
void _font_cache_show_text(vgContext ctx, const char* text);
// Get text dimmensions
void _font_cache_text_extents(vgContext ctx, const char* text, int length, vg_text_extents_t* extents);
// Get font global dimmensions
void _font_cache_font_extents(vgContext ctx, vg_font_extents_t* extents);
// Create text object that could be drawn multiple times minimizing harfbuzz and compute processing.
void _font_cache_create_text_run(vgContext ctx, const char* text, int length, vgText textRun);
// Release ressources held by a text run.
void _font_cache_destroy_text_run(vgText textRun);
// Draw text run
void _font_cache_show_text_run(vgContext ctx, vgText tr);
// update context font cache descriptor set
void _font_cache_update_context_descset(vgContext ctx);



#if 1 

static int defaultFontCharSize = 12 << 6;

void _fonts_cache_create(vgContext ctx) {
	_font_cache_t* cache = (_font_cache_t*)calloc(1, sizeof(_font_cache_t));

#ifdef VG_USE_FONTCONFIG
	cache->config = FcInitLoadConfigAndFonts();
	if (!cache->config) {
		assert(cache->config);
	}
#endif

	cache->texFormat = 0;
	cache->texPixelSize = 1;

	cache->texLength = FONT_CACHE_INIT_LAYERS;
	cache->texture = 0;
	cache->hostBuff = 0;
	cache->pensY = 0;// (int*)calloc(cache->texLength, sizeof(int));

	ctx->fontCache = cache;
}
//increase layer count of 2d texture array used as font cache.
void _increase_font_tex_array(vgContext ctx) {

	_font_cache_t* cache = ctx->fontCache;

}
// flush font stagging buffer to cache texture array
// Trigger stagging buffer to be uploaded in font cache. Groupping upload improve performances.
void _flush_chars_to_tex(vgContext dev, _vg_font_t* f) {

	_font_cache_t* cache = dev->fontCache;
	if (cache->stagingX == 0) // no char in stagging buff to flush
		return;

	f->curLine.penX += cache->stagingX;
	cache->stagingX = 0;
	memset(cache->hostBuff, 0, (uint64_t)FONT_PAGE_SIZE * FONT_PAGE_SIZE * cache->texPixelSize);
}
/// Start a new line in font cache, increase texture layer count if needed.
void _init_next_line_in_tex_cache(vgContext dev, _vg_font_t* f) {
	_font_cache_t* cache = dev->fontCache;
	int            i;
	for (i = 0; i < cache->texLength; ++i) {
		if (cache->pensY[i] + f->curLine.height >= FONT_PAGE_SIZE)
			continue;
		f->curLine.pageIdx = (unsigned char)i;
		f->curLine.penX = 0;
		f->curLine.penY = cache->pensY[i];
		cache->pensY[i] += f->curLine.height;
		return;
	}
	_flush_chars_to_tex(dev, f);
	_increase_font_tex_array(dev);
	_init_next_line_in_tex_cache(dev, f);
}
void _font_cache_destroy(vgContext dev) {
	_font_cache_t* cache = (_font_cache_t*)dev->fontCache;

	free(cache->hostBuff);

	for (int i = 0; i < cache->fontsCount; ++i) {
		_vg_font_identity_t* f = &cache->fonts[i];
		for (uint32_t j = 0; j < f->sizeCount; j++) {
			_vg_font_t* s = &f->sizes[j];

			//for (int g = 0; g < f->stbInfo.numGlyphs; ++g) {
			//	if (s->charLookup[g] != NULL)
			//		free(s->charLookup[g]);
			//}


			hb_font_destroy(s->hb_font);


			free(s->charLookup);
		}
		free(f->sizes);
		free(f->fontFile);
		for (uint32_t j = 0; j < f->namesCount; j++)
			free(f->names[j]);
		if (f->namesCount > 0)
			free(f->names);
		free(f->fontBuffer);
	}

	free(cache->fonts);
	free(cache->pensY);

	//vkh_buffer_reset(&cache->buff);
	//vkh_image_destroy(cache->texture);

#ifdef VG_USE_FONTCONFIG
	FcConfigDestroy(cache->config);
	FcFini();
#endif


	free(dev->fontCache);
}

void _font_cache_update_context_descset(vgContext ctx) {
	//if (ctx->fontCacheImg)
	//	vkh_image_destroy(ctx->fontCacheImg);


	//ctx->fontCacheImg = ctx->dev->fontCache->texture;
	//vkh_image_reference(ctx->fontCacheImg);

	//_update_descriptor_set(ctx, ctx->fontCacheImg, ctx->dsFont);

}
// create a new char entry and put glyph in stagging buffer, ready for upload.
_char_ref* _prepare_char(vgContext dev, vgText tr, uint32_t gindex) {
	_vg_font_t* f = tr->font;
	_char_ref* cr = (_char_ref*)malloc(sizeof(_char_ref));
#if 0
	stbtt_fontinfo* pStbInfo = &tr->fontId->stbInfo;
	int             c_x1, c_y1, c_x2, c_y2;
	stbtt_GetGlyphBitmapBox(pStbInfo, gindex, f->scale, f->scale, &c_x1, &c_y1, &c_x2, &c_y2);
	uint32_t bmpByteWidth = c_x2 - c_x1;
	uint32_t bmpPixelWidth = bmpByteWidth;
	uint32_t bmpRows = c_y2 - c_y1;

	uint8_t* data = dev->fontCache->hostBuff;

	if (dev->fontCache->stagingX + f->curLine.penX + bmpPixelWidth > FONT_PAGE_SIZE) {
		_flush_chars_to_tex(dev, f);
		_init_next_line_in_tex_cache(dev, f);
	}

	int        penX = dev->fontCache->stagingX;


	int      advance;
	int      lsb;
	stbtt_GetGlyphHMetrics(pStbInfo, gindex, &advance, &lsb);
	stbtt_MakeGlyphBitmap(pStbInfo, data + penX, bmpPixelWidth, bmpRows, FONT_PAGE_SIZE, f->scale, f->scale, gindex);
	cr->bmpDiff.x = (int16_t)c_x1;
	cr->bmpDiff.y = (int16_t)-c_y1;
	cr->advance = (glm::vec2){ (uint32_t)roundf(f->scale * advance) << 6, 0 };


	glm::vec4 uvBounds = { {(float)(penX + f->curLine.penX) / (float)FONT_PAGE_SIZE},
					 {(float)f->curLine.penY / (float)FONT_PAGE_SIZE},
					 {(float)bmpPixelWidth},
					 {(float)bmpRows} };
	cr->bounds = uvBounds;
	cr->pageIdx = f->curLine.pageIdx;

	f->charLookup[gindex] = cr;
	dev->fontCache->stagingX += bmpPixelWidth;
#endif
	return cr;
}
void _font_add_name(_vg_font_identity_t* font, const char* name) {
	if (++font->namesCount == 1)
		font->names = (char**)malloc(sizeof(char*));
	else
		font->names = (char**)realloc(font->names, font->namesCount * sizeof(char*));
	font->names[font->namesCount - 1] = (char*)calloc(strlen(name) + 1, sizeof(char));
	strcpy(font->names[font->namesCount - 1], name);
}
bool _font_cache_load_font_file_in_memory(_vg_font_identity_t* fontId) {
	FILE* fontFile = fopen(fontId->fontFile, "rb");
	if (!fontFile)
		return false;
	fseek(fontFile, 0, SEEK_END);
	fontId->fontBufSize = ftell(fontFile); /* how long is the file ? */
	fseek(fontFile, 0, SEEK_SET);          /* reset */
	fontId->fontBuffer = (unsigned char*)malloc(fontId->fontBufSize);
	fread(fontId->fontBuffer, fontId->fontBufSize, 1, fontFile);
	fclose(fontFile);
	return true;
}
_vg_font_identity_t* _font_cache_add_font_identity(vgContext ctx, const char* fontFilePath, const char* name) {
	_font_cache_t* cache = (_font_cache_t*)ctx->dev->fontCache;
	if (++cache->fontsCount == 1)
		cache->fonts = (_vg_font_identity_t*)malloc(cache->fontsCount * sizeof(_vg_font_identity_t));
	else
		cache->fonts =
		(_vg_font_identity_t*)realloc(cache->fonts, cache->fontsCount * sizeof(_vg_font_identity_t));
	_vg_font_identity_t nf = { 0 };

	if (fontFilePath) {
		int fflength = strlen(fontFilePath) + 1;
		nf.fontFile = (char*)malloc(fflength * sizeof(char));
		strcpy(nf.fontFile, fontFilePath);
	}

	_font_add_name(&nf, name);

	cache->fonts[cache->fontsCount - 1] = nf;
	return &cache->fonts[cache->fontsCount - 1];
}
// select current font for context
_vg_font_t* _find_or_create_font_size(vgContext ctx) {
	_vg_font_identity_t* font = ctx->currentFont;

	for (uint32_t i = 0; i < font->sizeCount; ++i) {
		if (font->sizes[i].charSize == ctx->selectedCharSize)
			return &font->sizes[i];
	}
	// if not found, create a new font size structure
	if (++font->sizeCount == 1)
		font->sizes = (_vg_font_t*)malloc(sizeof(_vg_font_t));
	else
		font->sizes = (_vg_font_t*)realloc(font->sizes, font->sizeCount * sizeof(_vg_font_t));
	_vg_font_t newSize = { .charSize = ctx->selectedCharSize };

	vgDevice dev = ctx->dev;
	//int result = stbtt_InitFont(&font->stbInfo, font->fontBuffer, stbtt_GetFontOffsetForIndex(font->fontBuffer, 0));
	//assert(result && "stbtt_initFont failed");
	//if (!result) {
	//	ctx->status = -2;
	//	return NULL;
	//}
	//stbtt_GetFontVMetrics(&font->stbInfo, &font->ascent, &font->descent, &font->lineGap);
	//newSize.charLookup = (_char_ref**)calloc(font->stbInfo.numGlyphs, sizeof(_char_ref*));
	//// newSize.scale		= stbtt_ScaleForPixelHeight(&font->stbInfo, newSize.charSize);
	//newSize.scale = stbtt_ScaleForMappingEmToPixels(&font->stbInfo, newSize.charSize);
	//newSize.curLine.height = roundf(newSize.scale * (font->ascent - font->descent + font->lineGap));
	//newSize.ascent = roundf(newSize.scale * font->ascent);
	//newSize.descent = roundf(newSize.scale * font->descent);
	//newSize.lineGap = roundf(newSize.scale * font->lineGap);

	newSize.hb_font = hb_font_create(newSize.hb_face);


	_init_next_line_in_tex_cache(dev, &newSize);

	font->sizes[font->sizeCount - 1] = newSize;
	return &font->sizes[font->sizeCount - 1];
}

// try find font already resolved with fontconfig by font name
bool _tryFindFontByName(vgContext ctx, _vg_font_identity_t** font) {
	_font_cache_t* cache = ctx->dev->fontCache;
	for (int i = 0; i < cache->fontsCount; ++i) {
		for (uint32_t j = 0; j < cache->fonts[i].namesCount; j++) {
			if (strcmp(cache->fonts[i].names[j], ctx->selectedFontName) == 0) {
				*font = &cache->fonts[i];
				return true;
			}
		}
	}
	return false;
}

#ifdef VG_USE_FONTCONFIG
bool _tryResolveFontNameWithFontConfig(vgContext ctx, _vg_font_identity_t** resolvedFont) {
	_font_cache_t* cache = (_font_cache_t*)ctx->dev->fontCache;
	char* fontFile = NULL;

	FcPattern* pat = FcNameParse((const FcChar8*)ctx->selectedFontName);
	FcConfigSubstitute(cache->config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult   result;
	FcPattern* font = FcFontMatch(cache->config, pat, &result);
	if (font)
		FcPatternGetString(font, FC_FILE, 0, (FcChar8**)&fontFile);
	*resolvedFont = NULL;
	if (fontFile) {
		// try find font in cache by path
		for (int i = 0; i < cache->fontsCount; ++i) {
			if (cache->fonts[i].fontFile && strcmp(cache->fonts[i].fontFile, fontFile) == 0) {
				_font_add_name(&cache->fonts[i], ctx->selectedFontName);
				*resolvedFont = &cache->fonts[i];
				break;
			}
		}
		if (!*resolvedFont) {
			// if not found, create a new vg_font
			_vg_font_identity_t* fid = _font_cache_add_font_identity(ctx, fontFile, ctx->selectedFontName);
			_font_cache_load_font_file_in_memory(fid);
			*resolvedFont = &cache->fonts[cache->fontsCount - 1];
		}
	}

	FcPatternDestroy(pat);
	FcPatternDestroy(font);

	return (fontFile != NULL);
}
#endif

// try to find corresponding font in cache (defined by context selectedFont) and create a new font entry if not found.
void _update_current_font(vgContext ctx) {
	if (ctx->currentFont == NULL) {

		if (ctx->selectedFontName[0] == 0)
			_select_font_face(ctx, "sans");

		if (!_tryFindFontByName(ctx, &ctx->currentFont)) {
			_tryResolveFontNameWithFontConfig(ctx, &ctx->currentFont);
		}

		ctx->currentFontSize = _find_or_create_font_size(ctx);

	}
}

// Get harfBuzz buffer for provided text.
hb_buffer_t* _get_hb_buffer(_vg_font_t* font, const char* text, int length) {
	hb_buffer_t* buf = hb_buffer_create();

	hb_script_t         script = HB_SCRIPT_LATIN;
	hb_unicode_funcs_t* ucfunc = hb_unicode_funcs_get_default();
	wchar_t             firstChar = 0;
	if (mbstowcs(&firstChar, text, 1))
		script = hb_unicode_script(ucfunc, firstChar);

	hb_direction_t dir = hb_script_get_horizontal_direction(script);
	hb_buffer_set_direction(buf, dir);
	hb_buffer_set_script(buf, script);
	// hb_buffer_set_language	(buf, hb_language_from_string (lng, (int)strlen(lng)));
	hb_buffer_add_utf8(buf, text, length, 0, length);

	hb_shape(font->hb_font, buf, NULL, 0);

	return buf;
}

// retrieve global font extends of context's current font as defined by FreeType
void _font_cache_font_extents(vgContext ctx, vg_font_extents_t* extents) {
	_update_current_font(ctx);

	if (ctx->status)
		return;

	// TODO: ensure correct metrics are returned (scalled/unscalled, etc..)
	_vg_font_t* font = ctx->currentFontSize;

	extents->ascent = roundf(font->scale * ctx->currentFont->ascent);
	extents->descent = -roundf(font->scale * ctx->currentFont->descent);
	extents->height =
		roundf(font->scale * (ctx->currentFont->ascent - ctx->currentFont->descent + ctx->currentFont->lineGap));
	extents->max_x_advance = 0; // TODO
	extents->max_y_advance = 0;

}
// compute text extends for provided string.
void _font_cache_text_extents(vgContext ctx, const char* text, int length, vg_text_extents_t* extents) {
	if (text == NULL) {
		memset(extents, 0, sizeof(vg_text_extents_t));
		return;
	}

	vg_text_run_t tr = { 0 };
	_font_cache_create_text_run(ctx, text, length, &tr);

	if (ctx->status)
		return;

	*extents = tr.extents;

	_font_cache_destroy_text_run(&tr);
}
// text is expected as utf8 encoded
// if length is < 0, text must be null terminated, else it contains glyph count
void _font_cache_create_text_run(vgContext ctx, const char* text, int length, vgText textRun) {

	_update_current_font(ctx);

	if (ctx->status)
		return;

	textRun->fontId = ctx->currentFont;
	textRun->font = ctx->currentFontSize;
	textRun->dev = ctx->dev;


	textRun->hbBuf = _get_hb_buffer(ctx->currentFontSize, text, length);
	textRun->glyphs = hb_buffer_get_glyph_positions(textRun->hbBuf, &textRun->glyph_count);



	unsigned int string_width_in_pixels = 0;
	for (uint32_t i = 0; i < textRun->glyph_count; ++i)
		string_width_in_pixels += textRun->glyphs[i].x_advance >> 6;

	textRun->extents.height = textRun->font->ascent - textRun->font->descent + textRun->font->lineGap;

	textRun->extents.x_advance = (float)string_width_in_pixels;
	if (textRun->glyph_count > 0) {
		textRun->extents.y_advance = (float)(textRun->glyphs[textRun->glyph_count - 1].y_advance >> 6);
		textRun->extents.x_bearing = -(float)(textRun->glyphs[0].x_offset >> 6);
		textRun->extents.y_bearing = -(float)(textRun->glyphs[0].y_offset >> 6);
	}

	textRun->extents.width = textRun->extents.x_advance;
}
void _font_cache_destroy_text_run(vgText textRun) {

	hb_buffer_destroy(textRun->hbBuf);

}
void font_add_tri_indices_for_rect(std::vector<uint32_t>& inds, uint32_t i) {
	inds.resize(inds.size() + 6);
	inds[0] = i;
	inds[1] = i + 2;
	inds[2] = i + 1;
	inds[3] = i + 1;
	inds[4] = i + 2;
	inds[5] = i + 3;
}
void _font_cache_show_text_run(vgContext ctx, vgText tr) {
	unsigned int glyph_count;
	hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(tr->hbBuf, &glyph_count);

	std::vector<uint32_t> inds;
	ovgVertex v = { }; v.color = ctx->curColor;
	glm::vec2   pen = { 0, 0 };
	std::vector<ovgVertex> verts;
	//if (!_current_path_is_empty(ctx))
	//	pen = _get_current_position(ctx);


	for (uint32_t i = 0; i < glyph_count; ++i) {
		_char_ref* cr = tr->font->charLookup[glyph_info[i].codepoint];

		if (cr == NULL)
			cr = _prepare_char(tr->dev, tr, glyph_info[i].codepoint);


		float uvWidth = cr->bounds.z / (float)FONT_PAGE_SIZE;
		float uvHeight = cr->bounds.w / (float)FONT_PAGE_SIZE;
		glm::vec2  p0 = { pen.x + cr->bmpDiff.x + (tr->glyphs[i].x_offset >> 6),
						  pen.y - cr->bmpDiff.y + (tr->glyphs[i].y_offset >> 6) };
		v.pos = p0;

		uint32_t firstIdx = (uint32_t)(ctx->vertCount - ctx->curVertOffset);

		v.uv.x = cr->bounds.x;
		v.uv.y = cr->bounds.y;
		//v.uv.z = cr->pageIdx;
		verts.push_back(v);

		v.pos.y += cr->bounds.w;
		v.uv.y += uvHeight;
		verts.push_back(v);

		v.pos.x += cr->bounds.z;
		v.pos.y = p0.y;
		v.uv.x += uvWidth;
		v.uv.y = cr->bounds.y;
		verts.push_back(v);

		v.pos.y += cr->bounds.w;
		v.uv.y += uvHeight;
		verts.push_back(v);


		font_add_tri_indices_for_rect(inds, firstIdx);

		pen.x += (tr->glyphs[i].x_advance >> 6);
		pen.y -= (tr->glyphs[i].y_advance >> 6);
	}

	// equivalent to a moveto
	//_finish_path(ctx);
	//_add_point(ctx, pen.x, pen.y);
	_flush_chars_to_tex(tr->dev, tr->font);
	if (ctx->fontCacheImg != ctx->dev->fontCache->texture) {
		//vg_flush(ctx);
		_font_cache_update_context_descset(ctx);
	}
}

void _font_cache_show_text(vgContext ctx, const char* text) {

	vg_text_run_t tr = { 0 };
	_font_cache_create_text_run(ctx, text, -1, &tr);

	if (ctx->status)
		return;

	_font_cache_show_text_run(ctx, &tr);

	_font_cache_destroy_text_run(&tr);

}


void _select_font_face(vgContext ctx, const char* name) {
	if (strcmp(ctx->selectedFontName, name) == 0)
		return;
	strcpy(ctx->selectedFontName, name);
	ctx->currentFont = NULL;
	ctx->currentFontSize = NULL;
}

#endif // 1


void vg_select_font_face(vgContext ctx, const char* name);
void vg_load_font_from_path(vgContext ctx, const char* path, const char* name);
void vg_load_font_from_memory(vgContext ctx, unsigned char* fontBuffer, long fontBufferByteSize, const char* name);
void vg_set_font_size(vgContext ctx, uint32_t size);
void vg_show_text(vgContext ctx, const char* utf8);
void vg_text_path(vgContext ctx, const char* utf8);
void vg_text_extents(vgContext ctx, const char* utf8, vg_text_extents_t* extents);
void vg_font_extents(vgContext ctx, vg_font_extents_t* extents);
vgText vg_text_run_create(vgContext ctx, const char* text);
vgText vg_text_run_create_with_length(vgContext ctx, const char* text, uint32_t length);
void vg_text_run_destroy(vgText textRun);
void vg_show_text_run(vgContext ctx, vgText textRun);
void vg_text_run_get_extents(vgText textRun, vg_text_extents_t* extents);
uint32_t vg_text_run_get_glyph_count(vgText textRun);
void vg_text_run_get_glyph_position(vgText textRun, uint32_t index, vg_glyph_info_t* pGlyphInfo);


void vg_select_font_face(vgContext ctx, const char* name) {
	if (!ctx)
		return;
	_select_font_face(ctx, name);
}
void vg_load_font_from_path(vgContext ctx, const char* path, const char* name) {
	if (!ctx)
		return;
	_vg_font_identity_t* fid = _font_cache_add_font_identity(ctx, path, name);
	if (!_font_cache_load_font_file_in_memory(fid)) {
		ctx->status = -1;// vg_STATUS_FILE_NOT_FOUND;
		return;
	}
	_select_font_face(ctx, name);
}
void vg_load_font_from_memory(vgContext ctx, unsigned char* fontBuffer, long fontBufferByteSize, const char* name) {
	if (!ctx)
		return;
	// RECORD(ctx, vg_CMD_SET_FONT_PATH, name);
	_vg_font_identity_t* fid = _font_cache_add_font_identity(ctx, NULL, name);
	fid->fontBuffer = fontBuffer;
	fid->fontBufSize = fontBufferByteSize;

	_select_font_face(ctx, name);
}
void vg_set_font_size(vgContext ctx, uint32_t size) {
	if (!ctx)
		return;
	long newSize = size;
	if (ctx->selectedCharSize == newSize)
		return;
	ctx->selectedCharSize = newSize;
	ctx->currentFont = NULL;
	ctx->currentFontSize = NULL;
}

void vg_show_text(vgContext ctx, const char* text) {
	if (!ctx)
		return;
	//_ensure_renderpass_is_started(ctx);
	_font_cache_show_text(ctx, text);
	//_flush_undrawn_vertices (ctx);
}

vgText vg_text_run_create(vgContext ctx, const char* text) {
	if (!ctx)
		return NULL;
	vgText tr = (vg_text_run_t*)calloc(1, sizeof(vg_text_run_t));
	_font_cache_create_text_run(ctx, text, -1, tr);
	return tr;
}
vgText vg_text_run_create_with_length(vgContext ctx, const char* text, uint32_t length) {
	if (!ctx)
		return NULL;
	vgText tr = (vg_text_run_t*)calloc(1, sizeof(vg_text_run_t));
	_font_cache_create_text_run(ctx, text, length, tr);
	return tr;
}
uint32_t vg_text_run_get_glyph_count(vgText textRun) { return textRun->glyph_count; }
void     vg_text_run_get_glyph_position(vgText textRun, uint32_t index, vg_glyph_info_t* pGlyphInfo) {
	if (index >= textRun->glyph_count) {
		*pGlyphInfo = {};
		return;
	}
	memcpy(pGlyphInfo, &textRun->glyphs[index], sizeof(vg_glyph_info_t));

}
void vg_text_run_destroy(vgText textRun) {
	_font_cache_destroy_text_run(textRun);
	free(textRun);
}
void vg_show_text_run(vgContext ctx, vgText textRun) {
	if (!ctx)
		return;
	_font_cache_show_text_run(ctx, textRun);
}
void vg_text_run_get_extents(vgText textRun, vg_text_extents_t* extents) { *extents = textRun->extents; }

void vg_text_extents(vgContext ctx, const char* text, vg_text_extents_t* extents) {
	if (!ctx)
		return;
	_font_cache_text_extents(ctx, text, -1, extents);
}
void vg_font_extents(vgContext ctx, vg_font_extents_t* extents) {
	if (!ctx)
		return;
	_font_cache_font_extents(ctx, extents);
}


#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ICU */
#include <unicode/ubrk.h>
#include <unicode/ubidi.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>

/* FreeType */
#include <ft2build.h>
#include FT_FREETYPE_H

/* HarfBuzz */
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h>

/* ─────────────────────────────────────────────
 * 工具函数：UTF-8 → UTF-16（ICU 用）
 * ───────────────────────────────────────────── */
static int utf8_to_utf16(const char* utf8, UChar* out16, int cap16, UErrorCode* st)
{
	int32_t len = 0;
	u_strFromUTF8(out16, cap16, &len, utf8, -1, st);
	return len;
}

/* ─────────────────────────────────────────────
 * 工具函数：UTF-16 切片 → UTF-8（打印用）
 * ───────────────────────────────────────────── */
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

class font_mg
{
public:
	struct FontStyle {
		std::string family, fullname, namecn;
		std::string style;
		std::string file;
		hb_font_t* font;
		int weight;
		int slant;
		int index;
	};
	std::map<std::string, std::vector<FontStyle>> _familys;
public:
	font_mg();
	~font_mg();
	void get_family_to_styles();

private:

};

font_mg::font_mg()
{}

font_mg::~font_mg()
{}

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
void font_mg::get_family_to_styles()
{
	std::map<std::string, std::vector<FontStyle>> result;

	FcConfig* cfg = FcInitLoadConfigAndFonts();
	FcPattern* pat = FcPatternCreate();
	FcObjectSet* os = FcObjectSetBuild(
		FC_FAMILY, FC_STYLE, FC_WEIGHT, FC_SLANT, FC_FILE, FC_INDEX, NULL);
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
		auto fullname = get_pat_str(p, FC_FULLNAME, 0);
		//auto font = load_font((char*)file, index);
		//if (font)
		result[(char*)family].push_back({ (char*)family,fullname,"",(char*)style,  file,font,weight, slant, index });
	}
	FcFontSetDestroy(fs);
	FcObjectSetDestroy(os);
	FcPatternDestroy(pat);

	FcConfigDestroy(cfg);
	_familys.swap(result);
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
int testfont()
{
	/* 原始 UTF-8 文本：中英阿 + 数字混排 */
	const char* text = "Hello 世界 مرحبا 123";

	printf("Input: %s\n\n", text);
	font_mg fmg;
	fmg.get_family_to_styles();
	UErrorCode st = U_ZERO_ERROR;

	/* ── ICU：UTF-8 → UTF-16 ── */
	UChar u16[256];
	int32_t len16 = utf8_to_utf16(text, u16, 256, &st);
	if (U_FAILURE(st)) {
		fprintf(stderr, "UTF-8 → UTF-16 failed: %s\n", u_errorName(st));
		return 1;
	}

	/* ── ICU：断行 ── */
	UBreakIterator* bi = ubrk_open(UBRK_LINE, "en", u16, len16, &st);
	if (U_FAILURE(st)) {
		fprintf(stderr, "ubrk_open failed: %s\n", u_errorName(st));
		return 1;
	}


	hb_font_t* hb_font = load_font("Noto Sans", NULL);
	if (!hb_font) {
		/* 备选：用系统默认 sans */
		hb_font = load_font("Sans", NULL);
	}
	if (!hb_font) {
		fprintf(stderr, "无法加载任何字体\n");
		return 1;
	}

	/* ── 逐行：断行 → Bidi → 整形 ── */
	int32_t start = ubrk_first(bi);
	int32_t end = ubrk_next(bi);
	int line_no = 0;

	while (end != UBRK_DONE) {
		int32_t line_len = end - start;
		const UChar* line_text = u16 + start;

		/* 打印当前行（逻辑序） */
		char line_utf8[256];
		utf16_slice_to_utf8(u16, start, line_len, line_utf8, 256);
		printf("Line %d (logical): \"%s\"\n", line_no, line_utf8);

		/* 检测段落方向（看首个强方向字符） */
		UBiDiDirection dir = (UBiDiDirection)UBIDI_DEFAULT_LTR;
		for (int32_t i = 0; i < line_len; i++) {
			if (u_charDirection(line_text[i]) == U_RIGHT_TO_LEFT ||
				u_charDirection(line_text[i]) == U_ARABIC_NUMBER) {
				dir = UBIDI_RTL;
				break;
			}
		}

		/* Bidi + HarfBuzz */
		shape_line(hb_font, line_text, line_len, dir);

		line_no++;
		start = end;
		end = ubrk_next(bi);
	}

	/* ── 清理（顺序：hb → ICU） ── */
	hb_font_destroy(hb_font);
	ubrk_close(bi);

	printf("\nDone.\n");
	return 0;
}
