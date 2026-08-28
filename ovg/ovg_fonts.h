#pragma once
/*
字体处理管理


2026/8/19 创建

*/
#include <set>
#ifdef __cplusplus 
extern "C" {
#endif
	typedef struct hb_raster_image_t hb_raster_image_t;
	typedef struct hb_glyph_position_t hb_glyph_position_t;

#ifdef __cplusplus 
}
#endif
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

class image_cache_cx
{
public:
	std::vector<stb_packer*> _packer;
	std::vector<ovg_image_s*> _data;
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
	ovg_image_s* push_cache_size(const glm::ivec2& ss, glm::ivec2* pos, int linegap = 0);
	// 复制像素到装箱，从pos返回坐标
	ovg_image_s* push_cache_bitmap(hb_raster_image_t* img, glm::ivec2* pos, int linegap = 0);
	// 清空所有缓存
	void clear();
private:
	stb_packer* get_last_packer(bool isnew);

};

class font_cache_cx
{
public:
	std::map<std::string, std::vector<FontStyle*>> _familys;
	// 自定义加载的字体
	std::map<std::string, std::vector<FontStyle*>> _familys_name;
	std::vector<FontStyle*> _emojis;
	std::vector<FontStyle*> _temp;
	FcConfig* cfg = 0;
	usp_ac_cx* ac = 0;
public:
	font_cache_cx();
	~font_cache_cx();
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

	//void set_font_size(int size);
	//void show_text(const char* utf8);
	//void text_path(const char* utf8);
	//void text_extents(const char* utf8, vg_text_extents_t* extents);
	//void font_extents(vg_font_extents_t* extents);
	//vgText text_run_create(const char* text);
	//vgText text_run_create_with_length(const char* text, uint32_t length);
	//void text_run_destroy(vgText textRun);
	//void show_text_run(vgText textRun);
	//void text_run_get_extents(vgText textRun, vg_text_extents_t* extents);
	//uint32_t text_run_get_glyph_count(vgText textRun);
	//void text_run_get_glyph_position(vgText textRun, uint32_t index, hb_glyph_info_t* pGlyphInfo);
private:

	size_t mk_font(std::map<std::string, std::vector<FontStyle*>>* p, const char* family, const char* style, int weight, int slant);
};
// 渲染普通文本
void render_text(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_ctx_cb* ovg, rvg_t* ovg_ctx, const glm::uvec3& color);

void* build_glyph_image_hb(vg_font* hp, uint32_t gid, int font_size, glm::ivec4* ot);


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
