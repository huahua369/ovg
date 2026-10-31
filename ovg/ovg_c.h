#pragma once
/*
矢量/图片/文本/三角形录制到rvg_t对象
*/
#ifndef OVG_H
#define OVG_H

#include <stdint.h>

typedef struct vg_fbo_t vg_fbo_t;

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
using uvec2 = glm::uvec2;
using uvec3 = glm::uvec3;
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using ivec2 = glm::ivec2;
using ivec4 = glm::ivec4;
using mat4 = glm::mat4;
using mat3x2 = glm::mat3x2;
class vg_alloc_cx
{
public:
	vg_alloc_cx();
	virtual ~vg_alloc_cx();
	virtual void* alloc(const size_t _Bytes, const size_t align);
	virtual void dealloc(void* t, size_t n);

	// These fields mirror mem_resource_t so a C++ allocator can be passed
	// through the C API without an adapter object.
	size_t _Align = 16;
	void* ptr = 0;
private:

};

/* Process-wide allocator used when a container has no explicit context.
 * The default vg_alloc_cx implementation uses malloc/free. */
vg_alloc_cx* vg_get_default_allocator();

/* Minimal C++ allocator used by OVG's standard containers.  Passing a context
 * routes all allocations through the caller's vg_alloc_cx implementation;
 * leaving it null uses vg_get_default_allocator(). */
template<class T>
class vg_allocator
{
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using pointer = T*;
	using const_pointer = const T*;
	using propagate_on_container_move_assignment = std::true_type;
	using propagate_on_container_copy_assignment = std::true_type;
	using propagate_on_container_swap = std::true_type;
	using is_always_equal = std::false_type;

	template<class U>
	struct rebind
	{
		using other = vg_allocator<U>;
	};

	vg_allocator() noexcept = default;
	vg_allocator(vg_alloc_cx* context) noexcept : ac(context) {}
	template<class U>
	vg_allocator(const vg_allocator<U>& other) noexcept : ac(other.ac) {}

	T* allocate(std::size_t n)
	{
		if (n > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
			throw std::bad_alloc();
		vg_alloc_cx* context = ac ? ac : vg_get_default_allocator();
		void* p = context->alloc(n * sizeof(T), alignof(T));
		if (!p)
			throw std::bad_alloc();
		return static_cast<T*>(p);
	}

	void deallocate(T* p, std::size_t n) noexcept
	{
		if (!p)
			return;
		vg_alloc_cx* context = ac ? ac : vg_get_default_allocator();
		context->dealloc(p, n * sizeof(T));
	}

	template<class U>
	bool operator==(const vg_allocator<U>& other) const noexcept
	{
		return ac == other.ac;
	}

	template<class U>
	bool operator!=(const vg_allocator<U>& other) const noexcept
	{
		return !(*this == other);
	}

private:
	vg_alloc_cx* ac = nullptr;
	template<class U> friend class vg_allocator;
};

template<class T>
using vg_vector = std::vector<T, vg_allocator<T>>;

template<class K, class V>
using vg_map = std::map<K, V, std::less<K>, vg_allocator<std::pair<const K, V>>>;

template<class K, class V>
using vg_unordered_map = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>,
	vg_allocator<std::pair<const K, V>>>;

extern "C" {
#else
typedef struct uvec2 {
	uint32_t x;
	uint32_t y;
} uvec2;
typedef struct uvec3 {
	uint32_t x;
	uint32_t y;
	uint32_t z;
} uvec3;
typedef struct vec2 {
	float x;
	float y;
} vec2;
typedef struct vec3 {
	float x;
	float y;
	float z;
} vec3;
typedef struct vec4 {
	float x;
	float y;
	float z;
	float w;
} vec4;
typedef struct ivec2 {
	int x;
	int y;
	int z;
} ivec2;
typedef struct ivec4 {
	int x;
	int y;
	int z;
	int w;
} ivec4;
typedef struct mat4 {
	float m[16];
}mat4;
typedef struct mat3x2 {
	float m[12];
}mat3x2;

#ifndef bool
#define bool uint8_t
#endif

#endif
//!cpp
typedef struct hb_font_t hb_font_t;
typedef struct hb_set_t hb_set_t;

#ifdef __cplusplus 
}
#endif
// 内存资源分配器
typedef struct mem_resource_t {
	void* vf;
	size_t _Align;
	void* ptr;
}mem_resource_t;

#ifdef __cplusplus
static_assert(sizeof(vg_alloc_cx) == sizeof(mem_resource_t),
	"vg_alloc_cx must stay layout-compatible with mem_resource_t");
#endif

typedef struct quadratic_v_t
{
	vec2 p0, p1, p2;
}quadratic_v_t;
typedef struct cubic_v_t
{
	vec2 p0, p1, p2, p3;	// p1 p2是控制点
}cubic_v_t;
/*
用 bezier curve（贝塞尔曲线） 来设置 color stop（颜色渐变规则），
这里使用下面的曲线形式，其中
X轴为 offset（偏移量，取值范围为 0~1，0 代表阴影绘制起点），
Y轴为 alpha（颜 色透明度，取值范围为0~1，0 代表完全透明），
*/
typedef struct rect_shadow_vt
{
	float radius;// = 4;	// 半径
	int segment;// = 6;	// 细分段
	vec4 cfrom;// = { 0,0,0,0.8 }, 
	vec4 cto;// = { 0.5,0.5,0.5,0.5 };// 颜色从cf到ct
	/*	cubic
		X轴为 offset（偏移量，取值范围为 0~1，0 代表阴影绘制起点），
		Y轴为 alpha（颜 色透明度，取值范围为0~1，0 代表完全透明），
	*/
	cubic_v_t cubic;// = { {0.0,0.6},{0.5,0.39},{0.4,0.1},{1.0,0.0 } };
}rect_shadow_vt;

#ifdef __cplusplus
// 线，二阶曲线，三阶曲线
enum class path_type_et :uint32_t
{
	e_vmove = 1,// 移动
	e_vline,	// 直线
	e_vcurve,	// 二次曲线
	e_quadratic = e_vcurve,	// 二次曲线
	e_vcubic,	// 三次曲线
	e_close		// 封闭线段
};

enum vg_line_cap_t :uint8_t {
	VG_LINE_CAP_BUTT,
	VG_LINE_CAP_ROUND,
	VG_LINE_CAP_SQUARE
};

enum vg_line_join_t :uint8_t {
	VG_LINE_JOIN_MITER,
	VG_LINE_JOIN_ROUND,
	VG_LINE_JOIN_BEVEL
};
enum vg_fill_rule_t {
	VG_FILL_RULE_EVEN_ODD,
	VG_FILL_RULE_NON_ZERO
};
enum vg_extend_t :uint8_t {
	VG_EXTEND_NONE,
	VG_EXTEND_REPEAT,
	VG_EXTEND_REFLECT,
	VG_EXTEND_PAD
};

enum vg_filter_t :uint8_t {
	VG_FILTER_FAST,
	VG_FILTER_GOOD,
	VG_FILTER_BEST,
	VG_FILTER_NEAREST,
	VG_FILTER_BILINEAR,
	VG_FILTER_GAUSSIAN,
};
enum vg_pattern_type_t :uint8_t {
	VG_PATTERN_TYPE_SOLID,        // 单色
	VG_PATTERN_TYPE_SURFACE,      // 纹理填充
	VG_PATTERN_TYPE_LINEAR,       // 线性渐变 /*!< linear gradient pattern */
	VG_PATTERN_TYPE_RADIAL,       // 径向渐变 /*!< radial gradient pattern */
	VG_PATTERN_TYPE_MESH,         // 网格渐变 /*!< not implemented */
	VG_PATTERN_TYPE_RASTER_SOURCE, //
	VG_PATTERN_TYPE_SWEEP,			// 锥形渐变 
};
enum vg_clip_state_t :uint8_t {
	vg_clip_state_none = 0x00,
	vg_clip_state_clear = 0x01,
	vg_clip_state_clip = 0x02,
	vg_clip_state_clip_saved = 0x06,
};

enum class vg_operator_t :uint8_t {
	VG_OPERATOR_CLEAR,

	VG_OPERATOR_SOURCE,
	VG_OPERATOR_OVER,
	VG_OPERATOR_DIFFERENCE,
	VG_OPERATOR_MAX,
};
enum vg_pipe_t :uint8_t {
	VG_PIPE_OVER,
	VG_PIPE_CLEAR,
	VG_PIPE_SUB,
	VG_PIPE_CLIPPING
};
#else
typedef uint32_t path_type_et;

typedef uint8_t vg_line_cap_t;
typedef uint8_t vg_line_join_t;
typedef uint8_t vg_fill_rule_t;
typedef uint8_t vg_extend_t;

typedef uint8_t vg_filter_t;
typedef uint8_t vg_pattern_type_t;
typedef uint8_t vg_clip_state_t;

typedef uint8_t vg_operator_t;
typedef uint8_t vg_pipe_t;
#endif
// 布局相关
#if 1
/*
	根元素要求
	assert(parent == NULL);
	assert(!isnan(width));
	assert(!isnan(height));
	assert(self_sizing == NULL);

	FLEX_ALIGN_SPACE_BETWEEN,	//两端对齐，两端间隔0，中间间隔1
	FLEX_ALIGN_SPACE_AROUND,	//分散居中,两端间隔0.5，中间间隔1
	FLEX_ALIGN_SPACE_EVENLY,	//分散居中,每个间隔1
*/
#ifdef __cplusplus
enum class flex_align :uint8_t {
	ALIGN_AUTO = 0,
	ALIGN_STRETCH,
	ALIGN_CENTER,
	ALIGN_START,
	ALIGN_END,
	ALIGN_SPACE_BETWEEN,
	ALIGN_SPACE_AROUND,
	ALIGN_SPACE_EVENLY,
	ALIGN_BASELINE
};

enum class flex_position :uint8_t {
	POS_RELATIVE = 0,
	POS_ABSOLUTE
};
// row行，reverse反向，column列
enum flex_direction :uint8_t {
	ROW = 0,
	ROW_REVERSE,
	COLUMN,
	COLUMN_REVERSE
};

enum class flex_wrap :uint8_t {
	NO_WRAP = 0,
	WRAP,
	WRAP_REVERSE
};
#else
typedef uint8_t flex_align;
typedef uint8_t flex_position;
typedef uint8_t flex_direction;
typedef uint8_t flex_wrap;
#endif // __cplusplus
typedef struct flex_data {
	float width, height;	// 大小NAN
	float left, right, top, bottom;	// 偏移
	float padding_left;		// 本元素内边距
	float padding_right;
	float padding_top;
	float padding_bottom;
	float margin_left;		// 本元素外边距
	float margin_right;
	float margin_top;
	float margin_bottom;
	float grow;		// 子元素:自身放大比例，默认为0不放大
	float shrink;	// 子元素:空间不足时自身缩小比例，默认为1自动缩小，0不缩小
	int	  order;	// 子元素:自身排列顺序。数值越小，越靠前
	float basis;// = -1;	// 子元素:定义最小空间NAN
	float baseline; // 基线位置
	flex_align justify_content;// = flex_align::ALIGN_START;	// 父元素:主轴上的元素的排列方式 start\end\center\space-between\space-around\space-evenly
	flex_align align_content;// = flex_align::ALIGN_STRETCH;	// 父元素:适用多行的flex容器 start\end\center\space-between\space-around\space-evenly\stretch 
	flex_align align_items;// = flex_align::ALIGN_STRETCH;		// 父元素:副轴上的元素的排列方式 start\end\center\stretch\baseline
	flex_align align_self;// = flex_align::ALIGN_AUTO;			// 子元素:覆盖父容器align-items的设置
	flex_position position;// = flex_position::POS_RELATIVE;	// 子元素:
	flex_direction direction;// = flex_direction::ROW;			// 父元素:
	flex_wrap wrap;// = flex_wrap::NO_WRAP;					// 父元素:是否换行，超出宽度自动换行
	bool should_order_children;// = false;
}flex_data;
typedef struct flex_data1 {
	float width, height;		// 大小NAN
	float offset[4];// = { 0, 0, 0, 0 };	// 偏移left, right, top, bottom
	float margin[4];//= { 0, 0, 0, 0 };		// 本元素内边距
	float padding[4];//= { 0, 0, 0, 0 };		// 本元素外边距
	float grow;		// 子元素:自身放大比例，默认为0不放大
	float shrink;	// 子元素:空间不足时自身缩小比例，默认为1自动缩小，0不缩小
	int	  order;	// 子元素:自身排列顺序。数值越小，越靠前
	float basis;//= -1;	// 子元素:定义最小空间NAN
	float baseline; // 基线位置
	flex_align justify_content;// = flex_align::ALIGN_START;	// 父元素:主轴上的元素的排列方式 start\end\center\space-between\space-around\space-evenly
	flex_align align_content;// = flex_align::ALIGN_STRETCH;	// 父元素:适用多行的flex容器 start\end\center\space-between\space-around\space-evenly\stretch 
	flex_align align_items;// = flex_align::ALIGN_STRETCH;		// 父元素:副轴上的元素的排列方式 start\end\center\stretch\baseline
	flex_align align_self;// = flex_align::ALIGN_AUTO;			// 子元素:覆盖父容器align-items的设置
	flex_position position;// = flex_position::POS_RELATIVE;	// 子元素:
	flex_direction direction;//= flex_direction::ROW;			// 父元素:
	flex_wrap wrap;// = flex_wrap::NO_WRAP;					// 父元素:是否换行，超出宽度自动换行
	bool should_order_children;// = false;
}flex_data1;

typedef struct node_dt node_dt;
struct node_dt
{
	vec2 size;	// in 原大小
	vec4 offset;	// in 偏移位置
	vec4 frame;	// out 输出位置大小
	size_t index;		// in 样式序号
	float baseline;	// in 基线位置
	int position;		// in 位置,0=relative，1=absolute
	node_dt* child;		// in 子元素指针
	size_t child_count;
	size_t tidx;		// out 自动计算节点索引
	size_t parent;		// out 自动计算父节点索引
	size_t line_count;	// out 行数量
	void* user;
};

typedef struct flex_run flex_run;
/*
 * 轨道尺寸约定：
 *   > 0  -> FR（比例单位，参与剩余空间分配）
 *   <= 0 -> 固定像素（绝对值）
 *   0    -> 未设置，退化为 1FR
 */

 /* ---- Grid 容器参数 ---- */
typedef struct grid_data {
	int   cols;              /* 列数 */
	int   rows;              /* 行数 */

	float* col_widths;       /* 每列宽度（px 或 FR），长度 cols */
	float* row_heights;      /* 每行高度（px 或 FR），长度 rows */

	float  gap_x;            /* 列间距 */
	float  gap_y;            /* 行间距 */

	flex_align justify_items; /* 单元格内水平对齐（默认 STRETCH） */
	flex_align align_items;   /* 单元格内垂直对齐（默认 STRETCH） */

	int    auto_flow;        /* 0 = row（先行后列），1 = column（先列后行） */
	int    dense;            /* 0 = sparse，1 = dense（回溯填充空洞） */
} grid_data;

/* ---- Grid 子元素参数（挂载在 node_dt.user 上） ---- */
typedef struct grid_child {
	int         col;            /* 起始列（0-based） */
	int         row;            /* 起始行（0-based） */
	int         col_span;       /* 跨列数（默认 1） */
	int         row_span;       /* 跨行数（默认 1） */

	flex_align  justify_self;   /* 覆盖容器 justify_items，ALIGN_AUTO = 继承 */
	flex_align  align_self;     /* 覆盖容器 align_items，ALIGN_AUTO = 继承 */

	/*
	 * 显式标志位：
	 *   0 = 未设置，走 auto-flow
	 *   1 = 显式定位（col/row 有效）
	 */
	uint8_t     explicit_pos;
	uint8_t     _pad[3];
} grid_child;


#endif // !1

// 渲染命令
#if 1
typedef struct push_constants_t {
	mat3x2 mat;
	mat3x2 matInv;
	vec4 source;
	vec2 size;
	uint32_t fsq_patternType;
	float opacity;
}push_constants_t;
#define MAX_STOPS 32
typedef struct vg_gradient_t {
	vec4 colors[MAX_STOPS];
	float stops[MAX_STOPS];
	vec4 cp[2];
	ivec4 m;
	vec2 scale;	// 缩放目标
	uint32_t count;
	int extend;
	int type;
}vg_gradient_t;

typedef struct font_family_t {
	hb_font_t* font;
	hb_set_t* coverage;  // hb_face_collect_unicodes
	float upem;
	// 从 hb_font_extents
	float ascender;
	float descender;
	float line_gap;
}font_family_t;
typedef struct font_familys_t {
	font_family_t** familys;
	int count;
}font_familys_t;

/* Font metrics used by UI layout.  Computed from the primary family at the
 * requested pixel size; both fields are already fallback-adjusted. */
typedef struct vg_font_metrics_t {
	float ascent;
	float line_height;
}vg_font_metrics_t;

typedef struct vg_pattern_t {
	int					status;
	uint32_t            references;
	vg_extend_t       extend;
	vg_filter_t       filter;
	vg_pattern_type_t	type;
	bool                hasMatrix;
	mat3x2			matrix;
	void* data;	// Surface指针或vg_gradient_t
}vg_pattern_t;

/* 矢量路径特效：外发光、内发光、阴影。
 * 特效随状态保存/恢复，作用于后续每次 fill/stroke，直到 type 设为 NONE。
 * sigma/offset 单位为设备像素；spread 取值 -1~1，用于近似扩张/收缩。 */
typedef enum vg_effect_type_t {
	VG_EFFECT_NONE = 0,
	VG_EFFECT_OUTER_GLOW = 1,
	VG_EFFECT_INNER_GLOW = 2,
	VG_EFFECT_SHADOW = 3,
} vg_effect_type_t;

typedef struct vg_effect_t {
	uint8_t		type;		// vg_effect_type_t
	uint8_t		blend;		// 0=over, 1=add（预留）
	uint16_t	reserved;
	uint32_t	color;		// 0xAARRGGBB
	float		sigma;		// 高斯模糊半径（设备像素）
	float		spread;		// -1~1，正数向外扩张
	float		opacity;	// 特效强度 0~1
	vec2		offset;		// 阴影偏移（设备像素）
} vg_effect_t;

typedef struct vg_state_save_t {
	float		lineWidth;
	float		miterLimit;
	uint32_t	dashCount;  // value count in dash array, 0 if dash not set.
	float		dashOffset; // an offset for dash
	float* dashes;     // an array of alternate lengths of on and off stroke.
	vg_operator_t curOperator;
	uint8_t		lineCap;
	uint8_t		lineJoin;
	uint8_t		curFillRule;
	push_constants_t	pushConsts;
	uint32_t			color;
	vg_pattern_t* pattern;
	vg_clip_state_t		clippingState;
	uint32_t			references;
	bool aa;
	vg_effect_t			effect;
}vg_state_save_t;
enum depth_stencil_State {
	d_depthtest_enable = 0x01,
	d_depthwrite_enable = 0x02,
	d_stenciltest_enable = 0x04
};
// 混合模式 
enum blendMode_e {
	none = -1,	// 不混合
	normal = 0,	// 普通混合
	additive,
	multiply,
	modulate,
	screen,
	normal_prem,	// 预乘alpha
	additive_prem,
};
enum shader_type_e {
	ST_NONE,
	ST_MASK,
	ST_DOUBLESIDED,
	ST_INSTANCE,
	ST_INSTANCE_DOUBLESIDED,
};
enum cmd_type_e {
	DRAW_VG,
	DRAW_GEOM,
};
// 0矢量图管线						2d
// 0普通三角形(纹理)					2d/3d		tex0
// 1普通三角形+遮罩纹理				2d/3d		tex0、tex1
// 2双面三角形(两种颜色/纹理)			doubleSided	tex0
// 3三角形(纹理)实例化							ubo0、tex1
// 4双面三角形(两种颜色/纹理)实例化				ubo0、tex1
typedef struct gem_info_t {
	int8_t blendMode;
	uint8_t topology;
	uint8_t polygon;
	uint8_t frontFace;	// COUNTER_CLOCKWISE = 0, CLOCKWISE = 1,
	uint8_t cullMode;	// NONE=0, FRONT=1, BACK=2, FRONT_AND_BACK=3
	uint8_t flags;		// depthTestEnable, depthWriteEnable, stencilTestEnable
	uint8_t lineWidth;
	uint8_t shader;		// shader_type_e
}gem_info_t;

// 普通三角形命令
typedef struct geom_cmd_t {
	int stype;// = 1;
	gem_info_t state;
	void* texture;
	void* texture_mask;
	mat4 mat;// = mat4(1.0f);	// 矩阵
	float mask_time;// = 1.0;				// 遮罩时间
	float multiply;				// 预乘1.0
	uint32_t elemCount;				// 元素计数，索引数量或顶点数量
	uint32_t firstIndex;			// -1则非索引渲染
	int32_t  vertexOffset;
	size_t v_offset;				// vbo绑定偏移：0单面，1双面
	mat4* instance_mat;		// 实例矩阵
	uint32_t instance_count;		// 实例数量
	uint32_t instance_ssbo_pos;		// 实例数据在gpu偏移,由vg计算
}geom_cmd_t;

// 矢量命令
typedef struct vgcmd_t {
	int stype;// = 0;
	int full_screen_quad;// = -1;
	ivec2 vertex;			// 顶点开始、数量
	ivec2 index;			// 索引开始、数量
	vg_state_save_t* state;	// 渲染参数
	vec4 bounds;			// 全屏填充,odd/clip专用
	int8_t type;				// 类型：填充0、描边1、裁剪2、全屏3、清屏4
	int8_t ref;					// 参考值
}vgcmd_t;
typedef union gcmd_t {
	vgcmd_t vg;
	geom_cmd_t g;
}gcmd_t;
typedef struct ovgVertex {
	vec2	pos;
	vec2	uv;
	uint32_t	color;
}ovgVertex;
typedef struct geomVertex1 {
	vec3 pos;
	vec2 uv;
	uint32_t color;
}geomVertex1;
typedef struct geomVertex2 {
	vec3 pos;
	vec2 uv;
	uint32_t color;
	uint32_t color1;
}geomVertex2;
enum vg_format_t {
	VG_FORMAT_RGBA8 = 0,	// 对应SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
	VG_FORMAT_BGRA8,
	VG_FORMAT_RGBA8_SRGB,	// SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
	VG_FORMAT_BGRA8_SRGB,
	VG_FORMAT_RGBA16F,
	VG_FORMAT_RGBA32F,
};

// 图片
typedef struct vg_image_t {
	uint32_t id;		// 由设备分配
	int width, height;		// 更新纹理时自动更新大小
	bool valid;	// 是否要更新到纹理
	bool copy_status;	// 复制状态
}vg_image_t;
/**
 * 更新图片数据（不重建句柄）：
 *   - 尺寸不变 → 覆盖上传（partial=true 时只传脏矩形）
 *   - 尺寸变化 → 后端重建纹理，旧纹理延迟释放
 */
typedef struct vg_image_desc_t {
	vg_image_t* img;		// 需要更新的纹理
	uint32_t	width;
	uint32_t	height;
	uint32_t	format;
	uint32_t	stride;
	void* pixels;			// CPU 像素数据。is_copy=false时vg_image_t.copy_status等于true时才能释放
	uint32_t x, y, w, h;	// 更新矩形区域
	uint32_t idx;			// 纹理数组层号
	uint32_t px_size;		// 像素字节大小
	bool is_destroy;		// true = 删除纹理；
	bool is_copy;			// 是否要复制内存；
	bool dirty;				// 内部使用：本帧是否有更新（image_update 置位，上传后清除）
}vg_image_desc_t;

#endif

enum ImageFlipMode
{
	FLIP_NONE,			// 不翻转
	FLIP_HORIZONTAL,	// 水平翻转 
	FLIP_VERTICAL,		// 垂直翻转
	FLIP_HORIZONTAL_AND_VERTICAL = (FLIP_HORIZONTAL | FLIP_VERTICAL)    // 水平和垂直翻转（不是对角翻转）
};

typedef struct image_ptr_t
{
	int width, height;
	int type;				// 0=rgba，1=bgra
	int stride;
	uint32_t* data;			// 像素数据 
	void* ptr;				// 用户数据
	int comp;				// 通道数0单色位图，1灰度图，4rgba/bgra
	int  blendmode;			// 混合模式
	bool static_tex;	// 静态纹理
	bool multiply;		// 预乘的纹理
	bool valid;			// 是否更新到纹理
}image_ptr_t;


#ifdef __cplusplus
struct ovg_image_data :public vg_image_t {
	int format;			// 0 rgba, 1 bgra
	int stride;			// 像素宽度
	uint32_t* data;
	bool multiply;		// 是否预乘  
};
#else
typedef struct ovg_image_data {
	vg_image_t v;
	int format;			// 0 rgba, 1 bgra
	int stride;			// 像素宽度
	uint32_t* data;
	bool multiply;		// 是否预乘  
}ovg_image_data;
#endif

typedef struct ovg_image_r
{
	vg_image_t* img;
	ivec4 rc;		// 所在纹理区域
	ivec4 sliced;	// 九宫格 
	ivec4 dst;		// 渲染坐标大小 
	uint32_t color;		// 混合颜色
	int8_t type;		// img的类型
}ovg_image_r;

/*
 * 原生 GPU 纹理句柄（值类型）。
 *
 * 由后端通过 ovg_texture_init_native / ovg_texture_update_native 填充，
 * 只借用调用方的 SDL_GPUTexture，不负责释放。句柄地址必须保持稳定，直到
 * 本次录制的 draw data 被渲染器消费完成。
 */
typedef struct ovg_texture_t
{
	vg_image_t image;
}ovg_texture_t;

/*
 * 原生纹理绘制参数。
 *
 * - rc:     源纹理区域 {x,y,w,h}；x < 0 或 w/h <= 0 表示整张纹理。
 * - sliced: 九宫格边宽 {left,top,right,bottom}；x <= 0 表示不切片。
 * - dst:    目标矩形 {x,y,w,h}，必须为正。
 * - color:  顶点颜色/tint（直通乘到纹理采样结果）。
 * - flip:   ImageFlipMode 位组合，按目标矩形中心镜像整个结果。
 * - angle:  旋转角度（度）。center_set != 0 时 center 为相对 dst 左上角的
 *           旋转中心；否则使用 dst 中心。
 * - matrix: 可选的 glm::mat3x2（列主序），在翻转/旋转之后应用。
 *
 * 先在 CPU 端生成顶点，再依次执行翻转、旋转、矩阵变换；UV 保持不变。
 */
typedef struct ovg_texture_r
{
	ovg_texture_t* tex;
	ivec4 rc;
	ivec4 sliced;
	ivec4 dst;
	uint32_t color;
	int8_t flip;
	int8_t center_set;
	float angle;
	vec2 center;
	const void* matrix;
}ovg_texture_r;
typedef struct text_st_t {
	vec2 pos;
	vec2 size;
	vec4 clip;		// 裁剪区域
	const char* text;
	int text_len;
}text_st_t;

// 文本样式
typedef struct text_style_t
{
	font_familys_t* family;
	float fontsize;
	float lineheight;
	vec2 shadow_pos;// = { 1.0,1.0 };
	float stroke;					// 描边宽度，正数使用矢量路径实现、负数用偏移4方向实现描边
	uint32_t color;// = 0xffc2c2c2;		// 文本颜色
	uint32_t color_stroke;// = 0xff000000;	// 描边颜色
	uint32_t color_shadow;			// 阴影颜色	0xcc121212;
	int min_subpixel;
}text_style_t;
// 文本区域
typedef struct text_box_rt {
	ivec4 rc;		// 设置文本渲染区域，偏移/大小
	vec2 text_align;// 文本对齐
	int baseline;		// 设置自定义基线
	int8_t auto_break;	// 是否自动换行
	int8_t word_wrap;	// = 2;	// 0字符换行，1单词换行，2换行点，3句子断开，4标题大小写断点
	int8_t ellipsis;	// 省略号
}text_box_rt;

// Stencil 位平面
#ifndef STENCIL_CLIP_BIT
#define STENCIL_CLIP_BIT    0x1   // bit1: 裁剪掩码（REPLACE 写入）
#endif
// 路径对象
typedef struct ovg_path_t ovg_path_t;
// 矢量对象 
typedef struct rvg_t {
	int width, height;
	ovg_path_t* path;
	vg_state_save_t* st;
}rvg_t;
// 打包数据引用
typedef struct ovg_draw_data_t {
	vg_fbo_t* fbo;
	gcmd_t* d;				// 渲染命令列表
	size_t count;
	ovgVertex* vg_vertex;	// 矢量顶点
	size_t v_count;
	uint32_t* vg_indices;	// 矢量索引
	size_t i_count;
	size_t uboCount;		// 渐变ubo结构数量
	geomVertex1* vertex1;	// 单面顶点
	size_t v1_count;
	geomVertex2* vertex2;	// 双面顶点
	size_t v2_count;
	uint32_t* geom_indices;	// 索引 
	size_t ig_count;		// 索引数量
	size_t instance_count;	// 实例矩阵总数量
	void* instance_data;	// 实例所有数据
	vg_image_desc_t** image_desc;
	size_t image_desc_count;	// 更新纹理数量
	gem_info_t* pipeinfo;
	size_t pipeinfo_count;	// 需要build的管线数量
	uvec2 vg_offset;	// 渲染自动计算用
	uvec3 geom_offset;
}ovg_draw_data_t;

// 录制
typedef struct ovg_recording_t ovg_recording_t;

// 快捷对象命令模式，没有destroy函数的对象都不需要手动释放，rvg_t可创建多个做缓存
typedef struct ovg_ctx_cb {
	mem_resource_t* ac;	// 内存分配器，由new_ctx_cb自己创建 	
	// 渲染操作，rvg_t可以多次执行fill或stroke/clip
	rvg_t* (*new_rvg)(mem_resource_t* ac);
	void (*destroy_rvg)(rvg_t* p);
	void(*clear)(rvg_t* v);			// 清空画布 
	void(*set_enable_tess2)(rvg_t* ctx, bool enable);	// 强制全量 tess2 交叉填充；默认按需自动路由（ODD/嵌套/重叠/自交轮廓）
	// 路径操作
	ovg_path_t* (*get_path)(rvg_t* ctx);
	void (*new_path)(rvg_t* ctx);
	void(*clear_path)(rvg_t* ctx);
	void(*close_path)(rvg_t* ctx);
	void(*new_sub_path)(rvg_t* ctx);
	void(*path_extents)(rvg_t* ctx, float* x1, float* y1, float* x2, float* y2);
	void(*get_current_point)(rvg_t* ctx, float* x, float* y);
	size_t(*get_segment_count)(rvg_t* ctx);
	void(*set_segment_color)(rvg_t* ctx, size_t idx, uint32_t color);
	// 添加数据到当前路径，参考path_type_e
	void(*add_path)(rvg_t* ctx, float* data, size_t count);
	void(*move_to)(rvg_t* ctx, float x, float y);
	void(*rel_move_to)(rvg_t* ctx, float x, float y);
	void(*line_to)(rvg_t* ctx, float x, float y);
	void(*rel_line_to)(rvg_t* ctx, float dx, float dy);
	void(*arc)(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2);
	void(*arc_negative)(rvg_t* ctx, float xc, float yc, float radius, float a1, float a2);
	// 有缩放时，先执行set_path一次再执行curve_to
	void(*curve_to)(rvg_t* ctx, float x1, float y1, float x2, float y2, float x3, float y3);
	void(*rel_curve_to)(rvg_t* ctx, float x1, float y1, float x2, float y2, float x3, float y3);
	void(*quadratic_to)(rvg_t* ctx, float x1, float y1, float x2, float y2);
	void(*rel_quadratic_to)(rvg_t* ctx, float x1, float y1, float x2, float y2);
	void(*rectangle)(rvg_t* ctx, float x, float y, float w, float h);
	void(*rounded_rectangle)(rvg_t* ctx, float x, float y, float w, float h, float radius);
	void(*rounded_rectangle2)(rvg_t* ctx, float x, float y, float w, float h, float rx, float ry);
	void(*ellipse)(rvg_t* ctx, float radiusX, float radiusY, float x, float y, float rotationAngle);
	void(*elliptic_arc_to)(rvg_t* ctx, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi);
	void(*rel_elliptic_arc_to)(rvg_t* ctx, float x, float y, bool large_arc_flag, bool sweep_flag, float rx, float ry, float phi);
	void(*circle)(rvg_t* ctx, float x, float y, float radius);
	// 配置状态
	void(*set_opacity)(rvg_t* ctx, float opacity);
	// 设置路径特效，effect 为 NULL 或 type=NONE 时关闭
	void(*set_effect)(rvg_t* ctx, const vg_effect_t* effect);
	void(*set_source_color)(rvg_t* ctx, uint32_t c);
	void(*set_source_rgba)(rvg_t* ctx, float r, float g, float b, float a);
	void(*set_source_rgb)(rvg_t* ctx, float r, float g, float b);
	void(*set_line_width)(rvg_t* ctx, float width);
	void(*set_miter_limit)(rvg_t* ctx, float limit);
	void(*set_line_cap)(rvg_t* ctx, int cap);
	void(*set_line_join)(rvg_t* ctx, int join);
	void(*set_source_surface)(rvg_t* ctx, vg_image_t* surf, float x, float y);
	void(*set_source)(rvg_t* ctx, vg_pattern_t* pat);
	void(*set_operator)(rvg_t* ctx, int op);
	void(*set_fill_rule)(rvg_t* ctx, int fr);	// 填充规则：单轮廓简单路径两规则等价；ODD/嵌套/重叠/自交轮廓自动走 tess2 细分（tess2 分支不支持逐段颜色）
	void(*set_dash)(rvg_t* ctx, const float* dashes, uint32_t num_dashes, float offset);		// 虚线
	void(*set_dash8)(rvg_t* ctx, uint64_t dashes, uint32_t num_dashes, float offset);								// 虚线,用uint8_t v8[8]表示
	void(*translate)(rvg_t* ctx, float dx, float dy);
	void(*scale)(rvg_t* ctx, float sx, float sy);
	void(*rotate)(rvg_t* ctx, float radians);
	void(*transform)(rvg_t* ctx, const void* matrix);
	void(*set_matrix)(rvg_t* ctx, const void* matrix);
	void(*get_matrix)(rvg_t* ctx, void* matrix);
	void(*identity_matrix)(rvg_t* ctx);
	void(*matrix_init)(void* mat, float xx, float yx, float xy, float yy, float x0, float y0);

	// 图案：渐变/图片 
	vg_pattern_t* (*new_pattern_linear)(rvg_t* ctx, float x0, float y0, float x1, float y1);
	vg_pattern_t* (*new_pattern_radial)(rvg_t* ctx, float cx0, float cy0, float radius0, float cx1, float cy1, float radius1, bool is_ellipse);
	vg_pattern_t* (*new_pattern_sweep)(rvg_t* ctx, float cx, float cy, float start_angle, float end_angle);
	int (*pattern_add_color_stop)(vg_pattern_t* pat, float o, float r, float g, float b, float a);
	int (*pattern_set_color_stop)(vg_pattern_t* pat, int idx, float o, float r, float g, float b, float a);
	void(*pattern_set_matrix)(vg_pattern_t* pat, const void* matrix);	// mat3x2
	void(*pattern_set_extend)(vg_pattern_t* pat, int extend);
	void(*pattern_set_filter)(vg_pattern_t* pat, int filter);

	// 更新纹理，vg_image_t指针由用户创建管理
	void (*image_update)(rvg_t* p, vg_image_t* img, vg_image_desc_t* desc);
	//标记图片不再使用（后端延迟释放 GPU 纹理）
	void (*image_destroy)(rvg_t* p, vg_image_t* img);

	void(*save)(rvg_t* v);		// 保存状态，（裁剪状态暂不实现）
	void(*restore)(rvg_t* v);	// 恢复状态
	void(*stroke)(rvg_t* v);
	void(*stroke_preserve)(rvg_t* v);
	void(*fill)(rvg_t* v);
	void(*fill_preserve)(rvg_t* v);
	void(*paint)(rvg_t* v);			// 全屏渲染
	void(*reset_clip)(rvg_t* v, uint8_t ref);	// 重置裁剪，0清空，1全部通过；路径裁剪(clip)前需先 reset_clip(v,0) 清模板才会生效
	void(*clip)(rvg_t* v);			// 路径裁剪，清空当前路径
	void(*clip_preserve)(rvg_t* v);	// 路径裁剪
	void(*clip_rect)(rvg_t* v, int x, int y, int width, int height);	// 矩形裁剪
	void(*set_clip_rect)(rvg_t* v, void* rc);	// 矩形裁剪,int[4]
	void(*get_clip_rect)(rvg_t* v, void* rc);	// 获取矩形裁剪

	// 添加文本，风格，渲染区可选
	void (*add_text)(rvg_t* dc, text_st_t* p, text_style_t* ts, text_box_rt* box);
	// 普通图片，支持九宫格、混合颜色
	void (*add_image)(rvg_t* dc, ovg_image_r* r);
	// 原生纹理，支持源裁剪、tint、翻转、九宫格、旋转、矩阵，按当前 clip 裁剪
	void (*add_texture)(rvg_t* dc, ovg_texture_r* r);

	// 原始三角形
	// gem_info_t或矩阵输入空指针则不修改该值
	void (*set_geom_state)(rvg_t* dc, gem_info_t* info, const void* matrix4x4);
	// 实例化
	size_t(*set_instance_mat)(rvg_t* dc, const void* instance_mat, size_t instance_count);
	// 添加几何数据到缓冲区，xy顶点坐标，color顶点颜色，uv顶点纹理坐标，indices索引数据，color_type=0表示float4，1表示uint32_t
	void (*add_geometry)(rvg_t* dc, vg_image_t* texture, const float* xy, int xy_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);
	// 添加3D几何数据到缓冲区，xyz顶点坐标，color顶点颜色（双面则要双倍），uv顶点纹理坐标，indices索引数据
	void (*add_geometry3d)(rvg_t* dc, vg_image_t* texture, const float* xyz, int xyz_stride, const void* color, int color_stride, const float* uv, int uv_stride, int num_vertices, const void* indices, int num_indices, int size_indices, int color_type);
	// 录制
	void (*start_recording)(rvg_t* ctx);
	// 可复用ovg_recording_t*
	ovg_recording_t* (*stop_recording)(rvg_t* ctx, ovg_recording_t* p);
	void (*replay)(rvg_t* ctx, ovg_recording_t* rec);
	void (*replay_command)(rvg_t* ctx, ovg_recording_t* rec, uint32_t cmdIndex);
	uint32_t(*recording_get_count)(ovg_recording_t* rec);
	void* (*recording_get_data)(ovg_recording_t* rec);
	void  (*recording_destroy)(ovg_recording_t* rec);

}ovg_ctx_cb;
// 工具函数：渲染两色交替的格子
void draw_grid_fill(rvg_t* vg, vec2 size, ivec2 cols, int width);
// tiny flex
flex_run* new_flex_run();
void free_flex_run(flex_run* p);
vec4 flex_run_layout(flex_run* ctx, flex_data* fd, size_t count, node_dt* p, size_t node_count);
#ifdef __cplusplus
// 指定分配器创建（传 nullptr 等同 new_flex_run）
flex_run* new_flex_run_ac(vg_alloc_cx* ac);
// 预留布局节点容量，只增不减；用于首帧前一次性分配。
void flex_run_reserve(flex_run* p, size_t node_cap);
vg_alloc_cx* flex_run_ac(flex_run* p);
#endif


/* ---- 生命周期 ---- */
grid_data* grid_create(int cols, int rows);
void        grid_destroy(grid_data* g);

/* ---- 轨道尺寸（FR 或 px） ---- */
void grid_set_col_widths(grid_data* g, const float* widths, int cols);
void grid_set_row_heights(grid_data* g, const float* heights, int rows);

/* ---- 单个轨道 ---- */
void grid_set_col(grid_data* g, int col, float width);
void grid_set_row(grid_data* g, int row, float height);

/* ---- 间距 ---- */
void grid_set_gap(grid_data* g, float gap_x, float gap_y);
void grid_set_gap_xy(grid_data* g, float gap);

/* ---- auto-flow 与 dense ---- */
void grid_set_auto_flow(grid_data* g, int auto_flow);   /* 0=row, 1=column */
void grid_set_dense(grid_data* g, int dense);           /* 0=sparse, 1=dense */

/* ---- 单元格对齐默认值 ---- */
void grid_set_justify_items(grid_data* g, flex_align a);
void grid_set_align_items(grid_data* g, flex_align a);

/* ---- 主布局入口 ---- */
void grid_layout(grid_data* g, float container_width, float container_height, node_dt* children, size_t count);

/*
 * 高级用法：布局后查询某行某列的计算后像素尺寸。
 * 用于嵌套布局或调试。
 */
float grid_get_col_width(const grid_data* g, int col);
float grid_get_row_height(const grid_data* g, int row);

#if 0
grid_data* g = grid_create(3, 2);
float cw[] = { 1, 1, 1 };
float rh[] = { 40, 40 };
grid_set_col_widths(g, cw, 3);
grid_set_row_heights(g, rh, 2);
grid_set_gap_xy(g, 8);

node_dt nodes[6] = { 0 };
for (int i = 0; i < 6; i++) {
	nodes[i].size.x = 60;
	nodes[i].size.y = 30;
}

/* 显式定位第 0 个元素跨 2 列 */
grid_child gc = { 0 };
gc.explicit_pos = 1;
gc.col = 0; gc.row = 0;
gc.col_span = 2;
nodes[0].user = &gc;

grid_layout(g, 200, 100, nodes, 6);

grid_destroy(g);
#endif

// 字体相关
#ifdef __cplusplus 
class font_cache_cx;
#else
typedef void* font_cache_cx;
#endif
font_cache_cx* new_font_cache();
void free_font_cache(font_cache_cx * p);
font_familys_t* new_font_family(font_cache_cx * p, const char* familys, const char* style);
void delete_font_family(font_familys_t * p);
/* 注册一个字体文件。name 可空：为 fontconfig 按文件内部族名登记（需在
 * get_sys_family() 后可被 new_font_family 找到）；给出 name 时额外按该名称
 * 立即登记，new_font_family 无需刷新即可找到。 */
bool load_font_from_path(font_cache_cx * p, const char* path, const char* name);
/* 注册目录下所有字体文件（同 path：findable 需 get_sys_family() 刷新）。 */
bool add_font_dir(font_cache_cx * p, const char* dir);
/* 从内存注册字体；内容立即拷贝，返回后调用方可释放缓冲。name 为注册的字体
 * 族键（可空：用字体文件内部族名），立即可被 new_font_family 找到。 */
bool load_font_from_memory(font_cache_cx * p, unsigned char* fontBuffer, long fontBufferByteSize, const char* name);
/* 重新枚举系统字体到 _familys（load_font_from_path/add_font_dir 的
 * fontconfig 登记在刷新后才对 new_font_family 可见）。 */
void get_sys_family(font_cache_cx * p);
vec3 get_font_extents(hb_font_t * font, int height, bool vert);
/* Text measurement shared by the UI layer.  Uses the same coverage-based
 * fallback selection and nominal advances as the renderer's font setup, but
 * does not shape the text (no kerning/ligatures/bidi).  Returns the widest
 * line width; '\n' splits lines. */
float vg_measure_text_width(const font_familys_t * ffs, int fontsize, const char* text, int len);
vg_font_metrics_t vg_get_font_metrics(const font_familys_t * ffs, float font_size);
// 创建接口实例
ovg_ctx_cb* new_ctx_cb();
void free_ctx_cb(ovg_ctx_cb*);
// 获取渲染数据
ovg_draw_data_t get_draw_list(rvg_t * p);

#endif // !OVG_H
