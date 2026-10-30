/*
矢量渲染

2026/10/10 修改椭圆/画圆/arc等函数实现
2026/9/9 文本渲染处理完善
2026/8/31 支持普通三角形渲染
2026/8/13 版本1.0
2026/8/8 创建文件

*/

#ifndef GLM_FORCE_XYZW_ONLY 
#define GLM_ENABLE_EXPERIMENTAL
//#define GLM_FORCE_ALIGNED
//#define GLM_FORCE_INTRINSICS
// 定义glm启用simd
//#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
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

#if (__has_include("ovg.h"))
#include "ovg.h"
#else
#include "../ovg.h"
#endif


#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stack>
#if (__has_include(<tesselator.h>))
#include <tesselator.h>
#define VG_FILL_NZ_GLUTESS2
#endif

#if (__has_include("ovg_fonts.h"))
#include "ovg_fonts.h"
#include <harfbuzz/hb.h> 
#include <fontconfig/fontconfig.h>
#endif

void init_ovg_cb(ovg_canvas_cb* cb);
void init_ovg_ctx_cb(ovg_ctx_cb* cb);

namespace {
	struct vg_alloc_header {
		void* base;
	};
} // namespace

vg_alloc_cx::vg_alloc_cx()
{}

vg_alloc_cx::~vg_alloc_cx()
{}

void* vg_alloc_cx::alloc(const size_t _Bytes, const size_t align)
{
	size_t bytes = _Bytes ? _Bytes : 1;
	size_t a = align ? align : alignof(std::max_align_t);
	if (a < alignof(void*))
		a = alignof(void*);
	if ((a & (a - 1)) != 0)
		a = alignof(std::max_align_t);
	if (bytes > (std::numeric_limits<size_t>::max)() - a - sizeof(vg_alloc_header))
		return nullptr;

	unsigned char* base = (unsigned char*)std::malloc(bytes + a - 1 + sizeof(vg_alloc_header));
	if (!base)
		return nullptr;

	std::uintptr_t p = (std::uintptr_t)(base + sizeof(vg_alloc_header));
	p = (p + a - 1) & ~(std::uintptr_t)(a - 1);
	((vg_alloc_header*)p)[-1].base = base;
	return (void*)p;
}

void vg_alloc_cx::dealloc(void* t, size_t n)
{
	(void)n;
	if (t)
		std::free(((vg_alloc_header*)t)[-1].base);
}

class usp_ac_cx :public vg_alloc_cx
{
public:
	usp_ac_cx() {}
	~usp_ac_cx() {}
public:
	void* allocate(const size_t _Bytes, const size_t align = 0) {
		return alloc(_Bytes, align > 0 ? align : _Align);
	}
	void* new_mem(size_t n)
	{
		n = std::max((size_t)1, n);
		auto p = alloc(n, _Align);
		if (p)
			memset(p, 0, n);
		return p;
	}
	void* new_mem0(size_t n)
	{
		n = std::max((size_t)1, n);
		return alloc(n, _Align);
	}
	template<class T>
	T* new_mem(size_t n)
	{
		n = std::max((size_t)1, n);
		auto p = (T*)alloc(sizeof(T) * n, alignof(T));
		if (p)
		{
			for (size_t i = 0; i < n; i++)
				p[i] = {};
		}
		return p;
	}
	template<class T >
	T* new_mem(size_t n, T*& p)
	{
		n = std::max((size_t)1, n);
		p = (T*)alloc(sizeof(T) * n, alignof(T));
		if (p)
		{
			for (size_t i = 0; i < n; i++)
				p[i] = {};
		}
		return p;
	}
	template<class T >
	T* new_mem_o(size_t n)
	{
		n = std::max((size_t)1, n);
		return (T*)alloc(sizeof(T) * n, alignof(T));
	}
	template<class T>
	T* new_mem(T*& p, size_t n)
	{
		return new_mem(n, p);
	}
	template<class T>
	void free_mem(T* t, size_t n)
	{
		if (t && n > 0)
			dealloc(t, sizeof(T) * n);
	}
	void free_mem0(void* t, size_t n)
	{
		if (t && n > 0)
			dealloc(t, n);
	}
	template<class T, class... Ts>
	T* new_obj(Ts &&... args)
	{
		auto p = (T*)alloc(sizeof(T), alignof(T));
		if (p)
			std::construct_at(p, std::forward<Ts>(args)...);
		return p;
	}
	template<class T>
	void free_obj(T* t)
	{
		if (t)
		{
			std::destroy_at(t);
			dealloc(t, sizeof(T));
		}
	}
};

vg_alloc_cx* vg_get_default_allocator()
{
	// Intentionally never destroyed: allocators may be used by static
	// containers during process shutdown.
	static vg_alloc_cx* alloc = new usp_ac_cx();
	return alloc;
}

namespace {
	usp_ac_cx* vg_default_ac()
	{
		return (usp_ac_cx*)vg_get_default_allocator();
	}
} // namespace

struct text_draw_list;

struct raster_batch {
	std::vector<float>    verts;
	std::vector<float>    uvs;
	std::vector<uint32_t> colors;
	std::vector<uint16_t> idx;

	// 只清长度，保留 capacity；下一帧继续复用同一批堆内存
	void reset() {
		verts.clear();
		uvs.clear();
		colors.clear();
		idx.clear();
	}
	// 按字形数量预留空间（reserve 不会缩小已有 capacity）
	void reserve_for(size_t glyph_count) {
		const size_t vtx = glyph_count * 4;
		if (verts.capacity() < vtx * 2) verts.reserve(vtx * 2);
		if (uvs.capacity() < vtx * 2) uvs.reserve(vtx * 2);
		if (colors.capacity() < vtx) colors.reserve(vtx);
		if (idx.capacity() < glyph_count * 6) idx.reserve(glyph_count * 6);
	}
};

// 文本位图字形合批缓存：
// 容器长期持有，跨帧只清 size，避免每次 add_text 重新分配 map 节点和顶点缓冲。
// 同一个 atlas 的字形仍然合并为一次 add_geometry。
class text_raster_cache
{
public:
	using batch_map = std::unordered_map<vg_image_t*, raster_batch>;

	void begin() {
		// 只清空上一轮用到的 batch，capacity 全部保留
		for (auto& kv : _batches)
			kv.second.reset();
		_last_img = nullptr;
		_last_batch = nullptr;
	}

	raster_batch& get(vg_image_t* img, size_t glyph_count) {
		// 字形通常来自同一个 atlas，用 last 命中避免每个字形都做一次哈希查找
		if (_last_img == img)
			return *_last_batch;

		auto it = _batches.find(img);
		if (it == _batches.end())
			it = _batches.emplace(img, raster_batch{}).first;

		auto& b = it->second;
		b.reserve_for(glyph_count);
		_last_img = img;
		_last_batch = &b;
		return b;
	}

	batch_map& batches() { return _batches; }
	const batch_map& batches() const { return _batches; }

private:
	batch_map _batches;
	vg_image_t* _last_img = nullptr;
	raster_batch* _last_batch = nullptr;
};

#ifdef OVG_FONT_H
// Shaped-run cache.  The text run previously lived directly in the callback
// object and was cleared/reshaped by every vg_add_text call, so static labels
// paid for ICU bidi/break iteration and HarfBuzz shaping on every frame.  Keep
// a bounded set of runs keyed by the inputs that affect shaping, and update
// only the per-draw fields (position, box, colours) on a hit.
class text_run_cache_cx
{
public:
	struct key_t
	{
		std::string text;
		const font_familys_t* family = nullptr;
		int fontsize = 16;
		int min_subpixel = 0;
		int word_wrap = 0;
		float lineheight = 0.0f;

		bool operator<(const key_t& o) const
		{
			if (family != o.family) return std::less<const font_familys_t*>()(family, o.family);
			if (fontsize != o.fontsize) return fontsize < o.fontsize;
			if (min_subpixel != o.min_subpixel) return min_subpixel < o.min_subpixel;
			if (word_wrap != o.word_wrap) return word_wrap < o.word_wrap;
			if (lineheight != o.lineheight) return lineheight < o.lineheight;
			return text < o.text;
		}
	};

	vg_text_run_cx* get(text_st_t* p, text_style_t* ts, text_box_rt* box)
	{
		if (!p || !p->text || !ts || !ts->family) return nullptr;
		const size_t len = p->text_len < 0 ? strlen(p->text) : (size_t)p->text_len;
		if (len == 0) return nullptr;

		// 复用查询键的字符串容量，静态文本命中缓存时不再产生堆分配
		key_t& key = _query;
		key.text.assign(p->text, len);
		key.family = ts->family;
		key.fontsize = ts->fontsize > 0 ? (int)ts->fontsize : 16;
		key.min_subpixel = ts->min_subpixel;
		key.word_wrap = box ? (int)box->word_wrap : 0;
		key.lineheight = ts->lineheight;

		auto it = _runs.find(key);
		if (it != _runs.end())
		{
			vg_text_run_cx* run = it->second.get();
			if (!run) return nullptr;
			// Preserve the line height computed by new_text_run when the caller
			// leaves it at zero; everything else is per-draw state.
			const int lineheight = run->_st.lineheight;
			run->_st = *ts;
			if (ts->lineheight == 0.0f)
				run->_st.lineheight = lineheight;
			run->_tt = *p;
			run->_box = box ? *box : text_box_rt{};
			return run;
		}

		if (_runs.size() >= max_runs)
			_runs.clear();

		std::unique_ptr<vg_text_run_cx> run(new vg_text_run_cx());
		if (!run) return nullptr;
		if (!new_text_run(run.get(), p, ts, box)) return nullptr;
		auto inserted = _runs.emplace(key, std::move(run));
		return inserted.first->second.get();
	}

private:
	static constexpr size_t max_runs = 512;
	std::map<key_t, std::unique_ptr<vg_text_run_cx>> _runs;
	key_t _query;	// 查询键缓存，复用 text 的 capacity
};
#endif

class ovg_canvas_cx :public ovg_canvas_cb
{
public:
#ifdef OVG_FONT_H
	text_run_cache_cx text_cache;

	text_raster_cache batches;
#endif
public:
	ovg_canvas_cx();
	~ovg_canvas_cx();

	void submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width);
	void submit_draw_list(rvg_t* rvg, const text_draw_list* list);
private:

};

ovg_canvas_cx::ovg_canvas_cx()
{
	auto a = new usp_ac_cx();
	ac = (mem_resource_t*)a;
	ac->ptr = this;
	init_ovg_cb(this);
}

ovg_canvas_cx::~ovg_canvas_cx()
{
	if (ac) {
		auto ac1 = (usp_ac_cx*)ac;
		if (ac1)
			delete ac1;
	}
	ac = 0;
}

ovg_canvas_cb* new_canvas_cb()
{
	auto p = new ovg_canvas_cx();
	return p;
}
void free_canvas_cb(ovg_canvas_cb* p) {
	if (p) {
		if (p->ac)delete p->ac;
		delete p;
	}
}


class ovg_ctx_cx :public ovg_ctx_cb
{
public:
#ifdef OVG_FONT_H
	text_run_cache_cx text_cache;
	text_raster_cache batches;
#endif
public:
	ovg_ctx_cx();
	~ovg_ctx_cx();

	void submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width);
	void submit_draw_list(rvg_t* rvg, const text_draw_list* list);
private:

};

template<typename T>
concept submit_fn = requires(T & obj) {
	{ obj.submit_vector_glyphs_stroked(static_cast<rvg_t*>(nullptr), static_cast<const text_draw_list*>(nullptr), static_cast<float>(0.0f)) } -> std::same_as<void>;
	{ obj.submit_draw_list(static_cast<rvg_t*>(nullptr), static_cast<const text_draw_list*>(nullptr)) } -> std::same_as<void>;
};

void submit_vector_glyphs_stroked(submit_fn auto& cb, rvg_t* rvg, const text_draw_list* list, float stroke_width);
void submit_draw_list(submit_fn auto& cb, rvg_t* rvg, const text_draw_list* list);
void submit_vector_glyphs_stroked(submit_fn auto& cb, rvg_t* rvg, const text_draw_list* list, float stroke_width) {
	cb.submit_vector_glyphs_stroked(rvg, list, stroke_width);
}
void submit_draw_list(submit_fn auto& cb, rvg_t* rvg, const text_draw_list* list) {
	cb.submit_draw_list(rvg, list);
}

void vg_add_text(ovg_canvas_cb* ocb, ovg_ctx_cb* ctx, rvg_t* rvg, text_st_t* p, text_style_t* ts, text_box_rt* box);

ovg_ctx_cx::ovg_ctx_cx()
{
	auto ac0 = new usp_ac_cx();
	ac = (mem_resource_t*)ac0;
	init_ovg_ctx_cb(this);
	ac0->ptr = this;
}

ovg_ctx_cx::~ovg_ctx_cx()
{
	auto ac1 = (usp_ac_cx*)ac;
	if (ac1)
		delete ac1;
	ac = 0;
}
ovg_ctx_cb* new_ctx_cb()
{
	auto p = new ovg_ctx_cx();
	//if (p)
	//{
	//	if (fctx)fctx->references++;
	//}
	return p;
}
void free_ctx_cb(ovg_ctx_cb* p) {
	auto p1 = (ovg_ctx_cx*)p;
	if (p) {
		delete p1;
	}
}
// vg
#if 1
struct ovg_path_t {
	usp_ac_cx* ac = 0;
	vg_vector<glm::vec2> points;	// 点数组
	vg_vector<uint32_t> pathes;	// 每段大小
	vg_vector<uint32_t> colors;	// 颜色数组，和pathes大小一样
	uint32_t color = 0xffffffff;		// 默认颜色
	uint32_t segmentPtr = 0;   // current segment count in current path having curves
	uint32_t subpathCount = 0; // store count of subpath, not straight forward to retrieve from segmented path array

	uint32_t  pathPtr = 0;		// 路径数组中的指针pointer in the path array  
	vg_state_save_t* t = 0;
	uint32_t curVertOffset = 0;
	bool     simpleConvex = false; // true if path is single rect or concave closed curve.
	bool enable_tess2 = false;	// 是否启用tess2实现完整矢量交叉计算

	explicit ovg_path_t(vg_alloc_cx* a)
		: ac((usp_ac_cx*)a), points(a), pathes(a), colors(a) {}
};

#define PATH_CLOSED_BIT 0x80000000 /* most significant bit of path elmts is closed/open path state */
#define PATH_HAS_CURVES_BIT 0x40000000  
/* 2rd most significant bit of path elmts is curved status
 * for main path, this indicate that curve datas are present.
 * For segments, this indicate that the segment is curved or not */
#define PATH_IS_CONVEX_BIT 0x20000000 /* simple rectangle or circle. */
#define PATH_ELT_MASK      0x1FFFFFFF /* Bit mask for fetching path element value */

#define ROUNDF(f, c)       (((float)((int)((f) * (c))) / (c)))
#define ROUND_DOWN(v, p)   (floorf(v * p) / p)
#define EQUF1(a, b)         (fabsf(a - (b)) <= FLT_EPSILON)
#define EQUF(a, b)         (fabsf(a - (b)) <= 1e-5)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_2_PI 0.63661977236758134308 // 2/pi
#endif
void _matrix_get_scale(const glm::mat3x2* matrix, float* sx, float* sy) {
	auto c0 = (*matrix)[0];
	auto c1 = (*matrix)[1];
	auto c2 = (*matrix)[2];
	*sx = sqrtf(c0.x * c0.x + c1.x * c1.x);   // sqrtf：避免每次 double 提升开销（曲线细分路径高频调用）
	/*if (matrix->xx < 0)
	 *sx = -*sx;*/
	*sy = sqrtf(c0.y * c0.y + c1.y * c1.y);
	/*if (matrix->yy < 0)
	 *sy = -*sy;*/
}

void o_remove_last_point(ovg_path_t* ctx) {
	ctx->points.pop_back();
	ctx->pathes[ctx->pathPtr]--;
	if (ctx->segmentPtr > 0) {                            // if path is segmented
		if (!ctx->pathes[ctx->pathPtr + ctx->segmentPtr]) // if current segment is empty
			ctx->segmentPtr--;
		ctx->pathes[ctx->pathPtr + ctx->segmentPtr]--;                          // decrement last segment point count
		if ((ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_ELT_MASK) == 0) // if no point left (was only one)
			ctx->pathes[ctx->pathPtr + ctx->segmentPtr] = 0;                    // reset current segment
		else if (ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_HAS_CURVES_BIT) // if segment is a curve
			ctx->segmentPtr++; // then segPtr has to be forwarded to new segment
	}
	if (ctx->pathes.size() < ctx->segmentPtr + ctx->pathPtr)
		ctx->pathes.resize(ctx->segmentPtr + ctx->pathPtr + 1);
}
// test equality of two single precision vectors
inline bool vec2_equ(const glm::vec2& a, const glm::vec2& b) {
	bool r = (EQUF(a.x, b.x) & EQUF(a.y, b.y));
	return r;
}
inline glm::vec2 vec2_line_norm(const glm::vec2& a, const glm::vec2& b) {
	glm::vec2  d = { b.x - a.x, b.y - a.y };
	float md = sqrtf(d.x * d.x + d.y * d.y);
	d.x /= md;
	d.y /= md;
	return d;
}
// compute sum of two single precision vectors
inline glm::vec2 vec2_add(const glm::vec2& a, const glm::vec2& b) { return glm::vec2{ a.x + b.x, a.y + b.y }; }
// compute subbstraction of two single precision vectors
inline glm::vec2 vec2_sub(const glm::vec2& a, const glm::vec2& b) { return glm::vec2{ a.x - b.x, a.y - b.y }; }
// multiply 2d vector by scalar
inline glm::vec2 vec2_mult_s(const glm::vec2& a, float m) { return glm::vec2{ a.x * m, a.y * m }; }
// devide 2d vector by scalar
inline glm::vec2 vec2_div_s(const glm::vec2& a, float m) { return glm::vec2{ a.x / m, a.y / m }; }
// normalize float vector
inline glm::vec2 vec2_norm(const glm::vec2& a) {
	float m = sqrtf(a.x * a.x + a.y * a.y);
	return glm::vec2{ a.x / m, a.y / m };
}
inline glm::vec2 vec2_perp(const glm::vec2& a) { return glm::vec2{ a.y, -a.x }; }

void matrix_transform_distance(const glm::mat3x2* matrix, float* dx, float* dy) {
	float new_x, new_y;
	auto m = *matrix;
	new_x = (m[0].x * *dx + m[1].x * *dy);
	new_y = (m[0].y * *dx + m[1].y * *dy);

	*dx = new_x;
	*dy = new_y;
}

void matrix_transform_point(const glm::mat3x2* matrix, float* x, float* y) {
	// 修复：原实现把输入值写回（transform 实际是 no-op），这里真正应用矩阵变换
	glm::mat3x2 m = *matrix;
	float nx = m[0].x * *x + m[1].x * *y + m[2].x;
	float ny = m[0].y * *x + m[1].y * *y + m[2].y;
	*x = nx; *y = ny;
}

inline float vec2_zcross(const glm::vec2& v1, const glm::vec2& v2) { return v1.x * v2.y - v1.y * v2.x; }

#ifndef VG_COL32_A_MASK
#define VG_COL32_A_MASK     0xFF000000
#endif // !VG_COL32_A_MASK
#ifndef FIXNORMAL2F_MAX_INVLEN2
void normalize2f_over_zero(float& VX, float& VY)
{
	float d2 = VX * VX + VY * VY;
	if (d2 > 0.0f) {
		float inv_len = 1.0f / sqrtf(d2);
		VX *= inv_len; VY *= inv_len;
	}
}
#define FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
void fixnormal2f(float& VX, float& VY)
{
	float d2 = VX * VX + VY * VY;
	if (d2 > 0.000001f) {
		float inv_len2 = 1.0f / d2;
		if (inv_len2 > FIXNORMAL2F_MAX_INVLEN2)
			inv_len2 = FIXNORMAL2F_MAX_INVLEN2;
		VX *= inv_len2; VY *= inv_len2;
	}
}
#endif
bool o_path_has_curves(uint32_t* pathes, uint32_t ptrPath) { return   pathes[ptrPath] & PATH_HAS_CURVES_BIT; }

void _ovg_path_extents(ovg_path_t* ctx, bool transformed, float* x1, float* y1, float* x2, float* y2) {
	uint32_t ptrPath = 0;
	uint32_t firstPtIdx = 0;

	float xMin = FLT_MAX, yMin = FLT_MAX;
	float xMax = FLT_MIN, yMax = FLT_MIN;

	while (ptrPath < ctx->pathPtr) {
		uint32_t pathPointCount = ctx->pathes[ptrPath] & PATH_ELT_MASK;

		for (uint32_t i = firstPtIdx; i < firstPtIdx + pathPointCount; i++) {
			glm::vec2 p = ctx->points[i];
			if (transformed)
				matrix_transform_point(&ctx->t->pushConsts.mat, &p.x, &p.y);
			if (p.x < xMin)
				xMin = p.x;
			if (p.x > xMax)
				xMax = p.x;
			if (p.y < yMin)
				yMin = p.y;
			if (p.y > yMax)
				yMax = p.y;
		}

		firstPtIdx += pathPointCount;
		if (o_path_has_curves(ctx->pathes.data(), ptrPath)) {
			// skip segments lengths used in stroke
			ptrPath++;
			uint32_t totPts = 0;
			while (totPts < pathPointCount)
				totPts += (ctx->pathes[ptrPath++] & PATH_ELT_MASK);
		}
		else
			ptrPath++;
	}
	*x1 = xMin;
	*x2 = xMax;
	*y1 = yMin;
	*y2 = yMax;
}
bool _current_path_is_empty(ovg_path_t* ctx) {
	return ctx && (ctx->pathes.empty() || ctx->pathes[ctx->pathPtr] == 0);
}
// this function expect that current point exists
glm::vec2 _get_current_position(ovg_path_t* ctx) {
	return ctx->points.empty() ? glm::vec2() : ctx->points.back();
}

glm::vec2 _get_current_point(ovg_path_t* ctx) {
	glm::vec2 cp = {};
	if (_current_path_is_empty(ctx)) {
	}
	else
	{
		cp = _get_current_position(ctx);
	}
	return cp;
}
#define VG_ARRAY_THRESHOLD 8
#define VG_PATHES_SIZE 16
bool _check_pathes_array(ovg_path_t* ctx) {
	if (ctx->pathes.size() - ctx->pathPtr - ctx->segmentPtr > VG_ARRAY_THRESHOLD)
		return false;
	ctx->pathes.resize(ctx->pathes.size() + VG_PATHES_SIZE);
	return false;
}
#if 1
void _set_curve_start(ovg_path_t* ctx) {
	_check_pathes_array(ctx);
	if (ctx->segmentPtr > 0) {
		// check if current segment has points (straight)
		if ((ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_ELT_MASK) > 0)
			ctx->segmentPtr++;
	}
	else {
		// not yet segmented path, first segment length is copied
		if (ctx->pathes[ctx->pathPtr] > 0) { // create first straight segment first
			ctx->pathes[ctx->pathPtr + 1] = ctx->pathes[ctx->pathPtr];
			ctx->segmentPtr = 2;
		}
		else
			ctx->segmentPtr = 1;
	}
	ctx->pathes[ctx->pathPtr + ctx->segmentPtr] = 0;
}
void _set_curve_end(ovg_path_t* ctx) {
	_check_pathes_array(ctx);
	ctx->pathes[ctx->pathPtr + ctx->segmentPtr] |= PATH_HAS_CURVES_BIT;
	ctx->segmentPtr++;
	ctx->pathes[ctx->pathPtr + ctx->segmentPtr] = 0;
}

void o_finish_path(ovg_path_t* ctx) {
	if (!ctx)return;
	do {
		if (ctx->pathes.empty())
			ctx->pathes.push_back(0);
		if (ctx->pathes[ctx->pathPtr] == 0) // empty
			break;
		if ((ctx->pathes[ctx->pathPtr] & PATH_ELT_MASK) < 2) {
			// only current pos is in path
			auto pointCount = ctx->points.size();
			pointCount -= ctx->pathes[ctx->pathPtr];
			ctx->points.resize(pointCount);
			ctx->pathes[ctx->pathPtr] = 0;
			ctx->segmentPtr = 0;
			break;
		}

		if (ctx->pathPtr == 0 && ctx->simpleConvex)
			ctx->pathes[0] |= PATH_IS_CONVEX_BIT;

		if (ctx->segmentPtr > 0) {
			ctx->pathes[ctx->pathPtr] |= PATH_HAS_CURVES_BIT;
			if ((ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_HAS_CURVES_BIT) == 0 &&
				(ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_ELT_MASK) > 0)
				ctx->segmentPtr++;
			ctx->pathPtr += ctx->segmentPtr;
		}
		else
			ctx->pathPtr++;

		if (_check_pathes_array(ctx))
			return;

		ctx->pathes[ctx->pathPtr] = 0;
		ctx->segmentPtr = 0;
		ctx->subpathCount++;
		ctx->simpleConvex = false;
	} while (0);

}
#else
void _set_curve_start(ovg_path_t* ctx) {
	if (ctx->segmentPtr > 0) {
		// check if current segment has points (straight)
		if ((ctx->pathes[ctx->pathPtr + ctx->segmentPtr] & PATH_ELT_MASK) > 0)
			ctx->segmentPtr++;
	}
	else {
		// not yet segmented path, first segment length is copied
		if (ctx->pathes[ctx->pathPtr] > 0) { // create first straight segment first
			ctx->pathes.push_back(ctx->pathes[ctx->pathPtr]);
			ctx->segmentPtr = 2;
		}
		else
			ctx->segmentPtr = 1;
	}
	//_check_pathes_array(ctx); 
	ctx->pathes.push_back(0);
	assert(ctx->pathes.size() > ctx->pathPtr + ctx->segmentPtr + 1);
}
void _set_curve_end(ovg_path_t* ctx) {
	ctx->pathes[ctx->pathPtr + ctx->segmentPtr] |= PATH_HAS_CURVES_BIT;
	ctx->segmentPtr++;
	ctx->pathes.push_back(0);
}
#endif
bool _path_is_closed(ovg_path_t* ctx, uint32_t ptrPath) { return ctx->pathes[ptrPath] & PATH_CLOSED_BIT; }
void _add_point(ovg_path_t* ctx, float x, float y) {
	if (isnan(x) || isnan(y)) {
		return;
	}
	glm::vec2 v = { x, y };
	ctx->points.push_back(v);
	if (ctx->pathes.size() <= ctx->pathPtr + ctx->segmentPtr)
		ctx->pathes.resize(ctx->pathPtr + ctx->segmentPtr + 1);
	ctx->pathes[ctx->pathPtr]++; // total point count in path
	if (ctx->segmentPtr > 0)
		ctx->pathes[ctx->pathPtr + ctx->segmentPtr]++; // total point count in path's segment
}

void _line_to(ovg_path_t* ctx, float x, float y) {
	glm::vec2 p = { x, y };
	if (!_current_path_is_empty(ctx)) {
		// prevent adding the same point
		auto v = _get_current_position(ctx);
		if (vec2_equ(v, p))
			return;
	}
	_add_point(ctx, x, y);
	ctx->simpleConvex = false;
}

float _get_arc_step(ovg_path_t* ctx, float radius) {
	float sx = 1.0, sy = 1.0;
	if (ctx->t)
		_matrix_get_scale(&ctx->t->pushConsts.mat, &sx, &sy);
	float r = radius * fabsf(fmaxf(sx, sy));
	if (r < 30.0f)
		return fminf(M_PI / 3.f, M_PI / r);
	return fminf(M_PI / 3.f, M_PI / (r * 0.4f));
}

void ovg_move_to(ovg_path_t* path, float x, float y);
void ovg_line_to(ovg_path_t* path, float x, float y);
void ovg_quadratic_to(ovg_path_t* path, float x1, float y1, float x2, float y2);
void ovg_curve_to(ovg_path_t* path, float x1, float y1, float x2, float y2, float x3, float y3);
void ovg_elliptic_arc_to(ovg_path_t* path, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi);

#define M_APPROXIMATION_SCALE         1.0
#define M_ANGLE_TOLERANCE             0.01
#define M_CUSP_LIMIT                  0.01
#define CURVE_RECURSION_LIMIT         100
#define CURVE_COLLINEARITY_EPSILON    1.7
#define CURVE_ANGLE_TOLERANCE_EPSILON 0.001
// no floating point arithmetic operation allowed in macro.
#pragma warning(disable : 4127)
void _recursive_bezier(ovg_path_t* ctx, float distanceTolerance, float x1, float y1, float x2, float y2, float x3,
	float y3, float x4, float y4, unsigned level) {
	if (level > CURVE_RECURSION_LIMIT) {
		return;
	}

	// Calculate all the mid-points of the line segments
	//----------------------
	float x12 = (x1 + x2) / 2;
	float y12 = (y1 + y2) / 2;
	float x23 = (x2 + x3) / 2;
	float y23 = (y2 + y3) / 2;
	float x34 = (x3 + x4) / 2;
	float y34 = (y3 + y4) / 2;
	float x123 = (x12 + x23) / 2;
	float y123 = (y12 + y23) / 2;
	float x234 = (x23 + x34) / 2;
	float y234 = (y23 + y34) / 2;
	float x1234 = (x123 + x234) / 2;
	float y1234 = (y123 + y234) / 2;

	if (level > 0) // Enforce subdivision first time
	{
		// Try to approximate the full cubic curve by a single straight line
		//------------------
		float dx = x4 - x1;
		float dy = y4 - y1;

		float d2 = fabsf(((x2 - x4) * dy - (y2 - y4) * dx));
		float d3 = fabsf(((x3 - x4) * dy - (y3 - y4) * dx));

		float da1, da2;

		if (d2 > CURVE_COLLINEARITY_EPSILON && d3 > CURVE_COLLINEARITY_EPSILON) {
			// Regular care
			//-----------------
			if ((d2 + d3) * (d2 + d3) <= (dx * dx + dy * dy) * distanceTolerance) {
				// If the curvature doesn't exceed the distance_tolerance value
				// we tend to finish subdivisions.
				//----------------------
				if (M_ANGLE_TOLERANCE < CURVE_ANGLE_TOLERANCE_EPSILON) {
					_add_point(ctx, x1234, y1234);
					return;
				}

				// Angle & Cusp Condition
				//----------------------
				float a23 = atan2f(y3 - y2, x3 - x2);
				da1 = fabsf(a23 - atan2f(y2 - y1, x2 - x1));
				da2 = fabsf(atan2f(y4 - y3, x4 - x3) - a23);
				if (da1 >= M_PI)
					da1 = M_2_PI - da1;
				if (da2 >= M_PI)
					da2 = M_2_PI - da2;

				if (da1 + da2 < (float)M_ANGLE_TOLERANCE) {
					// Finally we can stop the recursion
					//----------------------
					_add_point(ctx, x1234, y1234);
					return;
				}

				if (M_CUSP_LIMIT != 0.0) {
					if (da1 > M_CUSP_LIMIT) {
						_add_point(ctx, x2, y2);
						return;
					}

					if (da2 > M_CUSP_LIMIT) {
						_add_point(ctx, x3, y3);
						return;
					}
				}
			}
		}
		else {
			if (d2 > CURVE_COLLINEARITY_EPSILON) {
				// p1,p3,p4 are collinear, p2 is considerable
				//----------------------
				if (d2 * d2 <= distanceTolerance * (dx * dx + dy * dy)) {
					if (M_ANGLE_TOLERANCE < CURVE_ANGLE_TOLERANCE_EPSILON) {
						_add_point(ctx, x1234, y1234);
						return;
					}

					// Angle Condition
					//----------------------
					da1 = fabsf(atan2f(y3 - y2, x3 - x2) - atan2f(y2 - y1, x2 - x1));
					if (da1 >= M_PI)
						da1 = M_2_PI - da1;

					if (da1 < M_ANGLE_TOLERANCE) {
						_add_point(ctx, x2, y2);
						_add_point(ctx, x3, y3);
						return;
					}

					if (M_CUSP_LIMIT != 0.0) {
						if (da1 > M_CUSP_LIMIT) {
							_add_point(ctx, x2, y2);
							return;
						}
					}
				}
			}
			else if (d3 > CURVE_COLLINEARITY_EPSILON) {
				// p1,p2,p4 are collinear, p3 is considerable
				//----------------------
				if (d3 * d3 <= distanceTolerance * (dx * dx + dy * dy)) {
					if (M_ANGLE_TOLERANCE < CURVE_ANGLE_TOLERANCE_EPSILON) {
						_add_point(ctx, x1234, y1234);
						return;
					}

					// Angle Condition
					//----------------------
					da1 = fabsf(atan2f(y4 - y3, x4 - x3) - atan2f(y3 - y2, x3 - x2));
					if (da1 >= M_PI)
						da1 = M_2_PI - da1;

					if (da1 < M_ANGLE_TOLERANCE) {
						_add_point(ctx, x2, y2);
						_add_point(ctx, x3, y3);
						return;
					}

					if (M_CUSP_LIMIT != 0.0) {
						if (da1 > M_CUSP_LIMIT) {
							_add_point(ctx, x3, y3);
							return;
						}
					}
				}
			}
			else {
				// Collinear case
				//-----------------
				dx = x1234 - (x1 + x4) / 2;
				dy = y1234 - (y1 + y4) / 2;
				if (dx * dx + dy * dy <= distanceTolerance) {
					_add_point(ctx, x1234, y1234);
					return;
				}
			}
		}
	}

	// Continue subdivision
	//----------------------
	_recursive_bezier(ctx, distanceTolerance, x1, y1, x12, y12, x123, y123, x1234, y1234, level + 1);
	_recursive_bezier(ctx, distanceTolerance, x1234, y1234, x234, y234, x34, y34, x4, y4, level + 1);
}

static const glm::vec2 _v2_unit_x = { 1.f, 0 };
static const glm::vec2 _v2_unit_y = { 0, 1.f };
void _elliptic_arc(ovg_path_t* ctx, float x1, float y1, float x2, float y2, bool largeArc, bool counterClockWise, float _rx, float _ry, float phi) {
	if (!ctx)
		return;

	if (_rx == 0 || _ry == 0) {
		if (_current_path_is_empty(ctx))
			ovg_move_to(ctx, x1, y1);
		ovg_line_to(ctx, x2, y2);
		return;
	}
	float rx = fabsf(_rx);
	float ry = fabsf(_ry);

	glm::mat2 m = { { cosf(phi), sinf(phi) },{ -sinf(phi), cosf(phi) } };
	glm::vec2 p = { (x1 - x2) / 2, (y1 - y2) / 2 };
	glm::vec2 p1 = m * p;

	// radii corrections
	double lambda = (p1.x * p1.x) / (rx * rx) + (p1.y * p1.y) / (ry * ry);
	if (lambda > 1) {
		lambda = sqrtf(lambda);
		rx *= lambda;
		ry *= lambda;
	}

	p = glm::vec2{ rx * p1.y / ry, -ry * p1.x / rx };

	float rx2 = rx * rx, ry2 = ry * ry, px2 = p1.x * p1.x, py2 = p1.y * p1.y;
	glm::vec2 cp = p * sqrtf(fabsf((rx2 * ry2 - rx2 * py2 - ry2 * px2) /
		(rx2 * py2 + ry2 * px2)));

	if (largeArc == counterClockWise)
		cp = -cp;

	m = glm::mat2({ cosf(phi), -sinf(phi) }, { sinf(phi), cosf(phi) });
	p = glm::vec2((x1 + x2) / 2, (y1 + y2) / 2);
	glm::vec2 c = (m * cp) + p;

	glm::vec2   u = _v2_unit_x;
	glm::vec2   v = { (p1.x - cp.x) / rx, (p1.y - cp.y) / ry };
	double sa = acosf(glm::dot(u, v) / (fabsf(glm::length(v)) * fabsf(glm::length(u))));
	if (isnan((float)sa))
		sa = M_PI;
	if (u.x * v.y - u.y * v.x < 0)
		sa = -sa;

	u = v;
	v = glm::vec2{ (-p1.x - cp.x) / rx, (-p1.y - cp.y) / ry };
	double delta_theta = acosf(glm::dot(u, v) / (fabsf(glm::length(v)) * fabsf(glm::length(u))));
	if (isnan((float)delta_theta))
		delta_theta = M_PI;
	if (u.x * v.y - u.y * v.x < 0)
		delta_theta = -delta_theta;

	if (counterClockWise) {
		if (delta_theta < 0)
			delta_theta += M_PI * 2.0;
	}
	else if (delta_theta > 0)
		delta_theta -= M_PI * 2.0;

	m = glm::mat2{ { cosf(phi), -sinf(phi) },{ sinf(phi), cosf(phi) } };

	double theta = sa;
	double ea = sa + delta_theta;

	float step = fmaxf(0.001f, fminf(M_PI, _get_arc_step(ctx, fminf(rx, ry)) * 0.1f));

	p = glm::vec2{ rx * cosf(theta), ry * sinf(theta) };
	glm::vec2 xy = ((m * p) + c);
	if (_current_path_is_empty(ctx)) {
		_set_curve_start(ctx);
		_add_point(ctx, xy.x, xy.y);
		if (!ctx->pathPtr)
			ctx->simpleConvex = true;
		else
			ctx->simpleConvex = false;
	}
	else {
		ovg_line_to(ctx, xy.x, xy.y);
		_set_curve_start(ctx);
		ctx->simpleConvex = false;
	}

	_set_curve_start(ctx);

	if (sa < ea) {
		theta += step;
		while (theta < ea) {
			p = glm::vec2{ rx * cosf(theta), ry * sinf(theta) };
			xy = ((m * p) + c);
			_add_point(ctx, xy.x, xy.y);
			theta += step;
		}
	}
	else {
		theta -= step;
		while (theta > ea) {
			p = glm::vec2{ rx * cosf(theta), ry * sinf(theta) };
			xy = ((m * p) + c);
			_add_point(ctx, xy.x, xy.y);
			theta -= step;
		}
	}
	p = glm::vec2{ rx * cosf(ea), ry * sinf(ea) };
	xy = ((m * p) + c);
	_add_point(ctx, xy.x, xy.y);
	_set_curve_end(ctx);
}

// todo 接口实现开始

void ovg_clear_path(ovg_path_t* path) {
	if (!path)return;
	path->points.clear();
	path->pathes.clear();
	path->pathes.push_back(0);
	path->pathPtr = 0;
	path->segmentPtr = 0;
	path->subpathCount = 0;
	path->curVertOffset = 0;
	path->simpleConvex = 0;
}

void ovg_close_path(ovg_path_t* path)
{
	auto ctx = path;
	if (!ctx)
		return;
	if (ctx->pathes[ctx->pathPtr] & PATH_CLOSED_BIT) // already closed
		return;
	// check if at least 3 points are present
	if (ctx->pathes[ctx->pathPtr] < 3)
		return;
	auto pointCount = ctx->points.size();
	// prevent closing on the same point
	if (vec2_equ(ctx->points[pointCount - 1], ctx->points[pointCount - ctx->pathes[ctx->pathPtr]])) {
		if (ctx->pathes[ctx->pathPtr] < 4) // ensure enough points left for closing
			return;
		o_remove_last_point(ctx);
	}
	//pointCount = ctx->points.size();
	//auto v0 = ctx->points[pointCount - ctx->pathes[ctx->pathPtr]];
	//if (!vec2_equ(ctx->points[pointCount - 1], v0)) {
	//	_add_point(ctx, v0.x, v0.y);
	//}
	ctx->pathes[ctx->pathPtr] |= PATH_CLOSED_BIT;

	o_finish_path(ctx);
}
void ovg_new_sub_path(ovg_path_t* path)
{
	o_finish_path(path);
}
void ovg_path_extents(ovg_path_t* path, float* x1, float* y1, float* x2, float* y2)
{
	if (!path)return;
	o_finish_path(path);
	if (!path->pathPtr) { // no path
		*x1 = *x2 = *y1 = *y2 = 0;
		return;
	}
	_ovg_path_extents(path, false, x1, y1, x2, y2);
}
void ovg_get_current_point(ovg_path_t* path, float* x, float* y)
{
	auto cp = _get_current_point(path);
	if (x)*x = cp.x;
	if (y)*y = cp.y;
}
size_t ovg_get_segment_count(ovg_path_t* path) {
	return path ? path->pathes.size() : 0;
}
void ovg_set_segment_color(ovg_path_t* path, size_t idx, uint32_t color) {
	if (path)
	{
		if (path->colors.size() < path->pathes.size())
			path->colors.resize(path->pathes.size());
		if (idx < path->colors.size())
			path->colors[idx] = color;
	}
}
// 添加数据到当前路径，参考path_type_e
void ovg_add_path(ovg_path_t* path, float* data, size_t count)
{
	if (!path || !data || !count)return;
	auto d = data;
	for (; d - data < count;) {
		auto t = (path_type_et)*d;
		float x = d[1], y = d[2];
		d += 3;
		switch (t) {
		case path_type_et::e_vmove:
			ovg_move_to(path, x, y);//2
			break;
		case path_type_et::e_vline:
			ovg_line_to(path, x, y);//2
			break;
		case path_type_et::e_vcurve:
		{
			ovg_quadratic_to(path, x, y, d[0], d[1]);//5
			d += 2;
		}
		break;
		case path_type_et::e_vcubic:
		{
			ovg_curve_to(path, x, y, d[0], d[1], d[2], d[3]);//6
			d += 4;
		}
		break;
		case path_type_et::e_close:
		{
			ovg_close_path(path); d -= 2; //1
		}
		break;
		}
	}
}
// todo path copy
void ovg_add_path0(ovg_path_t* path, ovg_path_t* src)
{
	if (!path || !src)return;
	if (_current_path_is_empty(src))return;

}
void ovg_move_to(ovg_path_t* path, float x, float y)
{
	if (!path)
		return;
	o_finish_path(path);
	_add_point(path, x, y);

}
void ovg_rel_move_to(ovg_path_t* path, float x, float y)
{
	if (!path)return;
	if (_current_path_is_empty(path))
		_add_point(path, 0, 0);
	auto cp = _get_current_position(path);
	o_finish_path(path);
	_add_point(path, cp.x + x, cp.y + y);
}
void ovg_line_to(ovg_path_t* path, float x, float y)
{
	if (!path)
		return;
	_line_to(path, x, y);
}
void ovg_rel_line_to(ovg_path_t* path, float dx, float dy)
{
	if (!path)
		return;
	auto cp = _get_current_position(path);
	_line_to(path, cp.x + dx, cp.y + dy);
}
float ovg_arc_length_general(float xc, float yc, float radius, float a1, float a2, bool clockwise)
{
	// 参数校验
	if (isnan(radius) || isinf(radius) || radius < 0.f ||
		isnan(a1) || isinf(a1) || isnan(a2) || isinf(a2))
		return 0.f;

	// 2π 必须用 float 常量比较：delta 是 float，float(2π)=6.28318548 比
	// double 的 2.f*M_PI=6.28318531 大 1.7e-7，恰为整 2π 的跨度会被误判
	// "超期"而归一化成 ~0，maxcount 退化为 1、步长下限变成整跨度，
	// 整圆弧只落 1 个点后被 o_finish_path 丢弃（2026-10-25 ovg_tools_test
	// 三项 ring/clip 失败的根因）。
	const float k2pi = (float)M_PI * 2.f;
	float delta;
	if (clockwise) {
		// 顺时针（负方向）
		delta = a1 - a2;
		while (delta < 0.f) delta += k2pi;
		while (delta > k2pi) delta -= k2pi;
	}
	else {
		// 逆时针（正方向）
		delta = a2 - a1;
		while (delta < 0.f) delta += k2pi;
		while (delta > k2pi) delta -= k2pi;
	}
	auto ret = std::max(radius * delta, 1.0f);
	return ret;
}
void ovg_arc(ovg_path_t* path, float xc, float yc, float radius, float a1, float a2)
{
	if (!path)
		return;
	// 上游状态损坏（垃圾半径/角度）会让下面两个 while 无限加点或无法归一化
	if (isnan(radius) || isinf(radius) || radius < 0.f || isnan(a1) || isinf(a1) || isnan(a2) || isinf(a2))
		return;
	while (a2 < a1) // positive arc must have a1<a2
		a2 += 2.f * M_PI;
	if (a2 - a1 > 2.f * M_PI) // limit arc to 2PI
		a2 = a1 + 2.f * M_PI;
	glm::vec2 v = { cosf(a1) * radius + xc, sinf(a1) * radius + yc };
	float step = _get_arc_step(path, radius);
	// 垃圾半径/矩阵缩放会把 step 压成 denormal，while 循环将添加几十亿个重复点占满内存；
	int maxcount = ovg_arc_length_general(xc, yc, radius, a1, a2, false);
	step = fmaxf(step, (a2 - a1) / maxcount);
	float a = a1;
	if (_current_path_is_empty(path)) {
		_set_curve_start(path);
		_add_point(path, v.x, v.y);
		if (!path->pathPtr)
			path->simpleConvex = true;
		else
			path->simpleConvex = false;
	}
	else {
		_line_to(path, v.x, v.y);
		_set_curve_start(path);
		path->simpleConvex = false;
	}
	a += step;
	if (EQUF(a2, a1)) { // 零跨度：保留起点（cairo 语义），补齐段闭合标记
		_set_curve_end(path);
		return;
	}
	while (a < a2) {
		v.x = cosf(a) * radius + xc;
		v.y = sinf(a) * radius + yc;
		_add_point(path, v.x, v.y);
		a += step;
	}
	if (EQUF(a2 - a1, M_PI * 2.f)) { // if arc is complete circle, last point is the same as the first one
		_set_curve_end(path);
		ovg_close_path(path);
		return;
	}
	a = a2;
	// vec2 lastP = v;
	v.x = cosf(a) * radius + xc;
	v.y = sinf(a) * radius + yc;
	// if (!vec2_equ (v,lastP))//this test should not be required
	_add_point(path, v.x, v.y);
	_set_curve_end(path);
}
void ovg_arc_negative(ovg_path_t* path, float xc, float yc, float radius, float a1, float a2)
{
	if (!path)
		return;
	auto ctx = path;
	// 同 ovg_arc：状态损坏防护 + 细分点数上限
	if (isnan(radius) || isinf(radius) || radius < 0.f || isnan(a1) || isinf(a1) || isnan(a2) || isinf(a2))
		return;

	while (a2 > a1)
		a2 -= 2.f * M_PI;
	if (a1 - a2 > 2.f * M_PI) // limit arc to 2PI
		a2 = a1 - 2.f * M_PI;

	glm::vec2 v = { cosf(a1) * radius + xc, sinf(a1) * radius + yc };
	float step = _get_arc_step(ctx, radius);
	int maxcount = ovg_arc_length_general(xc, yc, radius, a1, a2, true);
	step = fmaxf(step, (a1 - a2) / maxcount);
	float a = a1;

	if (_current_path_is_empty(ctx)) {
		_set_curve_start(ctx);
		_add_point(ctx, v.x, v.y);
		if (!ctx->pathPtr)
			ctx->simpleConvex = true;
		else
			ctx->simpleConvex = false;
	}
	else {
		_line_to(ctx, v.x, v.y);
		_set_curve_start(ctx);
		ctx->simpleConvex = false;
	}

	a -= step;

	if (EQUF(a2, a1)) { // 零跨度：保留起点（cairo 语义），补齐段闭合标记
		_set_curve_end(ctx);
		return;
	}

	while (a > a2) {
		v.x = cosf(a) * radius + xc;
		v.y = sinf(a) * radius + yc;
		_add_point(ctx, v.x, v.y);
		a -= step;
	}

	if (EQUF(a1 - a2, M_PI * 2.f)) { // if arc is complete circle, last point is the same as the first one
		_set_curve_end(ctx);
		ovg_close_path(ctx);
		return;
	}

	a = a2;
	// vec2 lastP = v;
	v.x = cosf(a) * radius + xc;
	v.y = sinf(a) * radius + yc;
	// if (!vec2_equ (v,lastP))
	_add_point(ctx, v.x, v.y);
	_set_curve_end(ctx);
}
void ovg_curve_to(ovg_path_t* path, float x1, float y1, float x2, float y2, float x3, float y3)
{
	if (EQUF(x1, x2) && EQUF(x2, x3) && EQUF(y1, y2) && EQUF(y2, y3)) {
		auto cp = _get_current_position(path);
		if (_current_path_is_empty(path) || (EQUF(cp.x, x1) && EQUF(cp.y, y1)))
			return;
	}

	path->simpleConvex = false;
	_set_curve_start(path);
	if (_current_path_is_empty(path))
		_add_point(path, x1, y1);

	glm::vec2 cp = _get_current_position(path);
	float sx = 1, sy = 1;
	if (path->t)
		_matrix_get_scale(&path->t->pushConsts.mat, &sx, &sy);
	float distanceTolerance = fabs(0.25f / fmaxf(sx, sy));
	_recursive_bezier(path, distanceTolerance, cp.x, cp.y, x1, y1, x2, y2, x3, y3, 0);
	_add_point(path, x3, y3);
	_set_curve_end(path);
}
void ovg_rel_curve_to(ovg_path_t* path, float x1, float y1, float x2, float y2, float x3, float y3)
{
	glm::vec2 cp = _get_current_position(path);
	ovg_curve_to(path, cp.x + x1, cp.y + y1, cp.x + x2, cp.y + y2, cp.x + x3, cp.y + y3);
}
const double quadraticFact = 2.0 / 3.0;
void ovg_quadratic_to(ovg_path_t* path, float x1, float y1, float x2, float y2)
{
	float x0, y0;
	if (_current_path_is_empty(path)) {
		x0 = x1;
		y0 = y1;
	}
	else
	{
		glm::vec2 cp = _get_current_position(path);
		x0 = cp.x; y0 = cp.y;
	}
	ovg_curve_to(path, x0 + (x1 - x0) * quadraticFact, y0 + (y1 - y0) * quadraticFact, x2 + (x1 - x2) * quadraticFact,
		y2 + (y1 - y2) * quadraticFact, x2, y2);
}
void ovg_rel_quadratic_to(ovg_path_t* path, float x1, float y1, float x2, float y2)
{
	glm::vec2 cp = _get_current_position(path);
	ovg_quadratic_to(path, cp.x + x1, cp.y + y1, cp.x + x2, cp.y + y2);
}
void ovg_rectangle(ovg_path_t* path, float x, float y, float w, float h)
{
	if (!path)
		return;
	o_finish_path(path);
	if (w <= 0 || h <= 0)
		return;
	ovg_move_to(path, x, y);
	ovg_line_to(path, x + w, y);
	ovg_line_to(path, x + w, y + h);
	_add_point(path, x, y + h);
	assert(path->pathPtr < path->pathes.size());
	path->pathes[path->pathPtr] |= (PATH_CLOSED_BIT | PATH_IS_CONVEX_BIT);
	ovg_close_path(path);
}
void ovg_rounded_rectangle(ovg_path_t* path, float x, float y, float w, float h, float radius)
{
	if (!path)
		return;
	if (w <= 0 || h <= 0)
		return;
	o_finish_path(path);
	if ((radius > w / 2.0f) || (radius > h / 2.0f))
		radius = fmin(w / 2.0f, h / 2.0f);
	ovg_move_to(path, x, y + radius);
	ovg_arc(path, x + radius, y + radius, radius, M_PI, -M_PI_2);
	ovg_line_to(path, x + w - radius, y);
	ovg_arc(path, x + w - radius, y + radius, radius, -M_PI_2, 0);
	ovg_line_to(path, x + w, y + h - radius);
	ovg_arc(path, x + w - radius, y + h - radius, radius, 0, M_PI_2);
	ovg_line_to(path, x + radius, y + h);
	ovg_arc(path, x + radius, y + h - radius, radius, M_PI_2, M_PI);
	ovg_line_to(path, x, y + radius);
	ovg_close_path(path);
}
void ovg_rounded_rectangle2(ovg_path_t* path, float x, float y, float w, float h, float rx, float ry)
{
	if (!path)
		return;
	ovg_move_to(path, x + rx, y);
	ovg_line_to(path, x + w - rx, y);
	ovg_elliptic_arc_to(path, x + w, y + ry, false, true, rx, ry, 0);

	ovg_line_to(path, x + w, y + h - ry);
	ovg_elliptic_arc_to(path, x + w - rx, y + h, false, true, rx, ry, 0);

	ovg_line_to(path, x + rx, y + h);
	ovg_elliptic_arc_to(path, x, y + h - ry, false, true, rx, ry, 0);

	ovg_line_to(path, x, y + ry);
	ovg_elliptic_arc_to(path, x + rx, y, false, true, rx, ry, 0);

	ovg_close_path(path);
}
void ovg_ellipse(ovg_path_t* path, float radiusX, float radiusY, float x, float y, float rotationAngle)
{
	if (!path)
		return;
	float width_two_thirds = radiusX * 4 / 3;

	float dx1 = sinf(rotationAngle) * radiusY;
	float dy1 = cosf(rotationAngle) * radiusY;
	float dx2 = cosf(rotationAngle) * width_two_thirds;
	float dy2 = sinf(rotationAngle) * width_two_thirds;

	float topCenterX = x - dx1;
	float topCenterY = y + dy1;
	float topRightX = topCenterX + dx2;
	float topRightY = topCenterY + dy2;
	float topLeftX = topCenterX - dx2;
	float topLeftY = topCenterY - dy2;

	float bottomCenterX = x + dx1;
	float bottomCenterY = y - dy1;
	float bottomRightX = bottomCenterX + dx2;
	float bottomRightY = bottomCenterY + dy2;
	float bottomLeftX = bottomCenterX - dx2;
	float bottomLeftY = bottomCenterY - dy2;

	o_finish_path(path);
	ovg_move_to(path, bottomCenterX, bottomCenterY);

	ovg_curve_to(path, bottomRightX, bottomRightY, topRightX, topRightY, topCenterX, topCenterY);
	ovg_curve_to(path, topLeftX, topLeftY, bottomLeftX, bottomLeftY, bottomCenterX, bottomCenterY);

	path->pathes[path->pathPtr] |= PATH_CLOSED_BIT;
	o_finish_path(path);
}
void ovg_elliptic_arc_to(ovg_path_t* path, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi)
{
	if (!path)
		return;
	auto cp = _get_current_point(path);
	_elliptic_arc(path, cp.x, cp.y, x, y, large_arc_flag, sweep_flag, rx, ry, phi);
}
void ovg_rel_elliptic_arc_to(ovg_path_t* path, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi)
{
	if (!path)
		return;
	auto cp = _get_current_point(path);
	_elliptic_arc(path, cp.x, cp.y, x + cp.x, y + cp.y, large_arc_flag, sweep_flag, rx, ry, phi);
}
void ovg_circle(ovg_path_t* path, float x, float y, float radius) {
	static const float k = 0.55228475f;
	auto r = radius;
	ovg_move_to(path, x + r, y);
	ovg_curve_to(path, x + r, y + r * k, x + r * k, y + r, x, y + r);
	ovg_curve_to(path, x - r * k, y + r, x - r, y + r * k, x - r, y);
	ovg_curve_to(path, x - r, y - r * k, x - r * k, y - r, x, y - r);
	ovg_curve_to(path, x + r * k, y - r, x + r, y - r * k, x + r, y);
	ovg_close_path(path);
}

#ifdef CreateRgbaf
#undef CreateRgbaf
#endif
#define CreateRgbaf(r, g, b, a)                                                                                        \
    (((int)(a * 255.0f) << 24) | ((int)(b * 255.0f) << 16) | ((int)(g * 255.0f) << 8) | (int)(r * 255.0f))



struct ss_act :public  vg_state_save_t {
	usp_ac_cx* ac = 0;
};

struct pat_act :public  vg_pattern_t {
	vg_gradient_t g = {};
	usp_ac_cx* ac = 0;
};


vg_pattern_t* ovg_pattern_create_for_surface(usp_ac_cx* ac, void* surf) {
	if (!surf) {
		return 0;
	}
	if (!ac)
		ac = vg_default_ac();
	pat_act* pat = (pat_act*)ac->new_obj<pat_act>();
	if (!pat) {
		return 0;
	}
	*pat = {};
	pat->ac = ac;
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_SURFACE;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = surf;
	pat->references = 1;

	return pat;
}
// todo vg_state_save_t
void ovg_set_opacity(vg_state_save_t* ctx, float opacity) {
	if (ctx)ctx->pushConsts.opacity = opacity;
}
void ovg_set_effect(vg_state_save_t* ctx, const vg_effect_t* effect) {
	if (!ctx) return;
	if (!effect || effect->type == VG_EFFECT_NONE || effect->type > VG_EFFECT_SHADOW) {
		ctx->effect = {};
		return;
	}
	ctx->effect = *effect;
	if (ctx->effect.sigma < 0.0f) ctx->effect.sigma = 0.0f;
	if (ctx->effect.opacity < 0.0f) ctx->effect.opacity = 0.0f;
	if (ctx->effect.opacity > 1.0f) ctx->effect.opacity = 1.0f;
	if (ctx->effect.spread < -1.0f) ctx->effect.spread = -1.0f;
	if (ctx->effect.spread > 1.0f) ctx->effect.spread = 1.0f;
}
void ovg_set_source_color(vg_state_save_t* ctx, uint32_t c) {
	if (ctx)
	{
		ctx->color = c; ctx->pattern = 0;
	}
}
void ovg_set_source_rgba(vg_state_save_t* ctx, float r, float g, float b, float a) {
	if (ctx)
	{
		ctx->color = CreateRgbaf(r, g, b, a); ctx->pattern = 0;
	}
}
void ovg_set_source_rgb(vg_state_save_t* ctx, float r, float g, float b) {
	ovg_set_source_rgba(ctx, r, g, b, 1.0f);
}
void ovg_set_line_width(vg_state_save_t* ctx, float width) {
	if (ctx)ctx->lineWidth = width;
}
void ovg_set_miter_limit(vg_state_save_t* ctx, float limit) {
	if (ctx)ctx->miterLimit = limit;
}
void ovg_set_line_cap(vg_state_save_t* ctx, int cap) {
	if (ctx)ctx->lineCap = cap;
}
void ovg_set_line_join(vg_state_save_t* ctx, int join) {
	if (ctx)ctx->lineJoin = join;
}
void ovg_set_source_surface(vg_state_save_t* ctx, vg_image_t* surf, float x, float y) {
	auto p = (ss_act*)ctx;
	p->pushConsts.source.x = x;
	p->pushConsts.source.y = y;
	auto pat = ovg_pattern_create_for_surface(p->ac, surf);
	p->pattern = pat;
}
void ovg_set_source(vg_state_save_t* ctx, vg_pattern_t* pat) {
	if (ctx)ctx->pattern = pat;
}
void ovg_set_operator(vg_state_save_t* ctx, int op) {
	if (ctx)ctx->curOperator = (vg_operator_t)op;
}
void ovg_set_fill_rule(vg_state_save_t* ctx, int fr) {
	if (ctx)ctx->curFillRule = fr;
}
void ovg_set_dash(vg_state_save_t* ctx, const float* dashes, uint32_t num_dashes, float offset) {
	if (!ctx)return;
	auto t = (ss_act*)ctx;
	if (!dashes || !num_dashes) {
		if (t->dashes)
			t->ac->free_mem(t->dashes, t->dashCount);
		t->dashes = nullptr;
		t->dashCount = 0;
		t->dashOffset = offset;
		return;
	}
	if (!t->dashes || t->dashCount != num_dashes)
	{
		if (t->dashes)
			t->ac->free_mem(t->dashes, t->dashCount);
		t->dashes = (float*)t->ac->allocate(sizeof(float) * num_dashes);
		if (!t->dashes) {
			t->dashCount = 0;
			return;
		}
	}
	t->dashCount = num_dashes;
	t->dashOffset = offset;
	memcpy(t->dashes, dashes, sizeof(float) * t->dashCount);
}
void ovg_set_dash8(vg_state_save_t* ctx, uint64_t dashes0, uint32_t num_dashes, float offset) {

	float dashes[64] = {};
	uint64_t x = 1;
	auto t = dashes;
	auto v8 = (uint8_t*)&dashes0;
	if (num_dashes > 64)num_dashes = 64;
	{
		if (num_dashes > 8)num_dashes = 8;
		for (size_t i = 0; i < num_dashes; i++)
		{
			*t = v8[i]; t++;
		}
		if (num_dashes > 0)
			ovg_set_dash(ctx, dashes, num_dashes, offset);
	}
}
void ovg_translate(vg_state_save_t* ctx, float dx, float dy) {
	if (!ctx)return;
	auto m = glm::translate(glm::mat3x3(1.0), glm::vec2(dx, dy));
	glm::mat3x3 inv = ctx->pushConsts.mat;
	ctx->pushConsts.mat = inv * m; inv = ctx->pushConsts.mat;
	ctx->pushConsts.matInv = glm::inverse(inv);
}
void ovg_scale(vg_state_save_t* ctx, float sx, float sy) {
	if (!ctx)return;
	auto m = glm::scale(glm::mat3x3(1.0), glm::vec2(sx, sy));
	glm::mat3x3 inv = ctx->pushConsts.mat;
	ctx->pushConsts.mat = inv * m; inv = ctx->pushConsts.mat;
	ctx->pushConsts.matInv = glm::inverse(inv);
}
void ovg_rotate(vg_state_save_t* ctx, float radians) {
	if (!ctx)return;
	auto m = glm::rotate(glm::mat3x3(1.0), radians);
	glm::mat3x3 inv = ctx->pushConsts.mat;
	ctx->pushConsts.mat = inv * m; inv = ctx->pushConsts.mat;
	ctx->pushConsts.matInv = glm::inverse(inv);
}
void ovg_transform(vg_state_save_t* ctx, const void* matrix) {
	auto m = (glm::mat3x2*)matrix;
	if (!ctx || !m)return;
	glm::mat3x3 inv = ctx->pushConsts.mat;
	glm::mat3x3 m0 = *m;
	ctx->pushConsts.mat = inv * m0; inv = ctx->pushConsts.mat;
	ctx->pushConsts.matInv = glm::inverse(inv);
}
void ovg_set_matrix(vg_state_save_t* ctx, const void* matrix) {
	auto m = (glm::mat3x2*)matrix;
	if (!ctx || !m)return;
	ctx->pushConsts.mat = *m;
	glm::mat3x3 inv = *m;
	ctx->pushConsts.matInv = glm::inverse(inv);
}
void ovg_get_matrix(vg_state_save_t* ctx, void* matrix) {
	auto m = (glm::mat3x2*)matrix;
	if (!ctx || !m)return;
	*m = ctx->pushConsts.mat;
}
void ovg_identity_matrix(vg_state_save_t* ctx) {
	if (!ctx)return;
	ctx->pushConsts.mat = glm::mat3x2(1.0);
	glm::mat3x3 inv = ctx->pushConsts.mat;
	ctx->pushConsts.matInv = glm::inverse(inv);
}

void ovg_matrix_init(void* mat, float xx, float yx, float xy, float yy, float x0, float y0)
{
	if (!mat)return;
	*((glm::mat3x2*)mat) = glm::mat3x2(xx, yx, xy, yy, x0, y0);
}

int  ovg_pattern_add_color_stop(vg_pattern_t* pat, float o, float r, float g, float b, float a) {
	if (pat->type == vg_pattern_type_t::VG_PATTERN_TYPE_SURFACE || pat->type == vg_pattern_type_t::VG_PATTERN_TYPE_SOLID)
		return -1;
	vg_gradient_t* grad = (vg_gradient_t*)pat->data;
	if (grad->count < MAX_STOPS)
	{
		glm::vec4 c = { r, g, b, a };
		grad->colors[grad->count] = c;
#ifndef NOT_VG_ENABLE_VK_SCALAR_BLOCK_LAYOUT
		grad->stops[grad->count] = o;
#else
		grad->stops[grad->count].r = o;
#endif
		grad->count++;
	}
	return 0;
}
int  ovg_pattern_set_color_stop(vg_pattern_t* pat, int idx, float o, float r, float g, float b, float a) {
	if (!pat)return -1;
	if (pat->type == vg_pattern_type_t::VG_PATTERN_TYPE_SURFACE || pat->type == vg_pattern_type_t::VG_PATTERN_TYPE_SOLID)
		return -2;
	vg_gradient_t* grad = (vg_gradient_t*)pat->data;
	if (idx < 0 || idx >= MAX_STOPS)return -3;
	if (idx >= grad->count)
		grad->count = idx + 1;
	glm::vec4 c = { r, g, b, a };
	grad->colors[idx] = c;
	grad->stops[idx] = o;
	return 0;
}
void ovg_pattern_set_matrix(vg_pattern_t* pat, const void* matrix) {
	if (!pat || !matrix)return;
	pat->matrix = *((glm::mat3x2*)matrix);
	pat->hasMatrix = true;
}
void ovg_pattern_get_matrix(vg_pattern_t* pat, void* matrix) {
	if (!pat || !matrix)
		return;
	*((glm::mat3x2*)matrix) = (pat->hasMatrix) ? pat->matrix : glm::mat3x2(1.0);
}
void ovg_pattern_set_extend(vg_pattern_t* pat, int extend) {
	if (pat)pat->extend = (vg_extend_t)extend;
}
void ovg_pattern_set_filter(vg_pattern_t* pat, int filter) {
	if (pat)pat->filter = (vg_filter_t)filter;
}
void ovg_pattern_destroy(vg_pattern_t* pat) {
	if (pat) {
		auto p = (pat_act*)pat;
		if (p->ac) {
			p->ac->free_obj(p);
		}
	}
}



int _vg_pattern_edit_linear(vg_pattern_t* pat, float x0, float y0, float x1, float y1) {
	if (!pat)
		return -2;
	if (pat->type != vg_pattern_type_t::VG_PATTERN_TYPE_LINEAR)
		return -1;
	vg_gradient_t* grad = (vg_gradient_t*)pat->data;
	*grad = {};
	grad->cp[0] = glm::vec4{ { x0 },{ y0 },{ x1 },{ y1 } };
	grad->m = glm::ivec4(1024, 0, 0, 1024);
	grad->extend = pat->extend;
	grad->scale = glm::vec2{ 1.0,1.0 };
	grad->type = vg_pattern_type_t::VG_PATTERN_TYPE_LINEAR;
	return 0;
}
// 自定义分配
vg_pattern_t* ovg_new_pattern_linear(mem_resource_t* ac0, float x0, float y0, float x1, float y1) {
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	pat_act* pat = (pat_act*)ac->new_obj<pat_act>();
	if (!pat) {
		return 0;
	}
	pat->ac = ac;
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_LINEAR;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	_vg_pattern_edit_linear(pat, x0, y0, x1, y1);
	pat->matrix = glm::mat3x2(1.0);
	pat->references = 1;
	return pat;
}
int vg_pattern_edit_radial(pat_act* pat, float cx0, float cy0, float radius0, float cx1, float cy1, float radius1, bool is_ellipse) {
	if (!(pat))
		return -2;
	if (pat->type != vg_pattern_type_t::VG_PATTERN_TYPE_RADIAL)
		return -1;
	vg_gradient_t* grad = (vg_gradient_t*)pat->data;
	*grad = {};
	glm::vec2 c0 = { cx0, cy0 };
	glm::vec2 c1 = { cx1, cy1 };
	if (radius0 > radius1 - 1.0f)
		radius0 = radius1 - 1.0f;
	glm::vec2  u = (c0 - c1);
	float l = glm::length(u);
	if (l + radius0 + 1.0f >= radius1) {
		glm::vec2 v = (u / l);
		c0 = (c1 + (v * (radius1 - radius0 - 1.0f)));
	}
	grad->cp[0] = glm::vec4{ { c0.x },{ c0.y },{ radius0 },{ 0 } };
	grad->cp[1] = glm::vec4{ { c1.x },{ c1.y },{ radius1 },{ 0 } };
	grad->m = glm::ivec4(1024, 0, 0, 1024);
	grad->extend = pat->extend;
	grad->scale = glm::vec2{ 1.0,1.0 };
	grad->type = vg_pattern_type_t::VG_PATTERN_TYPE_RADIAL;
	if (is_ellipse)grad->scale.x *= 2;
	return 0;
}
int vg_pattern_edit_sweep(pat_act* pat, float cx, float cy, float start_angle, float end_angle) {
	if (!(pat))
		return -1;
	if (pat->type != vg_pattern_type_t::VG_PATTERN_TYPE_SWEEP)
		return -2;
	vg_gradient_t* grad = (vg_gradient_t*)pat->data;
	*grad = {};
	grad->cp[0] = glm::vec4{ cx, cy, start_angle, end_angle };
	grad->m = glm::ivec4(1024, 0, 0, 1024);
	grad->extend = pat->extend;
	grad->scale = glm::vec2{ 1.0,1.0 };
	grad->type = vg_pattern_type_t::VG_PATTERN_TYPE_SWEEP;
	return 0;
}
vg_pattern_t* ovg_new_pattern_radial(mem_resource_t* ac0, float cx0, float cy0, float radius0, float cx1, float cy1, float radius1, bool is_ellipse) {
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	pat_act* pat = (pat_act*)ac->new_obj<pat_act>();
	if (!pat) {
		return 0;
	}
	pat->ac = ac;
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_RADIAL;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	pat->matrix = glm::mat3x2(1.0);
	vg_pattern_edit_radial(pat, cx0, cy0, radius0, cx1, cy1, radius1, is_ellipse);
	pat->references = 1;
	return pat;
}
vg_pattern_t* ovg_new_pattern_sweep(mem_resource_t* ac0, float cx, float cy, float start_angle, float end_angle) {
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	pat_act* pat = (pat_act*)ac->new_obj<pat_act>();
	if (!pat) {
		return 0;
	}
	pat->ac = ac;
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_SWEEP;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	pat->matrix = glm::mat3x2(1.0);
	vg_pattern_edit_sweep(pat, cx, cy, start_angle, end_angle);
	pat->references = 1;
	return pat;
}


ovg_path_t* ovg_new_path(mem_resource_t* ac0) {
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	ovg_path_t* p = ac->new_obj<ovg_path_t>(ac);
	if (p)
		ovg_clear_path(p);
	return p;
}
void ovg_destroy_path(ovg_path_t* path) {
	if (path && path->ac)
		path->ac->free_obj(path);
}

void ovg_set_enable_tess2(ovg_path_t* path, bool enable) {
	if (path)
		path->enable_tess2 = enable;
}

vg_state_save_t* ovg_new_state(mem_resource_t* ac0) {
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	auto pp = (ss_act*)ac->new_mem(sizeof(ss_act));
	if (!pp)
		return 0;
	pp->ac = ac;
	vg_state_save_t* p = pp;
	*p = {};
	push_constants_t pc = {};
	pc.source.w = 1;
	pc.size = { (float)100, (float)100 };
	pc.fsq_patternType = VG_PATTERN_TYPE_SOLID;
	pc.opacity = 1.0f;
	pc.mat = pc.matInv = glm::mat3x2(1.0);
	p->lineWidth = 1.f;
	p->miterLimit = 10.f;
	p->curOperator = vg_operator_t::VG_OPERATOR_OVER;
	p->curFillRule = VG_FILL_RULE_NON_ZERO;
	p->pushConsts = pc;
	p->references = 1;
	return p;
}
void ovg_state_destroy(vg_state_save_t* p) {
	auto p0 = (ss_act*)p;
	if (!p0 || !p0->ac)return;
	if (p0->dashes) {
		p0->ac->free_mem(p0->dashes, p0->dashCount);
	}
	p0->ac->free_mem(p, 1);
}
// 渲染对象
#if 1


struct geom2d_cmd_c
{
	glm::ivec4 clip_rect = {};
	void* texid = 0;
	uint32_t vtxOffset = 0;
	uint32_t idxOffset = 0;
	uint32_t elemCount = 0;
	uint32_t vCount = 0;
	uint16_t blend_mode = 0;		// 混合模式	 
};

struct dash_context_t {
	bool     dashOn;
	uint32_t curDash;       // current dash index
	float    curDashOffset; // cur dash offset between defined path point and last dash segment(on/off) start
	float    totDashLength; // total length of dashes
	glm::vec2 normal;
};

struct stroke_context_t {
	uint32_t iL;
	uint32_t iR;
	uint32_t cp; // current point

	uint32_t firstIdx; // save first point idx for closed path
	float               hw;       // stroke half width, computed once.
	float               lhMax;    // miter limit * line width
	float arcStep; // cached arcStep, prevent compute multiple times for same stroke, 0 if not yet computed
};
struct rvg_cx;

class mesh2d_x
{
public:
	struct vertex_t
	{
		glm::vec2 position = {};		// 坐标	
		glm::vec2 tex_coord = {};		// 纹理uv
		uint32_t color = 0xffffffff;	// 顶点颜色 
	};

	std::vector<geom2d_cmd_c> cmd_data;	// 渲染命令
	std::vector<vertex_t> vtxs;		// 顶点数据
	std::vector<int> idxs;				// 索引
	glm::ivec4 viewport = { 0,0,0,0 };
	glm::ivec4 _clip_rect = {};// 当前裁剪 
public:
	mesh2d_x();
	virtual ~mesh2d_x();
	void set_viewport(const glm::ivec4& vp);
	void set_clip(const glm::ivec4& rc);
	// 清除数据,保留viewport
	void clear_m2d();
	bool nohas_clip(const glm::ivec4&);
	// 添加相同纹理/裁剪区域则自动合批
	void add(void* user_image, std::vector<vertex_t>& vertex, std::vector<int>& vt_index, const glm::ivec4& clip);
	void add(void* user_image, vertex_t* vertex, size_t vcount, int* vt_index, size_t icount, const glm::ivec4& clip);
	// 添加图片渲染，自动生成顶点数据
	void add_image0(void* img, const glm::ivec2& texsize, const glm::ivec4& clip, const glm::ivec4& dst, const glm::ivec4& src, const glm::ivec4& sliced, uint32_t color = 0xffffffff);
	// 添加九宫格图片渲染
	void add_image_sliced(void* user_image, const glm::ivec2& texsize, const glm::ivec4& a, const glm::ivec4& sliced, const glm::ivec4& rect, uint32_t col, const glm::ivec4& clip);
	// 添加旋转图片渲染，angle为旋转角度，center为旋转中心坐标（相对于dst）
	void add_image_angle(void* img, const glm::ivec2& texsize, const glm::ivec4& src, const glm::ivec4& dst, float angle, const glm::vec2* center, uint32_t col, const glm::ivec4& clip, int flip);
	// 原生纹理：源裁剪、tint、翻转、九宫格、旋转、矩阵。clip 为 {x,y,x1,y1}。
	void add_texture0(void* img, const glm::ivec2& texsize, const glm::ivec4& clip,
		const glm::ivec4& dst, const glm::ivec4& src, const glm::ivec4& sliced,
		uint32_t color, int flip, float angle, const glm::vec2& center,
		bool center_set, const glm::mat3x2* matrix);
private:

};

class geom_primitive :public mesh2d_x {
public:
	struct Vertex1 {
		glm::vec3 pos;
		glm::vec2 uv;
		uint32_t color;
	};
	struct Vertex2 {
		glm::vec3 pos;
		glm::vec2 uv;
		uint32_t color;
		uint32_t color1;
	};
public:
	vg_vector<Vertex1> vd1;	// 单面顶点
	vg_vector<Vertex2> vd2;	// 双面顶点
	vg_vector<uint32_t> ids;	// 索引
	vg_vector<glm::mat4> instance_mat;	// 实例矩阵
	glm::mat4 mat = glm::mat4(1.0f);// 当前矩阵
	gem_info_t curState = {};		// 当前状态	 
	vg_vector<gcmd_t>* gt = 0;
	vg_vector<gem_info_t> build_pipes;
	rvg_cx* dc = 0;
	size_t inst_count = 0;			// 当前实例
	size_t inst_idx = 0;			// 当前实例
public:
	explicit geom_primitive(vg_alloc_cx* a);
	~geom_primitive();
public:
	// 清空数据
	void clear();
	void set_state(gem_info_t* info, const glm::mat4* matrix);
	size_t set_instance_mat(const glm::mat4* matrix, size_t count);
	// 添加几何数据到缓冲区，xy顶点坐标，color顶点颜色，uv顶点纹理坐标，indices索引数据，color_type=0表示float4，1表示uint32_t
	bool add_geometry(void* texture, const float* xy, int xy_stride, const void* color, int color_stride
		, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);
	// 添加3D几何数据到缓冲区，xyz顶点坐标，color顶点颜色（双面则要双倍），uv顶点纹理坐标，indices索引数据
	bool add_geometry3d(void* texture, const float* xyz, int xyz_stride, const void* color, int color_stride
		, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);

	void add_image(ovg_image_r* r);
	void add_texture(ovg_texture_r* r);
};

// Keeps temporary states/patterns alive until rvg_cx::clear_all().  Memory is
// served from large chunks so the hot path stays pointer-bump fast, while the
// chunks themselves still come from the configured allocator.
class vg_bump_alloc
{
public:
	explicit vg_bump_alloc(vg_alloc_cx* context = nullptr)
		: ac(context), chunks(context) {}
	~vg_bump_alloc() { release(); }
	vg_bump_alloc(const vg_bump_alloc&) = delete;
	vg_bump_alloc& operator=(const vg_bump_alloc&) = delete;

	void* allocate(size_t bytes, size_t align = 0)
	{
		if (bytes == 0)
			bytes = 1;
		if (align == 0)
			align = alignof(std::max_align_t);
		if ((align & (align - 1)) != 0 || align < alignof(void*))
			align = alignof(std::max_align_t);

		if (!chunks.empty()) {
			chunk& c = chunks.back();
			std::uintptr_t begin = (std::uintptr_t)c.p;
			std::uintptr_t p = (begin + c.used + align - 1) & ~(std::uintptr_t)(align - 1);
			std::uintptr_t offset = p - begin;
			if (p >= begin && offset <= c.size && bytes <= c.size - offset) {
				c.used = (size_t)(offset + bytes);
				return (void*)p;
			}
		}

		if (bytes > (std::numeric_limits<size_t>::max)() - align - 1)
			return nullptr;
		const size_t min_chunk = 64 * 1024;
		size_t chunk_size = bytes + align - 1;
		if (chunk_size < min_chunk)
			chunk_size = min_chunk;

		vg_alloc_cx* context = ac ? ac : vg_get_default_allocator();
		void* mem = context->alloc(chunk_size, alignof(std::max_align_t));
		if (!mem)
			return nullptr;
		try {
			chunks.push_back({ mem, chunk_size, 0 });
		}
		catch (...) {
			context->dealloc(mem, chunk_size);
			throw;
		}

		chunk& c = chunks.back();
		std::uintptr_t begin = (std::uintptr_t)c.p;
		std::uintptr_t p = (begin + align - 1) & ~(std::uintptr_t)(align - 1);
		size_t offset = (size_t)(p - begin);
		c.used = offset + bytes;
		return (void*)p;
	}

	void release()
	{
		vg_alloc_cx* context = ac ? ac : vg_get_default_allocator();
		for (const auto& c : chunks)
			context->dealloc(c.p, c.size);
		chunks.clear();
	}

private:
	struct chunk {
		void* p;
		size_t size;
		size_t used;
	};
	vg_alloc_cx* ac = nullptr;
	vg_vector<chunk> chunks;
};

struct rvg_cx :public rvg_t {
	struct Vertex {
		glm::vec2	pos;
		glm::vec2	uv;
		uint32_t	color;
	};
	struct ear_clip_point {
		glm::vec2 pos;
		uint32_t idx;
		struct ear_clip_point* next;
	};
	usp_ac_cx* ac = 0;
	vg_bump_alloc mac;
	vg_vector<gcmd_t> cmdlist;		// 命令列表
	vg_vector<Vertex> _vertex;		// 矢量顶点
	vg_vector<uint32_t> _indices;	// 矢量索引
	// 临时缓冲用
	vg_vector<ear_clip_point> ecpsd;
	vg_vector<glm::vec2> _normals;
	// tess2 常驻资源：细分器与轮廓缓冲跨 fill 复用（惰性创建，析构释放）
	TESStesselator* _tess = nullptr;
	vg_vector<float> _tess_contour;
	vg_vector<glm::vec4> _fill_bbox;	// 子路径 bbox 临时缓冲（tess2 路由判定用）

	vg_map<vg_image_t*, vg_image_desc_t> _images;
	vg_vector<vg_image_desc_t*> _images_up;

	// 23d
	geom_primitive gps;

	size_t gCount = 0;	// ubo数量
	size_t _curVertOffset = 0;
	uint32_t curColor = 0;
	uint32_t fxCount = 0;	// 本draw list的特效槽位计数
	glm::ivec4 curClip = {};
	std::stack<vg_state_save_t*> _cst;	// 保存栈 

	// 录制快照：start_recording 时记录当前各 vector 的末尾位置，
	// stop_recording 时将 [快照起点, 当前末尾) 区间深拷贝进 ovg_recording_t。
	bool     _recording = false;
	uint32_t _rec_start_cmd = 0;
	uint32_t _rec_start_vtx = 0;
	uint32_t _rec_start_idx = 0;
public:
	explicit rvg_cx(vg_alloc_cx* a);
	~rvg_cx();
	void clear_all();
	void set_path(ovg_path_t* path, vg_state_save_t* st);
	void stroke_preserve();
	void fill_preserve();
	void clip_preserve();
	void fill();
	void paint();
	void clip();
	void clip0(uint8_t ref);
	void clip(const glm::ivec4* rc);

	void save();
	void restore();

	void image_update(vg_image_t* img, vg_image_desc_t* desc);
	void image_destroy(vg_image_t* img);
public:
	void fill_non_zero_tess2(ovg_path_t* p);
	// 保守判定路径是否必须走 tess2 交叉填充（EVEN_ODD / 子路径 bbox 重叠）
	bool fill_needs_tess2(ovg_path_t* p);
	void fill_non_zero(ovg_path_t* p);

	bool _build_vb_step(ovg_path_t* ctx, stroke_context_t* str, bool isCurve);
	void _draw_stoke_cap(ovg_path_t* ctx, stroke_context_t* str, glm::vec2 p0, glm::vec2 n, bool isStart);
	float _draw_dashed_segment(ovg_path_t* ctx, stroke_context_t* str, dash_context_t* dc, bool isCurve);
	void _draw_segment(ovg_path_t* ctx, stroke_context_t* str, dash_context_t* dc, bool isCurve);

	void _add_triangle_indices(ovg_path_t* ctx, uint32_t i0, uint32_t i1, uint32_t i2);
	void _add_tri_indices_for_rect(uint32_t i);
	void _add_vertexf(ovg_path_t* ctx, float x, float y);
	// 计算特效ROI并分配槽位，写入cmd.ref/cmd.bounds
	void apply_effect_bounds(vgcmd_t* c, bool isStroke);
	// 复制状态，自动释放
	void cp_cmdt(vgcmd_t* c, vg_state_save_t* t);

	vg_state_save_t* new_ss(vg_state_save_t* src);
	vg_state_save_t* new_state();
	void free_state(vg_state_save_t* p);
	void swap_state(vg_state_save_t* p, vg_state_save_t* p1);
};

rvg_cx::rvg_cx(vg_alloc_cx* a)
	: ac((usp_ac_cx*)a),
	mac(a),
	cmdlist(a),
	_vertex(a),
	_indices(a),
	ecpsd(a),
	_normals(a),
	_tess_contour(a),
	_fill_bbox(a),
	_images(a),
	_images_up(a),
	gps(a)
{
	gps.gt = &cmdlist;
	gps.dc = this;
}

rvg_cx::~rvg_cx()
{
	if (_tess) {
		tessDeleteTess(_tess);
		_tess = nullptr;
	}
}
void rvg_cx::clear_all()
{
	gps.clear();
	ovg_clear_path(path);
	_curVertOffset = 0;
	gCount = 0;
	fxCount = 0;
	mac.release();
	_vertex.clear();
	_indices.clear();
	cmdlist.clear();
	while (_cst.size())
	{
		auto c = _cst.top();
		free_state(c);
		_cst.pop();
	}
	free_state(st);
	st = new_state();
	for (auto& [k, v] : _images) {
		if (v.is_copy && v.pixels) {
			ac->free_mem0(v.pixels, v.px_size);
		}
	}
	_images.clear();
	// 清空录制快照 — clear_all() 在帧边界调用，cmdlist/_vertex/_indices 都被清了，
	// 此时旧的 _rec_start_* 索引已经失效，必须复位，否则 stop_recording 会越界读。
	_recording = false;
	_rec_start_cmd = _rec_start_vtx = _rec_start_idx = 0;
}
void rvg_cx::set_path(ovg_path_t* path0, vg_state_save_t* st0)
{
	path = path0;
	path->t = st0;
	st = st0;
}
void rvg_cx::stroke_preserve()
{
	o_finish_path(path);
	if (!path || !path->pathPtr || !st)
		return;
	auto p = path;
	p->t = st;
	if (p->t->pattern)
		gCount++;
	auto ctx = p;
	vgcmd_t c = { .full_screen_quad = -1 };
	c.vertex.x = _vertex.size();
	c.index.x = _indices.size();
	c.type = 1;
	cp_cmdt(&c, st);
	apply_effect_bounds(&c, true);
	ctx->curVertOffset = c.vertex.x;
	stroke_context_t str = { 0 };
	str.hw = p->t->lineWidth * 0.5f;
	str.lhMax = p->t->miterLimit * p->t->lineWidth;
	uint32_t ptrPath = 0;
	curColor = p->color = p->t->color;

	while (ptrPath < ctx->pathPtr) {
		uint32_t ptrSegment = 0, lastSegmentPointIdx = 0;
		uint32_t firstPathPointIdx = str.cp;
		uint32_t pathPointCount = ctx->pathes[ptrPath] & PATH_ELT_MASK;
		uint32_t lastPathPointIdx = str.cp + pathPointCount - 1;

		dash_context_t dc = { 0 };

		if (o_path_has_curves(ctx->pathes.data(), ptrPath)) {
			ptrSegment = 1;
			lastSegmentPointIdx = str.cp + (ctx->pathes[ptrPath + ptrSegment] & PATH_ELT_MASK) - 1;
		}

		str.firstIdx = (uint32_t)_vertex.size() - ctx->curVertOffset;

		if (p->t->dashCount > 0) {
			// init dash stroke
			dc.dashOn = true;
			dc.curDash = 0; // current dash index
			dc.totDashLength = 0; // limit offset to total length of dashes
			for (uint32_t i = 0; i < p->t->dashCount; i++)
				dc.totDashLength += p->t->dashes[i];
			if (dc.totDashLength == 0) {
				break;
				//return;
			}
			dc.curDashOffset = fmodf(fmodf(p->t->dashOffset, dc.totDashLength) + dc.totDashLength, dc.totDashLength);
			// cur dash offset between defined path point and last dash segment(on/off) start
			str.iL = lastPathPointIdx;
		}
		else if (_path_is_closed(ctx, ptrPath)) {
			str.iL = lastPathPointIdx;
		}
		else {
			_draw_stoke_cap(ctx, &str, ctx->points[str.cp],
				vec2_line_norm(ctx->points[str.cp], ctx->points[str.cp + 1]), true);
			str.iL = str.cp++;
		}

		if (o_path_has_curves(ctx->pathes.data(), ptrPath)) {
			while (str.cp < lastPathPointIdx) {

				bool curved = ctx->pathes[ptrPath + ptrSegment] & PATH_HAS_CURVES_BIT;
				if (lastSegmentPointIdx == lastPathPointIdx) // last segment of path, dont draw end point here
					lastSegmentPointIdx--;
				while (str.cp <= lastSegmentPointIdx)
					_draw_segment(ctx, &str, &dc, curved);

				ptrSegment++;
				uint32_t cptSegPts = ctx->pathes[ptrPath + ptrSegment] & PATH_ELT_MASK;
				lastSegmentPointIdx = str.cp + cptSegPts - 1;
				if (lastSegmentPointIdx == lastPathPointIdx && cptSegPts == 1) {
					// single point last segment
					ptrSegment++;
					break;
				}
			}
		}
		else
			while (str.cp < lastPathPointIdx)
				_draw_segment(ctx, &str, &dc, false);

		if (p->t->dashCount > 0) {
			if (_path_is_closed(ctx, ptrPath)) {
				str.iR = firstPathPointIdx;

				_draw_dashed_segment(ctx, &str, &dc, false);

				str.iL++;
				str.cp++;
			}
			//else {
			//	// 强制结束当前 dash
			//	dc.dashOn = false;
			//	dc.curDashOffset = 0.0f;
			//}
			if (!dc.dashOn) {
				// finishing last dash that is already started, draw end caps but not too close to start
				// the default gap is the next void
				int32_t prevDash = (int32_t)dc.curDash - 1;
				if (prevDash < 0)
					dc.curDash = p->t->dashCount - 1;
				float m = fminf(p->t->dashes[prevDash] - dc.curDashOffset, p->t->dashes[dc.curDash]);
				glm::vec2  p2 = vec2_sub(ctx->points[str.iR], vec2_mult_s(dc.normal, m));
				_draw_stoke_cap(ctx, &str, p2, dc.normal, false);
			}
		}
		else if (_path_is_closed(ctx, ptrPath)) {
			str.iR = firstPathPointIdx;
			bool inverse = _build_vb_step(ctx, &str, false);
			uint32_t* inds = &_indices[_indices.size() - 6];
			uint32_t  ii = str.firstIdx;
			if (inverse) {
				inds[1] = ii + 1;
				inds[4] = ii + 1;
				inds[5] = ii;
			}
			else {
				inds[1] = ii;
				inds[4] = ii;
				inds[5] = ii + 1;
			}
			str.cp++;
		}
		else
			_draw_stoke_cap(ctx, &str, ctx->points[str.cp],
				vec2_line_norm(ctx->points[str.cp - 1], ctx->points[str.cp]), false);

		str.cp = firstPathPointIdx + pathPointCount;

		if (ptrSegment > 0)
			ptrPath += ptrSegment;
		else
			ptrPath++;

	}
	c.vertex.y = _vertex.size() - c.vertex.x;
	c.index.y = _indices.size() - c.index.x;
	cmdlist.push_back({ .vg = c });

}

void rvg_cx::apply_effect_bounds(vgcmd_t* c, bool isStroke)
{
	if (!c)
		return;
	// 默认无特效槽位；早退时不能保留 slot 0，否则空路径会误用并覆盖第一个真实特效
	c->ref = -1;
	c->bounds = glm::vec4(0.0f);
	if (!st || st->effect.type == VG_EFFECT_NONE || st->effect.opacity <= 0.0f)
		return;
	if (((st->effect.color >> 24) & 0xFF) == 0)
		return;
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	ovg_path_extents(path, &x1, &y1, &x2, &y2);
	if (!(x2 > x1) || !(y2 > y1))
		return;
	// 描边几何在路径两侧展开
	float grow = 0.0f;
	if (isStroke && st->lineWidth > 0.0f) {
		float ml = st->miterLimit > 1.0f ? st->miterLimit : 2.0f;
		if (ml > 10.0f) ml = 10.0f;
		grow = st->lineWidth * 0.5f * ml;
	}
	x1 -= grow; y1 -= grow; x2 += grow; y2 += grow;
	// 变换到设备空间
	const glm::vec2 corners[4] = {
		{ x1, y1 },{ x2, y1 },{ x1, y2 },{ x2, y2 }
	};
	float minx = FLT_MAX, miny = FLT_MAX, maxx = -FLT_MAX, maxy = -FLT_MAX;
	for (int i = 0; i < 4; i++) {
		glm::vec2 p = st->pushConsts.mat * glm::vec3(corners[i], 1.0f);
		minx = std::min(minx, p.x); miny = std::min(miny, p.y);
		maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
	}
	const auto& fx = st->effect;
	// 模糊核半径固定为 3*sigma；spread 只放大 alpha，不扩大半径。
	// offset 只沿各自轴扩展，因此 x/y 分开计算，避免大 offset 时四边都过度膨胀。
	float blur = ceilf(fx.sigma * 3.0f);
	float pad_x = blur + fabsf(fx.offset.x) + 2.0f;
	float pad_y = blur + fabsf(fx.offset.y) + 2.0f;
	if (fxCount >= 127) {
		c->ref = -1;
		return;
	}
	c->ref = (int8_t)fxCount++;
	c->bounds = glm::vec4(minx - pad_x, miny - pad_y,
		(maxx - minx) + pad_x * 2.0f, (maxy - miny) + pad_y * 2.0f);
}

void rvg_cx::fill_preserve()
{
	o_finish_path(path);
	if (!path || !path->pathPtr || !st)
		return;
	auto p = path;
	p->t = st;
	if (p->t->pattern)
		gCount++;
	auto t = p->t;
	uint32_t color = t->color;
	p->color = color;
	vgcmd_t c = { .full_screen_quad = -1 };
	c.vertex.x = _vertex.size();
	c.index.x = _indices.size();
	c.type = 0;
	cp_cmdt(&c, st);
	apply_effect_bounds(&c, false);
	p->curVertOffset = c.vertex.x;
	fill_non_zero(p);
	c.vertex.y = _vertex.size() - c.vertex.x;
	c.index.y = _indices.size() - c.index.x;
	cmdlist.push_back({ .vg = c });
}

void rvg_cx::clip_preserve()
{
	o_finish_path(path);
	if (!path || !path->pathPtr || !st)
		return;
	path->t = st;
	auto p = path;
	auto t = st;
	vgcmd_t c = { .full_screen_quad = -1 };
	c.type = 2;
	{
		c.vertex.x = _vertex.size();
		c.index.x = _indices.size();
		cp_cmdt(&c, t);
		p->curVertOffset = c.vertex.x;
		fill_non_zero(p);
		c.vertex.y = _vertex.size() - c.vertex.x;
		c.index.y = _indices.size() - c.index.x;
	}
	c.full_screen_quad = _vertex.size();
	cmdlist.push_back({ .vg = c });
	Vertex v = {};
	v.pos = { -1,-1 };
	v.color = t->color;
	_vertex.push_back(v);
	v.pos = { 3,-1 };
	_vertex.push_back(v);
	v.pos = { -1,3 };
	_vertex.push_back(v);
}
void rvg_cx::clip0(uint8_t ref)
{
	// reset_clip 同时清空矩形裁剪状态；否则后续 add_texture/add_image 仍会
	// 读取上一帧的 curClip 并在 CPU 端提前剔除。
	curClip = {};
	vgcmd_t c = { .full_screen_quad = -1 };
	c.type = 2;
	c.ref = ref;
	c.full_screen_quad = _vertex.size();
	Vertex v = {};
	v.pos = { -1,-1 };
	v.color = st->color;
	_vertex.push_back(v);
	v.pos = { 3,-1 };
	_vertex.push_back(v);
	v.pos = { -1,3 };
	_vertex.push_back(v);
	cmdlist.push_back({ .vg = c });
}

void rvg_cx::clip()
{
	clip_preserve();
	ovg_clear_path(path);
}
void rvg_cx::clip(const glm::ivec4* rc)
{
	if (rc)
	{
		auto ct = _cst.size() ? _cst.top() : st;
		curClip = *rc;
		if (ct)
		{
			glm::vec3 ps = { curClip.x ,curClip.y,1.0 };
			auto nps = ct->pushConsts.mat * ps;
			curClip.x = nps.x; curClip.y = nps.y;
		}
		vgcmd_t c = { .full_screen_quad = -1 };
		c.type = 2;
		c.bounds = curClip;
		cmdlist.push_back({ .vg = c });
	}
}


vg_state_save_t* rvg_cx::new_ss(vg_state_save_t* src)
{
	vg_state_save_t* dst = new_state();
	if (!dst)return dst;
	if (src)
	{
		*dst = *src;
		// The state copy above aliases src->dashes.  A saved state must own its
		// dash array because restore/destroy frees it independently.
		dst->dashes = nullptr;
		dst->dashCount = 0;
		if (src->dashes && src->dashCount > 0) {
			dst->dashes = (float*)ac->allocate(sizeof(float) * src->dashCount);
			if (dst->dashes) {
				memcpy(dst->dashes, src->dashes, sizeof(float) * src->dashCount);
				dst->dashCount = src->dashCount;
			}
		}
	}
	else
		*dst = {};
	return dst;
}
vg_state_save_t* rvg_cx::new_state()
{
	auto t = (vg_state_save_t*)ac->allocate(sizeof(vg_state_save_t));
	*t = {};
	t->color = -1;
	//VkRect2D b = {};
	//b.extent = { ctx->pSurf->width, ctx->pSurf->height };
	push_constants_t pc = {};
	pc.source.w = 1;
	pc.size = { (float)100, (float)100 };
	pc.fsq_patternType = VG_PATTERN_TYPE_SOLID;
	pc.opacity = 1.0f;
	pc.mat = pc.matInv = glm::mat3x2(1.0);

	t->lineWidth = 1.f;
	t->miterLimit = 10.f;
	t->curOperator = vg_operator_t::VG_OPERATOR_OVER;
	t->curFillRule = VG_FILL_RULE_NON_ZERO;
	//t->bounds = b;
	t->pushConsts = pc;
	return t;
}
void rvg_cx::free_state(vg_state_save_t* p)
{
	if (p) {
		if (p->dashes && p->dashCount > 0)
			ac->free_mem(p->dashes, p->dashCount);
		ac->free_mem(p, 1);
	}
}
void rvg_cx::swap_state(vg_state_save_t* p, vg_state_save_t* p1)
{
	std::swap(*p, *p1);
}
void rvg_cx::save()
{
	auto ss = new_ss(st);
	_cst.push(ss);
}
void rvg_cx::restore()
{
	if (_cst.empty())return;
	auto c = _cst.top();
	swap_state(st, c);
	free_state(c);
	_cst.pop();
}

void rvg_cx::fill()
{
	fill_preserve();
	ovg_clear_path(path);
}

void rvg_cx::paint()
{
	auto ph = path;
	o_finish_path(ph);
	if (path && ph->pathPtr && st) {
		fill();
		return;
	}
	vgcmd_t c = {};
	c.type = 3;
	c.full_screen_quad = _vertex.size();

	Vertex v = {};
	if (st) {
		if (st->pattern)
			gCount++;
		cp_cmdt(&c, st);
	}
	v.pos = { -1,-1 };
	v.color = st ? st->color : -1;
	_vertex.push_back(v);
	v.pos = { 3,-1 };
	_vertex.push_back(v);
	v.pos = { -1,3 };
	_vertex.push_back(v);
	cmdlist.push_back({ .vg = c });
}

#define COLOR_R(c) ((c) & 0xFF)
#define COLOR_G(c) (((c) >> 8) & 0xFF)
#define COLOR_B(c) (((c) >> 16) & 0xFF)
#define COLOR_A(c) (((c) >> 24) & 0xFF)

#define MAKE_RGBA(r,g,b,a) \
    ((uint32_t)((a) << 24) | ((b) << 16) | ((g) << 8) | (r))
static inline uint8_t mul_unorm8(uint8_t a, uint8_t b)
{
	return (uint8_t)((a * b + 127) / 255);
}
// ---------------------------------------------------------------------------
// 工具函数：计算有符号面积，判断绕向
//   area > 0 : CCW (逆时针, 外轮廓)
//   area < 0 : CW  (顺时针, hole)
// ---------------------------------------------------------------------------
static float path_signed_area(const glm::vec2* pts, int n) {
	float area = 0.0f;
	for (int i = 0; i < n; ++i) {
		const glm::vec2& p1 = pts[i];
		const glm::vec2& p2 = pts[(i + 1) % n];
		area += (p2.x - p1.x) * (p2.y + p1.y);
	}
	return area * 0.5f; // CCW > 0
}

// ---------------------------------------------------------------------------
// 用 tess2 做 non-zero/odd fill 的主函数
// 替换原来的 fill_non_zero
// ---------------------------------------------------------------------------

#ifdef VG_FILL_NZ_GLUTESS2

// 注意：tess2 的 TESSalloc 若不提供 memrealloc，优先队列扩容会返回
// INV_HANDLE（内存不足哨兵）并在 sweep 中崩溃，因此这里必须用自带
// realloc 的默认分配器；常驻复用后其 bucket 缓存跨 fill 生效。

void rvg_cx::fill_non_zero_tess2(ovg_path_t* ctx)
{
	Vertex v{};
	v.color = ctx->color;
	v.uv = { 0, 0 };
	_curVertOffset = ctx->curVertOffset;
	/* ---------- 凸路径快速路径 ---------- */
	if (ctx->pathPtr == 1 && (ctx->pathes[0] & PATH_IS_CONVEX_BIT)) {
		uint32_t firstVertIdx = (uint32_t)(_vertex.size() - ctx->curVertOffset);
		uint32_t pathPointCount = ctx->pathes[0] & PATH_ELT_MASK;

		uint32_t i = 0;
		while (i < 2) {
			v.pos = ctx->points[i++];
			_vertex.push_back(v);
		}
		while (i < pathPointCount) {
			v.pos = ctx->points[i];
			_vertex.push_back(v);
			uint32_t inds[3] = {
				firstVertIdx,
				firstVertIdx + i - 1,
				firstVertIdx + i
			};
			_indices.push_back(inds[0]);
			_indices.push_back(inds[1]);
			_indices.push_back(inds[2]);
			++i;
		}
		return;
	}
	// 细分器常驻复用：bucket 缓存跨 fill 生效
	if (!_tess)
		_tess = tessNewTess(nullptr);
	TESStesselator* tess = _tess;
	if (!tess) return;
	uint32_t ptrPath = 0;
	uint32_t firstPtIdx = 0;
	while (ptrPath < ctx->pathPtr) {
		uint32_t pathPointCount = ctx->pathes[ptrPath] & PATH_ELT_MASK;
		if (pathPointCount > 2) {
			_tess_contour.clear();
			_tess_contour.reserve(pathPointCount * 2 + 2);
			for (uint32_t i = 0; i < pathPointCount; ++i) {
				const auto& p = ctx->points[firstPtIdx + i];
				_tess_contour.push_back(p.x);
				_tess_contour.push_back(p.y);
			}
			// 显式闭合（关键）
			if (pathPointCount < 3 ||
				(_tess_contour[0] != _tess_contour[(pathPointCount - 1) * 2 + 0] ||
					_tess_contour[1] != _tess_contour[(pathPointCount - 1) * 2 + 1])) {
				_tess_contour.push_back(_tess_contour[0]);
				_tess_contour.push_back(_tess_contour[1]);
			}
			tessAddContour(tess, 2, _tess_contour.data(), sizeof(TESSreal) * 2, (int)_tess_contour.size() / 2);
		}
		firstPtIdx += pathPointCount;
		if (o_path_has_curves(ctx->pathes.data(), ptrPath)) {
			ptrPath++;
			uint32_t totPts = 0;
			while (totPts < pathPointCount)
				totPts += (ctx->pathes[ptrPath++] & PATH_ELT_MASK);
		}
		else {
			ptrPath++;
		}
	}
	tessTesselate(tess, ctx->t->curFillRule ? TESS_WINDING_NONZERO : TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, nullptr);
	uint32_t base = (uint32_t)_vertex.size() - ctx->curVertOffset;
	const TESSreal* verts = tessGetVertices(tess);
	const TESSindex* elems = tessGetElements(tess);
	int nverts = tessGetVertexCount(tess);
	int nelems = tessGetElementCount(tess);
	for (int i = 0; i < nverts; ++i) {
		v.pos = { (float)verts[i * 2], (float)verts[i * 2 + 1] };
		_vertex.push_back(v);
	}
	for (int i = 0; i < nelems; ++i) {
		TESSindex a = elems[i * 3 + 0];
		TESSindex b = elems[i * 3 + 1];
		TESSindex c = elems[i * 3 + 2];
		if (a == TESS_UNDEF || b == TESS_UNDEF || c == TESS_UNDEF) continue;
		_indices.push_back(base + a);
		_indices.push_back(base + b);
		_indices.push_back(base + c);
	}
}
#endif

inline float ecp_zcross(rvg_cx::ear_clip_point* p0, rvg_cx::ear_clip_point* p1, rvg_cx::ear_clip_point* p2) {
	return vec2_zcross(vec2_sub(p1->pos, p0->pos), vec2_sub(p2->pos, p0->pos));
}

bool ptInTriangle(const glm::vec2& p, const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2) {
	float dX = p.x - p2.x;
	float dY = p.y - p2.y;
	float dX21 = p2.x - p1.x;
	float dY12 = p1.y - p2.y;
	float D = dY12 * (p0.x - p2.x) + dX21 * (p0.y - p2.y);
	float s = dY12 * dX + dX21 * dY;
	float t = (p2.y - p0.y) * dX + (p0.x - p2.x) * dY;
	if (D < 0)
		return (s <= 0) && (t <= 0) && (s + t >= D);
	return (s >= 0) && (t >= 0) && (s + t <= D);
}

// 保守判定路径是否必须走 tess2 交叉填充：
// - EVEN_ODD 且非"无曲线的单凸子路径"（后者两种规则等价，耳切即可）
// - 子路径 bbox 两两重叠（嵌套洞/相交轮廓，winding 语义必需）
// 自交单轮廓无法廉价预判，由 fill_non_zero 的耳切失败回退兜底。
bool rvg_cx::fill_needs_tess2(ovg_path_t* p)
{
	if (!p || !p->pathPtr) return false;
	const bool single_convex = (p->pathPtr == 1 && (p->pathes[0] & PATH_IS_CONVEX_BIT));
	if (p->t && p->t->curFillRule != VG_FILL_RULE_NON_ZERO && !single_convex)
		return true;
	// 一遍扫描各子路径 bbox（含曲线子路径的展平点）
	_fill_bbox.clear();
	uint32_t ptrPath = 0;
	uint32_t firstPtIdx = 0;
	while (ptrPath < p->pathPtr) {
		uint32_t n = p->pathes[ptrPath] & PATH_ELT_MASK;
		if (n > 2) {
			glm::vec4 bb(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (uint32_t i = 0; i < n; ++i) {
				const auto& pt = p->points[firstPtIdx + i];
				bb.x = std::min(bb.x, pt.x); bb.y = std::min(bb.y, pt.y);
				bb.z = std::max(bb.z, pt.x); bb.w = std::max(bb.w, pt.y);
			}
			_fill_bbox.push_back(bb);
		}
		firstPtIdx += n;
		if (o_path_has_curves(p->pathes.data(), ptrPath)) {
			ptrPath++;
			uint32_t totPts = 0;
			while (totPts < n)
				totPts += (p->pathes[ptrPath++] & PATH_ELT_MASK);
		}
		else
			ptrPath++;
	}
	if (_fill_bbox.size() < 2)
		return false;
	// 按 minX 排序后只测 x 区间有交集的对：平铺/网格类路径内层循环
	// 只覆盖同列元素，整体近似线性；嵌套/交叠路径会命中并提前返回
	std::sort(_fill_bbox.begin(), _fill_bbox.end(),
		[](const glm::vec4& a, const glm::vec4& b) { return a.x < b.x; });
	for (size_t i = 0; i < _fill_bbox.size(); ++i) {
		const auto& a = _fill_bbox[i];
		for (size_t j = i + 1; j < _fill_bbox.size() && _fill_bbox[j].x < a.z; ++j) {
			const auto& b = _fill_bbox[j];
			if (b.z > a.x && a.w > b.y && b.w > a.y)
				return true;
		}
	}
	return false;
}

// 默认填充：单轮廓简单多边形 / 互不相交子路径走耳切快速路径；
// 交叉填充语义（EVEN_ODD、嵌套/重叠/自交轮廓）自动路由到 tess2。
void rvg_cx::fill_non_zero(ovg_path_t* p)
{
	auto t = p->t;
	uint32_t color = t->color;
	p->color = color;
#ifdef VG_FILL_NZ_GLUTESS2
	if (p->enable_tess2 || fill_needs_tess2(p))
	{
		fill_non_zero_tess2(p);
		return;
	}
#endif
	uint32_t ptrPath = 0;
	uint32_t firstPtIdx = 0;
	const glm::vec3 uv = { 0,0,-1 };
	Vertex v = {}; v.color = color; v.uv = { 0, 0 };
	uint32_t cur_idx = _vertex.size() - p->curVertOffset;
	auto pcolor = p->colors.data();
	auto pcn = p->colors.size();
	if (p->colors.empty())pcolor = 0;
#ifdef VG_FILL_NZ_GLUTESS2
	// 耳切失败（自交/退化轮廓）时回滚本次几何，整条路径重走 tess2
	const size_t vtx_mark = _vertex.size();
	const size_t idx_mark = _indices.size();
	bool ear_clip_failed = false;
#endif
	while (ptrPath < p->pathPtr) {
		uint32_t pathPointCount = p->pathes[ptrPath] & PATH_ELT_MASK;
		auto col = pcolor && ptrPath < pcn ? pcolor[ptrPath] : color;
		v.color = col;
		if (pathPointCount > 2) {
			uint32_t firstVertIdx = (uint32_t)cur_idx;
			ecpsd.resize(pathPointCount);
			auto ecps = ecpsd.data();
			if (!ecps)break;
			uint32_t            ecps_count = pathPointCount;
			uint32_t i = 0;
			auto points = p->points.data() + firstPtIdx;
			while (i < pathPointCount - 1) {
				v.pos = points[i];
				ear_clip_point ecp = { v.pos, firstVertIdx + i, &ecps[i + 1] };
				ecps[i] = ecp;
				_vertex.push_back(v);
				i++;
			}
			v.pos = points[i];
			ear_clip_point ecp = { v.pos, firstVertIdx + i, ecps };
			ecps[i] = ecp;
			_vertex.push_back(v);
			ear_clip_point* ecp_current = ecps;
			uint32_t        tries = 0;
			while (ecps_count > 3) {
				if (tries > ecps_count) {
#ifdef VG_FILL_NZ_GLUTESS2
					ear_clip_failed = true;
#endif
					break;
				}
				ear_clip_point* v0 = ecp_current->next, * v1 = ecp_current, * v2 = ecp_current->next->next;
				if (ecp_zcross(v0, v2, v1) < 0) {
					ecp_current = ecp_current->next;
					tries++;
					continue;
				}
				ear_clip_point* vP = v2->next;
				bool            isEar = true;
				while (vP != v1) {
					if (ptInTriangle(vP->pos, v0->pos, v2->pos, v1->pos)) {
						isEar = false;
						break;
					}
					vP = vP->next;
				}
				if (isEar) {
					uint32_t t3[3] = { v0->idx, v1->idx, v2->idx };
					_indices.insert(_indices.end(), t3, t3 + 3);
					v1->next = v2;
					ecps_count--;
					tries = 0;
				}
				else {
					ecp_current = ecp_current->next;
					tries++;
				}
			}
			if (ecps_count == 3)
			{
				uint32_t t3[3] = { ecp_current->next->idx, ecp_current->idx, ecp_current->next->next->idx };
				_indices.insert(_indices.end(), t3, t3 + 3);
			}
			cur_idx += pathPointCount;
		}
		firstPtIdx += pathPointCount;
		if (o_path_has_curves(p->pathes.data(), ptrPath)) {
			// skip segments lengths used in stroke
			ptrPath++;
			uint32_t totPts = 0;
			while (totPts < pathPointCount)
				totPts += (p->pathes[ptrPath++] & PATH_ELT_MASK);
		}
		else
			ptrPath++;
	}
#ifdef VG_FILL_NZ_GLUTESS2
	if (ear_clip_failed) {
		_vertex.resize(vtx_mark);
		_indices.resize(idx_mark);
		fill_non_zero_tess2(p);
	}
#endif
}



void rvg_cx::_add_triangle_indices(ovg_path_t* ctx, uint32_t i0, uint32_t i1, uint32_t i2) {
	_indices.push_back(i0);
	_indices.push_back(i1);
	_indices.push_back(i2);
}
void rvg_cx::_add_tri_indices_for_rect(uint32_t i) {
	_indices.resize(_indices.size() + 6);
	uint32_t* inds = _indices.data() + _indices.size() - 6;
	inds[0] = i;
	inds[1] = i + 2;
	inds[2] = i + 1;
	inds[3] = i + 1;
	inds[4] = i + 2;
	inds[5] = i + 3;
}
void rvg_cx::_add_vertexf(ovg_path_t* ctx, float x, float y) {
	Vertex v = {};
	v.pos = { x,y };
	v.color = ctx->color;
	_vertex.push_back(v);
}
void rvg_cx::cp_cmdt(vgcmd_t* c, vg_state_save_t* t)
{
	c->state = (vg_state_save_t*)mac.allocate(sizeof(vg_state_save_t) * 1);
	if (!c->state)return;
	*c->state = *t;
	if (t->dashes && t->dashCount > 0) {
		c->state->dashes = (float*)mac.allocate(sizeof(float) * t->dashCount);
		if (c->state->dashes)
			memcpy(c->state->dashes, t->dashes, sizeof(float) * t->dashCount);
		else
			c->state->dashCount = 0;
	}
	if (t->pattern) {
		auto pat = (pat_act*)mac.allocate(sizeof(pat_act));
		memcpy(pat, t->pattern, sizeof(pat_act));
		pat->data = &pat->g;
		c->state->pattern = pat;
	}
}
bool rvg_cx::_build_vb_step(ovg_path_t* ctx, stroke_context_t* str, bool isCurve) {
	Vertex v = {};
	v.color = ctx->color; v.uv = {};
	glm::vec2   p0 = ctx->points[str->cp];
	glm::vec2   v0 = p0 - ctx->points[str->iL];
	glm::vec2   v1 = ctx->points[str->iR] - p0;
	float  length_v0 = glm::length(v0);
	float  length_v1 = glm::length(v1);
	if (length_v0 < FLT_EPSILON || length_v1 < FLT_EPSILON) {
		return false;
	}
	glm::vec2  v0n = (v0 / length_v0);
	glm::vec2  v1n = (v1 / length_v1);
	float dot = glm::dot(v0n, v1n);
	float det = v0n.x * v1n.y - v0n.y * v1n.x;
	if (EQUF(dot, 1.0f)) { // colinear

		return false;
	}
	if (EQUF(dot, -1.0f)) { // cusp (could draw line butt?)
		glm::vec2 vPerp = (vec2_perp(v0n) * str->hw);
		uint32_t idx = (uint32_t)(_vertex.size() - ctx->curVertOffset);
		v.pos = (p0 + vPerp);
		_vertex.push_back(v);
		v.pos = (p0 - vPerp);
		_vertex.push_back(v);
		_add_triangle_indices(ctx, idx, idx + 1, idx + 2);
		_add_triangle_indices(ctx, idx, idx + 2, idx + 3);
		return true;
	}
	glm::vec2  bisec_n = glm::normalize(v0n + v1n); // bisec/bisec_perp are inverted names
	float alpha = acosf(dot);

	if (det < 0)
		alpha = -alpha;
	float halfAlpha = alpha / 2.f;
	float cosHalfAlpha = cosf(halfAlpha);
	float lh = str->hw / cosHalfAlpha;
	glm::vec2  bisec_n_perp = vec2_perp(bisec_n);
	// limit bisectrice length
	float rlh = lh; // rlh is for inside pos tweeks
	if (dot < 0.f)
		rlh = fminf(rlh, fminf(length_v0, length_v1));
	//---

	glm::vec2 bisec = (bisec_n_perp * rlh);

	uint32_t idx = (uint32_t)(_vertex.size() - ctx->curVertOffset);

	glm::vec2 rlh_inside_pos, rlh_outside_pos;
	if (rlh < lh) {
		glm::vec2 vnPerp;
		if (length_v0 < length_v1)
			vnPerp = vec2_perp(v1n);
		else
			vnPerp = vec2_perp(v0n);
		glm::vec2 vHwPerp = (vnPerp * str->hw);

		double lbc = cosHalfAlpha * rlh;
		if (det < 0.f) {
			rlh_inside_pos = ((vnPerp * glm::vec2(-lbc) + (p0 + bisec)) + vHwPerp);
			rlh_outside_pos = (p0 - (bisec_n_perp * lh));
		}
		else {
			rlh_inside_pos = vec2_sub(vec2_add(vec2_mult_s(vnPerp, lbc), vec2_sub(p0, bisec)), vHwPerp);
			rlh_outside_pos = vec2_add(p0, vec2_mult_s(bisec_n_perp, lh));
		}
	}
	else {
		if (det < 0.0) {
			rlh_inside_pos = vec2_add(p0, bisec);
			rlh_outside_pos = vec2_sub(p0, bisec);
		}
		else {
			rlh_inside_pos = vec2_sub(p0, bisec);
			rlh_outside_pos = vec2_add(p0, bisec);
		}
	}

	auto join = (vg_line_join_t)ctx->t->lineJoin;

	if (isCurve) {
		if (dot < 0.8f)
			join = VG_LINE_JOIN_ROUND;
		else
			join = VG_LINE_JOIN_MITER;
	}

	if (join == VG_LINE_JOIN_MITER) {
		if (lh > str->lhMax) { // miter limit
			double x = (lh - str->lhMax) * cosHalfAlpha;
			glm::vec2   bisecPerp = vec2_mult_s(bisec_n, x);
			bisec = vec2_mult_s(bisec_n_perp, str->lhMax);
			if (det < 0) {
				v.pos = rlh_inside_pos;
				_vertex.push_back(v);

				glm::vec2 p = vec2_sub(p0, bisec);

				v.pos = vec2_sub(p, bisecPerp);
				_vertex.push_back(v);
				v.pos = vec2_add(p, bisecPerp);
				_vertex.push_back(v);

				_add_triangle_indices(ctx, idx, idx + 2, idx + 1);
				_add_triangle_indices(ctx, idx + 2, idx + 4, idx);
				_add_triangle_indices(ctx, idx, idx + 3, idx + 4);
				return true;
			}
			else {
				glm::vec2 p = vec2_add(p0, bisec);
				v.pos = vec2_sub(p, bisecPerp);
				_vertex.push_back(v);

				v.pos = rlh_inside_pos;
				_vertex.push_back(v);

				v.pos = vec2_add(p, bisecPerp);
				_vertex.push_back(v);

				_add_triangle_indices(ctx, idx, idx + 2, idx + 1);
				_add_triangle_indices(ctx, idx + 2, idx + 3, idx + 1);
				_add_triangle_indices(ctx, idx + 1, idx + 3, idx + 4);
				return false;
			}

		}
		else { // normal miter
			if (det < 0) {
				v.pos = rlh_inside_pos;
				_vertex.push_back(v);
				v.pos = rlh_outside_pos;
				_vertex.push_back(v);
			}
			else {
				v.pos = rlh_outside_pos;
				_vertex.push_back(v);
				v.pos = rlh_inside_pos;
				_vertex.push_back(v);
			}

			_add_tri_indices_for_rect(idx);
			return false;
		}
	}
	else {
		glm::vec2 vp = vec2_perp(v0n);

		if (det < 0) {
			if (dot < 0 && rlh < lh)
				v.pos = rlh_inside_pos;
			else
				v.pos = vec2_add(p0, bisec);
			_vertex.push_back(v);
			v.pos = vec2_sub(p0, vec2_mult_s(vp, str->hw));
		}
		else {
			v.pos = vec2_add(p0, vec2_mult_s(vp, str->hw));
			_vertex.push_back(v);
			if (dot < 0 && rlh < lh)
				v.pos = rlh_inside_pos;
			else
				v.pos = vec2_sub(p0, bisec);
		}
		_vertex.push_back(v);

		if (join == VG_LINE_JOIN_BEVEL) {
			if (det < 0) {
				_add_triangle_indices(ctx, idx, idx + 2, idx + 1);
				_add_triangle_indices(ctx, idx + 2, idx + 4, idx + 0);
				_add_triangle_indices(ctx, idx, idx + 3, idx + 4);
			}
			else {
				_add_triangle_indices(ctx, idx, idx + 2, idx + 1);
				_add_triangle_indices(ctx, idx + 2, idx + 3, idx + 1);
				_add_triangle_indices(ctx, idx + 1, idx + 3, idx + 4);
			}
		}
		else if (join == VG_LINE_JOIN_ROUND) {
			if (!str->arcStep)
				str->arcStep = _get_arc_step(ctx, str->hw);
			float a = acosf(vp.x);
			if (vp.y < 0)
				a = -a;

			if (det < 0) {
				a += M_PI;
				float a1 = a + alpha;
				a -= str->arcStep;
				while (a > a1) {
					_add_vertexf(ctx, cosf(a) * str->hw + p0.x, sinf(a) * str->hw + p0.y);
					a -= str->arcStep;
				}
			}
			else {
				float a1 = a + alpha;
				a += str->arcStep;
				while (a < a1) {
					_add_vertexf(ctx, cosf(a) * str->hw + p0.x, sinf(a) * str->hw + p0.y);
					a += str->arcStep;
				}
			}
			uint32_t p0Idx = (uint32_t)(_vertex.size() - ctx->curVertOffset);
			_add_triangle_indices(ctx, idx, idx + 2, idx + 1);
			if (det < 0) {
				for (uint32_t p = idx + 2; p < p0Idx; p++)
					_add_triangle_indices(ctx, p, p + 1, idx);
				_add_triangle_indices(ctx, p0Idx, p0Idx + 2, idx);
				_add_triangle_indices(ctx, idx, p0Idx + 1, p0Idx + 2);
			}
			else {
				for (uint32_t p = idx + 2; p < p0Idx; p++)
					_add_triangle_indices(ctx, p, p + 1, idx + 1);
				_add_triangle_indices(ctx, p0Idx, p0Idx + 1, idx + 1);
				_add_triangle_indices(ctx, idx + 1, p0Idx + 1, p0Idx + 2);
			}
		}

		vp = vec2_mult_s(vec2_perp(v1n), str->hw);
		if (det < 0)
			v.pos = vec2_sub(p0, vp);
		else
			v.pos = vec2_add(p0, vp);
		_vertex.push_back(v);
	}

	return (det < 0);
}

void rvg_cx::_draw_stoke_cap(ovg_path_t* ctx, stroke_context_t* str, glm::vec2 p0, glm::vec2 n, bool isStart) {
	Vertex v = {}; v.color = ctx->color; v.uv = {};

	uint32_t firstIdx = (uint32_t)(_vertex.size() - ctx->curVertOffset);

	if (isStart) {
		glm::vec2 vhw = vec2_mult_s(n, str->hw);

		if (ctx->t->lineCap == VG_LINE_CAP_SQUARE)
			p0 = vec2_sub(p0, vhw);

		vhw = vec2_perp(vhw);

		if (ctx->t->lineCap == VG_LINE_CAP_ROUND) {
			if (!str->arcStep)
				str->arcStep = _get_arc_step(ctx, str->hw);

			float a = acosf(n.x) + M_PI_2;
			if (n.y < 0)
				a = M_PI - a;
			float a1 = a + M_PI;

			a += str->arcStep;
			while (a < a1) {
				_add_vertexf(ctx, cosf(a) * str->hw + p0.x, sinf(a) * str->hw + p0.y);
				a += str->arcStep;
			}
			uint32_t p0Idx = (uint32_t)(_vertex.size() - ctx->curVertOffset);
			for (uint32_t p = firstIdx; p < p0Idx; p++)
				_add_triangle_indices(ctx, p0Idx + 1, p, p + 1);
			firstIdx = p0Idx;
		}

		v.pos = vec2_add(p0, vhw);
		_vertex.push_back(v);
		v.pos = vec2_sub(p0, vhw);
		_vertex.push_back(v);

		_add_tri_indices_for_rect(firstIdx);
	}
	else {
		glm::vec2 vhw = vec2_mult_s(n, str->hw);

		if (ctx->t->lineCap == VG_LINE_CAP_SQUARE)
			p0 = vec2_add(p0, vhw);

		vhw = vec2_perp(vhw);

		v.pos = vec2_add(p0, vhw);
		_vertex.push_back(v);
		v.pos = vec2_sub(p0, vhw);
		_vertex.push_back(v);

		firstIdx = (uint32_t)(_vertex.size() - ctx->curVertOffset);

		if (ctx->t->lineCap == VG_LINE_CAP_ROUND) {
			if (!str->arcStep)
				str->arcStep = _get_arc_step(ctx, str->hw);

			float a = acosf(n.x) + M_PI_2;
			if (n.y < 0)
				a = M_PI - a;
			float a1 = a - M_PI;

			a -= str->arcStep;
			while (a > a1) {
				_add_vertexf(ctx, cosf(a) * str->hw + p0.x, sinf(a) * str->hw + p0.y);
				a -= str->arcStep;
			}

			uint32_t p0Idx = (uint32_t)(_vertex.size() - ctx->curVertOffset - 1);
			for (uint32_t p = firstIdx - 1; p < p0Idx; p++)
				_add_triangle_indices(ctx, p + 1, p, firstIdx - 2);
		}
	}
}
float rvg_cx::_draw_dashed_segment(ovg_path_t* ctx, stroke_context_t* str, dash_context_t* dc, bool isCurve) {
	// vec2 pL = ctx->points[str->iL];
	glm::vec2 p = ctx->points[str->cp];
	glm::vec2 pR = ctx->points[str->iR];

	if (!dc->dashOn) // we test in fact the next dash start, if dashOn = true => next segment is a void.
		_build_vb_step(ctx, str, isCurve);

	glm::vec2 d = vec2_sub(pR, p);
	dc->normal = vec2_norm(d);
	float segmentLength = glm::length(d);

	uint64_t iter = 0;
	while (dc->curDashOffset < segmentLength) {
		if (++iter > 100000) {
			std::fprintf(stderr, "DASH LOOP offset=%f segment=%f count=%u cur=%u dash0=%f dash1=%f\n",
				dc->curDashOffset, segmentLength, ctx->t->dashCount, dc->curDash,
				ctx->t->dashCount > 0 ? ctx->t->dashes[0] : 0.0f,
				ctx->t->dashCount > 1 ? ctx->t->dashes[1] : 0.0f);
			break;
		}
		glm::vec2 p0 = vec2_add(p, vec2_mult_s(dc->normal, dc->curDashOffset));

		_draw_stoke_cap(ctx, str, p0, dc->normal, dc->dashOn);
		dc->dashOn ^= true;
		dc->curDashOffset += ctx->t->dashes[dc->curDash];
		if (++dc->curDash == ctx->t->dashCount)
			dc->curDash = 0;
	}
	dc->curDashOffset -= segmentLength;
	dc->curDashOffset = fmodf(dc->curDashOffset, dc->totDashLength);
	return segmentLength;
}
void rvg_cx::_draw_segment(ovg_path_t* ctx, stroke_context_t* str, dash_context_t* dc, bool isCurve) {
	str->iR = str->cp + 1;
	if (ctx->t->dashCount > 0)
		_draw_dashed_segment(ctx, str, dc, isCurve);
	else
		_build_vb_step(ctx, str, isCurve);
	str->iL = str->cp++;
}

void rvg_cx::image_update(vg_image_t* img, vg_image_desc_t* desc)
{
	if (!desc || !desc->width || !desc->height)return;
	if (!img)img = desc->img;
	auto& dst = _images[img]; dst = *desc;
	img->width = desc->width;
	img->height = desc->height;
	dst.px_size = dst.height * dst.stride;
	dst.dirty = true;   // 标记本帧需要上传到 GPU
	if (desc->is_copy && (dst.px_size > 0)) {
		dst.pixels = ac->new_mem(dst.px_size);
		memcpy((void*)dst.pixels, desc->pixels, dst.px_size);
	}
}

void rvg_cx::image_destroy(vg_image_t* img)
{
	if (img)
		_images[img].is_destroy = true;
}


// todo 渲染操作，rvg_cx可以多次执行fill或stroke/clip
rvg_t* ovg_new_rvg(mem_resource_t* ac0)
{
	auto ac = ac0 ? (usp_ac_cx*)ac0 : vg_default_ac();
	auto p = ac->new_obj<rvg_cx>(ac);
	if (p)
		p->ac = ac;
	return p;
}
void ovg_destroy_rvg(rvg_t* p0) {
	auto p = (rvg_cx*)p0;
	if (p && p->ac) {
		p->ac->free_obj(p);
	}
}
void ovg_clear(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (v)v->clear_all();
}
void ovg_set_path(rvg_t* v0, ovg_path_t* path, vg_state_save_t* st)
{
	auto v = (rvg_cx*)v0;
	if (!v)return;
	v->set_path(path, st);
}
void ovg_reset_clip(rvg_t* v0, uint8_t ref)
{
	auto v = (rvg_cx*)v0;
	if (v)v->clip0(ref);
}
void ovg_clip(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (v)v->clip();
}
void ovg_clip_preserve(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (v)v->clip_preserve();
}
void ovg_clip_rect(rvg_t* v0, int x, int y, int width, int height)
{
	auto v = (rvg_cx*)v0;
	glm::ivec4 c[1] = { { x,y,width,height } };
	if (v)v->clip(c);
}
void ovg_set_clip_rect(rvg_t* v0, void* rc) {
	auto v = (rvg_cx*)v0;
	if (v && rc) {
		if (v)v->clip((glm::ivec4*)rc);
	}
}
void ovg_get_clip_rect(rvg_t* v0, void* rc) {
	auto v = (rvg_cx*)v0;
	if (v && rc) {
		*((glm::ivec4*)rc) = v->curClip;
	}
}
void ovg_stroke(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (!v)return;
	v->stroke_preserve();
	ovg_clear_path(v->path);
}
void ovg_stroke_preserve(rvg_t* v0) {
	auto v = (rvg_cx*)v0;
	if (!v)return;
	v->stroke_preserve();
}
void ovg_fill(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (!v)return;
	v->fill_preserve();
	ovg_clear_path(v->path);
}
void ovg_fill_preserve(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (!v)return;
	v->fill_preserve();
}
void ovg_paint(rvg_t* v0)
{
	auto v = (rvg_cx*)v0;
	if (v)v->paint();
}

// 添加文本，风格，渲染区可选
void  ovg_add_text(rvg_t* v0, text_st_t* p, text_style_t* ts, text_box_rt* box)
{
	auto dc = (rvg_cx*)v0;
	if (dc) {
		auto cb = (ovg_canvas_cx*)dc->ac->ptr;
		if (cb)
			vg_add_text(cb, 0, v0, p, ts, box);
	}
}
// 普通图片，支持九宫格、混合颜色
void  ovg_add_image(rvg_t* v0, ovg_image_r* r)
{
	auto dc = (rvg_cx*)v0;
	if (dc) {
		dc->gps.set_viewport({ 0,0,dc->width,dc->height });
		dc->gps.add_image(r);
	}
}
// 原生纹理，支持源裁剪、tint、翻转、九宫格、旋转、矩阵，按当前 clip 裁剪
void ovg_add_texture(rvg_t* v0, ovg_texture_r* r)
{
	auto dc = (rvg_cx*)v0;
	if (dc && r && r->tex) {
		dc->gps.set_viewport({ 0,0,dc->width,dc->height });
		dc->gps.add_texture(r);
	}
}
// 原始三角形，输入0则不修改
void  ovg_set_geom_state(rvg_t* v0, gem_info_t* info, const glm::mat4* matrix)
{
	auto dc = (rvg_cx*)v0;
	if (dc)dc->gps.set_state(info, matrix);
}
size_t ovg_set_instance_mat(rvg_t* v0, const glm::mat4* matrix, size_t count)
{
	auto dc = (rvg_cx*)v0;
	return (dc) ? dc->gps.set_instance_mat(matrix, count) : 0;
}
// 添加几何数据到缓冲区，xy顶点坐标，color顶点颜色，uv顶点纹理坐标，indices索引数据，color_type=0表示float4，1表示uint32_t
void  ovg_add_geometry(rvg_t* v0, vg_image_t* texture, const float* xy, int xy_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type)
{
	auto dc = (rvg_cx*)v0;
	if (dc)dc->gps.add_geometry(texture, xy, xy_stride, color, color_stride, uv, uv_stride, num_vertices, indices, num_indices, size_indices, color_type);
}
// 添加3D几何数据到缓冲区，xyz顶点坐标，color顶点颜色（双面则要双倍），uv顶点纹理坐标，indices索引数据
void  ovg_add_geometry3d(rvg_t* v0, vg_image_t* texture, const float* xyz, int xyz_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type)
{
	auto dc = (rvg_cx*)v0;
	if (dc)dc->gps.add_geometry3d(texture, xyz, xyz_stride, color, color_stride, uv, uv_stride, num_vertices, indices, num_indices, size_indices, color_type);
}

void ovg_image_update(rvg_t* p, vg_image_t* img, vg_image_desc_t* desc) {
	if (!p || !desc)return;
	auto dc = (rvg_cx*)p;
	dc->image_update(img, desc);
}
//标记图片不再使用（后端延迟释放 GPU 纹理）
void ovg_image_destroy(rvg_t* p, vg_image_t* img) {
	if (!p || !img)return;
	auto dc = (rvg_cx*)p;
	dc->image_destroy(img);
}
#endif // 1

// todo init cb
void init_ovg_cb(ovg_canvas_cb* cb) {
	if (!cb)return;
	cb->set_enable_tess2 = ovg_set_enable_tess2;
	cb->new_path = ovg_new_path;		// 可自定义分配
	cb->destroy_path = ovg_destroy_path;
	cb->clear_path = ovg_clear_path;
	cb->close_path = ovg_close_path;
	cb->new_sub_path = ovg_new_sub_path;
	cb->path_extents = ovg_path_extents;
	cb->get_current_point = ovg_get_current_point;
	cb->get_segment_count = ovg_get_segment_count;
	cb->set_segment_color = ovg_set_segment_color;
	cb->add_path = ovg_add_path;
	//cb->add_path0 = ovg_add_path0;
	cb->move_to = ovg_move_to;
	cb->rel_move_to = ovg_rel_move_to;
	cb->line_to = ovg_line_to;
	cb->rel_line_to = ovg_rel_line_to;
	cb->arc = ovg_arc;
	cb->arc_negative = ovg_arc_negative;
	cb->curve_to = ovg_curve_to;
	cb->rel_curve_to = ovg_rel_curve_to;
	cb->quadratic_to = ovg_quadratic_to;
	cb->rel_quadratic_to = ovg_rel_quadratic_to;
	cb->rectangle = ovg_rectangle;
	cb->rounded_rectangle = ovg_rounded_rectangle;
	cb->rounded_rectangle2 = ovg_rounded_rectangle2;
	cb->ellipse = ovg_ellipse;
	cb->elliptic_arc_to = ovg_elliptic_arc_to;
	cb->rel_elliptic_arc_to = ovg_rel_elliptic_arc_to;
	cb->circle = ovg_circle;

	cb->new_state = ovg_new_state;
	cb->state_destroy = ovg_state_destroy;
	cb->set_opacity = ovg_set_opacity;
	cb->set_effect = ovg_set_effect;
	cb->set_source_color = ovg_set_source_color;
	cb->set_source_rgba = ovg_set_source_rgba;
	cb->set_source_rgb = ovg_set_source_rgb;
	cb->set_line_width = ovg_set_line_width;
	cb->set_miter_limit = ovg_set_miter_limit;
	cb->set_line_cap = ovg_set_line_cap;
	cb->set_line_join = ovg_set_line_join;
	cb->set_source_surface = ovg_set_source_surface;
	cb->set_source = ovg_set_source;
	cb->set_operator = ovg_set_operator;
	cb->set_fill_rule = ovg_set_fill_rule;
	cb->set_dash = ovg_set_dash;
	cb->set_dash8 = ovg_set_dash8;
	cb->translate = ovg_translate;
	cb->scale = ovg_scale;
	cb->rotate = ovg_rotate;
	cb->transform = ovg_transform;
	cb->set_matrix = ovg_set_matrix;
	cb->get_matrix = ovg_get_matrix;
	cb->identity_matrix = ovg_identity_matrix;
	cb->matrix_init = ovg_matrix_init;

	cb->new_pattern_linear = ovg_new_pattern_linear;
	cb->new_pattern_radial = ovg_new_pattern_radial;
	cb->new_pattern_sweep = ovg_new_pattern_sweep;
	cb->pattern_add_color_stop = ovg_pattern_add_color_stop;
	cb->pattern_set_color_stop = ovg_pattern_set_color_stop;
	cb->pattern_set_matrix = ovg_pattern_set_matrix;
	cb->pattern_set_extend = ovg_pattern_set_extend;
	cb->pattern_set_filter = ovg_pattern_set_filter;
	cb->pattern_destroy = ovg_pattern_destroy;

	cb->image_update = ovg_image_update;
	cb->image_destroy = ovg_image_destroy;
	// 渲染操作，rvg_cx可以多次执行fill或stroke/clip
	cb->new_rvg = ovg_new_rvg;
	cb->destroy_rvg = ovg_destroy_rvg;
	cb->set_path = ovg_set_path;
	cb->stroke = ovg_stroke;
	cb->stroke_preserve = ovg_stroke_preserve;
	cb->fill = ovg_fill;
	cb->fill_preserve = ovg_fill_preserve;
	cb->paint = ovg_paint;
	cb->clear = ovg_clear;
	cb->reset_clip = ovg_reset_clip;
	cb->clip = ovg_clip;
	cb->clip_preserve = ovg_clip_preserve;
	cb->clip_rect = ovg_clip_rect;
	cb->set_clip_rect = ovg_set_clip_rect;
	cb->get_clip_rect = ovg_get_clip_rect;
	cb->add_text = ovg_add_text;
	cb->add_image = ovg_add_image;
	cb->add_texture = ovg_add_texture;
	cb->set_geom_state = (void (*)(rvg_t*, gem_info_t*, const void*)) ovg_set_geom_state;
	cb->set_instance_mat = (size_t(*)(rvg_t*, const void*, size_t)) ovg_set_instance_mat;
	cb->add_geometry = ovg_add_geometry;
	cb->add_geometry3d = ovg_add_geometry3d;

}

#endif // 1


geom_primitive::geom_primitive(vg_alloc_cx* a)
	: vd1(a), vd2(a), ids(a), instance_mat(a), build_pipes(a)
{}

geom_primitive::~geom_primitive()
{
	clear();
}

void geom_primitive::clear()
{
	vd1.clear();
	vd2.clear();
	ids.clear();
	instance_mat.clear();
	build_pipes.clear();
	mat = glm::mat4(1.0f);
	curState = {};
	inst_idx = 0;
	inst_count = 0;
}

void geom_primitive::set_state(gem_info_t* info, const glm::mat4* matrix)
{
	if (info) {
		curState = *info;
		build_pipes.push_back(curState);
	}
	if (matrix) { mat = *matrix; }
}

size_t geom_primitive::set_instance_mat(const glm::mat4* matrix, size_t count)
{
	auto ps = instance_mat.size();
	if (matrix && count > 0)
	{
		instance_mat.resize(ps + count);
		memcpy(instance_mat.data() + ps, matrix, count * sizeof(glm::mat4));
		inst_idx = ps;
	}
	inst_count = count;
	return ps;
}
float get_has_multiply(const gem_info_t& state) {
	return ((blendMode_e)state.blendMode == blendMode_e::additive_prem || (blendMode_e)state.blendMode == blendMode_e::normal_prem) ? 1.0f : 0.0f;
}
bool geom_primitive::add_geometry(void* texture, const float* xy, int xy_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type)
{
	if (!xy || num_vertices < 1)return false;
	geom_cmd_t c = { .stype = 1 };
	c.state = curState;
	c.texture = texture;
	c.mat = mat;
	c.multiply = get_has_multiply(c.state);
	c.instance_count = inst_count;
	if (inst_count > 0)
		c.instance_ssbo_pos = inst_idx;
	float scale_x = 1.0, scale_y = 1.0;
	float u_scale = 1.0, v_scale = 1.0;
	size_indices = indices ? size_indices : 0;
	ids.reserve(ids.size() + num_indices);
	c.firstIndex = ids.size();
	c.elemCount = num_indices;
	if (num_indices < 1 || size_indices < 1)
	{
		c.elemCount = num_vertices;
	}
	if (curState.shader == ST_DOUBLESIDED) {
		c.vertexOffset = vd2.size();
		c.v_offset = 1;
		vd2.resize(vd2.size() + num_vertices);
		auto mem = vd2.data() + c.vertexOffset;	// 双面顶点
		auto verts = mem;
		for (size_t i = 0; i < num_indices; i++) {
			int j;
			float* xy_;
			if (size_indices == 4) {
				j = ((const uint32_t*)indices)[i];
			}
			else if (size_indices == 2) {
				j = ((const uint16_t*)indices)[i];
			}
			else if (size_indices == 1) {
				j = ((const uint8_t*)indices)[i];
			}
			else {
				j = i;
			}
			ids.push_back(j);
		}
		for (size_t i = 0; i < num_vertices; i++) {
			float* xy_;
			xy_ = (float*)((char*)xy + i * xy_stride);
			verts->pos.x = xy_[0] * scale_x;
			verts->pos.y = xy_[1] * scale_y;
			if (color_type == 1) {
				auto c8 = (uint32_t*)((char*)color + i * color_stride);
				verts->color = *c8; c8++;
				verts->color1 = *c8;
			}
			else
			{
				auto c4 = (glm::vec4*)((char*)color + i * color_stride);
				verts->color = CreateRgbaf(c4->x, c4->y, c4->z, c4->w); c4++;
				verts->color1 = CreateRgbaf(c4->x, c4->y, c4->z, c4->w);
			}
			if (texture && uv) {
				float* uv_ = (float*)((char*)uv + i * uv_stride);
				verts->uv.x = uv_[0] * u_scale;
				verts->uv.y = uv_[1] * v_scale;
			}
			else {
				verts->uv = { 0.0f, 0.0f };
			}
			verts += 1;
		}
	}
	else
	{
		c.vertexOffset = vd1.size();
		vd1.resize(vd1.size() + num_vertices);
		auto mem = vd1.data() + c.vertexOffset;	// 单面顶点
		auto verts = mem;
		for (size_t i = 0; i < num_indices; i++) {
			int j;
			float* xy_;
			if (size_indices == 4) {
				j = ((const uint32_t*)indices)[i];
			}
			else if (size_indices == 2) {
				j = ((const uint16_t*)indices)[i];
			}
			else if (size_indices == 1) {
				j = ((const uint8_t*)indices)[i];
			}
			else {
				j = i;
			}
			ids.push_back(j);
		}
		for (size_t i = 0; i < num_vertices; i++) {
			float* xy_;
			xy_ = (float*)((char*)xy + i * xy_stride);
			verts->pos.x = xy_[0] * scale_x;
			verts->pos.y = xy_[1] * scale_y;
			if (color_type == 1) {
				auto c8 = (uint32_t*)((char*)color + i * color_stride);
				verts->color = *c8; c8++;
			}
			else
			{
				auto c4 = (glm::vec4*)((char*)color + i * color_stride);
				verts->color = CreateRgbaf(c4->x, c4->y, c4->z, c4->w);
			}
			if (texture && uv) {
				float* uv_ = (float*)((char*)uv + i * uv_stride);
				verts->uv.x = uv_[0] * u_scale;
				verts->uv.y = uv_[1] * v_scale;
			}
			else {
				verts->uv = { 0.0f, 0.0f };
			}
			verts += 1;
		}
	}

	gt->push_back({ .g = c });
	return true;
}

bool geom_primitive::add_geometry3d(void* texture, const float* xyz, int xyz_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type)
{
	if (!xyz || num_vertices < 1)return false;
	geom_cmd_t c = { .stype = 1 };
	c.state = curState;
	c.texture = texture;
	c.mat = mat;
	c.multiply = get_has_multiply(c.state);
	c.instance_count = inst_count;
	if (inst_count > 0)
		c.instance_ssbo_pos = inst_idx;
	float scale_x = 1.0, scale_y = 1.0, scale_z = 1.0;
	float u_scale = 1.0, v_scale = 1.0;
	size_indices = indices ? size_indices : 0;
	ids.reserve(ids.size() + num_indices);
	c.firstIndex = ids.size();
	c.elemCount = num_indices;
	if (num_indices < 1 || size_indices < 1)
	{
		c.elemCount = num_vertices;
	}
	if (curState.shader == ST_DOUBLESIDED) {
		c.vertexOffset = vd2.size();
		c.v_offset = 1;
		vd2.resize(vd2.size() + num_vertices);
		auto mem = vd2.data() + c.vertexOffset;	// 双面顶点
		auto verts = mem;
		for (size_t i = 0; i < num_indices; i++) {
			int j;
			if (size_indices == 4) {
				j = ((const uint32_t*)indices)[i];
			}
			else if (size_indices == 2) {
				j = ((const uint16_t*)indices)[i];
			}
			else if (size_indices == 1) {
				j = ((const uint8_t*)indices)[i];
			}
			else {
				j = i;
			}
			ids.push_back(j);
		}
		for (size_t i = 0; i < num_vertices; i++) {
			float* xyz_;
			xyz_ = (float*)((char*)xyz + i * xyz_stride);
			verts->pos.x = xyz_[0] * scale_x;
			verts->pos.y = xyz_[1] * scale_y;
			verts->pos.z = xyz_[2] * scale_z;
			if (color_type == 1) {
				auto c8 = (uint32_t*)((char*)color + i * color_stride);
				verts->color = *c8; c8++;
				verts->color1 = *c8;
			}
			else
			{
				auto c4 = (glm::vec4*)((char*)color + i * color_stride);
				verts->color = CreateRgbaf(c4->x, c4->y, c4->z, c4->w); c4++;
				verts->color1 = CreateRgbaf(c4->x, c4->y, c4->z, c4->w);
			}
			if (texture && uv) {
				float* uv_ = (float*)((char*)uv + i * uv_stride);
				verts->uv.x = uv_[0] * u_scale;
				verts->uv.y = uv_[1] * v_scale;
			}
			else {
				verts->uv = { 0.0f, 0.0f };
			}
			verts += 1;
		}
	}
	else
	{
		c.vertexOffset = vd1.size();
		vd1.resize(vd1.size() + num_vertices);
		auto mem = vd1.data() + c.vertexOffset;	// 单面顶点
		auto verts = mem;
		for (size_t i = 0; i < num_indices; i++) {
			int j;
			if (size_indices == 4) {
				j = ((const uint32_t*)indices)[i];
			}
			else if (size_indices == 2) {
				j = ((const uint16_t*)indices)[i];
			}
			else if (size_indices == 1) {
				j = ((const uint8_t*)indices)[i];
			}
			else {
				j = i;
			}
			ids.push_back(j);
		}
		for (size_t i = 0; i < num_vertices; i++) {
			float* xyz_;
			xyz_ = (float*)((char*)xyz + i * xyz_stride);
			verts->pos.x = xyz_[0] * scale_x;
			verts->pos.y = xyz_[1] * scale_y;
			verts->pos.z = xyz_[2] * scale_z;
			if (color_type == 1) {
				auto c8 = (uint32_t*)((char*)color + i * color_stride);
				verts->color = *c8; c8++;
			}
			else
			{
				auto c4 = (glm::vec4*)((char*)color + i * color_stride);
				verts->color = CreateRgbaf(c4->x, c4->y, c4->z, c4->w);
			}
			if (texture && uv) {
				float* uv_ = (float*)((char*)uv + i * uv_stride);
				verts->uv.x = uv_[0] * u_scale;
				verts->uv.y = uv_[1] * v_scale;
			}
			else {
				verts->uv = { 0.0f, 0.0f };
			}
			verts += 1;
		}
	}
	gt->push_back({ .g = c });
	return true;
}

glm::mat4 ovg_ortho(float width, float height, float znear, float zfar, bool is_top)
{
	return is_top ? glm::ortho(0.0f, width, height, 0.0f, znear, zfar) : glm::ortho(0.0f, width, 0.0f, height, znear, zfar);
}
void draw_mesh2d_x(rvg_cx* ctx, geom_primitive* gp, const glm::vec2& render_scale)
{
	mesh2d_x* dc = gp;
	glm::vec2 clip_off = {};
	glm::vec2 clip_scale = render_scale;
	glm::ivec4 vp = { 0,0,-1,-1 };
	if (dc->viewport.z > 0 && dc->viewport.w > 0)
	{
		vp.x = dc->viewport.x;
		vp.y = dc->viewport.y;
		vp.z = dc->viewport.z;
		vp.w = dc->viewport.w;
	}
	auto av = dc;
	auto vd = av->vtxs.data();
	auto vdt = av->vtxs.data();
	auto idv = av->idxs.data();
	auto vbs = av->vtxs.size();
	auto ibs = av->idxs.size();
	std::vector<int> idxs;
	struct { void* texture; int blendMode; } states = {};
	glm::ivec4 oldclip = {};
	ovg_get_clip_rect(ctx, (int*)&oldclip);
	size_t cclip = 0;
	gem_info_t info = {};
	info.blendMode = (int8_t)blendMode_e::normal;
	info.topology = 0;
	//info.doubleSided = false;
	//info.depthTestEnable = false;
	//info.depthWriteEnable = false;
	//info.stencilTestEnable = true;
	info.flags = (uint8_t)depth_stencil_State::d_stenciltest_enable;
	info.frontFace = 0;
	info.cullMode = 0;
	glm::mat4 mat = ovg_ortho(dc->viewport.z, dc->viewport.w, -1.0f, 1.0f, 0);
	ovg_set_clip_rect(ctx, &vp);
	gp->set_state(&info, &mat);
	for (auto& pcmd : av->cmd_data)
	{
		glm::vec2 clip_min((pcmd.clip_rect.x - clip_off.x) * clip_scale.x, (pcmd.clip_rect.y - clip_off.y) * clip_scale.y);
		glm::vec2 clip_max((pcmd.clip_rect.z - clip_off.x) * clip_scale.x, (pcmd.clip_rect.w - clip_off.y) * clip_scale.y);
		if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
		{
			ovg_set_clip_rect(ctx, &vp); cclip++;
		}
		else
		{
			glm::ivec4 r = { (int)(clip_min.x), (int)(clip_min.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y) };
			ovg_set_clip_rect(ctx, &r); cclip++;
		}
		auto texture = pcmd.texid;
		auto vertices = vdt + pcmd.vtxOffset;
		const float* xy = &vertices->position.x;
		int stride = sizeof(mesh2d_x::vertex_t);
		auto color = &vertices->color;
		const float* uv = &vertices->tex_coord.x;
		int size_indices = 4;
		auto indices = ibs ? idv + pcmd.idxOffset : nullptr;
		auto num_indices = pcmd.elemCount;
		int8_t blend = pcmd.blend_mode;
		if (states.blendMode != blend) {
			states.blendMode = blend;
			info.blendMode = states.blendMode;
			gp->set_state(&info, &mat);
		}
		gp->add_geometry(texture, xy, stride, color, stride, uv, stride, pcmd.vCount, indices, num_indices, size_indices, 1);
	}
	dc->clear_m2d();
	if (cclip > 0) {
		ovg_set_clip_rect(ctx, &oldclip);
	}
}

void geom_primitive::add_image(ovg_image_r* r)
{
	if (!dc || !r || !r->img || (r->dst.z * r->dst.w <= 0) || (r->rc.z < 1 || r->rc.w < 1))return;

	// 与 add_texture 一致：按当前矩形裁剪生成 mesh，否则 CPU 图片会
	// 越过滚动面板等父级裁剪区域。
	glm::ivec4 clip = dc->curClip;
	if (clip.z <= 0 || clip.w <= 0)
		clip = {};
	set_clip(clip);
	if (nohas_clip(r->dst))
	{
		clear_m2d();
		return;
	}
	glm::ivec4 mesh_clip = {};
	if (clip.z > 0 && clip.w > 0)
		mesh_clip = { clip.x, clip.y, clip.x + clip.z, clip.y + clip.w };

	add_image0(r->img, { r->img->width,r->img->height }, mesh_clip, r->dst, r->rc, r->sliced, r->color);

	// 空 mesh 不能调用 draw_mesh2d_x：它会把 scissor 改成 viewport，
	// 且不会执行末尾的 oldclip 恢复。
	if (vtxs.empty())
	{
		clear_m2d();
		return;
	}
	draw_mesh2d_x(dc, this, { 1.0,1.0 });
}

void geom_primitive::add_texture(ovg_texture_r* r)
{
	if (!dc || !r || !r->tex || r->tex->image.id == 0)return;
	const int tw = r->tex->image.width;
	const int th = r->tex->image.height;
	if (tw <= 0 || th <= 0 || r->dst.z <= 0 || r->dst.w <= 0)return;

	// curClip 使用 {x,y,w,h}；draw_mesh2d_x 的 mesh 命令使用 {x,y,x1,y1}。
	glm::ivec4 clip = dc->curClip;
	if (clip.z <= 0 || clip.w <= 0)
		clip = {};
	set_clip(clip);
	if (nohas_clip(r->dst))
	{
		clear_m2d();
		return;
	}
	glm::ivec4 mesh_clip = {};
	if (clip.z > 0 && clip.w > 0)
		mesh_clip = { clip.x, clip.y, clip.x + clip.z, clip.y + clip.w };

	add_texture0(&r->tex->image, { tw, th }, mesh_clip, r->dst, r->rc, r->sliced,
		r->color, r->flip, r->angle, { r->center.x, r->center.y },
		r->center_set != 0, (const glm::mat3x2*)r->matrix);

	// 空 mesh 不能调用 draw_mesh2d_x：它会把 scissor 改成 viewport，
	// 且不会执行末尾的 oldclip 恢复。
	if (vtxs.empty())
	{
		clear_m2d();
		return;
	}
	draw_mesh2d_x(dc, this, { 1.0,1.0 });
}
#if 1

mesh2d_x::mesh2d_x()
{}

mesh2d_x::~mesh2d_x()
{}

void mesh2d_x::set_viewport(const glm::ivec4& vp)
{
	viewport = vp;
}

void mesh2d_x::set_clip(const glm::ivec4& rc)
{
	_clip_rect = rc;
}

void mesh2d_x::clear_m2d()
{
	vtxs.clear();
	idxs.clear();
	cmd_data.clear();
	_clip_rect = viewport;
	_clip_rect.x = _clip_rect.y = 0;
}

inline uint8_t is_rect_intersect0(int x01, int x02, int y01, int y02,
	int x11, int x12, int y11, int y12)
{
	int zx = abs(x01 + x02 - x11 - x12);
	int x = abs(x01 - x02) + abs(x11 - x12);
	int zy = abs(y01 + y02 - y11 - y12);
	int y = abs(y01 - y02) + abs(y11 - y12);
	if (zx <= x && zy <= y)
		return 1;
	else
		return 0;
}
inline bool is_rect_intersect(glm::vec4 r1, glm::vec4 r2)
{
	//第一种情况：如果b.x > a.x + a.w，则a和b一定不相交，
	//第二种情况：如果a.y > b.y + b.h，则a和b一定不相交，
	//第三种情况：如果b.y > a.y + a.h，则a和b一定不相交，
	//第四种情况：如果a.x > b.x + b.w，则a和b一定不相交
	auto& a = r1; auto& b = r2;
	if (a.x > b.x + b.z || b.x > a.x + a.z || a.y > b.y + b.w || b.y > a.y + a.w) {
		return false;
	}
	else {
		return true;
	}
	return is_rect_intersect0(r1.x, r1.y, r1.z, r1.w, r2.x, r2.y, r2.z, r2.w);
}
bool mesh2d_x::nohas_clip(const glm::ivec4& a)
{
	auto clip = _clip_rect;
	if (clip.z == 0 || clip.w == 0 || viewport.z == 0 || viewport.w == 0)return false;
	if (clip.z > viewport.z || clip.z < 0)clip.z = viewport.z;
	if (clip.w > viewport.w || clip.w < 0)clip.w = viewport.w;
	if (clip.z < 0 || clip.w < 0)
	{
		return false;
	}
	return (!is_rect_intersect(clip, a));
}
void mesh2d_x::add(void* user_image, std::vector<vertex_t>& vertex, std::vector<int>& vt_index, const glm::ivec4& clip)
{
	add(user_image, vertex.data(), vertex.size(), vt_index.data(), vt_index.size(), clip);
}

void mesh2d_x::add(void* user_image, vertex_t* vertex, size_t vcount, int* vt_index, size_t icount, const glm::ivec4& clip)
{
	auto ps0 = vcount;
	auto ps = vtxs.size();
	auto ix = idxs.size();
	auto ic = icount;
	vtxs.resize(ps + vcount);
	idxs.resize(ix + icount);
	auto& cd = cmd_data;
	if (cd.empty())
	{
		cd.push_back({});
	}
	auto dt = &cd.back();
	auto pidx = idxs.data() + ix;
	if (dt->texid != user_image || dt->clip_rect != clip)
	{
		if (dt->elemCount > 0)
			cd.push_back({});
		dt = &cd.back();
		dt->texid = user_image;
		dt->clip_rect = clip;
		dt->vtxOffset = ps;
		dt->idxOffset = ix;
		dt->elemCount = ic;
		dt->vCount = ps0;
	}
	else
	{
		// 合批
		dt->elemCount += ic;
		dt->vCount += ps0;
		auto idt = vt_index;
		for (size_t i = 0; i < ic; i++)
		{
			idt[i] += ix;
		}
	}
	memcpy(vtxs.data() + ps, vertex, vcount * sizeof(vertex[0]));
	memcpy(pidx, vt_index, icount * sizeof(vt_index[0]));
}


void mesh2d_x::add_image0(void* img, const glm::ivec2& texsize, const glm::ivec4& clip, const glm::ivec4& dst, const glm::ivec4& src, const glm::ivec4& sliced, uint32_t color)
{
	auto a = glm::vec4(dst);
	glm::ivec2 pos = { a.x, a.y }, size = { a.z, a.w };
	glm::vec4 v4 = { 0, 0, 1, 1 };
	glm::vec4 uv = v4;
	glm::vec2 s = size;
	if (a.z < 0)
		a.z *= -std::min(src.z, texsize.x);
	if (a.w < 0)
		a.w *= -std::min(src.w, texsize.y);
	if (nohas_clip(a))
		return;

	if (sliced.x > 0)
	{
		add_image_sliced(img, texsize, a, sliced, src, color, clip);// 生成九宫格到mesh
	}
	else
	{
		if (!(src.x < 0))
		{
			v4 = src;
			v4.z += v4.x; v4.w += v4.y;//加上原点坐标
			v4.z = glm::min(v4.z, (float)texsize.x);
			v4.w = glm::min(v4.w, (float)texsize.y);
			uv = { v4.x / texsize.x, v4.y / texsize.y, v4.z / texsize.x, v4.w / texsize.y };
			if (uv.x < 0) { uv.x = 0; }
			if (uv.y < 0) { uv.y = 0; }
		}
		glm::vec2 av = pos, cv = { pos.x + s.x, pos.y + s.y }, uv_a = { uv.x, uv.y }, uv_c{ uv.z, uv.w };
		auto& col = color;
		glm::vec2 bv(cv.x, av.y), dv(av.x, cv.y), uv_b(uv_c.x, uv_a.y), uv_d(uv_a.x, uv_c.y);

		vertex_t vertex[] = {
		   { av, uv_a, col },
		   { bv, uv_b, col },
		   { cv, uv_c, col },
		   { dv, uv_d, col },
		};
		int rect_index_order[] = { 0, 1, 2, 0, 2, 3 };
		add(img, vertex, 4, rect_index_order, 6, clip);// 添加矩形(两个三角形)到mesh
	}
}

/*


九宫格渲染:
+--+---------------+--+
|0 |       1       |2 |
+--+---------------+--+
|  |               |  |
|  |               |  |
|3 |    center     |4 |
|  |               |  |
+--+---------------+--+
|5 |       6       |7 |
+--+---------------+--+

九宫格:索引
0  12                     14  2
8  4                      6   10

9  5                      7   11
1  13                     15  3
+--+-------------------------+--+
|  |                         |  |
+--+-------------------------+--+
|  |                         |  |
|  |                         |  |
+--+-------------------------+--+
|  |                         |  |
+--+-------------------------+--+
sliced.x=左宽，y上高，z右宽，w下高

*/
void mesh2d_x::add_image_sliced(void* user_image, const glm::ivec2& texsize, const glm::ivec4& a, const glm::ivec4& sliced, const glm::ivec4& rect, uint32_t col, const glm::ivec4& clip)
{
	// add() 合批时会修改索引数组，所以这里使用局部副本。
	int vt_index[] =// { 0,8,12,4,14,6,2,10,11,6,7,4,5,8,9,1,5,13,7,15,11,3 };//E_TRIANGLE_STRIP
	{ 0, 8, 12, 8, 12, 4, 12, 4, 14, 4, 14, 6, 14, 6, 2, 6, 2, 10,
		6, 7, 10, 7, 10, 11, 4, 5, 6, 5, 6, 7, 8, 9, 4, 9, 4, 5,
		9, 1, 5, 1, 5, 13, 5, 13, 7, 13, 7, 15, 7, 15, 11, 15, 11, 3 };//E_TRIANGLE_LIST

	glm::ivec2 pos = { a.x, a.y }, size = { a.z, a.w };
	glm::vec4 uv = { 0, 0, 1, 1 };
	glm::vec4 v4 = { 0, 0, texsize.x, texsize.y };
	if (!(rect.x < 0))
	{
		v4 = rect;
		v4.z += v4.x; v4.w += v4.y;//加上原点坐标
		uv = { v4.x / texsize.x, v4.y / texsize.y, v4.z / texsize.x, v4.w / texsize.y, };
	}
	float left = sliced.x,
		top = sliced.y,
		right = sliced.z,
		bottom = sliced.w;
	float x = pos.x, y = pos.y, width = size.x, height = size.y;
	glm::vec4 suv = { (left + v4.x) / texsize.x, (top + v4.y) / texsize.y,
		(v4.z - right) / texsize.x, (v4.w - bottom) / texsize.y };

	vertex_t vertex[] = {
		//0
		{ { x, y },{ uv.x, uv.y }, col },
		//1
		{ { x, y + height },{ uv.x, uv.w }, col },
		//2
		{ { x + width, y },{ uv.z, uv.y }, col },
		//3
		{ { x + width, y + height },{ uv.z, uv.w }, col },
		//4
		{ { x + left, y + top },{ suv.x, suv.y }, col },
		//5
		{ { x + left, y + height - bottom },{ suv.x, suv.w }, col },
		//6
		{ { x + width - right, y + top },{ suv.z, suv.y }, col },
		//7
		{ { x + width - right, y + height - bottom },{ suv.z, suv.w }, col },
		//8
		{ { x, y + top },{ uv.x, suv.y }, col },
		//9
		{ { x, y + height - bottom },{ uv.x, suv.w }, col },
		//10
		{ { x + width, y + top },{ uv.z, suv.y }, col },
		//11
		{ { x + width, y + height - bottom },{ uv.z, suv.w }, col },
		//12
		{ { x + left, y },{ suv.x, uv.y }, col },
		//13
		{ { x + left, y + height },{ suv.x, uv.w }, col },
		//14
		{ { x + width - right, y },{ suv.z, uv.y }, col },
		//15
		{ { x + width - right, y + height },{ suv.z, uv.w }, col }
	};

	add(user_image, vertex, 16, vt_index, sizeof(vt_index) / sizeof(vt_index[0]), clip);

	return;
}
void mesh2d_x::add_texture0(void* img, const glm::ivec2& texsize, const glm::ivec4& clip,
	const glm::ivec4& dst, const glm::ivec4& src, const glm::ivec4& sliced,
	uint32_t color, int flip, float angle, const glm::vec2& center,
	bool center_set, const glm::mat3x2* matrix)
{
	if (!img || texsize.x <= 0 || texsize.y <= 0 || dst.z <= 0 || dst.w <= 0)
		return;

	// 源区域；x < 0 或宽高非法表示整张纹理。
	glm::vec4 s4 = { 0, 0, (float)texsize.x, (float)texsize.y };
	glm::vec4 uv = { 0, 0, 1, 1 };
	if (src.x >= 0 && src.z > 0 && src.w > 0)
	{
		const float sx = (float)std::max(src.x, 0);
		const float sy = (float)std::max(src.y, 0);
		const float ex = (float)std::min(src.x + src.z, texsize.x);
		const float ey = (float)std::min(src.y + src.w, texsize.y);
		if (ex <= sx || ey <= sy)
			return;
		s4 = { sx, sy, ex, ey };
		uv = { sx / texsize.x, sy / texsize.y, ex / texsize.x, ey / texsize.y };
	}

	const float x = (float)dst.x;
	const float y = (float)dst.y;
	const float width = (float)dst.z;
	const float height = (float)dst.w;
	const float dst_cx = x + width * 0.5f;
	const float dst_cy = y + height * 0.5f;
	const float rot_cx = center_set ? x + center.x : dst_cx;
	const float rot_cy = center_set ? y + center.y : dst_cy;
	const bool flip_x = (flip & FLIP_HORIZONTAL) != 0;
	const bool flip_y = (flip & FLIP_VERTICAL) != 0;
	const bool rotate = angle != 0.0f;
	const float rad = glm::radians(angle);
	const float cs = std::cos(rad);
	const float sn = std::sin(rad);

	auto xform = [&](glm::vec2 p) -> glm::vec2
		{
			if (flip_x) p.x = dst_cx * 2.0f - p.x;
			if (flip_y) p.y = dst_cy * 2.0f - p.y;
			if (rotate)
			{
				const float dx = p.x - rot_cx;
				const float dy = p.y - rot_cy;
				p.x = rot_cx + cs * dx - sn * dy;
				p.y = rot_cy + sn * dx + cs * dy;
			}
			if (matrix)
			{
				const glm::vec2 q = (*matrix) * glm::vec3(p.x, p.y, 1.0f);
				p = q;
			}
			return p;
		};

	if (sliced.x <= 0)
	{
		vertex_t vertex[] = {
			{ { x, y },{ uv.x, uv.y }, color },
			{ { x + width, y },{ uv.z, uv.y }, color },
			{ { x + width, y + height },{ uv.z, uv.w }, color },
			{ { x, y + height },{ uv.x, uv.w }, color },
		};
		for (auto& v : vertex)
			v.position = xform(v.position);
		int rect_index_order[] = { 0, 1, 2, 0, 2, 3 };
		add(img, vertex, 4, rect_index_order, 6, clip);
		return;
	}

	// 九宫格边宽同时用于目标布局。目标过小时按比例压缩，避免出现负宽高。
	float left = (float)std::max(sliced.x, 0);
	float top = (float)std::max(sliced.y, 0);
	float right = (float)std::max(sliced.z, 0);
	float bottom = (float)std::max(sliced.w, 0);
	if (left + right > width && left + right > 0.0f)
	{
		const float k = width / (left + right);
		left *= k;
		right *= k;
	}
	if (top + bottom > height && top + bottom > 0.0f)
	{
		const float k = height / (top + bottom);
		top *= k;
		bottom *= k;
	}

	// 源纹理上的边宽按源区域裁剪；目标边宽仍使用上面的值。
	const float src_w = s4.z - s4.x;
	const float src_h = s4.w - s4.y;
	const float src_left = std::min(left, src_w);
	const float src_top = std::min(top, src_h);
	const float src_right = std::min(right, std::max(0.0f, src_w - src_left));
	const float src_bottom = std::min(bottom, std::max(0.0f, src_h - src_top));
	const glm::vec4 suv = {
		(s4.x + src_left) / texsize.x,
		(s4.y + src_top) / texsize.y,
		(s4.z - src_right) / texsize.x,
		(s4.w - src_bottom) / texsize.y,
	};

	vertex_t vertex[] = {
		// 0
		{ { x, y },{ uv.x, uv.y }, color },
		// 1
		{ { x, y + height },{ uv.x, uv.w }, color },
		// 2
		{ { x + width, y },{ uv.z, uv.y }, color },
		// 3
		{ { x + width, y + height },{ uv.z, uv.w }, color },
		// 4
		{ { x + left, y + top },{ suv.x, suv.y }, color },
		// 5
		{ { x + left, y + height - bottom },{ suv.x, suv.w }, color },
		// 6
		{ { x + width - right, y + top },{ suv.z, suv.y }, color },
		// 7
		{ { x + width - right, y + height - bottom },{ suv.z, suv.w }, color },
		// 8
		{ { x, y + top },{ uv.x, suv.y }, color },
		// 9
		{ { x, y + height - bottom },{ uv.x, suv.w }, color },
		// 10
		{ { x + width, y + top },{ uv.z, suv.y }, color },
		// 11
		{ { x + width, y + height - bottom },{ uv.z, suv.w }, color },
		// 12
		{ { x + left, y },{ suv.x, uv.y }, color },
		// 13
		{ { x + left, y + height },{ suv.x, uv.w }, color },
		// 14
		{ { x + width - right, y },{ suv.z, uv.y }, color },
		// 15
		{ { x + width - right, y + height },{ suv.z, uv.w }, color },
	};
	for (auto& v : vertex)
		v.position = xform(v.position);

	// add() 合批时会修改索引数组，所以这里使用局部副本。
	int vt_index[] = {
		0, 8, 12, 8, 12, 4, 12, 4, 14, 4, 14, 6, 14, 6, 2, 6, 2, 10,
		6, 7, 10, 7, 10, 11, 4, 5, 6, 5, 6, 7, 8, 9, 4, 9, 4, 5,
		9, 1, 5, 1, 5, 13, 5, 13, 7, 13, 7, 15, 7, 15, 11, 15, 11, 3,
	};
	add(img, vertex, 16, vt_index, sizeof(vt_index) / sizeof(vt_index[0]), clip);
}

void mesh2d_x::add_image_angle(void* img, const glm::ivec2& texsize, const glm::ivec4& srcrect, const glm::ivec4& dstrect, float angle, const glm::vec2* center, uint32_t col, const glm::ivec4& clip, int flip)
{
	int rect_index_order[] = { 0, 1, 2, 0, 2, 3 };
	glm::ivec4 real_srcrect = {};
	glm::vec2 real_center = {};
	if (flip == FLIP_NONE && (int)(angle / 360) == angle / 360) { // fast path when we don't need rotation or flipping
		add_image0(img, texsize, clip, srcrect, dstrect, {}, col);
		return;
	}
	real_srcrect.x = 0.0f;
	real_srcrect.y = 0.0f;
	real_srcrect.z = (float)texsize.x;
	real_srcrect.w = (float)texsize.y;
	if (center) {
		real_center = *center;
	}
	else {
		real_center.x = dstrect.z / 2.0f;
		real_center.y = dstrect.w / 2.0f;
	}
	vertex_t v[4];
	//float xy[8];
	const int xy_stride = 2 * sizeof(float);
	//float uv[8];
	const int uv_stride = 2 * sizeof(float);
	const int num_vertices = 4;
	const int* indices = rect_index_order;
	const int num_indices = 6;
	const int size_indices = 4;
	glm::vec2 minuv, maxuv;
	glm::vec2 minxy, maxxy;
	float centerx, centery;

	float s_minx, s_miny, s_maxx, s_maxy;
	float c_minx, c_miny, c_maxx, c_maxy;

	const float radian_angle = glm::radians(angle);
	const float s = glm::sin(radian_angle);
	const float c = glm::cos(radian_angle);

	minuv.x = real_srcrect.x / texsize.x;
	minuv.y = real_srcrect.y / texsize.y;
	maxuv.x = (real_srcrect.x + real_srcrect.z) / texsize.x;
	maxuv.y = (real_srcrect.y + real_srcrect.w) / texsize.y;

	centerx = real_center.x + dstrect.x;
	centery = real_center.y + dstrect.y;

	if (flip & FLIP_HORIZONTAL) {
		minxy.x = dstrect.x + dstrect.z;
		maxxy.x = dstrect.x;
	}
	else {
		minxy.x = dstrect.x;
		maxxy.x = dstrect.x + dstrect.z;
	}

	if (flip & FLIP_VERTICAL) {
		minxy.y = dstrect.y + dstrect.w;
		maxxy.y = dstrect.y;
	}
	else {
		minxy.y = dstrect.y;
		maxxy.y = dstrect.y + dstrect.w;
	}

	v[0].tex_coord = minuv;
	v[1].tex_coord = maxuv;
	v[2].tex_coord = maxuv;
	v[3].tex_coord = minuv;

	/* apply rotation with 2x2 matrix ( c -s )
	 *                                ( s  c ) */
	s_minx = s * (minxy.x - centerx);
	s_miny = s * (minxy.y - centery);
	s_maxx = s * (maxxy.x - centerx);
	s_maxy = s * (maxxy.y - centery);
	c_minx = c * (minxy.x - centerx);
	c_miny = c * (minxy.y - centery);
	c_maxx = c * (maxxy.x - centerx);
	c_maxy = c * (maxxy.y - centery);

	// (minx, miny)
	v[0].position = glm::vec2((c_minx - s_miny) + centerx, (s_minx + c_miny) + centery);
	// (maxx, miny)
	v[1].position = glm::vec2((c_maxx - s_miny) + centerx, (s_maxx + c_miny) + centery);
	// (maxx, maxy)
	v[2].position = glm::vec2((c_maxx - s_maxy) + centerx, (s_maxx + c_maxy) + centery);
	// (minx, maxy)
	v[3].position = glm::vec2((c_minx - s_maxy) + centerx, (s_minx + c_maxy) + centery);
	auto c4 = (col);
	v[0].color = c4;
	v[1].color = c4;
	v[2].color = c4;
	v[3].color = c4;
	add(img, v, 4, rect_index_order, 6, clip);
}


#endif // 1

ovg_draw_data_t get_draw_list(rvg_t* p0)
{
	rvg_cx* p = (rvg_cx*)p0;
	ovg_draw_data_t ret = {};
	if (p)
	{
		ret.d = p->cmdlist.data(); ret.count = p->cmdlist.size();
		ret.vg_vertex = (ovgVertex*)p->_vertex.data();
		ret.v_count = p->_vertex.size();
		ret.vg_indices = p->_indices.data();
		ret.i_count = p->_indices.size();
		ret.uboCount = p->gCount;
		ret.vertex1 = (geomVertex1*)p->gps.vd1.data();
		ret.v1_count = p->gps.vd1.size();
		ret.vertex2 = (geomVertex2*)p->gps.vd2.data();
		ret.v2_count = p->gps.vd2.size();
		ret.geom_indices = p->gps.ids.data();
		ret.ig_count = p->gps.ids.size();
		ret.instance_count = p->gps.instance_mat.size();
		ret.instance_data = p->gps.instance_mat.data();
		p->_images_up.clear();
		for (auto& [k, v] : p->_images) {
			v.img = k;
			p->_images_up.push_back(&v);
		}
		ret.image_desc = p->_images_up.data();
		ret.image_desc_count = p->_images_up.size();
		ret.pipeinfo = p->gps.build_pipes.data();
		ret.pipeinfo_count = p->gps.build_pipes.size();
	}
	return ret;
}

// cmd ctx
#if 1

rvg_t* vctx_new_rvg(mem_resource_t* ac);
void  vctx_destroy_rvg(rvg_t* p);
void vctx_clear(rvg_t* v);			// 清空画布 
// 路径操作
ovg_path_t* vctx_get_path(rvg_t* ctx);
void  vctx_new_path(rvg_t* ctx);
void vctx_clear_path(rvg_t* ctx);
void vctx_close_path(rvg_t* ctx);
void vctx_new_sub_path(rvg_t* ctx);
void vctx_path_extents(rvg_t* ctx, float* x1, float* y1, float* x2, float* y2);
void vctx_get_current_point(rvg_t* ctx, float* x, float* y);
size_t vctx_get_segment_count(rvg_t* ctx);
void vctx_set_segment_color(rvg_t* ctx, size_t idx, uint32_t color);
// 添加数据到当前路径，参考path_type_e
void vctx_add_path(rvg_t* ctx, float* data, size_t count);
void vctx_move_to(rvg_t* ctx, float x, float y);
void vctx_rel_move_to(rvg_t* ctx, float x, float y);
void vctx_line_to(rvg_t* ctx, float x, float y);
void vctx_rel_line_to(rvg_t* ctx, float dx, float dy);
void vctx_arc(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2);
void vctx_arc_negative(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2);
// 有缩放时，先执行set_path一次再执行curve_to
void vctx_curve_to(rvg_t* ctx, float x1, float y1, float x2, float y2, float x3, float y3);
void vctx_rel_curve_to(rvg_t* ctx, float x1, float y1, float x2, float y2, float x3, float y3);
void vctx_quadratic_to(rvg_t* ctx, float x1, float y1, float x2, float y2);
void vctx_rel_quadratic_to(rvg_t* ctx, float x1, float y1, float x2, float y2);
void vctx_rectangle(rvg_t* ctx, float x, float y, float w, float h);
void vctx_rounded_rectangle(rvg_t* ctx, float x, float y, float w, float h, float radius);
void vctx_rounded_rectangle2(rvg_t* ctx, float x, float y, float w, float h, float rx, float ry);
void vctx_ellipse(rvg_t* ctx, float radiusX, float radiusY, float x, float y, float rotationAngle);
void vctx_elliptic_arc_to(rvg_t* ctx, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi);
void vctx_rel_elliptic_arc_to(rvg_t* ctx, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi);
void vctx_circle(rvg_t* ctx, float x, float y, float radius);
// 配置
void vctx_set_opacity(rvg_t* ctx, float opacity);
void vctx_set_effect(rvg_t* ctx, const vg_effect_t* effect);
void vctx_set_source_color(rvg_t* ctx, uint32_t c);
void vctx_set_source_rgba(rvg_t* ctx, float r, float g, float b, float a);
void vctx_set_source_rgb(rvg_t* ctx, float r, float g, float b);
void vctx_set_line_width(rvg_t* ctx, float width);
void vctx_set_miter_limit(rvg_t* ctx, float limit);
void vctx_set_line_cap(rvg_t* ctx, int cap);
void vctx_set_line_join(rvg_t* ctx, int join);
void vctx_set_source_surface(rvg_t* ctx, vg_image_t* surf, float x, float y);
void vctx_set_source(rvg_t* ctx, vg_pattern_t* pat);
void vctx_set_operator(rvg_t* ctx, int op);
void vctx_set_fill_rule(rvg_t* ctx, int fr);
void vctx_set_dash(rvg_t* ctx, const float* dashes, uint32_t num_dashes, float offset);		// 虚线
void vctx_set_dash8(rvg_t* ctx, uint64_t dashes, uint32_t num_dashes, float offset);								// 虚线,用uint8_t v8[8]表示
void vctx_translate(rvg_t* ctx, float dx, float dy);
void vctx_scale(rvg_t* ctx, float sx, float sy);
void vctx_rotate(rvg_t* ctx, float radians);
void vctx_transform(rvg_t* ctx, const void* matrix);
void vctx_set_matrix(rvg_t* ctx, const void* matrix);
void vctx_get_matrix(rvg_t* ctx, void* matrix);
void vctx_identity_matrix(rvg_t* ctx);
void vctx_matrix_init(void* mat, float xx, float yx, float xy, float yy, float x0, float y0);

// 图案：渐变/图片 
vg_pattern_t* vctx_new_pattern_linear(rvg_t* ctx, float x0, float y0, float x1, float y1);
vg_pattern_t* vctx_new_pattern_radial(rvg_t* ctx, float cx0, float cy0, float radius0, float cx1, float cy1, float radius1, bool is_ellipse);
vg_pattern_t* vctx_new_pattern_sweep(rvg_t* ctx, float cx, float cy, float start_angle, float end_angle);
int  vctx_pattern_add_color_stop(vg_pattern_t* pat, float o, float r, float g, float b, float a);
int  vctx_pattern_set_color_stop(vg_pattern_t* pat, int idx, float o, float r, float g, float b, float a);
void vctx_pattern_set_matrix(vg_pattern_t* pat, const void* matrix);	// mat3x2
void vctx_pattern_set_extend(vg_pattern_t* pat, int extend);
void vctx_pattern_set_filter(vg_pattern_t* pat, int filter);

void vctx_save(rvg_t* v);
void vctx_restore(rvg_t* v);
void vctx_stroke(rvg_t* v);
void vctx_stroke_preserve(rvg_t* v);
void vctx_fill(rvg_t* v);
void vctx_fill_preserve(rvg_t* v);
void vctx_paint(rvg_t* v);			// 全屏渲染
void vctx_reset_clip(rvg_t* v, uint8_t ref);	// 重置裁剪
void vctx_clip(rvg_t* v);			// 路径裁剪，清空当前路径
void vctx_clip_preserve(rvg_t* v);	// 路径裁剪
void vctx_clip_rect(rvg_t* v, int x, int y, int width, int height);	// 矩形裁剪
void vctx_set_clip_rect(rvg_t* v, void* rc);	// 矩形裁剪,int[4]
void vctx_get_clip_rect(rvg_t* v, void* rc);	// 获取矩形裁剪

// 添加文本，风格，渲染区可选
void  vctx_add_text(rvg_t* dc, text_st_t* p, text_style_t* ts, text_box_rt* box);
// 普通图片，支持九宫格、混合颜色
void  vctx_add_image(rvg_t* dc, ovg_image_r* r);
// 原生纹理，支持源裁剪、tint、翻转、九宫格、旋转、矩阵
void  vctx_add_texture(rvg_t* dc, ovg_texture_r* r);
// 原始三角形，输入0则不修改
void  vctx_set_geom_state(rvg_t* dc, gem_info_t* info, const void* matrix4x4);
// 添加几何数据到缓冲区，xy顶点坐标，color顶点颜色，uv顶点纹理坐标，indices索引数据，color_type=0表示float4，1表示uint32_t
void  vctx_add_geometry(rvg_t* dc, vg_image_t* texture, const float* xy, int xy_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);
// 添加3D几何数据到缓冲区，xyz顶点坐标，color顶点颜色（双面则要双倍），uv顶点纹理坐标，indices索引数据
void  vctx_add_geometry3d(rvg_t* dc, vg_image_t* texture, const float* xyz, int xyz_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);

void vctx_start_recording(rvg_t* ctx);
ovg_recording_t* vctx_stop_recording(rvg_t* ctx);
void vctx_replay(rvg_t* ctx, ovg_recording_t* rec);
void vctx_replay_command(rvg_t* ctx, ovg_recording_t* rec, uint32_t cmdIndex);
uint32_t vctx_recording_get_count(ovg_recording_t* rec);
void* vctx_recording_get_data(ovg_recording_t* rec);
void  vctx_recording_destroy(ovg_recording_t* rec);
// familys多个字体时用小写逗号分隔，style逗号分隔字体的风格(可空) 比如(char*)u8"Consolas,新宋体,Segoe UI Emoji


// TODO 命令模式实现
#define PRI2CTX(ac) 
rvg_t* vctx_new_rvg(mem_resource_t* ac) {
	auto r = (rvg_cx*)ovg_new_rvg(ac);
	if (r)
	{
		r->path = ovg_new_path((mem_resource_t*)r->ac);
		r->st = r->new_state();
		assert(r->path && r->st);
	}
	return r;
}
void  vctx_destroy_rvg(rvg_t* p) {
	ovg_destroy_rvg(p);
}
void vctx_set_enable_tess2(rvg_t* p, bool b) {
	if (p && p->path)
		ovg_set_enable_tess2(p->path, b);
}
void vctx_clear(rvg_t* v) {
	ovg_clear(v);
}
ovg_path_t* vctx_get_path(rvg_t* ctx) {
	return ctx->path;
}
void  vctx_new_path(rvg_t* ctx) {
	if (ctx)
		ovg_clear_path(ctx->path);
}
void vctx_clear_path(rvg_t* ctx) {
	vctx_new_path(ctx);
}
void vctx_close_path(rvg_t* ctx) {
	if (ctx)ovg_close_path(ctx->path);
}
void vctx_new_sub_path(rvg_t* ctx) {
	if (ctx)ovg_new_sub_path(ctx->path);
}
void vctx_path_extents(rvg_t* ctx, float* x1, float* y1, float* x2, float* y2) {
	if (ctx)ovg_path_extents(ctx->path, x1, y1, x2, y2);
}
void vctx_get_current_point(rvg_t* ctx, float* x, float* y) {
	if (ctx)ovg_get_current_point(ctx->path, x, y);
}
size_t vctx_get_segment_count(rvg_t* ctx) {
	return ctx ? ovg_get_segment_count(ctx->path) : 0;
}
void vctx_set_segment_color(rvg_t* ctx, size_t idx, uint32_t color) {
	if (ctx)ovg_set_segment_color(ctx->path, idx, color);
}
// 添加数据到当前路径，参考path_type_e
void vctx_add_path(rvg_t* ctx, float* data, size_t count) {
	if (ctx)ovg_add_path(ctx->path, data, count);
}
void vctx_move_to(rvg_t* ctx, float x, float y) {
	if (ctx)ovg_move_to(ctx->path, x, y);
}
void vctx_rel_move_to(rvg_t* ctx, float x, float y) {
	if (!ctx) return;
	float cx, cy;
	ovg_get_current_point(ctx->path, &cx, &cy);
	ovg_move_to(ctx->path, cx + x, cy + y);
}

void vctx_line_to(rvg_t* ctx, float x, float y) {
	if (ctx) ovg_line_to(ctx->path, x, y);
}

void vctx_rel_line_to(rvg_t* ctx, float dx, float dy) {
	if (!ctx) return;
	float cx, cy;
	ovg_get_current_point(ctx->path, &cx, &cy);
	ovg_line_to(ctx->path, cx + dx, cy + dy);
}

void vctx_arc(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2) {
	if (ctx) ovg_arc(ctx->path, xc, yc, radius, a1, a2);
}

void vctx_arc_negative(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2) {
	if (ctx) ovg_arc_negative(ctx->path, xc, yc, radius, a1, a2);
}

void vctx_curve_to(rvg_t* ctx, float x1, float y1,
	float x2, float y2,
	float x3, float y3) {
	if (ctx) ovg_curve_to(ctx->path, x1, y1, x2, y2, x3, y3);
}

void vctx_rel_curve_to(rvg_t* ctx, float x1, float y1,
	float x2, float y2,
	float x3, float y3) {
	if (!ctx) return;
	float cx, cy;
	ovg_get_current_point(ctx->path, &cx, &cy);
	ovg_curve_to(ctx->path,
		cx + x1, cy + y1,
		cx + x2, cy + y2,
		cx + x3, cy + y3);
}

void vctx_quadratic_to(rvg_t* ctx, float x1, float y1, float x2, float y2) {
	if (ctx) ovg_quadratic_to(ctx->path, x1, y1, x2, y2);
}

void vctx_rel_quadratic_to(rvg_t* ctx, float x1, float y1, float x2, float y2) {
	if (!ctx) return;
	float cx, cy;
	ovg_get_current_point(ctx->path, &cx, &cy);
	ovg_quadratic_to(ctx->path,
		cx + x1, cy + y1,
		cx + x2, cy + y2);
}

void vctx_rectangle(rvg_t* ctx, float x, float y, float w, float h) {
	if (ctx) ovg_rectangle(ctx->path, x, y, w, h);
}

void vctx_rounded_rectangle(rvg_t* ctx, float x, float y,
	float w, float h, float radius) {
	if (ctx) radius > 0 ? ovg_rounded_rectangle(ctx->path, x, y, w, h, radius) : ovg_rectangle(ctx->path, x, y, w, h);
}

void vctx_rounded_rectangle2(rvg_t* ctx, float x, float y,
	float w, float h, float rx, float ry) {
	if (ctx)(rx > 0 || ry > 0) ? ovg_rounded_rectangle2(ctx->path, x, y, w, h, rx, ry) : ovg_rectangle(ctx->path, x, y, w, h);
}

void vctx_ellipse(rvg_t* ctx, float radiusX, float radiusY,
	float x, float y, float rotationAngle) {
	if (ctx) ovg_ellipse(ctx->path, radiusX, radiusY, x, y, rotationAngle);
}

void vctx_elliptic_arc_to(rvg_t* ctx, float x, float y,
	bool large_arc_flag, bool sweep_flag,
	float rx, float ry, float phi) {
	if (ctx) ovg_elliptic_arc_to(ctx->path, x, y,
		large_arc_flag, sweep_flag,
		rx, ry, phi);
}

void vctx_rel_elliptic_arc_to(rvg_t* ctx, float x, float y,
	bool large_arc_flag, bool sweep_flag,
	float rx, float ry, float phi) {
	if (!ctx) return;
	float cx, cy;
	ovg_get_current_point(ctx->path, &cx, &cy);
	ovg_elliptic_arc_to(ctx->path,
		cx + x, cy + y,
		large_arc_flag, sweep_flag,
		rx, ry, phi);
}

void vctx_circle(rvg_t* ctx, float x, float y, float radius) {
	if (ctx) ovg_circle(ctx->path, x, y, radius);
}
// 配置 
void vctx_set_opacity(rvg_t* ctx, float opacity) {
	if (ctx) ovg_set_opacity(ctx->st, opacity);
}
void vctx_set_effect(rvg_t* ctx, const vg_effect_t* effect) {
	if (ctx) ovg_set_effect(ctx->st, effect);
}

void vctx_set_source_color(rvg_t* ctx, uint32_t c) {
	if (ctx) ovg_set_source_color(ctx->st, c);
}

void vctx_set_source_rgba(rvg_t* ctx, float r, float g, float b, float a) {
	if (ctx) ovg_set_source_rgba(ctx->st, r, g, b, a);
}

void vctx_set_source_rgb(rvg_t* ctx, float r, float g, float b) {
	if (ctx) ovg_set_source_rgba(ctx->st, r, g, b, 1.0f);
}

void vctx_set_line_width(rvg_t* ctx, float width) {
	if (ctx) ovg_set_line_width(ctx->st, width);
}

void vctx_set_miter_limit(rvg_t* ctx, float limit) {
	if (ctx) ovg_set_miter_limit(ctx->st, limit);
}

void vctx_set_line_cap(rvg_t* ctx, int cap) {
	if (ctx) ovg_set_line_cap(ctx->st, cap);
}

void vctx_set_line_join(rvg_t* ctx, int join) {
	if (ctx) ovg_set_line_join(ctx->st, join);
}

void vctx_set_source_surface(rvg_t* ctx, vg_image_t* surf, float x, float y) {
	if (ctx) ovg_set_source_surface(ctx->st, surf, x, y);
}

void vctx_set_source(rvg_t* ctx, vg_pattern_t* pat) {
	if (ctx) ovg_set_source(ctx->st, pat);
}

void vctx_set_operator(rvg_t* ctx, int op) {
	if (ctx) ovg_set_operator(ctx->st, op);
}

void vctx_set_fill_rule(rvg_t* ctx, int fr) {
	if (ctx) ovg_set_fill_rule(ctx->st, fr);
}

void vctx_set_dash(rvg_t* v0, const float* dashes, uint32_t num_dashes, float offset) {
	auto ctx = (rvg_cx*)v0;
	if (!ctx || !ctx->st)return;
	auto t = ctx->st;
	if (!dashes || !num_dashes) {
		if (t->dashes)
			ctx->ac->free_mem(t->dashes, t->dashCount);
		t->dashes = nullptr;
		t->dashCount = 0;
		t->dashOffset = offset;
		return;
	}
	if (!t->dashes || t->dashCount != num_dashes)
	{
		if (t->dashes)
			ctx->ac->free_mem(t->dashes, t->dashCount);
		t->dashes = (float*)ctx->ac->allocate(sizeof(float) * num_dashes);
		if (!t->dashes) {
			t->dashCount = 0;
			return;
		}
	}
	t->dashCount = num_dashes;
	t->dashOffset = offset;
	memcpy(t->dashes, dashes, sizeof(float) * t->dashCount);
}

void vctx_set_dash8(rvg_t* ctx, uint64_t dashes0, uint32_t num_dashes, float offset) {
	if (ctx) {
		float dashes[64] = {};
		uint64_t x = 1;
		auto t = dashes;
		auto v8 = (uint8_t*)&dashes0;
		if (num_dashes > 64)num_dashes = 64;
		{
			if (num_dashes > 8)num_dashes = 8;
			for (size_t i = 0; i < num_dashes; i++)
			{
				*t = v8[i]; t++;
			}
			if (num_dashes > 0)
				vctx_set_dash(ctx, dashes, num_dashes, offset);
		}
	}
}

void vctx_translate(rvg_t* ctx, float dx, float dy) {
	if (ctx) ovg_translate(ctx->st, dx, dy);
}

void vctx_scale(rvg_t* ctx, float sx, float sy) {
	if (ctx) ovg_scale(ctx->st, sx, sy);
}

void vctx_rotate(rvg_t* ctx, float radians) {
	if (ctx) ovg_rotate(ctx->st, radians);
}

void vctx_transform(rvg_t* ctx, const void* matrix) {
	if (ctx) ovg_transform(ctx->st, matrix);
}

void vctx_set_matrix(rvg_t* ctx, const void* matrix) {
	if (ctx) ovg_set_matrix(ctx->st, matrix);
}

void vctx_get_matrix(rvg_t* ctx, void* matrix) {
	if (ctx) ovg_get_matrix(ctx->st, matrix);
}

void vctx_identity_matrix(rvg_t* ctx) {
	if (ctx) ovg_identity_matrix(ctx->st);
}
void vctx_matrix_init(void* mat, float xx, float yx, float xy, float yy, float x0, float y0) {
	if (mat) { ovg_matrix_init(mat, xx, yx, xy, yy, x0, y0); }
}
typedef glm::mat3x2 ovg_matrix_t;
// 图案：渐变/图片 
vg_pattern_t* vctx_new_pattern_linear(rvg_t* v0, float x0, float y0, float x1, float y1) {
	auto ctx = (rvg_cx*)v0;
	if (!ctx)return 0;
	auto pat = (pat_act*)ctx->mac.allocate(sizeof(pat_act));
	if (!pat) {
		return 0;
	}
	*pat = {};
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_LINEAR;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	_vg_pattern_edit_linear(pat, x0, y0, x1, y1);
	pat->matrix = glm::mat3x2(1.0);
	pat->references = 1;
	return pat;
}
vg_pattern_t* vctx_new_pattern_radial(rvg_t* v0, float cx0, float cy0, float radius0, float cx1, float cy1, float radius1, bool is_ellipse) {
	auto ctx = (rvg_cx*)v0;
	if (!ctx)return 0;
	auto pat = (pat_act*)ctx->mac.allocate(sizeof(pat_act));
	if (!pat) {
		return 0;
	}
	*pat = {};
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_RADIAL;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	pat->matrix = glm::mat3x2(1.0);
	vg_pattern_edit_radial(pat, cx0, cy0, radius0, cx1, cy1, radius1, is_ellipse);
	pat->references = 1;
	return pat;
}
vg_pattern_t* vctx_new_pattern_sweep(rvg_t* v0, float cx, float cy, float start_angle, float end_angle) {
	auto ctx = (rvg_cx*)v0;
	if (!ctx)return 0;
	auto pat = (pat_act*)ctx->mac.allocate(sizeof(pat_act));
	if (!pat) {
		return 0;
	}
	*pat = {};
	pat->type = vg_pattern_type_t::VG_PATTERN_TYPE_SWEEP;
	pat->extend = vg_extend_t::VG_EXTEND_NONE;
	pat->data = &pat->g;
	pat->matrix = glm::mat3x2(1.0);
	vg_pattern_edit_sweep(pat, cx, cy, start_angle, end_angle);
	pat->references = 1;
	return pat;
}
int  vctx_pattern_add_color_stop(vg_pattern_t* pat, float o, float r, float g, float b, float a) {
	if (!pat) return -1;
	return ovg_pattern_add_color_stop(pat, o, r, g, b, a);
}
int  vctx_pattern_set_color_stop(vg_pattern_t* pat, int idx, float o, float r, float g, float b, float a) {
	if (!pat || idx < 0) return -1;
	return ovg_pattern_set_color_stop(pat, idx, o, r, g, b, a);
}
void vctx_pattern_set_matrix(vg_pattern_t* pat, const void* matrix) {
	if (pat) ovg_pattern_set_matrix(pat, (const ovg_matrix_t*)matrix);
}
void vctx_pattern_set_extend(vg_pattern_t* pat, int extend) {
	if (pat) ovg_pattern_set_extend(pat, extend);
}
void vctx_pattern_set_filter(vg_pattern_t* pat, int filter) {
	if (pat) ovg_pattern_set_filter(pat, filter);
}

void vctx_save(rvg_t* v0) {
	auto v = (rvg_cx*)v0;
	if (v)v->save();
}
void vctx_restore(rvg_t* v0) {
	auto v = (rvg_cx*)v0;
	if (v)v->restore();
}
void vctx_stroke(rvg_t* v) {
	if (v) ovg_stroke(v);
}
void vctx_stroke_preserve(rvg_t* v) {
	if (v) ovg_stroke_preserve(v);
}

void vctx_fill(rvg_t* v) {
	if (v) ovg_fill(v);
}

void vctx_fill_preserve(rvg_t* v) {
	if (v) ovg_fill_preserve(v);
}

void vctx_paint(rvg_t* v) {
	if (v) ovg_paint(v);
}

void vctx_reset_clip(rvg_t* v, uint8_t ref) {
	if (v) ovg_reset_clip(v, ref);
}

void vctx_clip(rvg_t* v) {
	if (v) ovg_clip(v);
}

void vctx_clip_preserve(rvg_t* v) {
	if (v) ovg_clip_preserve(v);
}

void vctx_clip_rect(rvg_t* v, int x, int y, int width, int height) {
	if (v) ovg_clip_rect(v, x, y, width, height);
}

void vctx_set_clip_rect(rvg_t* v, void* rc) {
	if (v && rc) ovg_set_clip_rect(v, rc);
}

void vctx_get_clip_rect(rvg_t* v, void* rc) {
	if (v && rc) ovg_get_clip_rect(v, rc);
}

/* ================= 高层绘制命令 ================= */

// 添加文本，风格，渲染区可选
void vctx_add_text(rvg_t* v0, text_st_t* p, text_style_t* ts, text_box_rt* box) {

	auto dc = (rvg_cx*)v0;
	if (dc) {
		auto cb = (ovg_ctx_cx*)dc->ac->ptr;
		if (cb)
			vg_add_text(0, cb, v0, p, ts, box);
	}
}

// 普通图片，支持九宫格、混合颜色
void vctx_add_image(rvg_t* dc, ovg_image_r* r) {
	if (dc) ovg_add_image(dc, r);
}

// 原生纹理，支持源裁剪、tint、翻转、九宫格、旋转、矩阵
void vctx_add_texture(rvg_t* dc, ovg_texture_r* r) {
	if (dc) ovg_add_texture(dc, r);
}

// 原始三角形，输入0则不修改
void vctx_set_geom_state(rvg_t* dc, gem_info_t* info, const void* matrix4x4) {
	if (dc) ovg_set_geom_state(dc, info, (glm::mat4*)matrix4x4);
}

// 添加几何数据到缓冲区
void vctx_add_geometry(rvg_t* dc,
	vg_image_t* texture,
	const float* xy, int xy_stride,
	const void* color, int color_stride,
	const float* uv, int uv_stride,
	int num_vertices,
	const void* indices, int num_indices,
	int size_indices, int color_type) {
	if (dc) {
		ovg_add_geometry(dc,
			texture,
			xy, xy_stride,
			color, color_stride,
			uv, uv_stride,
			num_vertices,
			indices, num_indices,
			size_indices, color_type);
	}
}

// 添加3D几何数据到缓冲区
void vctx_add_geometry3d(rvg_t* dc,
	vg_image_t* texture,
	const float* xyz, int xyz_stride,
	const void* color, int color_stride,
	const float* uv, int uv_stride,
	int num_vertices,
	const void* indices, int num_indices,
	int size_indices, int color_type) {
	if (dc) {
		ovg_add_geometry3d(dc,
			texture,
			xyz, xyz_stride,
			color, color_stride,
			uv, uv_stride,
			num_vertices,
			indices, num_indices,
			size_indices, color_type);
	}
}


// ============================================================
// 录制 / 回放
// 设计思路：start_recording 记录当前各 vector 的末尾位置作为"快照起点"；
// stop_recording 时将 [起点, 当前末尾) 区间的命令、顶点、索引深拷贝进
// ovg_recording_t，同时把 vgcmd_t 里的 state 和 pattern 也深拷贝，
// 并把顶点/索引偏移改写为相对于 recording 内部存储的坐标。
// replay 时把 recording 追加到目标 ctx，重新计算绝对偏移并把 state
// 克隆到目标 ctx 的 bump allocator 上。
// ============================================================
struct ovg_recording_t
{
	usp_ac_cx* ac = nullptr;
	vg_vector<gcmd_t> cmds;              // 命令（偏移已指向 recording 内部）
	vg_vector<rvg_cx::Vertex> vertices;  // 2D 矢量顶点快照
	vg_vector<uint32_t> indices;          // 2D 矢量索引快照
	// 需要手动释放的深拷贝资源（state dashes / pattern）
	std::vector<std::pair<void*, size_t>> owned;
};

// 深拷贝一条 vg_state_save_t（含 dashes 和 pattern），
// 结果中的裸指针全部存入 owner，供 recording_destroy 释放。
static vg_state_save_t* rec_clone_state(vg_state_save_t* src,
	std::vector<std::pair<void*, size_t>>& owned)
{
	if (!src) return nullptr;
	auto ns = (vg_state_save_t*)malloc(sizeof(vg_state_save_t));
	*ns = *src;
	owned.push_back({ ns, sizeof(vg_state_save_t) });
	if (src->dashes && src->dashCount > 0) {
		size_t ds = sizeof(float) * src->dashCount;
		ns->dashes = (float*)malloc(ds);
		memcpy(ns->dashes, src->dashes, ds);
		owned.push_back({ ns->dashes, ds });
	}
	if (src->pattern) {
		size_t ps = sizeof(pat_act);
		auto np = (pat_act*)malloc(ps);
		memcpy(np, src->pattern, ps);
		np->data = &np->g;   // 更新 data 指针指向新对象内部的 gradient
		ns->pattern = np;
		owned.push_back({ np, ps });
	}
	return ns;
}

void vctx_start_recording(rvg_t* ctx0)
{
	auto v = (rvg_cx*)ctx0;
	if (!v) return;
	v->_recording = true;
	v->_rec_start_cmd = (uint32_t)v->cmdlist.size();
	v->_rec_start_vtx = (uint32_t)v->_vertex.size();
	v->_rec_start_idx = (uint32_t)v->_indices.size();
}

ovg_recording_t* vctx_stop_recording(rvg_t* ctx0)
{
	auto v = (rvg_cx*)ctx0;
	if (!v || !v->_recording) return nullptr;
	v->_recording = false;

	auto rec = new ovg_recording_t();
	rec->ac = v->ac;

	uint32_t sc = v->_rec_start_cmd;
	uint32_t sv = v->_rec_start_vtx;
	uint32_t si = v->_rec_start_idx;

	// 1) 拷贝命令
	uint32_t cmd_count = (uint32_t)v->cmdlist.size() - sc;
	rec->cmds = vg_vector<gcmd_t>(v->ac);
	rec->cmds.resize(cmd_count);
	memcpy(rec->cmds.data(), &v->cmdlist[sc], sizeof(gcmd_t) * cmd_count);

	// 2) 遍历命令 — 深拷贝 state/pattern & 重写顶点/索引偏移
	//    同时追踪实际被引用的 [copy_v_from, copy_v_to) 和 [copy_i_from, copy_i_to)
	uint32_t copy_v_from = UINT_MAX, copy_v_to = sv;
	uint32_t copy_i_from = UINT_MAX, copy_i_to = si;

	for (uint32_t i = 0; i < cmd_count; i++)
	{
		auto& c = rec->cmds[i];
		if (c.vg.stype == 0)  // vgcmd_t — 2D 矢量命令
		{
			// 顶点引用范围
			if (c.vg.vertex.y > 0)
			{
				copy_v_from = std::min(copy_v_from, (uint32_t)c.vg.vertex.x);
				copy_v_to = std::max(copy_v_to, (uint32_t)(c.vg.vertex.x + c.vg.vertex.y));
				// 重写：从绝对偏移 → 相对 recording 内部的偏移
				c.vg.vertex.x = (int)(c.vg.vertex.x - sv);
			}
			if (c.vg.index.y > 0)
			{
				copy_i_from = std::min(copy_i_from, (uint32_t)c.vg.index.x);
				copy_i_to = std::max(copy_i_to, (uint32_t)(c.vg.index.x + c.vg.index.y));
				c.vg.index.x = (int)(c.vg.index.x - si);
			}
			c.vg.state = rec_clone_state(c.vg.state, rec->owned);
		}
		else if (c.g.stype == 1)  // geom_cmd_t — 3D 几何（vertexOffset 已指向 rvg 外部，保持不动）
		{
			// 暂做浅拷贝：geom 顶点不在 _vertex/_indices 里，而是 gps 自己管理的
		}
	}

	// 3) 拷贝顶点（[sv, copy_v_to) — 快照起点到引用末尾）
	if (copy_v_from != UINT_MAX && copy_v_to > sv)
	{
		uint32_t vcount = copy_v_to - sv;
		rec->vertices = vg_vector<rvg_cx::Vertex>(v->ac);
		rec->vertices.resize(vcount);
		memcpy(rec->vertices.data(), &v->_vertex[sv], sizeof(rvg_cx::Vertex) * vcount);
	}

	// 4) 拷贝索引
	if (copy_i_from != UINT_MAX && copy_i_to > si)
	{
		uint32_t icount = copy_i_to - si;
		rec->indices = vg_vector<uint32_t>(v->ac);
		rec->indices.resize(icount);
		memcpy(rec->indices.data(), &v->_indices[si], sizeof(uint32_t) * icount);
	}

	return rec;
}

// 在目标 ctx 的 bump allocator 上克隆一条 state（与 cp_cmdt 逻辑相同）
static vg_state_save_t* rec_clone_state_into_target(rvg_cx* v, vg_state_save_t* src)
{
	if (!src) return nullptr;
	auto ns = (vg_state_save_t*)v->mac.allocate(sizeof(vg_state_save_t));
	*ns = *src;
	ns->dashes = nullptr;
	if (src->dashes && src->dashCount > 0)
	{
		ns->dashes = (float*)v->mac.allocate(sizeof(float) * src->dashCount);
		memcpy(ns->dashes, src->dashes, sizeof(float) * src->dashCount);
		ns->dashCount = src->dashCount;
	}
	if (src->pattern)
	{
		auto pat = (pat_act*)v->mac.allocate(sizeof(pat_act));
		memcpy(pat, src->pattern, sizeof(pat_act));
		pat->data = &pat->g;
		ns->pattern = pat;
	}
	return ns;
}

void vctx_replay(rvg_t* ctx0, ovg_recording_t* rec)
{
	auto v = (rvg_cx*)ctx0;
	if (!v || !rec) return;

	uint32_t delta_vtx = (uint32_t)v->_vertex.size();
	uint32_t delta_idx = (uint32_t)v->_indices.size();

	// 1) 追加顶点
	if (rec->vertices.size() > 0)
	{
		uint32_t old = (uint32_t)v->_vertex.size();
		v->_vertex.resize(old + rec->vertices.size());
		memcpy(&v->_vertex[old], rec->vertices.data(),
			sizeof(rvg_cx::Vertex) * rec->vertices.size());
	}

	// 2) 追加索引
	if (rec->indices.size() > 0)
	{
		uint32_t old = (uint32_t)v->_indices.size();
		v->_indices.resize(old + rec->indices.size());
		memcpy(&v->_indices[old], rec->indices.data(),
			sizeof(uint32_t) * rec->indices.size());
	}

	// 3) 追加命令（重写偏移 + 深拷贝 state 到目标 ctx 的 mac）
	for (size_t i = 0; i < rec->cmds.size(); i++)
	{
		auto c = rec->cmds[i];
		if (c.vg.stype == 0)
		{
			if (c.vg.vertex.y > 0) c.vg.vertex.x += (int)delta_vtx;
			if (c.vg.index.y > 0)   c.vg.index.x += (int)delta_idx;
			c.vg.state = rec_clone_state_into_target(v, c.vg.state);
		}
		v->cmdlist.push_back(c);
	}
}

void vctx_replay_command(rvg_t* ctx0, ovg_recording_t* rec, uint32_t cmdIndex)
{
	auto v = (rvg_cx*)ctx0;
	if (!v || !rec || cmdIndex >= rec->cmds.size()) return;

	auto& rc = rec->cmds[cmdIndex];
	if (rc.vg.stype != 0) return;   // 3D geom 暂不支持单命令回放

	uint32_t delta_vtx = (uint32_t)v->_vertex.size();
	uint32_t delta_idx = (uint32_t)v->_indices.size();

	// 只拷贝这条命令需要的顶点/索引
	if (rc.vg.vertex.y > 0)
	{
		uint32_t start = (uint32_t)rc.vg.vertex.x;
		uint32_t count = (uint32_t)rc.vg.vertex.y;
		if (start + count <= rec->vertices.size())
		{
			uint32_t old = (uint32_t)v->_vertex.size();
			v->_vertex.resize(old + count);
			memcpy(&v->_vertex[old], &rec->vertices[start],
				sizeof(rvg_cx::Vertex) * count);
		}
	}
	if (rc.vg.index.y > 0)
	{
		uint32_t start = (uint32_t)rc.vg.index.x;
		uint32_t count = (uint32_t)rc.vg.index.y;
		if (start + count <= rec->indices.size())
		{
			uint32_t old = (uint32_t)v->_indices.size();
			v->_indices.resize(old + count);
			memcpy(&v->_indices[old], &rec->indices[start],
				sizeof(uint32_t) * count);
		}
	}

	auto c = rc;
	if (c.vg.vertex.y > 0) c.vg.vertex.x += (int)delta_vtx;
	if (c.vg.index.y > 0)   c.vg.index.x += (int)delta_idx;
	c.vg.state = rec_clone_state_into_target(v, c.vg.state);

	v->cmdlist.push_back(c);
}

uint32_t vctx_recording_get_count(ovg_recording_t* rec)
{
	return rec ? (uint32_t)rec->cmds.size() : 0;
}

void* vctx_recording_get_data(ovg_recording_t* rec)
{
	return rec ? rec->cmds.data() : nullptr;
}

void vctx_recording_destroy(ovg_recording_t* rec)
{
	if (!rec) return;
	for (auto& o : rec->owned)
		free(o.first);
	delete rec;
}



void init_ovg_ctx_cb(ovg_ctx_cb* cb)
{
	if (!cb)return;
	cb->new_rvg = vctx_new_rvg;
	cb->destroy_rvg = vctx_destroy_rvg;
	cb->set_enable_tess2 = vctx_set_enable_tess2;
	cb->clear = vctx_clear;
	cb->get_path = vctx_get_path;
	cb->new_path = vctx_new_path;
	cb->clear_path = vctx_clear_path;
	cb->close_path = vctx_close_path;
	cb->new_sub_path = vctx_new_sub_path;
	cb->path_extents = vctx_path_extents;
	cb->get_current_point = vctx_get_current_point;
	cb->get_segment_count = vctx_get_segment_count;
	cb->set_segment_color = vctx_set_segment_color;
	cb->add_path = vctx_add_path;
	cb->move_to = vctx_move_to;
	cb->rel_move_to = vctx_rel_move_to;
	cb->line_to = vctx_line_to;
	cb->rel_line_to = vctx_rel_line_to;
	cb->arc = vctx_arc;
	cb->arc_negative = vctx_arc_negative;
	cb->curve_to = vctx_curve_to;
	cb->rel_curve_to = vctx_rel_curve_to;
	cb->quadratic_to = vctx_quadratic_to;
	cb->rel_quadratic_to = vctx_rel_quadratic_to;
	cb->rectangle = vctx_rectangle;
	cb->rounded_rectangle = vctx_rounded_rectangle;
	cb->rounded_rectangle2 = vctx_rounded_rectangle2;
	cb->ellipse = vctx_ellipse;
	cb->elliptic_arc_to = vctx_elliptic_arc_to;
	cb->rel_elliptic_arc_to = vctx_rel_elliptic_arc_to;
	cb->circle = vctx_circle;
	cb->set_opacity = vctx_set_opacity;
	cb->set_effect = vctx_set_effect;
	cb->set_source_color = vctx_set_source_color;
	cb->set_source_rgba = vctx_set_source_rgba;
	cb->set_source_rgb = vctx_set_source_rgb;
	cb->set_line_width = vctx_set_line_width;
	cb->set_miter_limit = vctx_set_miter_limit;
	cb->set_line_cap = vctx_set_line_cap;
	cb->set_line_join = vctx_set_line_join;
	cb->set_source_surface = vctx_set_source_surface;
	cb->set_source = vctx_set_source;
	cb->set_operator = vctx_set_operator;
	cb->set_fill_rule = vctx_set_fill_rule;
	cb->set_dash = vctx_set_dash;
	cb->set_dash8 = vctx_set_dash8;
	cb->translate = vctx_translate;
	cb->scale = vctx_scale;
	cb->rotate = vctx_rotate;
	cb->transform = vctx_transform;
	cb->set_matrix = vctx_set_matrix;
	cb->get_matrix = vctx_get_matrix;
	cb->identity_matrix = vctx_identity_matrix;
	cb->matrix_init = vctx_matrix_init;
	cb->new_pattern_linear = vctx_new_pattern_linear;
	cb->new_pattern_radial = vctx_new_pattern_radial;
	cb->new_pattern_sweep = vctx_new_pattern_sweep;
	cb->pattern_add_color_stop = vctx_pattern_add_color_stop;
	cb->pattern_set_color_stop = vctx_pattern_set_color_stop;
	cb->pattern_set_matrix = vctx_pattern_set_matrix;
	cb->pattern_set_extend = vctx_pattern_set_extend;
	cb->pattern_set_filter = vctx_pattern_set_filter;

	cb->image_update = ovg_image_update;
	cb->image_destroy = ovg_image_destroy;

	cb->save = vctx_save;
	cb->restore = vctx_restore;
	cb->stroke = vctx_stroke;
	cb->stroke_preserve = vctx_stroke_preserve;
	cb->fill = vctx_fill;
	cb->fill_preserve = vctx_fill_preserve;
	cb->paint = vctx_paint;
	cb->reset_clip = vctx_reset_clip;
	cb->clip = vctx_clip;
	cb->clip_preserve = vctx_clip_preserve;
	cb->clip_rect = vctx_clip_rect;
	cb->set_clip_rect = vctx_set_clip_rect;
	cb->get_clip_rect = vctx_get_clip_rect;
	cb->add_text = vctx_add_text;
	cb->add_image = vctx_add_image;
	cb->add_texture = vctx_add_texture;
	cb->set_geom_state = vctx_set_geom_state;
	cb->set_instance_mat = (size_t(*)(rvg_t*, const void*, size_t)) ovg_set_instance_mat;
	cb->add_geometry = vctx_add_geometry;
	cb->add_geometry3d = vctx_add_geometry3d;
	cb->start_recording = vctx_start_recording;
	cb->stop_recording = vctx_stop_recording;
	cb->replay = vctx_replay;
	cb->replay_command = vctx_replay_command;
	cb->recording_get_count = vctx_recording_get_count;
	cb->recording_get_data = vctx_recording_get_data;
	cb->recording_destroy = vctx_recording_destroy;

}
#endif // 1


void draw_grid_fill(rvg_t* vg, glm::vec2 size, glm::ivec2 cols, int width)
{
	int x = fmod(size.x, width);
	int y = fmod(size.y, width);
	int xn = size.x / width;
	int yn = size.y / width;
	if (x > 0)xn++;
	if (y > 0)yn++;

	vctx_rectangle(vg, 0, 0, size.x, size.y);
	vctx_clip(vg);
	auto c = cols[0];
	vctx_set_source_color(vg, c);
	for (size_t i = 0; i < yn; i++)
	{
		auto iw = i * width;
		for (size_t j = 0; j < xn; j++)
		{
			bool k0 = (j & 1);
			bool k1 = !(j & 1);
			auto k = !(i & 1) ? k0 : k1;
			if (k)
			{
				vctx_rectangle(vg, j * width, iw, width, width);
			}
		}
	}
	vctx_fill(vg);
	c = cols[1];
	vctx_set_source_color(vg, c);
	for (size_t i = 0; i < yn; i++)
	{
		auto iw = i * width;
		for (size_t j = 0; j < xn; j++)
		{
			bool k0 = (j & 1);
			bool k1 = !(j & 1);
			auto k = (i & 1) ? k0 : k1;
			if (k)
			{
				vctx_rectangle(vg, j * width, iw, width, width);
			}
		}
	}
	vctx_fill(vg);
}


// flex.c


#ifndef NO_FLEX_IMP


class flex_item :public flex_data
{
public:
	// size[0] == width, size[1] == height
	typedef void (*flex_self_sizing)(flex_item* item, float* size);
	void* managed_ptr = NULL;	// 用户数据指针
	flex_self_sizing self_sizing = NULL; // 运行时计算大小
	float frame[4] = {};	// 输出坐标、大小
	flex_item* parent = 0;	// 父级
	size_t line_count = 0;
	vg_alloc_cx* ac = nullptr;
	vg_vector<flex_item*> children;	// 子级
	vg_vector<char> temp_layout;
public:
	flex_item();
	~flex_item();

	void init();
	void set_allocator(vg_alloc_cx* a);
	void setdata(flex_data* d);
	void update_should_order_children();	// 子元素属性改变时执行

	void item_add(flex_item* child);
	void item_insert(uint32_t index, flex_item* child);
	flex_item* item_delete(uint32_t index);
	flex_item* detach(flex_item* child);
	// 清空子元素
	void clear();
	// 执行布局计算
	void layout();
private:
	void layout_items(uint32_t child_begin, uint32_t child_end, uint32_t children_count, struct flex_layout* layout, uint32_t last_count);
	void layout_item(float width, float height);
};

flex_item::flex_item()
{}

flex_item::~flex_item()
{}

void flex_item::set_allocator(vg_alloc_cx* a)
{
	if (ac == a)
		return;
	ac = a;
	children = vg_vector<flex_item*>(a);
	temp_layout = vg_vector<char>(a);
}



void flex_item::update_should_order_children()
{
	if (order != 0 && parent != NULL) {
		parent->should_order_children = true;
	}
}

void flex_item::init()
{
	children.clear();
	temp_layout.clear();
	parent = NULL;
	should_order_children = true;
	return;
}

void flex_item::setdata(flex_data* d)
{
	*((flex_data*)this) = *d;
}

void flex_item::item_add(flex_item* child)
{
	flex_item* item = this;
	children.push_back(child);
	child->parent = item;
	should_order_children = true;
	child->update_should_order_children();
}


void flex_item::item_insert(uint32_t index, flex_item* child)
{
	flex_item* item = this;
	children.insert(children.begin() + index, child);
	child->parent = item;
	child->update_should_order_children();
}


flex_item* flex_item::item_delete(uint32_t index)
{
	flex_item* child = children.data()[index];
	children.erase(children.begin() + index);
	if (child)
		child->parent = 0;
	return child;
}

flex_item* flex_item::detach(flex_item* c)
{
	size_t i = 0;
	flex_item* child = 0;
	auto p = children.data();
	for (i = 0; i < children.size(); i++)
	{
		if (p[i] == c)
		{
			child = c;
			break;
		}
	}
	children.erase(children.begin() + i);
	if (child)
		child->parent = 0;
	return child;
}

void flex_item::clear()
{
	children.clear();
}


flex_item* flex_item_root(flex_item* item)
{
	while (item->parent != NULL) {
		item = item->parent;
	}
	return item;
}
//
//#define FRAME_GETTER(name, index) \
//     float flex_item_get_frame_##name(flex_item *item) { \
//        return item->frame[index]; \
//    }
//
//FRAME_GETTER(x, 0)
//FRAME_GETTER(y, 1)
//FRAME_GETTER(width, 2)
//FRAME_GETTER(height, 3)
//
//#undef FRAME_GETTER

struct flex_layout {
	// Set during init.
	bool wrap;
	bool reverse;               // whether main axis is reversed
	bool reverse2;              // whether cross axis is reversed (wrap only)
	bool vertical;
	float size_dim;             // main axis parent size
	float align_dim;            // cross axis parent size
	uint32_t frame_pos_i;   // main axis position
	uint32_t frame_pos2_i;  // cross axis position
	uint32_t frame_size_i;  // main axis size
	uint32_t frame_size2_i; // cross axis size
	uint32_t* ordered_indices;
	size_t ordered_count;
	// Set for each line layout.
	float line_dim;             // the cross axis size
	float flex_dim;             // the flexible part of the main axis size
	float extra_flex_dim;       // sizes of flexible items
	float flex_grows;
	float flex_shrinks;
	float pos2;                 // cross axis position
	float baseline;
	// Calculated layout lines - only tracked when needed:
	//   - if the root's align_content property isn't set to FLEX_ALIGN_START
	//   - or if any child item doesn't have a cross-axis size set
	bool need_lines;
	struct flex_layout_line {
		uint32_t child_begin;
		uint32_t child_end;
		float size;
	};
	flex_layout_line* lines;
	size_t lines_idx;
	//uint32_t lines_count;
	float lines_sizes;
	//uint32_t lines_cap;
	size_t lines_idx0;
};

flex_align child_align(flex_item* child, flex_item* parent)
{
	auto align = child->align_self;
	if (align == flex_align::ALIGN_AUTO && parent) {
		align = parent->align_items;
	}
	return align;
}

void layout_init(flex_item* item, float width, float height, struct flex_layout* layout)
{
	assert(item->padding_left >= 0);
	assert(item->padding_right >= 0);
	assert(item->padding_top >= 0);
	assert(item->padding_bottom >= 0);
	width -= item->padding_left + item->padding_right;
	height -= item->padding_top + item->padding_bottom;
	assert(width >= 0);
	assert(height >= 0);

	layout->reverse = false;
	layout->vertical = true;
	switch (item->direction) {
	case flex_direction::ROW_REVERSE:
		layout->reverse = true;
	case flex_direction::ROW:
		layout->vertical = false;
		layout->size_dim = width;
		layout->align_dim = height;
		layout->frame_pos_i = 0;
		layout->frame_pos2_i = 1;
		layout->frame_size_i = 2;
		layout->frame_size2_i = 3;
		break;

	case flex_direction::COLUMN_REVERSE:
		layout->reverse = true;
	case flex_direction::COLUMN:
		layout->size_dim = height;
		layout->align_dim = width;
		layout->frame_pos_i = 1;
		layout->frame_pos2_i = 0;
		layout->frame_size_i = 3;
		layout->frame_size2_i = 2;
		break;

	default:
		assert(false && "incorrect direction");
	}

	//layout->ordered_indices.clear();
	if (item->should_order_children && item->children.size() > 0) {
		item->temp_layout.resize(item->children.size() * (sizeof(flex_layout::flex_layout_line) + sizeof(uint32_t)));
		layout->ordered_indices = (uint32_t*)item->temp_layout.data();
		auto indices = layout->ordered_indices;
		assert(indices != NULL);
		// Creating a list of item indices sorted using the children's `order'
		// attribute values. We are using a simple insertion sort as we need
		// stability (insertion order must be preserved) and cross-platform
		// support. We should eventually switch to merge sort (or something
		// else) if the number of items becomes significant enough.
		auto icv = item->children.data();
		for (uint32_t i = 0; i < item->children.size(); i++) {
			indices[i] = i;
			for (uint32_t j = i; j > 0; j--) {
				uint32_t prev = indices[j - 1];
				uint32_t curr = indices[j];
				if (icv[prev]->order <= icv[curr]->order) {
					break;
				}
				indices[j - 1] = curr;
				indices[j] = prev;
			}
		}
	}

	layout->flex_dim = 0;
	layout->flex_grows = 0;
	layout->flex_shrinks = 0;

	layout->reverse2 = false;
	layout->wrap = item->wrap != flex_wrap::NO_WRAP;
	if (layout->wrap) {
		if (item->wrap == flex_wrap::WRAP_REVERSE) {
			layout->reverse2 = true;
			layout->pos2 = layout->align_dim;
		}
	}
	else {
		layout->pos2 = layout->vertical
			? item->padding_left : item->padding_top;
	}

	layout->need_lines = layout->wrap && item->align_content != flex_align::ALIGN_START;
	layout->lines = (flex_layout::flex_layout_line*)(item->temp_layout.data() + sizeof(uint32_t) * item->children.size());
	layout->lines_idx = 0;
	layout->lines_idx0 = 1;
	layout->lines_sizes = 0;
	auto align_items = child_align(item, item->parent);
	if (align_items == flex_align::ALIGN_BASELINE)
	{
		layout->baseline = 0;
		for (auto& it : item->children) {
			layout->baseline = std::max(layout->baseline, it->baseline);
		}
	}
}

void layout_cleanup(struct flex_layout* layout)
{
	if (layout)
	{
		layout->ordered_indices = 0;
		layout->lines = 0;
	}
}

#define LAYOUT_RESET() \
    do { \
        layout->line_dim = layout->wrap ? 0 : layout->align_dim; \
        layout->flex_dim = layout->size_dim; \
        layout->extra_flex_dim = 0; \
        layout->flex_grows = 0; \
        layout->flex_shrinks = 0; \
    } \
    while (0)

#define LAYOUT_CHILD_AT(item, i) (item->children[(layout->ordered_count ? layout->ordered_indices[i] : i)])
//#define LAYOUT_CHILD_AT(item, i) ((*item->children)[(layout->ordered_count ? layout->ordered_indices[i] : i)])
//#define LAYOUT_CHILD_AT(item, i) ((*item->children)[(layout->ordered_indices.size() ? layout->ordered_indices[i] : i)])
//#define LAYOUT_CHILD_AT(item, i) (item->children.ary[(layout->ordered_indices != NULL ? layout->ordered_indices[i] : i)])  

#define _LAYOUT_FRAME(child, name) child->frame[layout->frame_##name##_i]

#define CHILD_POS(child) _LAYOUT_FRAME(child, pos)
#define CHILD_POS2(child) _LAYOUT_FRAME(child, pos2)
#define CHILD_SIZE(child) _LAYOUT_FRAME(child, size)
#define CHILD_SIZE2(child) _LAYOUT_FRAME(child, size2)

#define CHILD_MARGIN(child, if_vertical, if_horizontal) \
    (layout->vertical \
     ? child->margin_##if_vertical \
     : child->margin_##if_horizontal)


bool layout_align(flex_align align, float flex_dim, uint32_t children_count, float* pos_p, float* spacing_p, bool stretch_allowed)
{
	assert(flex_dim > 0);

	float pos = 0;
	float spacing = 0;
	switch (align) {
	case flex_align::ALIGN_START:
		break;

	case flex_align::ALIGN_END:
		pos = flex_dim;
		break;

	case flex_align::ALIGN_CENTER:
		pos = flex_dim / 2;
		break;

	case flex_align::ALIGN_SPACE_BETWEEN:
		if (children_count > 0) {
			spacing = flex_dim / (children_count - 1);
		}
		break;

	case flex_align::ALIGN_SPACE_AROUND:
		if (children_count > 0) {
			spacing = flex_dim / children_count;
			pos = spacing / 2;
		}
		break;

	case flex_align::ALIGN_SPACE_EVENLY:
		if (children_count > 0) {
			spacing = flex_dim / (children_count + 1);
			pos = spacing;
		}
		break;

	case flex_align::ALIGN_AUTO:
	case flex_align::ALIGN_STRETCH:
		if (stretch_allowed) {
			spacing = flex_dim / children_count;
			break;
		}
		// fall through
		break;
	default:
		return false;
	}

	*pos_p = pos;
	*spacing_p = spacing;
	return true;
}

void flex_item::layout_items(uint32_t child_begin, uint32_t child_end, uint32_t children_count, struct flex_layout* layout, uint32_t last_count)
{
	flex_item* item = this;
	assert(children_count <= (child_end - child_begin));
	if (children_count <= 0) {
		return;
	}
	if (last_count > 0 && last_count > children_count)
	{
		//children_count = last_count;
	}
	if (layout->flex_dim > 0 && layout->extra_flex_dim > 0) {
		// If the container has a positive flexible space, let's add to it
		// the sizes of all flexible children->
		layout->flex_dim += layout->extra_flex_dim;
	}

	// Determine the main axis initial position and optional spacing.
	float pos = 0;
	float spacing = 0;
	if (layout->flex_grows == 0 && layout->flex_dim > 0) {
		if (!layout_align(item->justify_content, layout->flex_dim,
			children_count, &pos, &spacing, false))
		{
			assert(0 && "incorrect justify_content");
		}
		if (layout->reverse) {
			pos = layout->size_dim - pos;
		}
	}

	if (layout->reverse) {
		pos -= layout->vertical ? item->padding_bottom : item->padding_right;
	}
	else {
		pos += layout->vertical ? item->padding_top : item->padding_left;
	}
	if (layout->wrap && layout->reverse2) {
		layout->pos2 -= layout->line_dim;
	}

	for (uint32_t i = child_begin; i < child_end; i++) {
		flex_item* child = LAYOUT_CHILD_AT(item, i);
		if (child->position == flex_position::POS_ABSOLUTE) {
			// Already positioned.
			continue;
		}

		// Grow or shrink the main axis item size if needed.
		float flex_size = 0;
		if (layout->flex_dim > 0) {
			if (child->grow != 0) {
				CHILD_SIZE(child) = 0; // Ignore previous size when growing.
				flex_size = (layout->flex_dim / layout->flex_grows)
					* child->grow;
			}
		}
		else if (layout->flex_dim < 0) {
			if (child->shrink != 0) {
				flex_size = (layout->flex_dim / layout->flex_shrinks)
					* child->shrink;
			}
		}
		CHILD_SIZE(child) += flex_size;

		// Set the cross axis position (and stretch the cross axis size if
		// needed).
		float align_size = CHILD_SIZE2(child);
		float align_pos = layout->pos2 + 0;
		switch (child_align(child, item)) {
		case flex_align::ALIGN_END:
			align_pos += layout->line_dim - align_size
				- CHILD_MARGIN(child, right, bottom);
			break;

		case flex_align::ALIGN_CENTER:
			align_pos += (layout->line_dim / 2) - (align_size / 2)
				+ (CHILD_MARGIN(child, left, top)
					- CHILD_MARGIN(child, right, bottom));
			break;

		case flex_align::ALIGN_STRETCH:
			if (align_size == 0) {
				CHILD_SIZE2(child) = layout->line_dim
					- (CHILD_MARGIN(child, left, top)
						+ CHILD_MARGIN(child, right, bottom));
			}
			// fall through
			align_pos += CHILD_MARGIN(child, left, top);
			break;
		case flex_align::ALIGN_START:
			align_pos += CHILD_MARGIN(child, left, top);
			break;
		case flex_align::ALIGN_BASELINE:
			align_pos += CHILD_MARGIN(child, left, top);
			if (child->baseline > 0) {
				align_pos += layout->baseline - child->baseline;
			}
			break;
		default:
			assert(false && "incorrect align_self");
		}
		CHILD_POS2(child) = align_pos;

		// Set the main axis position.
		if (layout->reverse) {
			pos -= CHILD_MARGIN(child, bottom, right);
			pos -= CHILD_SIZE(child);
			CHILD_POS(child) = pos;
			pos -= spacing;
			pos -= CHILD_MARGIN(child, top, left);
		}
		else {
			pos += CHILD_MARGIN(child, top, left);
			CHILD_POS(child) = pos;
			pos += CHILD_SIZE(child);
			pos += spacing;
			pos += CHILD_MARGIN(child, bottom, right);
		}

		// Now that the item has a frame, we can layout its children.
		child->layout_item(child->frame[2], child->frame[3]);
	}

	if (layout->wrap && !layout->reverse2) {
		layout->pos2 += layout->line_dim;
	}

	if (layout->need_lines) {
		flex_layout::flex_layout_line line[1] = {};
		line->child_begin = child_begin;
		line->child_end = child_end;
		line->size = layout->line_dim;
		layout->lines[layout->lines_idx] = (line[0]);
		layout->lines_idx++;
		//layout->lines.push_back(line[0]);
		layout->lines_sizes += line->size;
	}
}

void flex_item::layout_item(float width, float height)
{
	flex_item* item = this;
	if (item->children.size() == 0) {
		return;
	}

	struct flex_layout layout_s = { 0 }, * layout = &layout_s;
	layout_init(item, width, height, &layout_s);

	LAYOUT_RESET();
	uint32_t last_count = 0;
	uint32_t last_layout_child = 0;
	uint32_t relative_children_count = 0;
	for (uint32_t i = 0; i < item->children.size(); i++) {
		flex_item* child = LAYOUT_CHILD_AT(item, i);

		// Items with an absolute position have their frames determined
		// directly and are skipped during layout.
		if (child->position == flex_position::POS_ABSOLUTE) {
#define ABSOLUTE_SIZE(val, pos1, pos2, dim) \
            (!isnan(val) \
             ? val \
             : (!isnan(pos1) && !isnan(pos2) \
                 ? dim - pos2 - pos1 \
                 : 0))

#define ABSOLUTE_POS(pos1, pos2, size, dim) \
            (!isnan(pos1) \
             ? pos1 \
             : (!isnan(pos2) \
                 ? dim - size - pos2 \
                 : 0))

			float child_width = ABSOLUTE_SIZE(child->width, child->left,
				child->right, width);

			float child_height = ABSOLUTE_SIZE(child->height, child->top,
				child->bottom, height);

			float child_x = ABSOLUTE_POS(child->left, child->right,
				child_width, width);

			float child_y = ABSOLUTE_POS(child->top, child->bottom,
				child_height, height);

			child->frame[0] = child_x;
			child->frame[1] = child_y;
			child->frame[2] = child_width;
			child->frame[3] = child_height;

			// Now that the item has a frame, we can layout its children.
			child->layout_item(child->frame[2], child->frame[3]);

#undef ABSOLUTE_POS
#undef ABSOLUTE_SIZE
			continue;
		}

		// Initialize frame.
		child->frame[0] = 0;
		child->frame[1] = 0;
		child->frame[2] = child->width;
		child->frame[3] = child->height;

		// Main axis size defaults to 0.
		if (isnan(CHILD_SIZE(child))) {
			CHILD_SIZE(child) = 0;
		}

		// Cross axis size defaults to the parent's size (or line size in wrap
		// mode, which is calculated later on).
		if (isnan(CHILD_SIZE2(child))) {
			if (layout->wrap) {
				layout->need_lines = true;
			}
			else {
				CHILD_SIZE2(child) = (layout->vertical ? width : height)
					- CHILD_MARGIN(child, left, top)
					- CHILD_MARGIN(child, right, bottom);
			}
		}

		// Call the self_sizing callback if provided. Only non-NAN values
		// are taken into account. If the item's cross-axis align property
		// is set to stretch, ignore the value returned by the callback.
		if (child->self_sizing != NULL) {
			float size[2] = { child->frame[2], child->frame[3] };

			child->self_sizing(child, size);

			for (uint32_t j = 0; j < 2; j++) {
				uint32_t size_off = j + 2;
				if (size_off == layout->frame_size2_i
					&& child_align(child, item) == flex_align::ALIGN_STRETCH) {
					continue;
				}
				float val = size[j];
				if (!isnan(val)) {
					child->frame[size_off] = val;
				}
			}
		}

		// Honor the `basis' property which overrides the main-axis size.
		if (!(isnan(child->basis) || child->basis < 0)) {
			assert(child->basis >= 0);
			CHILD_SIZE(child) = child->basis;
		}

		float child_size = CHILD_SIZE(child);
		if (layout->wrap) {
			if (layout->flex_dim < child_size) {
				// Not enough space for this child on this line, layout the
				// remaining items and move it to a new line.
				item->layout_items(last_layout_child, i, relative_children_count, layout, last_count);

				LAYOUT_RESET();
				last_layout_child = i;
				if (last_count < relative_children_count)
					last_count = relative_children_count;
				relative_children_count = 0;
				layout->lines_idx0++;
			}

			float child_size2 = CHILD_SIZE2(child);
			if (!isnan(child_size2) && child_size2 > layout->line_dim) {
				layout->line_dim = child_size2;
			}
		}

		assert(child->grow >= 0);
		assert(child->shrink >= 0);

		layout->flex_grows += child->grow;
		layout->flex_shrinks += child->shrink;

		layout->flex_dim -= child_size
			+ (CHILD_MARGIN(child, top, left)
				+ CHILD_MARGIN(child, bottom, right));

		relative_children_count++;

		if (child_size > 0 && child->grow > 0) {
			layout->extra_flex_dim += child_size;
		}
	}

	// Layout remaining items in wrap mode, or everything otherwise.
	item->layout_items(last_layout_child, item->children.size(), relative_children_count, layout, last_count);

	// In wrap mode we may need to tweak the position of each line according to
	// the align_content property as well as the cross-axis size of items that
	// haven't been set yet.
	if (layout->need_lines && layout->lines_idx > 0) {
		float pos = 0;
		float spacing = 0;
		float flex_dim = layout->align_dim - layout->lines_sizes;
		if (flex_dim > 0) {
			if (!layout_align(item->align_content, flex_dim, layout->lines_idx, &pos, &spacing, true))
			{
				assert(0 && "incorrect align_content");
			}
		}

		float old_pos = 0;
		if (layout->reverse2) {
			pos = layout->align_dim - pos;
			old_pos = layout->align_dim;
		}

		for (uint32_t i = 0; i < layout->lines_idx; i++) {
			auto line = &layout->lines[i];

			if (layout->reverse2) {
				pos -= line->size;
				pos -= spacing;
				old_pos -= line->size;
			}

			// Re-position the children of this line, honoring any child
			// alignment previously set within the line.
			for (uint32_t j = line->child_begin; j < line->child_end;
				j++) {
				flex_item* child = LAYOUT_CHILD_AT(item, j);
				if (child->position == flex_position::POS_ABSOLUTE) {
					// Should not be re-positioned.
					continue;
				}
				if (isnan(CHILD_SIZE2(child))) {
					// If the child's cross axis size hasn't been set it, it
					// defaults to the line size.
					CHILD_SIZE2(child) = line->size
						+ (item->align_content == flex_align::ALIGN_STRETCH
							? spacing : 0);
				}
				CHILD_POS2(child) = pos + (CHILD_POS2(child) - old_pos);
			}

			if (!layout->reverse2) {
				pos += line->size;
				pos += spacing;
				old_pos += line->size;
			}
		}
	}
	line_count = layout->lines_idx > 0 ? layout->lines_idx : layout->lines_idx0;
	layout_cleanup(layout);
}

#undef CHILD_MARGIN
#undef CHILD_POS
#undef CHILD_POS2
#undef CHILD_SIZE
#undef CHILD_SIZE2
#undef _LAYOUT_FRAME
#undef LAYOUT_CHILD_AT
#undef LAYOUT_RESET


void flex_item::layout()
{
	assert(parent == NULL);
	assert(!isnan(width));
	assert(!isnan(height));
	assert(self_sizing == NULL);
	layout_item(width, height);
}
struct flex_run {
	vg_vector<flex_item> items;
	std::stack<node_dt*> q;
	usp_ac_cx* ac = 0;
	explicit flex_run(vg_alloc_cx* a) : items(a), ac((usp_ac_cx*)a) {}
};
flex_run* new_flex_run() {
	usp_ac_cx* ac = vg_default_ac();
	auto p = ac->new_obj<flex_run>(ac);
	if (p)
		p->ac = ac;
	return p;
}
void free_flex_run(flex_run* p) {
	if (p && p->ac)
		p->ac->free_obj(p);
}

// 输入样式数据，根节点指针，所有节点数量 
glm::vec4 flex_run_layout(flex_run* ctx, flex_data* fd, size_t count, node_dt* p, size_t node_count) {
	glm::vec4 rect = {};
	if (!fd || count == 0 || !p || !node_count || !p->child || !p->child_count || !ctx)
		return rect;
	auto& items = ctx->items;
	size_t old_count = items.size();
	items.resize(node_count);
	for (size_t i = old_count; i < items.size(); i++)
		items[i].set_allocator(ctx->ac);
	auto fitem = items.data();
	if (!fitem) return rect;
	for (size_t i = 0; i < node_count; i++) {
		fitem[i].init();
	}
	size_t idx = 0;
	auto q = ctx->q;  // 队列存储待处理坐标 
	while (q.size()) {
		q.pop();
	}
	q.push(p);
	p->tidx = idx;
	while (q.size()) {
		auto it = q.top(); q.pop();
		if (it && it->tidx < node_count)
		{
			auto& k = fitem[it->tidx];
			auto pidx = it->tidx;
			idx++;
			if (it->index < count)
			{
				k.setdata(fd + it->index);
			}
			else {
				k.setdata(fd);
			}
			k.position = it->position ? flex_position::POS_ABSOLUTE : flex_position::POS_RELATIVE;
			k.managed_ptr = it;
			k.baseline = it->baseline;
			k.width = it->size.x; k.height = it->size.y;
			k.left = it->offset.x; k.top = it->offset.y;
			k.right = it->offset.z; k.bottom = it->offset.w;
			for (size_t i = 0; i < it->child_count; i++) {
				it->child[i].parent = pidx;
				it->child[i].tidx = idx;
				q.push(it->child + i);
				k.item_add(fitem + idx);
				idx++;
			}
		}
	}
	fitem->layout();
	for (size_t i = 0; i < node_count; i++)
	{
		auto& it = fitem[i];
		auto pt = (node_dt*)it.managed_ptr;
		pt->frame = glm::vec4(it.frame[0], it.frame[1], it.frame[2], it.frame[3]);
		rect.x = std::min(rect.x, it.frame[0]);
		rect.y = std::min(rect.y, it.frame[1]);
		rect.z = std::max(rect.z, it.frame[2] + it.frame[0]);
		rect.w = std::max(rect.w, it.frame[3] + it.frame[1]);

	}
	p->line_count = fitem->line_count;
	return rect;
}

#if 1

#ifndef MALLOC
#define MALLOC(sz)    malloc(sz)
#define FREE(p)       free(p)
#endif

/* =====================================================================
 * 内部工具
 * ===================================================================== */
static int imin(int a, int b) { return a < b ? a : b; }

/* 轨道尺寸是否有效（>0 视为 FR，<=0 视为固定 0/未设置） */
static int is_fr(float v) { return v > 0.0f; }

/* =====================================================================
 * 生命周期
 * ===================================================================== */
grid_data* grid_create(int cols, int rows) {
	if (cols <= 0 || rows <= 0) return NULL;

	grid_data* g = (grid_data*)MALLOC(sizeof(grid_data));
	if (!g) return NULL;

	memset(g, 0, sizeof(grid_data));

	g->cols = cols;
	g->rows = rows;

	g->col_widths = (float*)MALLOC(sizeof(float) * (size_t)cols);
	g->row_heights = (float*)MALLOC(sizeof(float) * (size_t)rows);

	if (!g->col_widths || !g->row_heights) {
		FREE(g->col_widths);
		FREE(g->row_heights);
		FREE(g);
		return NULL;
	}

	/* 默认全部 1FR */
	for (int i = 0; i < cols; i++) g->col_widths[i] = 1.0f;
	for (int i = 0; i < rows; i++) g->row_heights[i] = 1.0f;

	g->justify_items = flex_align::ALIGN_STRETCH;
	g->align_items = flex_align::ALIGN_STRETCH;
	g->auto_flow = 0;   /* row */
	g->dense = 0;   /* sparse */

	return g;
}

void grid_destroy(grid_data* g) {
	if (!g) return;
	FREE(g->col_widths);
	FREE(g->row_heights);
	FREE(g);
}

/* =====================================================================
 * 轨道设置
 * ===================================================================== */
void grid_set_col_widths(grid_data* g, const float* widths, int cols) {
	if (!g || !widths || cols <= 0) return;
	int n = imin(cols, g->cols);
	for (int i = 0; i < n; i++) g->col_widths[i] = widths[i];
}

void grid_set_row_heights(grid_data* g, const float* heights, int rows) {
	if (!g || !heights || rows <= 0) return;
	int n = imin(rows, g->rows);
	for (int i = 0; i < n; i++) g->row_heights[i] = heights[i];
}

void grid_set_col(grid_data* g, int col, float width) {
	if (!g || col < 0 || col >= g->cols) return;
	g->col_widths[col] = width;
}

void grid_set_row(grid_data* g, int row, float height) {
	if (!g || row < 0 || row >= g->rows) return;
	g->row_heights[row] = height;
}

void grid_set_gap(grid_data* g, float gap_x, float gap_y) {
	if (!g) return;
	g->gap_x = gap_x;
	g->gap_y = gap_y;
}

void grid_set_gap_xy(grid_data* g, float gap) {
	if (!g) return;
	g->gap_x = gap;
	g->gap_y = gap;
}

void grid_set_auto_flow(grid_data* g, int auto_flow) {
	if (!g) return;
	g->auto_flow = auto_flow ? 1 : 0;
}

void grid_set_dense(grid_data* g, int dense) {
	if (!g) return;
	g->dense = dense ? 1 : 0;
}

void grid_set_justify_items(grid_data* g, flex_align a) {
	if (!g) return;
	g->justify_items = a;
}

void grid_set_align_items(grid_data* g, flex_align a) {
	if (!g) return;
	g->align_items = a;
}

/* =====================================================================
 * 轨道尺寸查询（布局后有效）
 * ===================================================================== */
float grid_get_col_width(const grid_data* g, int col) {
	if (!g || col < 0 || col >= g->cols) return 0.0f;
	return g->col_widths[col];
}

float grid_get_row_height(const grid_data* g, int row) {
	if (!g || row < 0 || row >= g->rows) return 0.0f;
	return g->row_heights[row];
}

/* =====================================================================
 * 第一步：分配 FR，把 FR 单位换算成最终像素
 *
 * 说明：本轻量实现采用"先全部按 FR 分配剩余空间"的策略。
 * 负数 / 0 视为无效，退化为 1FR，保证总能铺满容器。
 * 若要实现"固定 px + 剩余给 FR"的混合布局，见下方注释区。
 * ===================================================================== */
static void grid_calc_tracks(grid_data* g, float cw, float ch) {
	float total_gap_x = g->gap_x * (float)(g->cols - 1);
	float total_gap_y = g->gap_y * (float)(g->rows - 1);

	float avail_w = cw - total_gap_x;
	float avail_h = ch - total_gap_y;
	if (avail_w < 0.0f) avail_w = 0.0f;
	if (avail_h < 0.0f) avail_h = 0.0f;

	float fr_w = 0.0f, fr_h = 0.0f;
	for (int i = 0; i < g->cols; i++) if (is_fr(g->col_widths[i]))  fr_w += g->col_widths[i];
	for (int i = 0; i < g->rows; i++) if (is_fr(g->row_heights[i])) fr_h += g->row_heights[i];

	/* 退化保护：没有任何 FR 轨道时，全部视为 1FR */
	if (fr_w <= 0.0f) { for (int i = 0; i < g->cols; i++) g->col_widths[i] = 1.0f; fr_w = (float)g->cols; }
	if (fr_h <= 0.0f) { for (int i = 0; i < g->rows; i++) g->row_heights[i] = 1.0f; fr_h = (float)g->rows; }

	for (int i = 0; i < g->cols; i++) {
		if (is_fr(g->col_widths[i]))
			g->col_widths[i] = avail_w * (g->col_widths[i] / fr_w);
	}
	for (int i = 0; i < g->rows; i++) {
		if (is_fr(g->row_heights[i]))
			g->row_heights[i] = avail_h * (g->row_heights[i] / fr_h);
	}

	/*
	 * TODO（可选增强，未启用）：
	 * 混合布局支持 —— 固定 px 轨道优先扣除，剩余空间再分给 FR。
	 * 启用时把上面 FR 分配逻辑替换为：
	 *
	 *   float fixed_w = 0, fixed_h = 0;
	 *   for (int i = 0; i < g->cols; i++) if (!is_fr(g->col_widths[i]))  fixed_w += g->col_widths[i];
	 *   for (int i = 0; i < g->rows; i++) if (!is_fr(g->row_heights[i])) fixed_h += g->row_heights[i];
	 *   float real_avail_w = avail_w - fixed_w;   // 可能为负
	 *   float real_avail_h = avail_h - fixed_h;
	 *   if (real_avail_w < 0) real_avail_w = 0;
	 *   if (real_avail_h < 0) real_avail_h = 0;
	 *   for (int i = 0; i < g->cols; i++) {
	 *       if (!is_fr(g->col_widths[i])) continue;
	 *       g->col_widths[i] = fr_w > 0 ? real_avail_w * (g->col_widths[i] / fr_w) : 0;
	 *   }
	 *   // rows 同理
	 */
}

/* =====================================================================
 * 第二步：跨度单元格尺寸累加
 * ===================================================================== */
static float cell_span_size(const float* tracks, float gap, int start, int span) {
	float total = 0.0f;
	for (int i = 0; i < span; i++) {
		total += tracks[start + i];
		if (i < span - 1) total += gap;
	}
	return total;
}

static float track_start_pos(const float* tracks, float gap, int idx) {
	float p = 0.0f;
	for (int i = 0; i < idx; i++) p += tracks[i] + gap;
	return p;
}

/* =====================================================================
 * 第三步：单元格内对齐
 * ===================================================================== */
static void grid_align_in_cell(
	const grid_data* g,
	const grid_child* gc,
	float cell_w, float cell_h,
	float item_w, float item_h,
	float* ox, float* oy
) {
	flex_align js = (gc && gc->justify_self != flex_align::ALIGN_AUTO) ? gc->justify_self : g->justify_items;
	flex_align al = (gc && gc->align_self != flex_align::ALIGN_AUTO) ? gc->align_self : g->align_items;

	*ox = 0.0f;
	*oy = 0.0f;

	if (js == flex_align::ALIGN_CENTER)       *ox = (cell_w - item_w) * 0.5f;
	else if (js == flex_align::ALIGN_END)     *ox = cell_w - item_w;
	/* STRETCH / START -> 占满或贴边，ox = 0 */

	if (al == flex_align::ALIGN_CENTER)       *oy = (cell_h - item_h) * 0.5f;
	else if (al == flex_align::ALIGN_END)     *oy = cell_h - item_h;
}

/* =====================================================================
 * 单元格占用标记
 * ===================================================================== */
typedef struct {
	int cols;
	int rows;
	uint8_t* bits;   /* rows * cols，1 = 已占用 */
} occ_map;

static occ_map* occ_create(int cols, int rows) {
	occ_map* o = (occ_map*)MALLOC(sizeof(occ_map));
	if (!o) return NULL;
	o->cols = cols;
	o->rows = rows;
	o->bits = (uint8_t*)MALLOC((size_t)cols * (size_t)rows);
	if (!o->bits) { FREE(o); return NULL; }
	memset(o->bits, 0, (size_t)cols * (size_t)rows);
	return o;
}

static void occ_destroy(occ_map* o) {
	if (!o) return;
	FREE(o->bits);
	FREE(o);
}

static int occ_test(const occ_map* o, int c, int r, int cspan, int rspan) {
	if (c < 0 || r < 0) return 0;
	if (c + cspan > o->cols || r + rspan > o->rows) return 0;
	for (int rr = 0; rr < rspan; rr++) {
		for (int cc = 0; cc < cspan; cc++) {
			if (o->bits[(r + rr) * o->cols + (c + cc)]) return 0;
		}
	}
	return 1;
}

static void occ_mark(occ_map* o, int c, int r, int cspan, int rspan) {
	for (int rr = 0; rr < rspan; rr++)
		for (int cc = 0; cc < cspan; cc++)
			o->bits[(r + rr) * o->cols + (c + cc)] = 1;
}

/*
 * dense 回溯：从 (c, r) 往后找一个能放下 cspan x rspan 的空洞。
 * 返回 1 表示找到并标记，0 表示找不到（调用方应追加到末尾）。
 */
static int occ_find_hole(
	const occ_map* o, int* c, int* r,
	int cspan, int rspan, int auto_flow
) {
	int cc = *c, rr = *r;
	if (auto_flow == 0) {
		/* row 主序：逐行扫 */
		while (rr + rspan <= o->rows) {
			while (cc + cspan <= o->cols) {
				if (occ_test(o, cc, rr, cspan, rspan)) {
					*c = cc; *r = rr;
					return 1;
				}
				cc++;
			}
			cc = 0;
			rr++;
		}
	}
	else {
		/* column 主序：逐列扫 */
		while (cc + cspan <= o->cols) {
			while (rr + rspan <= o->rows) {
				if (occ_test(o, cc, rr, cspan, rspan)) {
					*c = cc; *r = rr;
					return 1;
				}
				rr++;
			}
			rr = 0;
			cc++;
		}
	}
	return 0;
}

/*
 * 在 auto-flow 末尾游标之后推进，找到下一个能放下该元素的空位。
 */
static int advance_cursor(
	occ_map* o, int* c, int* r,
	int cspan, int rspan, int auto_flow
) {
	int cc = *c, rr = *r;
	if (auto_flow == 0) {
		do {
			cc++;
			if (cc + cspan > o->cols) {
				cc = 0;
				rr++;
			}
			if (rr + rspan > o->rows) return 0;
		} while (!occ_test(o, cc, rr, cspan, rspan));
	}
	else {
		do {
			rr++;
			if (rr + rspan > o->rows) {
				rr = 0;
				cc++;
			}
			if (cc + cspan > o->cols) return 0;
		} while (!occ_test(o, cc, rr, cspan, rspan));
	}
	*c = cc;
	*r = rr;
	return 1;
}

/* =====================================================================
 * 第四步：主布局
 * ===================================================================== */
void grid_layout(
	grid_data* g,
	float container_width,
	float container_height,
	node_dt* children,
	size_t count
) {
	if (!g || !children || count == 0) return;

	grid_calc_tracks(g, container_width, container_height);

	occ_map* occ = occ_create(g->cols, g->rows);
	if (!occ) return;

	/* auto-flow 游标 */
	int flow_c = 0;
	int flow_r = 0;

	for (size_t i = 0; i < count; i++) {
		grid_child* gc = NULL;
		if (children[i].user) gc = (grid_child*)children[i].user;

		int explicit_pos = gc ? gc->explicit_pos : 0;
		int col = 0, row = 0, cspan = 1, rspan = 1;

		if (explicit_pos && gc) {
			col = gc->col;
			row = gc->row;
			cspan = gc->col_span > 0 ? gc->col_span : 1;
			rspan = gc->row_span > 0 ? gc->row_span : 1;

			/* 边界钳制 */
			if (col < 0) col = 0;
			if (row < 0) row = 0;
			if (col + cspan > g->cols) {
				if (col >= g->cols) col = g->cols - 1;
				cspan = g->cols - col;
			}
			if (row + rspan > g->rows) {
				if (row >= g->rows) row = g->rows - 1;
				rspan = g->rows - row;
			}
		}
		else {
			/* ---- auto-flow ---- */
			cspan = (gc && gc->col_span > 0) ? gc->col_span : 1;
			rspan = (gc && gc->row_span > 0) ? gc->row_span : 1;

			if (g->dense) {
				/* dense：回溯找空洞 */
				if (!occ_find_hole(occ, &flow_c, &flow_r, cspan, rspan, g->auto_flow)) {
					/*
					 * 没找到空洞 -> 从原点开始按稀疏规则往后排。
					 * 这里简化为把游标重置到当前 auto-flow 的"末尾"，
					 * 即上次放置位置之后。
					 */
				}
			}

			/* 从当前游标推进到下一个合法空位 */
			if (!occ_test(occ, flow_c, flow_r, cspan, rspan)) {
				if (!advance_cursor(occ, &flow_c, &flow_r, cspan, rspan, g->auto_flow)) {
					/* 网格已满，剩余元素全部叠放在最后一个格子（降级处理） */
					flow_c = g->cols - cspan;
					flow_r = g->rows - rspan;
					if (flow_c < 0) flow_c = 0;
					if (flow_r < 0) flow_r = 0;
				}
			}

			col = flow_c;
			row = flow_r;

			/* sparse 模式：更新游标到下一个位置 */
			if (!g->dense) {
				advance_cursor(occ, &flow_c, &flow_r, 1, 1, g->auto_flow);
			}
		}

		/* 标记占用 */
		occ_mark(occ, col, row, cspan, rspan);

		/* ---- 计算单元格位置与尺寸 ---- */
		float x = track_start_pos(g->col_widths, g->gap_x, col);
		float y = track_start_pos(g->row_heights, g->gap_y, row);

		float cell_w = cell_span_size(g->col_widths, g->gap_x, col, cspan);
		float cell_h = cell_span_size(g->row_heights, g->gap_y, row, rspan);

		/* 子元素期望尺寸 */
		float item_w = children[i].size.x;
		float item_h = children[i].size.y;

		/* STRETCH：元素尺寸跟随单元格（减去 padding 留给调用方） */
		if (gc) {
			if (gc->justify_self == flex_align::ALIGN_STRETCH || (!gc->explicit_pos && g->justify_items == flex_align::ALIGN_STRETCH)) {
				if (item_w <= 0.0f) item_w = cell_w;
			}
			if (gc->align_self == flex_align::ALIGN_STRETCH || (!gc->explicit_pos && g->align_items == flex_align::ALIGN_STRETCH)) {
				if (item_h <= 0.0f) item_h = cell_h;
			}
		}
		else {
			if (g->justify_items == flex_align::ALIGN_STRETCH && item_w <= 0.0f) item_w = cell_w;
			if (g->align_items == flex_align::ALIGN_STRETCH && item_h <= 0.0f) item_h = cell_h;
		}

		float ox = 0.0f, oy = 0.0f;
		grid_align_in_cell(g, gc, cell_w, cell_h, item_w, item_h, &ox, &oy);

		children[i].frame.x = x + ox;
		children[i].frame.y = y + oy;
		children[i].frame.z = item_w;
		children[i].frame.w = item_h;
	}

	occ_destroy(occ);
}

#endif // 1

#endif // !NO_FLEX_IMP 

struct font_familys_cx :public font_familys_t {
	vg_vector<font_family_t*> v;
	usp_ac_cx* ac = 0;
	explicit font_familys_cx(vg_alloc_cx* a) : v(a), ac((usp_ac_cx*)a) {}
};
void vg_split(std::string str, const std::string& pattern, std::vector<std::string>& result)
{
	std::string::size_type pos;
	str += pattern;//扩展字符串以方便操作
	int size = str.size();
	result.clear();
	int ct = 0;
	for (int i = 0; i < size; i++)
	{
		pos = str.find(pattern, i);
		if (pos < size)
		{
			std::string s = str.substr(i, pos - i);
			result.push_back(s);
			i = pos + pattern.size() - 1;
			ct++;
		}
	}
}
font_cache_cx* new_font_cache()
{
#ifdef OVG_FONT_H
	auto p = new font_cache_cx();
	if (p)
	{
		auto ac = new usp_ac_cx();
		if (!ac)
		{
			delete p; p = 0;
		}
		else
		{
			p->set_alloc_ptr(ac);
		}
	}
	return p;
#endif
	return 0;
}
void free_font_cache(font_cache_cx* p)
{
#ifdef OVG_FONT_H
	if (p) {
		p->references--;
		if (p->references > 0) return;
		if (p->ac)delete p->ac;
		delete p;
	}
#endif
}
font_familys_t* new_font_family(font_cache_cx* ctx, const char* familys, const char* styles) {
	if (!ctx || !familys || !*familys)return nullptr;
	font_familys_cx* p = 0;
#ifdef OVG_FONT_H
	auto ac = (usp_ac_cx*)ctx->ac;
	do {
		std::vector<std::string> v, st;
		vg_split(familys, ",", v);
		if (styles && *styles)
			vg_split(styles, ",", st);
		if (v.empty() || !ctx || !ac)
		{
			break;
		}
		const char* style = st.size() ? st[0].c_str() : nullptr;
		size_t ix = 1;
		p = (font_familys_cx*)ac->new_obj<font_familys_cx>(ac);
		if (!p) {
			return 0;
		}
		p->count = 0;
		for (auto& it : v)
		{
			auto font = ctx->get_font(it.c_str(), style, 0, 0);
			if (ix < st.size())
			{
				style = st.size() ? st[ix].c_str() : nullptr;
			}
			ix++;
			if (font)
			{
				auto ff = (font_family_t*)font;
				p->v.push_back(ff);
			}
		}
		p->familys = p->v.data();
		p->count = p->v.size();
		p->ac = ac;
	} while (0);
#endif
	return p;
}
void delete_font_family(font_familys_t* p) {
	auto p1 = (font_familys_cx*)p;
	if (p && p1->ac) {
		p1->ac->free_obj(p1);
	}
}

#ifdef OVG_FONT_H
// 位图字形按 atlas 合批。缓存只清长度、保留 capacity，
// 因此连续帧/同一文本的多次提交（阴影、描边、主文本）不会反复分配顶点缓冲。
void build_raster_batches(text_raster_cache& cache, const text_draw_list* list)
{
	cache.begin();

	size_t glyph_count = 0;
	for (const auto& cmd : list->cmds) {
		if (cmd.type == glyph_draw_cmd::RASTER)
			++glyph_count;
	}
	if (!glyph_count)
		return;

	for (const auto& cmd : list->cmds) {
		if (cmd.type != glyph_draw_cmd::RASTER)
			continue;

		auto& b = cache.get(cmd.entry->atlas_img, glyph_count);
		uint16_t base = (uint16_t)(b.verts.size() / 2);

		float x0 = cmd.pos.x + cmd.entry->offset.x;
		float y0 = cmd.pos.y - cmd.entry->offset.y;
		float w = cmd.size.x;
		float h = cmd.size.y;
		b.verts.push_back(x0);     b.verts.push_back(y0);
		b.verts.push_back(x0 + w); b.verts.push_back(y0);
		b.verts.push_back(x0 + w); b.verts.push_back(y0 + h);
		b.verts.push_back(x0);     b.verts.push_back(y0 + h);

		b.uvs.push_back(cmd.uv_rect.x); b.uvs.push_back(cmd.uv_rect.y);
		b.uvs.push_back(cmd.uv_rect.z); b.uvs.push_back(cmd.uv_rect.y);
		b.uvs.push_back(cmd.uv_rect.z); b.uvs.push_back(cmd.uv_rect.w);
		b.uvs.push_back(cmd.uv_rect.x); b.uvs.push_back(cmd.uv_rect.w);

		auto color = cmd.color;
		if (list->has_color && cmd.entry->has_color)
			color = -1;
		b.colors.push_back(color);
		b.colors.push_back(color);
		b.colors.push_back(color);
		b.colors.push_back(color);

		b.idx.push_back(base + 0);
		b.idx.push_back(base + 1);
		b.idx.push_back(base + 2);
		b.idx.push_back(base + 0);
		b.idx.push_back(base + 2);
		b.idx.push_back(base + 3);
	}
}

// 每个 atlas 一次 add_geometry；对象模式和 ctx 模式的提交逻辑相同
template<class CB>
void submit_raster_batches(CB* cb, rvg_t* rvg, text_raster_cache& cache)
{
	for (auto& [atlas_img, b] : cache.batches()) {
		if (b.verts.empty())
			continue;

		auto img = (ovg_image_data*)atlas_img;
		vg_image_desc_t desc = {};
		if (img->valid)
		{
			desc.width = img->width;
			desc.height = img->height;
			desc.format = VG_FORMAT_BGRA8;
			desc.stride = desc.width * sizeof(int);
			desc.pixels = img->data;
			desc.x = 0, desc.y = 0, desc.w = img->width, desc.h = img->height;		// 更新矩形区域
			desc.is_copy = false;
			img->valid = false;
			//std::string fn = "temp/font_pack_ovg.png";
			//write_png_bgra(fn.c_str(), (uint8_t*)img->data, img->width, img->height);
			cb->image_update(rvg, img, &desc);
		}
		cb->add_geometry(
			rvg, img,
			b.verts.data(), sizeof(float) * 2,
			b.colors.data(), sizeof(uint32_t),
			b.uvs.data(), sizeof(float) * 2,
			(int)b.verts.size() / 2,
			b.idx.data(), (int)b.idx.size(),
			2, 1
		);
	}
}

// 对象模式：整段文本复用同一个 path/state，避免每个字形都从 内存管理 分配并释放
void submit_vector_cmds(ovg_canvas_cb* cb, rvg_t* rvg, const text_draw_list* list, bool stroke_mode, float stroke_width)
{
	ovg_path_t* path = nullptr;
	vg_state_save_t* st = nullptr;
	ovg_path_t* old_path = rvg->path;
	vg_state_save_t* old_st = rvg->st;

	for (const auto& cmd : list->cmds) {
		if (cmd.type != glyph_draw_cmd::VECTOR)
			continue;

		if (!path) {
			path = cb->new_path(cb->ac);
			if (!path)
				break;
			st = cb->new_state(cb->ac);
			if (!st)
				break;
		}

		cb->clear_path(path);

		// 复用 state：先复位上一个字形的矩阵，再设置当前字形
		cb->identity_matrix(st);
		cb->set_source_color(st, cmd.color);
		float scale = (float)cmd.fontsize / (float)cmd.entry->em_units;
		cb->translate(st, cmd.pos.x, cmd.pos.y);
		cb->scale(st, scale, scale);
		if (stroke_mode)
			cb->set_line_width(st, stroke_width / scale);

		// add_path 内的曲线细分会用 path->t 的缩放计算像素容差，
		// 必须在灌入路径前绑定当前字形的变换，否则会退化到身份矩阵过度细分。
		path->t = st;
		cb->add_path(path, cmd.entry->path_data, cmd.entry->path_size);

		cb->set_path(rvg, path, st);
		if (stroke_mode)
			cb->stroke(rvg);	// ← stroke 而非 fill
		else
			cb->fill(rvg);
	}

	// 临时 path/state 销毁前恢复 rvg 自己的指针，避免后续 clip 等操作访问已释放对象
	rvg->path = old_path;
	rvg->st = old_st;
	if (path)
		cb->destroy_path(path);
	if (st)
		cb->state_destroy(st);
}

// ctx 模式：整段文本只 save/restore 一次，逐字形恢复基准矩阵后重新灌入路径
void submit_vector_cmds_ctx(ovg_ctx_cb* cb, rvg_t* rvg, const text_draw_list* list, bool stroke_mode, float stroke_width)
{
	bool saved = false;
	glm::mat3x2 base_mat = glm::mat3x2(1.0);
	for (const auto& cmd : list->cmds) {
		if (cmd.type != glyph_draw_cmd::VECTOR)
			continue;

		if (!saved) {
			cb->save(rvg);
			cb->get_matrix(rvg, &base_mat);	// 保留调用方当前变换，文字仍跟随 ctx 变换
			saved = true;
		}

		// 复用当前 state：先恢复基准矩阵，避免上一个字形的变换累积
		cb->set_matrix(rvg, &base_mat);
		cb->set_source_color(rvg, cmd.color);
		float scale = (float)cmd.fontsize / (float)cmd.entry->em_units;
		cb->translate(rvg, cmd.pos.x, cmd.pos.y);
		cb->scale(rvg, scale, scale);
		if (stroke_mode)
			cb->set_line_width(rvg, stroke_width / scale);

		cb->new_path(rvg);
		// 同上：曲线细分容差依赖 path->t 的缩放
		if (rvg->path)
			rvg->path->t = rvg->st;
		cb->add_path(rvg, cmd.entry->path_data, cmd.entry->path_size);

		if (stroke_mode)
			cb->stroke(rvg);	// ← stroke 而非 fill
		else
			cb->fill(rvg);
	}
	if (saved)
		cb->restore(rvg);
}

// ovg_text.cpp 
void ovg_canvas_cx::submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width)
{
	submit_vector_cmds(this, rvg, list, true, stroke_width);
}
void ovg_canvas_cx::submit_draw_list(rvg_t* rvg, const text_draw_list* list)
{
	auto cb = this;
	gem_info_t info2d = {};
	info2d.blendMode = (uint8_t)blendMode_e::normal_prem;
	info2d.topology = 0;
	info2d.flags = (uint8_t)depth_stencil_State::d_stenciltest_enable;
	info2d.frontFace = 0;
	info2d.cullMode = 0;
	info2d.shader = ST_NONE;
	auto mat = ovg_ortho(rvg->width, rvg->height, -1.0f, 1.0f, 0);
	cb->set_geom_state(rvg, &info2d, &mat);

	// 位图 glyph：按 atlas 合批（容器复用）
	build_raster_batches(batches, list);
	submit_raster_batches(cb, rvg, batches);

	// 矢量 glyph：整段复用 path/state
	submit_vector_cmds(cb, rvg, list, false, 0.0f);
}
void ovg_ctx_cx::submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width)
{
	submit_vector_cmds_ctx(this, rvg, list, true, stroke_width);
}
void ovg_ctx_cx::submit_draw_list(rvg_t* rvg, const text_draw_list* list)
{
	auto cb = this;
	gem_info_t info2d = {};
	info2d.blendMode = (uint8_t)blendMode_e::normal_prem;
	info2d.topology = 0;
	info2d.flags = (uint8_t)depth_stencil_State::d_stenciltest_enable;
	info2d.frontFace = 0;
	info2d.cullMode = 0;
	info2d.shader = ST_NONE;
	auto mat = ovg_ortho(rvg->width, rvg->height, -1.0f, 1.0f, 0);
	cb->set_geom_state(rvg, &info2d, &mat);

	// 位图 glyph：按 atlas 合批（容器复用）
	build_raster_batches(batches, list);
	submit_raster_batches(cb, rvg, batches);

	// 矢量 glyph：整段只 save/restore 一次
	submit_vector_cmds_ctx(cb, rvg, list, false, 0.0f);
}
void vg_add_text_run(ovg_canvas_cb* ocb, ovg_ctx_cb* ctx, rvg_t* rvg, vg_text_run_cx* run)
{
	if (!run || (!ocb && !ctx)) return;

	auto ctxp = (ovg_ctx_cx*)ctx;
	auto ocbp = (ovg_canvas_cx*)ocb;
	if (run->glyph_count() == 0) return;
	auto ts = &run->_st;
	auto box = &run->_box;
	int fontsize = ts->fontsize > 0 ? (int)ts->fontsize : 16;
	// ── 2. 布局计算 ──
	const auto& extents = run->extents();

	// 对齐偏移
	float box_w = box && box->rc.z > 0 ? (float)box->rc.z : extents.width;
	float box_h = box && box->rc.w > 0 ? (float)box->rc.w : extents.height;

	float align_x = box->text_align.x;  // 0=左, 0.5=中, 1=右
	float align_y = box->text_align.y;

	int offset_x = run->_tt.pos.x + (box_w - extents.width) * align_x;
	int offset_y = run->_tt.pos.y + (box_h - extents.height) * align_y;
	// 基线位置 = offset_y + ascender
	hb_font_extents_t fextents = {};
	// 用主字体取 ascender
	hb_font_t* primary = ts->family->familys[0]->font;
	hb_font_set_scale(primary, fontsize, fontsize);
	hb_font_get_extents_for_direction(primary, HB_DIRECTION_LTR, &fextents);
	int baseline_y = offset_y + box->baseline;

	text_draw_list& main_list = run->drawable;
	main_list.has_color = false;
	// ── 4. 阴影 ──
	if (ts->color_shadow & 0xFF000000) {
		main_list.clear();
		run->populate_draw_list(main_list, offset_x + ts->shadow_pos.x, baseline_y + ts->shadow_pos.y, ts->color_shadow, vg_text_run_cx::RASTER_FIRST);
		if (ctxp)
			submit_draw_list(*ctxp, rvg, &main_list);
		else if (ocbp)
			submit_draw_list(*ocbp, rvg, &main_list);
	}

	// ── 5. 描边 ──
	if (ts->stroke != 0 && ts->color_stroke & 0xFF000000) {
		main_list.clear();
		float stroke = abs(ts->stroke);
		if (ts->stroke > 0) {
			stroke *= 2;
			run->populate_draw_list(main_list, offset_x, baseline_y, ts->color_stroke, vg_text_run_cx::VECTOR_ONLY);
			if (ctxp)
				submit_vector_glyphs_stroked(*ctxp, rvg, &main_list, stroke);
			else if (ocbp)
				submit_vector_glyphs_stroked(*ocbp, rvg, &main_list, stroke);
		}
		else {
			const int stroke_px = (int)stroke;
			int pxx[4] = { -stroke_px, 0, stroke_px, 0 };
			int pyy[4] = { 0, -stroke_px, 0, stroke_px };
			for (int e = 0; e < 4; e++)
			{
				glm::vec2 ps1 = { offset_x, baseline_y };
				ps1.x += pxx[e];
				ps1.y += pyy[e];
				run->populate_draw_list(main_list, ps1.x, ps1.y, ts->color_stroke, vg_text_run_cx::RASTER_FIRST);
			}
			if (ctxp)
				submit_draw_list(*ctxp, rvg, &main_list);
			else if (ocbp)
				submit_draw_list(*ocbp, rvg, &main_list);
		}
	}

	// ── 6. 主文本 ──
	main_list.clear();
	main_list.has_color = true;
	run->populate_draw_list(main_list, offset_x, baseline_y, ts->color, vg_text_run_cx::RASTER_FIRST);
	if (ctxp)
		submit_draw_list(*ctxp, rvg, &main_list);
	else if (ocbp)
		submit_draw_list(*ocbp, rvg, &main_list);

}

void vg_add_text(ovg_canvas_cb* ocb, ovg_ctx_cb* ctx, rvg_t* rvg, text_st_t* p, text_style_t* ts, text_box_rt* box)
{
	if (!p || !p->text || !ts || !ts->family || (!ocb && !ctx)) return;

	auto ctxp = (ovg_ctx_cx*)ctx;
	auto ocbp = (ovg_canvas_cx*)ocb;

	// ── 1. shape：按文本/字体缓存，命中时不再重复 ICU/HarfBuzz ──
	vg_text_run_cx* run = nullptr;
	if (ctxp)
		run = ctxp->text_cache.get(p, ts, box);
	else if (ocbp)
		run = ocbp->text_cache.get(p, ts, box);
	if (!run || run->glyph_count() == 0)
		return;
	vg_add_text_run(ocb, ctx, rvg, run);
}
#else

void ovg_canvas_cx::submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width)
{}
void ovg_canvas_cx::submit_draw_list(rvg_t* rvg, const text_draw_list* list)
{}

void ovg_ctx_cx::submit_vector_glyphs_stroked(rvg_t* rvg, const text_draw_list* list, float stroke_width)
{}
void ovg_ctx_cx::submit_draw_list(rvg_t* rvg, const text_draw_list* list)
{}
void vg_add_text(ovg_canvas_cb* ocb, ovg_ctx_cb* ctx, rvg_t* rvg, text_st_t* p, text_style_t* ts, text_box_rt* box)
{
	if (!p || !p->text || !ts || !ts->family || (!ocb && !ctx)) return;

}
#endif
