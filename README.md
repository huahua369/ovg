# OVG — 即时模式 2D 矢量/图片/文本/三角形录制渲染库

OVG 是一个 **C/C++ 即时模式（Immediate Mode）2D 图形录制与渲染库**，支持将矢量路径、图片、文本以及自定义 2D/3D 三角形几何录制到 `rvg_t` 对象中，最终打包为 GPU 渲染命令和数据，交由后端（如 SDL3 GPU）执行绘制。

> 录制阶段只负责把绘图命令追加到显存/内存缓冲区，不立即执行 GPU 调用；提交阶段一次性提交所有命令，实现低 CPU 开销的批量渲染。

---

## 目录

- [特性](#特性)
- [架构概览](#架构概览)
- [快速开始](#快速开始)
- [API 参考](#api-参考)
  - [上下文与生命周期](#上下文与生命周期)
  - [路径绘制](#路径绘制)
  - [样式与状态](#样式与状态)
  - [变换](#变换)
  - [渐变与图案](#渐变与图案)
  - [图片](#图片)
  - [文本](#文本)
  - [几何（2D / 3D 三角形）](#几何2d--3d-三角形)
  - [裁剪](#裁剪)
  - [录制与回放](#录制与回放)
- [Flex 布局](#flex-布局)
- [数据结构](#数据结构)
- [枚举参考](#枚举参考)
- [构建与集成](#构建与集成)
- [许可证](#许可证)

---

## 特性

| 类别 | 能力 |
|---|---|
| **矢量路径** | Move / Line / 二次贝塞尔 / 三次贝塞尔 / Close；矩形、圆角矩形、圆、椭圆、椭圆弧、圆弧 |
| **描边与填充** | 线宽、虚线（float 数组 / 8×uint8 紧凑）、线帽（Butt/Round/Square）、线连接（Miter/Round/Bevel）、填充规则（EvenOdd/NonZero） |
| **渐变** | 线性渐变、径向渐变（支持椭圆）、锥形（Sweep）渐变，最多 32 个颜色停靠点 |
| **图片** | RGBA8 / BGRA8 / sRGB / RGBA16F / RGBA32F；脏矩形局部更新；九宫格（9-slice）拉伸；混合着色；垂直/水平翻转 |
| **文本** | HarfBuzz 驱动；描边、阴影、对齐、自动换行、省略号、多字体族回退 |
| **几何** | 自定义 2D / 3D 三角形（任意顶点布局）；单面 / 双面；实例化渲染；遮罩纹理 |
| **混合模式** | Normal / Additive / Multiply / Modulate / Screen（含预乘 Alpha 版本） |
| **裁剪** | 路径裁剪、矩形裁剪、Stencil 位平面 |
| **布局** | 内置 Tiny Flex 引擎（Flexbox 子集），支持主轴/交叉轴对齐、换行、排序、基线 |
| **录制回放** | 将一帧的绘图命令录制为 `ovg_recording_t`，可单帧或单命令回放，便于缓存静态 UI |
| **自定义内存** | 通过 `mem_resource_t` 注入自定义分配器 |
| **C ABI** | 头文件纯 C 兼容（`#ifdef __cplusplus`），C 与 C++ 项目均可直接接入 |

---

## 架构概览

```
┌──────────────────────────────────────────────────────┐
│                  用户应用 (C/C++)                      │
├──────────────────────────────────────────────────────┤
│  ovg_ctx_cb* cb = new_ctx_cb();                       │
│  rvg_t* vg = cb->new_rvg(ac);                         │
│                                                       │
│  cb->new_path(vg);                                    │
│  cb->rounded_rectangle(vg, 10, 10, 200, 100, 12);     │
│  cb->set_source_rgba(vg, 0.2, 0.6, 1.0, 1.0);         │
│  cb->fill(vg);                                        │
├──────────────────────────────────────────────────────┤
│               录制阶段（CPU 端缓冲区）                  │
│   ovgVertex[]  ovgVertex2/3[]  gcmd_t[]  UBO[]       │
├──────────────────────────────────────────────────────┤
│   ovg_draw_data_t dl = get_draw_list(vg);             │
│   // 提交到 GPU 后端                                  │
├──────────────────────────────────────────────────────┤
│              SDL3 GPU / 自研渲染后端                    │
└──────────────────────────────────────────────────────┘
```

---

## 快速开始

```c
#include "ovg/ovg_c.h"

int main()
{
    /* 1. 创建上下文接口 */
    ovg_ctx_cb* cb = new_ctx_cb();

    /* 2. 创建矢量录制对象 */
    mem_resource_t* ac = NULL;  // 传 NULL 使用默认分配器
    rvg_t* vg = cb->new_rvg(ac);

    /* 3. 录制绘图命令 */
    cb->new_path(vg);
    cb->rounded_rectangle(vg, 20, 20, 300, 180, 16);
    cb->set_source_rgba(vg, 0.2f, 0.5f, 0.9f, 1.0f);
    cb->fill(vg);

    cb->new_path(vg);
    cb->circle(vg, 160, 110, 60);
    cb->set_line_width(vg, 4.0f);
    cb->set_source_rgba(vg, 1.0f, 1.0f, 1.0f, 0.8f);
    cb->stroke(vg);

    /* 4. 获取渲染数据并提交给 GPU 后端 */
    ovg_draw_data_t dl = get_draw_list(vg);

    /* 5. 释放资源 */
    cb->destroy_rvg(vg);
    free_ctx_cb(cb);
    return 0;
}
```

---

## API 参考

### 上下文与生命周期

| 函数 | 说明 |
|---|---|
| `ovg_ctx_cb* new_ctx_cb()` | 创建上下文接口实例，返回包含所有回调函数的虚表 |
| `void free_ctx_cb(ovg_ctx_cb*)` | 销毁上下文接口 |
| `rvg_t* cb->new_rvg(mem_resource_t* ac)` | 创建一个矢量录制对象，可创建多个做缓存 |
| `void cb->destroy_rvg(rvg_t*)` | 销毁录制对象 |
| `void cb->clear(rvg_t*)` | 清空画布，重置所有录制数据 |

### 路径绘制

| 函数 | 说明 |
|---|---|
| `void new_path(rvg_t*)` | 开始一条新路径（清除当前路径） |
| `void clear_path(rvg_t*)` | 清空当前路径数据 |
| `void close_path(rvg_t*)` | 闭合当前子路径 |
| `void new_sub_path(rvg_t*)` | 开始新的子路径 |
| `void move_to(rvg_t*, float x, float y)` | 移动到 |
| `void rel_move_to(rvg_t*, float dx, float dy)` | 相对移动到 |
| `void line_to(rvg_t*, float x, float y)` | 直线到 |
| `void rel_line_to(rvg_t*, float dx, float dy)` | 相对直线到 |
| `void curve_to(rvg_t*, float x1,y1, x2,y2, x3,y3)` | 三次贝塞尔（x1,y1 / x2,y2 为控制点） |
| `void rel_curve_to(rvg_t*, ...)` | 相对三次贝塞尔 |
| `void quadratic_to(rvg_t*, float x1,y1, x2,y2)` | 二次贝塞尔 |
| `void rel_quadratic_to(rvg_t*, ...)` | 相对二次贝塞尔 |
| `void arc(rvg_t*, float xc, yc, radius, a1, a2)` | 圆弧（顺时针） |
| `void arc_negative(rvg_t*, ...)` | 圆弧（逆时针） |
| `void rectangle(rvg_t*, float x, y, w, h)` | 矩形 |
| `void rounded_rectangle(rvg_t*, float x,y,w,h, radius)` | 圆角矩形（四角同半径） |
| `void rounded_rectangle2(rvg_t*, float x,y,w,h, rx, ry)` | 圆角矩形（椭圆角） |
| `void ellipse(rvg_t*, float rx, ry, x, y, rotationAngle)` | 椭圆 |
| `void elliptic_arc_to(rvg_t*, float x,y, large_arc, sweep, rx,ry, phi)` | SVG 椭圆弧 |
| `void circle(rvg_t*, float x, y, radius)` | 圆 |
| `void add_path(rvg_t*, float* data, size_t count)` | 批量添加路径段（按 `path_type_et` 编码） |
| `void path_extents(rvg_t*, float* x1,*y1,*x2,*y2)` | 获取路径包围盒 |
| `void get_current_point(rvg_t*, float* x, *y)` | 获取当前点 |

### 样式与状态

| 函数 | 说明 |
|---|---|
| `void set_opacity(rvg_t*, float opacity)` | 全局不透明度 |
| `void set_source_color(rvg_t*, uint32_t rgba)` | 设置纯色源（0xAARRGGBB 或 0xRRGGBBAA，视端序） |
| `void set_source_rgba(rvg_t*, float r,g,b,a)` | 设置纯色源（0~1） |
| `void set_source_rgb(rvg_t*, float r,g,b)` | 设置纯色源（0~1，alpha=1） |
| `void set_line_width(rvg_t*, float width)` | 线宽 |
| `void set_miter_limit(rvg_t*, float limit)` | Miter 限制 |
| `void set_line_cap(rvg_t*, int cap)` | 线帽：`VG_LINE_CAP_BUTT/ROUND/SQUARE` |
| `void set_line_join(rvg_t*, int join)` | 线连接：`VG_LINE_JOIN_MITER/ROUND/BEVEL` |
| `void set_source_surface(rvg_t*, vg_image_t*, float x, y)` | 纹理填充源 |
| `void set_source(rvg_t*, vg_pattern_t*)` | 通用图案源（渐变/纹理） |
| `void set_operator(rvg_t*, int op)` | 合成操作：`VG_OPERATOR_CLEAR/SOURCE/OVER/DIFFERENCE` |
| `void set_fill_rule(rvg_t*, int fr)` | 填充规则：`VG_FILL_RULE_EVEN_ODD/NON_ZERO` |
| `void set_dash(rvg_t*, const float* dashes, uint32_t count, float offset)` | 虚线样式（float 数组） |
| `void set_dash8(rvg_t*, uint64_t dashes, uint32_t count, float offset)` | 虚线样式（8×uint8 紧凑编码） |
| `void save(rvg_t*)` | 保存当前图形状态（裁剪状态暂不保存） |
| `void restore(rvg_t*)` | 恢复上一图形状态 |

### 变换

| 函数 | 说明 |
|---|---|
| `void translate(rvg_t*, float dx, dy)` | 平移 |
| `void scale(rvg_t*, float sx, sy)` | 缩放 |
| `void rotate(rvg_t*, float radians)` | 旋转 |
| `void transform(rvg_t*, const void* mat3x2)` | 叠加变换矩阵 |
| `void set_matrix(rvg_t*, const void* mat3x2)` | 设置变换矩阵 |
| `void get_matrix(rvg_t*, void* mat3x2)` | 获取变换矩阵 |
| `void identity_matrix(rvg_t*)` | 重置为单位矩阵 |
| `void matrix_init(void* mat, float xx,yx, xy,yy, x0,y0)` | 初始化 3×2 矩阵 |

### 渐变与图案

```c
vg_pattern_t* pat = cb->new_pattern_linear(vg, x0, y0, x1, y1);
cb->pattern_add_color_stop(pat, 0.0f, 1,0,0,1);
cb->pattern_add_color_stop(pat, 1.0f, 0,0,1,1);
cb->pattern_set_extend(pat, VG_EXTEND_PAD);
cb->set_source(vg, pat);
cb->fill(vg);
```

| 函数 | 说明 |
|---|---|
| `new_pattern_linear(ctx, x0,y0, x1,y1)` | 线性渐变 |
| `new_pattern_radial(ctx, cx0,cy0,r0, cx1,cy1,r1, is_ellipse)` | 径向渐变（支持椭圆） |
| `new_pattern_sweep(ctx, cx, cy, start_angle, end_angle)` | 锥形渐变 |
| `pattern_add_color_stop(pat, offset, r,g,b,a)` | 添加颜色停靠点 |
| `pattern_set_color_stop(pat, idx, ...)` | 修改指定停靠点 |
| `pattern_set_matrix(pat, mat3x2)` | 设置图案变换 |
| `pattern_set_extend(pat, extend)` | 平铺模式：`NONE/REPEAT/REFLECT/PAD` |
| `pattern_set_filter(pat, filter)` | 过滤：`FAST/GOOD/BEST/NEAREST/BILINEAR/GAUSSIAN` |

### 图片

```c
vg_image_desc_t desc = {0};
desc.img        = &my_img;
desc.width      = 256;
desc.height     = 256;
desc.format     = VG_FORMAT_RGBA8;
desc.stride     = 256 * 4;
desc.pixels     = pixel_data;
desc.x = desc.y = 0;
desc.w = desc.width;
desc.h = desc.height;
desc.px_size    = 4;
desc.is_destroy = false;
desc.is_copy    = true;   // 为 true 时 OVG 内部会复制像素
cb->image_update(vg, &my_img, &desc);
```

| 函数 | 说明 |
|---|---|
| `cb->image_update(vg, img, desc)` | 创建或更新纹理。尺寸不变→覆盖上传（可只传脏矩形）；尺寸变化→后端重建纹理，旧纹理延迟释放 |
| `cb->image_destroy(vg, img)` | 标记图片不再使用，GPU 纹理延迟释放 |

`vg_image_desc_t` 关键字段：

| 字段 | 说明 |
|---|---|
| `img` | 需要更新/创建的纹理对象（由用户管理生命周期） |
| `width/height` | 像素尺寸 |
| `format` | `VG_FORMAT_RGBA8 / BGRA8 / RGBA8_SRGB / BGRA8_SRGB / RGBA16F / RGBA32F` |
| `stride` | 行字节跨度 |
| `pixels` | CPU 像素指针；`is_copy=false` 时须等待 `img->copy_status==true` 方可释放 |
| `x,y,w,h` | 脏矩形区域 |
| `idx` | 纹理数组层号 |
| `is_destroy` | `true` 表示删除纹理 |
| `is_copy` | `true` 表示 OVG 内部复制像素内存 |

绘制图片：

```c
ovg_image_r ir = {0};
ir.img    = &my_img;
ir.rc     = {0, 0, 256, 256};   // 纹理区域
ir.sliced = {0,0,0,0};          // 九宫格：左/上/右/下
ir.dst    = {50, 50, 200, 120}; // 目标位置与大小
ir.color  = 0xFFFFFFFF;         // 混合颜色
ir.type   = 0;
cb->add_image(vg, &ir);
```

### 文本

```c
font_familys_t* ff = new_font_family(font_cache, "Microsoft YaHei", NULL);

text_style_t ts = {0};
ts.family      = ff;
ts.fontsize    = 24.0f;
ts.lineheight  = 1.4f;
ts.align       = {0.5f, 0.5f};
ts.color       = 0xFFC2C2C2;     // 文本颜色
ts.color_stroke= 0xFF000000;     // 描边颜色
ts.color_shadow= 0xCC121212;     // 阴影颜色
ts.shadow_pos  = {1.0f, 1.0f};
ts.stroke      = 0.0f;           // >0 用矢量路径描边，<0 用 4 方向偏移描边

text_st_t t = {0};
t.pos   = {100.0f, 100.0f};
t.text  = "Hello OVG";
t.text_len = strlen(t.text);

text_box_rt box = {0};
box.rc        = {100, 100, 300, 60};
box.auto_break= 1;
box.word_wrap = 2;
box.ellipsis  = 1;

cb->add_text(vg, &t, &ts, &box);
```

`text_style_t` 关键字段：`fontsize`、`lineheight`、`align`（0~1）、`shadow_pos`、`stroke`（正负含义不同）、`color`/`color_stroke`/`color_shadow`、`min_subpixel`。

### 几何（2D / 3D 三角形）

```c
gem_info_t info = {0};
info.blendMode = normal;
info.cullMode  = 2; // BACK
info.frontFace = 0; // CCW
cb->set_geom_state(vg, &info, NULL);

float verts[] = { /* xy */ };
uint32_t colors[] = { /* rgba8 */ };
float uvs[] = { /* uv */ };
uint32_t indices[] = { 0,1,2 };

cb->add_geometry(vg,
    NULL,                    // texture
    verts,  2 * sizeof(float),
    colors, sizeof(uint32_t),
    uvs,    2 * sizeof(float),
    3, indices, 3, sizeof(uint32_t), 1);
```

| 函数 | 说明 |
|---|---|
| `set_geom_state(vg, gem_info_t*, mat4*)` | 设置几何管线状态（任一为 NULL 则不修改） |
| `set_instance_mat(vg, mats, count)` | 设置实例化矩阵，返回写入的实例槽位 |
| `add_geometry(vg, tex, xy,xy_stride, color,color_stride, uv,uv_stride, n_vert, indices, n_idx, idx_size, color_type)` | 添加 2D 三角形（xy 顶点） |
| `add_geometry3d(vg, tex, xyz,..., color, color_stride,..., uv,..., n_vert, indices, n_idx, idx_size, color_type)` | 添加 3D 三角形（xyz 顶点，双面需双倍颜色） |

`gem_info_t` 字段：`blendMode`、`topology`、`polygon`、`frontFace`（0=CCW, 1=CW）、`cullMode`（0=None, 1=Front, 2=Back, 3=Both）、`flags`（depth/stencil）、`lineWidth`、`shader`（`ST_NONE/MASK/DOUBLESIDED/INSTANCE/INSTANCE_DOUBLESIDED`）。

### 裁剪

| 函数 | 说明 |
|---|---|
| `void clip(rvg_t*)` | 用当前路径裁剪并清空路径 |
| `void clip_preserve(rvg_t*)` | 用当前路径裁剪并保留路径 |
| `void clip_rect(rvg_t*, int x,y,w,h)` | 矩形裁剪 |
| `void reset_clip(rvg_t*, uint8_t ref)` | 重置裁剪：`ref=0` 清空，`ref=1` 全部通过 |
| `void set_clip_rect / get_clip_rect(rvg_t*, int[4])` | 设置/获取矩形裁剪 |

### 录制与回放
todo:
```c
/* 录制一帧 */
cb->start_recording(vg);
// ... 录制所有绘图命令 ...
ovg_recording_t* rec = cb->stop_recording(vg);

/* 回放整个录制 */
cb->replay(vg, rec);

/* 回放单条命令 */
cb->replay_command(vg, rec, cmdIndex);

/* 查询 */
uint32_t count = cb->recording_get_count(rec);
void* data     = cb->recording_get_data(rec);

/* 销毁 */
cb->recording_destroy(rec);
```

> 录制的命令可跨帧复用，适合静态 UI、图标、背景等不常变化的内容做缓存。

---

## Flex 布局

OVG 内置一个轻量级 Flex布局引擎（"Tiny Flex"）。

```c
flex_run* fr = new_flex_run(ac);
vec4 result  = flex_run_layout(fr, flex_data_array, item_count, node_array, node_count);
free_flex_run(fr);
```

`flex_data` 支持：

| 类别 | 字段 |
|---|---|
| 尺寸 | `width, height`（NAN = 自适应） |
| 偏移 | `left, right, top, bottom` |
| 内边距 | `padding_left/right/top/bottom` |
| 外边距 | `margin_left/right/top/bottom` |
| 弹性 | `grow, shrink, basis, order` |
| 主轴对齐 | `justify_content`：`START/END/CENTER/SPACE_BETWEEN/SPACE_AROUND/SPACE_EVENLY` |
| 交叉轴对齐 | `align_items`、`align_self`：`STRETCH/CENTER/START/END/BASELINE/AUTO` |
| 多行对齐 | `align_content` |
| 方向 | `direction`：`ROW/ROW_REVERSE/COLUMN/COLUMN_REVERSE` |
| 换行 | `wrap`：`NO_WRAP/WRAP/WRAP_REVERSE` |
| 定位 | `position`：`RELATIVE/ABSOLUTE` |
| 其他 | `baseline`、`should_order_children` |

---

## 数据结构

### 核心对象

| 类型 | 说明 |
|---|---|
| `rvg_t` | 矢量录制对象，含 `width/height/path/st`，可创建多个做缓存 |
| `ovg_path_t` | 路径对象（不透明指针） |
| `ovg_ctx_cb` | 上下文虚表，包含所有绘图回调 |
| `ovg_draw_data_t` | 提交给 GPU 的打包数据：命令列表、顶点、索引、UBO、几何数据、纹理更新、管线信息 |

### 命令类型

| 类型 | 说明 |
|---|---|
| `DRAW_VG` | 矢量图管线（2D） |
| `DRAW_GEOM` | 普通/遮罩/双面/实例化三角形（2D 或 3D） |

`gcmd_t` 是 `vgcmd_t` 与 `geom_cmd_t` 的联合体，`ovg_draw_data_t` 中通过 `d[i].vg.stype` / `d[i].g.stype` 区分（`0=VG, 1=GEOM`）。

### 顶点布局

| 类型 | 布局 |
|---|---|
| `ovgVertex` | `vec2 pos + vec2 uv + uint32_t color`（矢量） |
| `geomVertex1` | `vec3 pos + vec2 uv + uint32_t color`（单面） |
| `geomVertex2` | `vec3 pos + vec2 uv + uint32_t color + uint32_t color1`（双面） |

### 内存分配器

```c
typedef struct mem_resource_t {
    void* vf;       // 虚表指针
    size_t _Align;
    void* ptr;
} mem_resource_t;
```

`rvg_t` 的所有动态内存都通过传入的 `mem_resource_t*` 分配。传 `NULL` 时使用默认分配器。C++ 侧可继承 `vg_alloc_cx` 实现自定义分配器。

---

## 枚举参考

### 合成操作 `vg_operator_t`

`VG_OPERATOR_CLEAR`、`VG_OPERATOR_SOURCE`、`VG_OPERATOR_OVER`、`VG_OPERATOR_DIFFERENCE`

### 混合模式 `blendMode_e`

`none(-1)`、`normal(0)`、`additive`、`multiply`、`modulate`、`screen`、`normal_prem`、`additive_prem`

### 线帽 `vg_line_cap_t`

`VG_LINE_CAP_BUTT`、`VG_LINE_CAP_ROUND`、`VG_LINE_CAP_SQUARE`

### 线连接 `vg_line_join_t`

`VG_LINE_JOIN_MITER`、`VG_LINE_JOIN_ROUND`、`VG_LINE_JOIN_BEVEL`

### 填充规则 `vg_fill_rule_t`

`VG_FILL_RULE_EVEN_ODD`、`VG_FILL_RULE_NON_ZERO`

### 平铺 `vg_extend_t`

`VG_EXTEND_NONE`、`VG_EXTEND_REPEAT`、`VG_EXTEND_REFLECT`、`VG_EXTEND_PAD`

### 过滤 `vg_filter_t`

`VG_FILTER_FAST`、`VG_FILTER_GOOD`、`VG_FILTER_BEST`、`VG_FILTER_NEAREST`、`VG_FILTER_BILINEAR`、`VG_FILTER_GAUSSIAN`

### 图案类型 `vg_pattern_type_t`

`SOLID`、`SURFACE`、`LINEAR`、`RADIAL`、`MESH`（未实现）、`RASTER_SOURCE`、`SWEEP`

### 纹理格式 `vg_format_t`

`VG_FORMAT_RGBA8`、`BGRA8`、`RGBA8_SRGB`、`BGRA8_SRGB`、`RGBA16F`、`RGBA32F`

### 图片翻转 `ImageFlipMode`

`FLIP_NONE`、`FLIP_HORIZONTAL`、`FLIP_VERTICAL`、`FLIP_HORIZONTAL_AND_VERTICAL`

### 路径段类型 `path_type_et`

`e_vmove=1`（移动）、`e_vline`（直线）、`e_vcurve`/`e_quadratic`（二次）、`e_vcubic`（三次）、`e_close`（封闭）

### Flex 对齐 `flex_align`

`ALIGN_AUTO`、`STRETCH`、`CENTER`、`START`、`END`、`SPACE_BETWEEN`、`SPACE_AROUND`、`SPACE_EVENLY`、`BASELINE`

---

## 构建与集成

OVG 为头文件 + 实现分离设计。典型的集成方式：

```bash
# 将 ovg/ 目录加入包含路径
gcc -I./ovg main.c ovg/ovg_c.c -o my_app

# 或作为 CMake 子项目
add_subdirectory(ovg)
target_link_libraries(my_app ovg)
```

依赖（视后端选择）：

- **SDL3**（`SDL_GPU` 纹理格式枚举依赖） — 可选，取决于后端
- **HarfBuzz** — 文本 shaping（`hb_font_t`、`hb_set_t`）
- **GLM**（C++ 侧）— 矩阵/向量类型别名
- **C99 / C++17** 编译器

---

## 许可证

见仓库 `LICENSE` 文件。

---

## 参考

- [Cairo — 类似的 2D 矢量绘制模型](https://www.cairographics.org/)
- [HarfBuzz — 文本 shaping](https://harfbuzz.github.io/)
- [flex — 布局引擎](https://github.com/xamarin/flex)
- [SDL3 GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU)
