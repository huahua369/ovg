#pragma once
/*
字体处理管理


2026/8/19 创建

*/
#include <set>
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
struct vg_font;
struct FontStyle;
struct vg_text_run_t {
	vg_text_extents_t extents;
	const char* text;
	unsigned int glyph_count;
	hb_glyph_position_t* glyphs;
	vg_font* font;
};
typedef struct vg_text_run_t* vgText;

class usp_ac_cx;

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
	const char* weight_to_string(int w);
	const char* slant_to_string(int s);
private:

	size_t mk_font(std::map<std::string, std::vector<FontStyle*>>* p, const char* family, const char* style, int weight, int slant);
};
// 渲染普通文本
void render_text_shaped(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_ctx_cb* ovg, rvg_t* ovg_ctx, const glm::uvec3& color);
// colrv1文本
void render_text_print(const font_familys_t* ffs, const void* str8, size_t len, float x, float y, ovg_canvas_cb* ovg, ovg_ctx_cb* ctx, rvg_t* vg, const glm::uvec3& color);

void* build_glyph_image_hb(vg_font* hp, uint32_t gid, int font_size, glm::ivec4* ot);

