#pragma once
/*
字体处理管理


2026/8/19 创建

*/
#ifdef __cplusplus 
#include <set>
#include <map>
#include <unordered_map>
extern "C" {
#endif
	typedef struct hb_raster_image_t hb_raster_image_t;
	typedef struct hb_glyph_position_t hb_glyph_position_t;
	typedef struct hb_buffer_t hb_buffer_t;
	typedef struct hb_draw_funcs_t hb_draw_funcs_t;

	typedef struct _FcConfig    FcConfig;

#ifdef __cplusplus 
}
#endif
enum class SubpixelLayout {
	NONE = 0,   // 未知 / 非标准 → 强制灰阶 AA
	RGB,         // 水平 R-G-B（默认，最常见）
	BGR,         // 水平 B-G-R（常见于部分笔记本/外接屏）
	VRGB,        // 垂直 R-G-B
	VBGR,        // 垂直 B-G-R
};
#define MINSUBPIXEL 36
struct vg_font_extents_t {
	float ascent;
	float descent;
	float height;
	float max_x_advance;
	float max_y_advance;
};
struct vg_text_extents_t {
	float x_bearing;
	float y_bearing;
	float width;
	float height;
	float x_advance;
	float y_advance;
};
struct vg_glyph_info_t {
	int32_t x_advance;
	int32_t y_advance;
	int32_t x_offset;
	int32_t y_offset;
	/* private */
	uint32_t codepoint; // should be named glyphIndex, but for harfbuzz compatibility...
};
struct vg_font;
struct FontStyle;
struct vg_text_run_t {
	vg_text_extents_t extents;
	const char* text;
	unsigned int glyph_count;
	hb_buffer_t* hbBuf;
	vg_glyph_info_t* glyphs;
	vg_font* font;
};
class text_run_cx;
typedef class text_run_cx* vgText;
class usp_ac_cx;

// 纹理图像打包器接口
class packer_base
{
public:
	int width = 0, height = 0;
public:
	packer_base();
	virtual ~packer_base();
	virtual void init_target(int width, int height, int heuristic);
	virtual void clear();
	virtual size_t push_rect(glm::ivec4* rc, int n, size_t stride);
	virtual bool push_rect(const glm::ivec2& rc, glm::ivec2* pos);
};
// 创建空装箱对象
packer_base* new_packer(int width, int height);
void free_packer(packer_base* p);

class stb_packer;
#ifndef OVG_H
struct ovg_image_data :public vg_image_t {
	int format;			// 0 rgba, 1 bgra
	int stride;			// 像素宽度
	uint32_t* data;
	bool multiply;		// 是否预乘  
};
#endif
class image_cache_cx
{
public:
	std::vector<stb_packer*> _packer;
	std::vector<ovg_image_data*> _data;
	int width = 1024;					// 纹理宽高
	int height = 1024;
public:
	image_cache_cx();
	~image_cache_cx();
	// 重置大小，会清空原有内容
	void resize(int w, int h);
	// 装箱矩形并填充颜色
	glm::ivec2 fill_color(int w, int h, uint32_t color);
	// 装箱一个矩形，返回坐标/图像
	ovg_image_data* push_cache_size(const glm::ivec2& ss, glm::ivec2* pos, int linegap = 0);
	// 复制像素到装箱，从pos返回坐标
	ovg_image_data* push_cache_bitmap(hb_raster_image_t* img, glm::ivec2* pos, int linegap = 0);
	// 清空所有缓存
	void clear();
private:
	stb_packer* get_last_packer(bool isnew);

};
struct glyph_atlas_entry {
	enum { RASTER, VECTOR } type = RASTER;
	int advance;			// 水平步进
	vg_image_t* atlas_img;	// 指向字体 atlas 纹理
	glm::ivec4 uv_rect;		// (x, y, w, h) 在 atlas 中的像素区域
	glm::ivec2 offset;		// 字形偏移（bearing）x_bearing/y_bearing
	float* path_data;		// 路径数据，不同字号共用一份
	size_t path_size;
};

class font_cache_cx
{
public:
	struct user_data_key_t {
		/*< private >*/
		char unused;
	};
	union glyph_key {
		struct {
			glm::u16vec2 idsize;
			uint32_t glyph_index;
		}s;
		uint64_t v;
	};
	union path_key {
		struct {
			uint32_t id;
			uint32_t glyph_index;
		}s;
		uint64_t v;
	};
	std::map<std::string, std::vector<FontStyle*>> _familys;
	// 自定义加载的字体
	std::map<std::string, std::vector<FontStyle*>> _familys_name;
	std::vector<FontStyle*> _emojis;
	std::vector<FontStyle*> _temp;
	// key=[uint16字体id，uint16字号，uint32字形id]
	std::unordered_map<uint64_t, glyph_atlas_entry> glyph_cache;
	// key=[uint32字体id，uint32字形id]
	std::unordered_map<uint64_t, glyph_atlas_entry> path_cache;
	// 位图缓存
	image_cache_cx image_cache;
	std::vector<float> tem_vec;
	hb_draw_funcs_t* funcs = 0;
	FcConfig* cfg = 0;
	// 内存分配器
	vg_alloc_cx* ac = 0;
	uint32_t next_font_id = 1;
	int max_raster_size = 256;
	int references = 1;
public:
	font_cache_cx();
	~font_cache_cx();
	void set_alloc_ptr(vg_alloc_cx* p);
	void get_sys_family();
	void clear_sys();
	void clear_load();
	vg_font* get_font(const char* family, const char* style, int weight, int slant);

	void select_font_face(const char* family, const char* style, int weight, int slant);
	// name自定义名称，可空
	bool load_font_from_path(const char* path, const char* name);
	bool add_font_dir(const char* dir);
	bool load_font_from_memory(unsigned char* fontBuffer, long fontBufferByteSize, const char* name);
	const char* weight_to_string(int w);
	const char* slant_to_string(int s);
	glyph_atlas_entry* get_cache_lookup_glyph(hb_font_t* font, uint32_t glyph_id, int fontsize);
private:

	size_t mk_font(std::map<std::string, std::vector<FontStyle*>>* p, const char* family, const char* style, int weight, int slant);
};
// 渲染普通文本
void render_text(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_ctx_cb* ovg, rvg_t* ovg_ctx, const glm::uvec3& color);
 


vgText text_run_new(const font_familys_t* familys, int font_size, const char* text);
vgText text_run_new_with_length(const font_familys_t* familys, int font_size, const char* text, uint32_t length);
void text_run_destroy(vgText textRun);
// 设置字体，字号，0则不改
void text_run_set_font(vgText textRun, const font_familys_t* familys, int font_size);
// 重新设置文本
void show_text_set(vgText textRun, const char* text, uint32_t length);
void show_text_run(vgText textRun);
void show_text_run_path(vgText textRun);
void text_run_get_extents(vgText textRun, vg_text_extents_t* extents);
uint32_t text_run_get_glyph_count(vgText textRun);
void text_run_get_glyph_position(vgText textRun, uint32_t index, vg_glyph_info_t* pGlyphInfo);
