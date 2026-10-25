#pragma once
/*
	ovg_tools：基于 ovg_ctx_cb 的常用图形绘制工具（纯头文件、无状态、即时模式）

	所有函数第一个参数都是 (ovg_ctx_cb* cb, rvg_t* vg)，直接向当前 rvg_t 录制命令，
	不持有资源、不创建窗口，可同时在多个 rvg_t 上使用。

	颜色约定：uint32_t，0xAABBGGRR（R 在最低字节，即内存 RGBA 字节序）——
	这是 ovg 的原生字序；vgui 的 0xAARRGGBB 主题色需先做 vgui_ovg_color 式的
	R/B 换位再传入。rgba() 帮助函数按本约定打包。

	状态约定：描边类函数每次都会改写 line_width/line_cap/line_join（与
	vgui_stroke_rect 的"每次显式设置"约定一致），但不还原；虚线函数用完后会
	清除 dash。需要保持状态的调用方自行 cb->save/restore（或用 state_guard）。

	编辑器需求映射（对应 vgui docs/editor_roadmap.md）：
	  UI 编辑器  ：checkerboard/canvas_grid/origin_axes（画布底）、selection_frame/
	              rotate_handle/marquee（选择变换）、guide_v/h/insertion_marker/
	              measure（对齐吸附与拖放指示）、hue_bar/sv_box/alpha_bar/
	              color_wheel/swatch/sv_cursor/bar_thumb/swatch_alpha
	              （P05 取色器：渐变原语 + 交互光标 + 透明预览）。
	  动画编辑器 ：time_ruler/playhead/track_lane/range_bar/keyframe/keyframe_link
	              （T02 Timeline）、bezier3/bezier2/curve_handles/sample_bezier3
	              （P09 曲线与缓动编辑，cubic_v_t 可直接对接 gui/anim.h）。
	  GUI 库通用 ：矩形/圆角/圆/椭圆/药丸/环弧/星形/多边形/折线/勾叉箭头/虚线/
	              渐变/发光阴影特效。
	  颜色数学  ：hsva/rgb_to_hsv（HSV 往返）、srgb_to_linear/linear_to_srgb
	              （P05 颜色空间策略，IEC 61966-2-1 分段传输函数，alpha 直通）。
*/

// ovg_c.h 的 C++ 模式要求先包含 glm（vec2/mat3x2 等直接 using glm 类型）
#include <glm/glm.hpp>
#include "../ovg_c.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace ovg_tools {

// ---------- 常量 ----------
constexpr float kPi = 3.14159265358979323846f;
constexpr float kPi2 = kPi * 2.0f;

// ---------- 颜色工具 ----------
// 打包 0xAABBGGRR（ovg 原生字序）
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return (uint32_t)a << 24 | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}
constexpr uint32_t rgba_f(float r, float g, float b, float a = 1.f)
{
	auto c8 = [](float v) -> uint8_t {
		return (uint8_t)(v < 0.f ? 0.f : (v > 1.f ? 1.f : v) * 255.f + 0.5f);
	};
	return rgba(c8(r), c8(g), c8(b), c8(a));
}
// 只替换 alpha 通道
constexpr uint32_t with_alpha(uint32_t c, uint8_t a)
{
	return (c & 0x00FFFFFFu) | ((uint32_t)a << 24);
}
// 逐通道线性插值（不含 gamma 校正）
constexpr uint32_t lerp_channel(uint32_t a, uint32_t b, int shift, float t)
{
	float v = (float)((a >> shift) & 0xFF) + ((int)((b >> shift) & 0xFF) - (int)((a >> shift) & 0xFF)) * t;
	return (uint32_t)(v < 0.f ? 0.f : (v > 255.f ? 255.f : v)) << shift;
}
constexpr uint32_t lerp_color(uint32_t c0, uint32_t c1, float t)
{
	return lerp_channel(c0, c1, 0, t) | lerp_channel(c0, c1, 8, t)
		| lerp_channel(c0, c1, 16, t) | lerp_channel(c0, c1, 24, t);
}
// HSV -> 0xAABBGGRR；h 单位度，s/v 0~1
inline uint32_t hsva(float h, float s, float v, float a = 1.f)
{
	h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f);
	float c = v * s;
	float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
	float m = v - c;
	float r, g, b;
	if (h < 60.f)       { r = c; g = x; b = 0; }
	else if (h < 120.f) { r = x; g = c; b = 0; }
	else if (h < 180.f) { r = 0; g = c; b = x; }
	else if (h < 240.f) { r = 0; g = x; b = c; }
	else if (h < 300.f) { r = x; g = 0; b = c; }
	else                { r = c; g = 0; b = x; }
	return rgba_f(r + m, g + m, b + m, a);
}
// 0xAABBGGRR -> HSV；h 单位度、s/v 0~1（hsva 的逆变换，无彩色时 h=s=0）。
// 经 8bit 量化的往返存在固有误差（约 ±2 度），光标定位足够。
inline void rgb_to_hsv(uint32_t c, float& h, float& s, float& v)
{
	float r = (float)(c & 0xFF) / 255.f;
	float g = (float)((c >> 8) & 0xFF) / 255.f;
	float b = (float)((c >> 16) & 0xFF) / 255.f;
	float maxc = std::max({ r, g, b });
	float minc = std::min({ r, g, b });
	float d = maxc - minc;
	v = maxc;
	if (d <= 0.f)
	{
		h = 0.f;
		s = 0.f;
		return;
	}
	s = d / maxc;
	if (maxc == r)
		h = 60.f * std::fmod((g - b) / d, 6.f);
	else if (maxc == g)
		h = 60.f * ((b - r) / d + 2.f);
	else
		h = 60.f * ((r - g) / d + 4.f);
	if (h < 0.f)
		h += 360.f;
}
// sRGB 传输函数（IEC 61966-2-1 分段式，非纯 2.2 幂）；通道值 0~1，越界裁剪
inline float srgb_to_linear(float c)
{
	c = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
	return c <= 0.04045f ? c / 12.92f
		: std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linear_to_srgb(float c)
{
	c = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
	return c <= 0.0031308f ? 12.92f * c
		: 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
// 8bit 量化版（四舍五入，与 rgba_f 的 c8 同款约定；两端点 0/255 精确映射）
inline uint8_t srgb_to_linear_u8(uint8_t c)
{
	float v = srgb_to_linear((float)c / 255.f) * 255.f + 0.5f;
	return (uint8_t)(v > 255.f ? 255.f : v);
}
inline uint8_t linear_to_srgb_u8(uint8_t c)
{
	float v = linear_to_srgb((float)c / 255.f) * 255.f + 0.5f;
	return (uint8_t)(v > 255.f ? 255.f : v);
}
// 整色 0xAABBGGRR 转换，alpha 直通（透明度是覆盖率而非强度，不做 gamma）；
// 逐字节转换后按原位移回填，与 R/B 字序无关。P05 颜色空间策略：应用在
// 属性读写边界选择存储空间（sRGB 直存或线性化存储）。
inline uint32_t srgb_color_to_linear(uint32_t c)
{
	return (uint32_t)srgb_to_linear_u8((uint8_t)(c & 0xFF))
		| (uint32_t)srgb_to_linear_u8((uint8_t)((c >> 8) & 0xFF)) << 8
		| (uint32_t)srgb_to_linear_u8((uint8_t)((c >> 16) & 0xFF)) << 16
		| (c & 0xFF000000u);
}
inline uint32_t linear_color_to_srgb(uint32_t c)
{
	return (uint32_t)linear_to_srgb_u8((uint8_t)(c & 0xFF))
		| (uint32_t)linear_to_srgb_u8((uint8_t)((c >> 8) & 0xFF)) << 8
		| (uint32_t)linear_to_srgb_u8((uint8_t)((c >> 16) & 0xFF)) << 16
		| (c & 0xFF000000u);
}
// 颜色字 -> 0~1 浮点（渐变 stop 用）
inline void color_to_f(uint32_t c, float& r, float& g, float& b, float& a)
{
	r = (float)(c & 0xFF) / 255.f;
	g = (float)((c >> 8) & 0xFF) / 255.f;
	b = (float)((c >> 16) & 0xFF) / 255.f;
	a = (float)((c >> 24) & 0xFF) / 255.f;
}

// ---------- 状态 ----------
// RAII save/restore；析构时还原 lineWidth/cap/join/dash/矩阵/特效等全部状态
struct state_guard
{
	ovg_ctx_cb* cb;
	rvg_t* vg;
	state_guard(ovg_ctx_cb* c, rvg_t* v) : cb(c), vg(v) { if (cb) cb->save(vg); }
	~state_guard() { if (cb) cb->restore(vg); }
};

// ---------- 基础形状 ----------
inline void fill_rect(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h, uint32_t color)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	cb->rectangle(vg, x, y, w, h);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
inline void stroke_rect(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float line_w = 1.f)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	cb->rectangle(vg, x, y, w, h);
	cb->set_line_width(vg, line_w);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
inline void fill_round_rect(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float r, uint32_t color)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
	if (r > 0) cb->rounded_rectangle(vg, x, y, w, h, r);
	else cb->rectangle(vg, x, y, w, h);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
inline void stroke_round_rect(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float r, uint32_t color, float line_w = 1.f)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
	if (r > 0) cb->rounded_rectangle(vg, x, y, w, h, r);
	else cb->rectangle(vg, x, y, w, h);
	cb->set_line_width(vg, line_w);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 椭圆圆角（rx/ry 独立）
inline void fill_round_rect2(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float rx, float ry, uint32_t color)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	cb->rounded_rectangle2(vg, x, y, w, h, std::clamp(rx, 0.f, w * 0.5f), std::clamp(ry, 0.f, h * 0.5f));
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
inline void stroke_round_rect2(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float rx, float ry, uint32_t color, float line_w = 1.f)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	cb->rounded_rectangle2(vg, x, y, w, h, std::clamp(rx, 0.f, w * 0.5f), std::clamp(ry, 0.f, h * 0.5f));
	cb->set_line_width(vg, line_w);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 四角独立圆角路径：radii = { 左上, 右上, 右下, 左下 }
inline void round_rect4_path(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	const float radii[4])
{
	if (!cb || !vg) return;
	float tl = std::clamp(radii[0], 0.f, std::min(w, h) * 0.5f);
	float tr = std::clamp(radii[1], 0.f, std::min(w, h) * 0.5f);
	float br = std::clamp(radii[2], 0.f, std::min(w, h) * 0.5f);
	float bl = std::clamp(radii[3], 0.f, std::min(w, h) * 0.5f);
	// 同一条边两端的半径之和不能超过边长，超出则等比压缩
	float sx = std::min(1.f, w / std::max(tl + tr, 1e-6f));
	float sx2 = std::min(1.f, w / std::max(bl + br, 1e-6f));
	float sy = std::min(1.f, h / std::max(tl + bl, 1e-6f));
	float sy2 = std::min(1.f, h / std::max(tr + br, 1e-6f));
	tl *= std::min(sx, sy); tr *= std::min(sx, sy2);
	br *= std::min(sx2, sy2); bl *= std::min(sx2, sy);
	cb->new_path(vg);
	cb->move_to(vg, x + tl, y);
	cb->line_to(vg, x + w - tr, y);
	if (tr > 0) cb->arc(vg, x + w - tr, y + tr, tr, -kPi * 0.5f, 0);
	cb->line_to(vg, x + w, y + h - br);
	if (br > 0) cb->arc(vg, x + w - br, y + h - br, br, 0, kPi * 0.5f);
	cb->line_to(vg, x + bl, y + h);
	if (bl > 0) cb->arc(vg, x + bl, y + h - bl, bl, kPi * 0.5f, kPi);
	cb->line_to(vg, x, y + tl);
	if (tl > 0) cb->arc(vg, x + tl, y + tl, tl, kPi, kPi * 1.5f);
	cb->close_path(vg);
}
inline void fill_round_rect4(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	const float radii[4], uint32_t color)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	round_rect4_path(cb, vg, x, y, w, h, radii);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
inline void stroke_round_rect4(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	const float radii[4], uint32_t color, float line_w = 1.f)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	round_rect4_path(cb, vg, x, y, w, h, radii);
	cb->set_line_width(vg, line_w);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
inline void fill_circle(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r, uint32_t color)
{
	if (!cb || !vg || r <= 0) return;
	cb->circle(vg, cx, cy, r);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
inline void stroke_circle(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r,
	uint32_t color, float line_w = 1.f)
{
	if (!cb || !vg || r <= 0) return;
	cb->circle(vg, cx, cy, r);
	cb->set_line_width(vg, line_w);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
inline void fill_ellipse(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float rx, float ry,
	uint32_t color, float rotation_rad = 0.f)
{
	if (!cb || !vg || rx <= 0 || ry <= 0) return;
	cb->ellipse(vg, rx, ry, cx, cy, rotation_rad);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
// 药丸形（两端半圆），开关滑轨/标签胶囊
inline void fill_capsule(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h, uint32_t color)
{
	fill_round_rect(cb, vg, x, y, w, h, std::min(w, h) * 0.5f, color);
}
inline void stroke_capsule(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float line_w = 1.f)
{
	stroke_round_rect(cb, vg, x, y, w, h, std::min(w, h) * 0.5f, color, line_w);
}
inline void fill_triangle(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	float x2, float y2, uint32_t color)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, x0, y0);
	cb->line_to(vg, x1, y1);
	cb->line_to(vg, x2, y2);
	cb->close_path(vg);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
// 多边形填充（点序按给定顺序；凹多边形正确性取决于当前 fill rule）
inline void fill_polygon(ovg_ctx_cb* cb, rvg_t* vg, const vec2* pts, size_t n, uint32_t color)
{
	if (!cb || !vg || !pts || n < 3) return;
	cb->new_path(vg);
	cb->move_to(vg, pts[0].x, pts[0].y);
	for (size_t i = 1; i < n; ++i)
		cb->line_to(vg, pts[i].x, pts[i].y);
	cb->close_path(vg);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
// 折线描边；close=true 时闭合
inline void stroke_polyline(ovg_ctx_cb* cb, rvg_t* vg, const vec2* pts, size_t n, uint32_t color,
	float line_w = 1.f, int cap = VG_LINE_CAP_ROUND, int join = VG_LINE_JOIN_ROUND, bool close = false)
{
	if (!cb || !vg || !pts || n < 2) return;
	cb->new_path(vg);
	cb->move_to(vg, pts[0].x, pts[0].y);
	for (size_t i = 1; i < n; ++i)
		cb->line_to(vg, pts[i].x, pts[i].y);
	if (close)
		cb->close_path(vg);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, cap);
	cb->set_line_join(vg, join);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 绕点旋转（屏幕坐标系，正角度 = 顺时针）
inline vec2 rot_pt(vec2 p, vec2 c, float rad)
{
	float s = std::sin(rad), co = std::cos(rad);
	vec2 d = p - c;
	return c + vec2(d.x * co - d.y * s, d.x * s + d.y * co);
}
// 正多边形（默认第一个顶点朝上）
inline void fill_regular_polygon(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r,
	int sides, uint32_t color, float rotation_rad = -kPi * 0.5f)
{
	if (!cb || !vg || r <= 0 || sides < 3) return;
	vec2 pts[64];
	if (sides > 64) sides = 64;
	for (int i = 0; i < sides; ++i)
	{
		float a = rotation_rad + (float)i / sides * kPi2;
		pts[i] = vec2(cx + std::cos(a) * r, cy + std::sin(a) * r);
	}
	fill_polygon(cb, vg, pts, (size_t)sides, color);
}
// 星形（points 个外尖，内半径 r_inner）
inline void fill_star(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r_outer, float r_inner,
	int points, uint32_t color, float rotation_rad = -kPi * 0.5f)
{
	if (!cb || !vg || r_outer <= 0 || points < 3) return;
	vec2 pts[128];
	int n = points * 2;
	if (n > 128) n = 128;
	for (int i = 0; i < n; ++i)
	{
		float r = (i & 1) ? r_inner : r_outer;
		float a = rotation_rad + (float)i / n * kPi2;
		pts[i] = vec2(cx + std::cos(a) * r, cy + std::sin(a) * r);
	}
	fill_polygon(cb, vg, pts, (size_t)n, color);
}
// 圆环（粗描边实现：中线半径 + 线宽 = 环厚）。
// 不用"外圈+内圈双子路径 fill"——ovg 按子路径独立光栅化，双子路径无法出洞。
inline void fill_ring(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r_outer, float r_inner,
	uint32_t color)
{
	if (!cb || !vg || r_outer <= 0 || r_inner >= r_outer) return;
	cb->circle(vg, cx, cy, (r_outer + r_inner) * 0.5f);
	cb->set_line_width(vg, r_outer - r_inner);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 扇形（饼图切片）。角度：弧度，0 = 正右方，递增为屏幕顺时针
inline void fill_pie(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r, float a0, float a1,
	uint32_t color)
{
	if (!cb || !vg || r <= 0) return;
	cb->new_path(vg);
	cb->move_to(vg, cx, cy);
	cb->arc(vg, cx, cy, r, a0, a1);
	cb->close_path(vg);
	cb->set_source_color(vg, color);
	cb->fill(vg);
}
// 弧线描边（进度环：line_w 即环厚，圆头端点）
inline void stroke_arc(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r, float a0, float a1,
	uint32_t color, float line_w = 4.f, int cap = VG_LINE_CAP_ROUND)
{
	if (!cb || !vg || r <= 0) return;
	cb->new_path(vg);
	cb->arc(vg, cx, cy, r, a0, a1);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, cap);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}

// ---------- 线条 ----------
inline void line(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color, float line_w = 1.f, int cap = VG_LINE_CAP_ROUND)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, x0, y0);
	cb->line_to(vg, x1, y1);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, cap);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 虚线（用完自动清除 dash）
inline void dashed_line(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color, float line_w = 1.f, float on = 4.f, float off = 4.f, float offset = 0.f)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, x0, y0);
	cb->line_to(vg, x1, y1);
	cb->set_line_width(vg, line_w);
	float d[2] = { on, off };
	cb->set_dash(vg, d, 2, offset);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
	cb->set_dash(vg, nullptr, 0, 0);
}
inline void dashed_rect(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float line_w = 1.f, float on = 4.f, float off = 4.f, float offset = 0.f)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	cb->rectangle(vg, x, y, w, h);
	cb->set_line_width(vg, line_w);
	float d[2] = { on, off };
	cb->set_dash(vg, d, 2, offset);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
	cb->set_dash(vg, nullptr, 0, 0);
}
// 实心箭头头部（在 tip 处沿方向 n 画三角形）
inline void arrow_head(ovg_ctx_cb* cb, rvg_t* vg, float tip_x, float tip_y, float nx, float ny,
	float head, uint32_t color)
{
	float bx = tip_x - nx * head, by = tip_y - ny * head;
	fill_triangle(cb, vg, tip_x, tip_y,
		bx - ny * head * 0.42f, by + nx * head * 0.42f,
		bx + ny * head * 0.42f, by - nx * head * 0.42f, color);
}
// 单向箭头（标注、流向、坐标轴）
inline void arrow(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color, float line_w = 1.5f, float head = 7.f)
{
	if (!cb || !vg) return;
	float dx = x1 - x0, dy = y1 - y0;
	float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-4f) return;
	float nx = dx / len, ny = dy / len;
	line(cb, vg, x0, y0, x1 - nx * head * 0.85f, y1 - ny * head * 0.85f, color, line_w);
	arrow_head(cb, vg, x1, y1, nx, ny, head, color);
}
// 实心十字
inline void cross(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, uint32_t color,
	float line_w = 1.f)
{
	line(cb, vg, cx - size, cy, cx + size, cy, color, line_w, VG_LINE_CAP_BUTT);
	line(cb, vg, cx, cy - size, cx, cy + size, color, line_w, VG_LINE_CAP_BUTT);
}
// 带缺口的编辑器十字（中心空 out 半径 gap，四臂长 len）
inline void crosshair(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float gap, float len,
	uint32_t color, float line_w = 1.f)
{
	line(cb, vg, cx - gap - len, cy, cx - gap, cy, color, line_w, VG_LINE_CAP_BUTT);
	line(cb, vg, cx + gap, cy, cx + gap + len, cy, color, line_w, VG_LINE_CAP_BUTT);
	line(cb, vg, cx, cy - gap - len, cx, cy - gap, color, line_w, VG_LINE_CAP_BUTT);
	line(cb, vg, cx, cy + gap, cx, cy + gap + len, color, line_w, VG_LINE_CAP_BUTT);
}

// ---------- 图标符号 ----------
// 对勾（以 cx,cy 为中心，size 约为半宽）
inline void check(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, uint32_t color,
	float line_w = 2.f)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, cx - size * 0.50f, cy + size * 0.02f);
	cb->line_to(vg, cx - size * 0.10f, cy + size * 0.40f);
	cb->line_to(vg, cx + size * 0.52f, cy - size * 0.42f);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, VG_LINE_CAP_ROUND);
	cb->set_line_join(vg, VG_LINE_JOIN_ROUND);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// 叉号
inline void xmark(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, uint32_t color,
	float line_w = 2.f)
{
	float s = size * 0.45f;
	line(cb, vg, cx - s, cy - s, cx + s, cy + s, color, line_w);
	line(cb, vg, cx + s, cy - s, cx - s, cy + s, color, line_w);
}
inline void plus(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, uint32_t color,
	float line_w = 2.f)
{
	cross(cb, vg, cx, cy, size * 0.6f, color, line_w);
}
inline void minus(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, uint32_t color,
	float line_w = 2.f)
{
	line(cb, vg, cx - size * 0.6f, cy, cx + size * 0.6f, cy, color, line_w);
}
// chevron（V 形箭头）：dir 0=右 1=下 2=左 3=上；树展开、下拉指示
inline void chevron(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, int dir,
	uint32_t color, float line_w = 2.f)
{
	if (!cb || !vg) return;
	vec2 c(cx, cy);
	vec2 a = vec2(cx - size * 0.35f, cy - size * 0.60f);
	vec2 b = vec2(cx + size * 0.45f, cy);
	vec2 d = vec2(cx - size * 0.35f, cy + size * 0.60f);
	float ang = (float)(dir & 3) * kPi * 0.5f;
	a = rot_pt(a, c, ang);
	b = rot_pt(b, c, ang);
	d = rot_pt(d, c, ang);
	vec2 pts[3] = { a, b, d };
	stroke_polyline(cb, vg, pts, 3, color, line_w, VG_LINE_CAP_ROUND, VG_LINE_JOIN_ROUND);
}
// 实心小三角：dir 0=右 1=下 2=左 3=上；排序、播放、下拉
inline void tri_glyph(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size, int dir,
	uint32_t color)
{
	if (!cb || !vg) return;
	vec2 c(cx, cy);
	vec2 tip = vec2(cx + size * 0.85f, cy);
	vec2 b0 = vec2(cx - size * 0.55f, cy - size * 0.70f);
	vec2 b1 = vec2(cx - size * 0.55f, cy + size * 0.70f);
	float ang = (float)(dir & 3) * kPi * 0.5f;
	tip = rot_pt(tip, c, ang);
	b0 = rot_pt(b0, c, ang);
	b1 = rot_pt(b1, c, ang);
	fill_triangle(cb, vg, tip.x, tip.y, b0.x, b0.y, b1.x, b1.y, color);
}

// ---------- 渐变 ----------
struct gradient_stop
{
	float t;
	uint32_t color;
};
// 通用线性渐变（返回 ovg 管理的 pattern，无需手动释放）
inline vg_pattern_t* linear_pattern(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	const gradient_stop* stops, size_t n, int extend = VG_EXTEND_PAD)
{
	if (!cb || !vg || !stops || n == 0) return nullptr;
	vg_pattern_t* p = cb->new_pattern_linear(vg, x0, y0, x1, y1);
	for (size_t i = 0; i < n; ++i)
	{
		float r, g, b, a;
		color_to_f(stops[i].color, r, g, b, a);
		cb->pattern_add_color_stop(p, stops[i].t, r, g, b, a);
	}
	cb->pattern_set_extend(p, extend);
	return p;
}
// 双色渐变矩形；horizontal=false 为上->下
inline void fill_rect_gradient(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t c0, uint32_t c1, bool horizontal = false)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	gradient_stop st[2] = { {0.f, c0}, {1.f, c1} };
	vg_pattern_t* p = horizontal
		? linear_pattern(cb, vg, x, y, x + w, y, st, 2)
		: linear_pattern(cb, vg, x, y, x, y + h, st, 2);
	if (!p) return;
	cb->rectangle(vg, x, y, w, h);
	cb->set_source(vg, p);
	cb->fill(vg);
}
inline void fill_round_rect_gradient(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float r, uint32_t c0, uint32_t c1, bool horizontal = false)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	gradient_stop st[2] = { {0.f, c0}, {1.f, c1} };
	vg_pattern_t* p = horizontal
		? linear_pattern(cb, vg, x, y, x + w, y, st, 2)
		: linear_pattern(cb, vg, x, y, x, y + h, st, 2);
	if (!p) return;
	r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
	if (r > 0) cb->rounded_rectangle(vg, x, y, w, h, r);
	else cb->rectangle(vg, x, y, w, h);
	cb->set_source(vg, p);
	cb->fill(vg);
}
// N 色标渐变条（P09 渐变编辑器预览）
inline void gradient_bar(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	const gradient_stop* stops, size_t n)
{
	if (!cb || !vg || w <= 0 || h <= 0 || !stops || n == 0) return;
	vg_pattern_t* p = linear_pattern(cb, vg, x, y, x + w, y, stops, n);
	if (!p) return;
	cb->rectangle(vg, x, y, w, h);
	cb->set_source(vg, p);
	cb->fill(vg);
}

// ---------- 特效（ovg 内建外/内发光、阴影） ----------
inline void set_shadow(ovg_ctx_cb* cb, rvg_t* vg, uint32_t color, float sigma,
	float offset_x, float offset_y, float opacity = 1.f)
{
	if (!cb || !vg) return;
	vg_effect_t e = {};
	e.type = VG_EFFECT_SHADOW;
	e.color = color;
	e.sigma = sigma;
	e.spread = 0;
	e.opacity = opacity;
	e.offset = vec2(offset_x, offset_y);
	cb->set_effect(vg, &e);
}
inline void set_glow(ovg_ctx_cb* cb, rvg_t* vg, uint32_t color, float sigma,
	bool inner = false, float opacity = 1.f)
{
	if (!cb || !vg) return;
	vg_effect_t e = {};
	e.type = inner ? VG_EFFECT_INNER_GLOW : VG_EFFECT_OUTER_GLOW;
	e.color = color;
	e.sigma = sigma;
	e.spread = 0;
	e.opacity = opacity;
	e.offset = vec2(0, 0);
	cb->set_effect(vg, &e);
}
inline void clear_effect(ovg_ctx_cb* cb, rvg_t* vg)
{
	if (cb && vg) cb->set_effect(vg, nullptr);
}

// ---------- 文本（薄包装；字体族由调用方持有） ----------
// 左上角定位 + 盒内对齐（ax/ay 0~1）；不传 ts 则不绘制
inline void draw_text(ovg_ctx_cb* cb, rvg_t* vg, const text_style_t* ts, float x, float y,
	float w, float h, const char* text, float ax = 0.f, float ay = 0.f)
{
	if (!cb || !vg || !ts || !text || !text[0]) return;
	text_st_t t = {};
	t.pos = vec2(x, y);
	t.size = vec2(w, h);
	t.text = text;
	t.text_len = -1;
	text_box_rt box = {};
	box.rc = ivec4((int)x, (int)y, (int)w, (int)h);
	box.text_align = vec2(ax, ay);
	cb->add_text(vg, &t, const_cast<text_style_t*>(ts), &box);
}

// ---------- UI 编辑器：画布 / 选择 / 对齐（V08、O03） ----------
// 透明背景棋盘（图片预览、取色器底）
inline void checkerboard(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float cell = 8.f, uint32_t c0 = 0xffe9e9e9u, uint32_t c1 = 0xffffffffu)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	if (cell < 1.f) cell = 1.f;
	fill_rect(cb, vg, x, y, w, h, c1);
	int cols = (int)std::ceil(w / cell);
	int rows = (int)std::ceil(h / cell);
	for (int j = 0; j < rows; ++j)
	{
		float cy = y + (float)j * cell;
		float ch = std::min(cell, h - (float)j * cell);
		for (int i = 0; i < cols; ++i)
		{
			if ((i + j) & 1) continue;
			float cx = x + (float)i * cell;
			float cw = std::min(cell, w - (float)i * cell);
			fill_rect(cb, vg, cx, cy, cw, ch, c0);
		}
	}
}
// 画布网格：每 spacing 一条次线，每 major_every 条一条主线
inline void canvas_grid(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	float spacing, uint32_t minor_color, int major_every = 5, uint32_t major_color = 0)
{
	if (!cb || !vg || spacing <= 0 || w <= 0 || h <= 0) return;
	int i = 0;
	for (float gx = x; gx <= x + w + 0.01f; gx += spacing, ++i)
	{
		uint32_t c = (major_color && (i % major_every) == 0) ? major_color : minor_color;
		line(cb, vg, gx, y, gx, y + h, c, 1.f, VG_LINE_CAP_BUTT);
	}
	i = 0;
	for (float gy = y; gy <= y + h + 0.01f; gy += spacing, ++i)
	{
		uint32_t c = (major_color && (i % major_every) == 0) ? major_color : minor_color;
		line(cb, vg, x, gy, x + w, gy, c, 1.f, VG_LINE_CAP_BUTT);
	}
}
// 原点坐标轴（红 X 向右、绿 Y 向下）
inline void origin_axes(ovg_ctx_cb* cb, rvg_t* vg, float ox, float oy, float len)
{
	arrow(cb, vg, ox, oy, ox + len, oy, rgba(235, 90, 90), 1.5f, 6.f);
	arrow(cb, vg, ox, oy, ox, oy + len, rgba(100, 215, 110), 1.5f, 6.f);
}
// 方形手柄（白底 + 描边）
inline void selection_handle(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size,
	uint32_t fill_color, uint32_t border_color)
{
	float h = size * 0.5f;
	fill_rect(cb, vg, cx - h, cy - h, size, size, fill_color);
	stroke_rect(cb, vg, cx - h, cy - h, size, size, border_color, 1.f);
}
// 选择框：边框 + 8 个手柄（四角 + 四边中点）
inline void selection_frame(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float handle_size = 6.f, uint32_t handle_fill = 0xffffffffu, bool dashed = false)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	if (dashed)
		dashed_rect(cb, vg, x, y, w, h, color, 1.f);
	else
		stroke_rect(cb, vg, x, y, w, h, color, 1.f);
	float mx = x + w * 0.5f, my = y + h * 0.5f;
	selection_handle(cb, vg, x, y, handle_size, handle_fill, color);
	selection_handle(cb, vg, x + w, y, handle_size, handle_fill, color);
	selection_handle(cb, vg, x, y + h, handle_size, handle_fill, color);
	selection_handle(cb, vg, x + w, y + h, handle_size, handle_fill, color);
	selection_handle(cb, vg, mx, y, handle_size, handle_fill, color);
	selection_handle(cb, vg, mx, y + h, handle_size, handle_fill, color);
	selection_handle(cb, vg, x, my, handle_size, handle_fill, color);
	selection_handle(cb, vg, x + w, my, handle_size, handle_fill, color);
}
// 旋转手柄：从 (cx,cy) 向上伸出 len 的线 + 顶部空心圆
inline void rotate_handle(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float len, float r,
	uint32_t color)
{
	line(cb, vg, cx, cy, cx, cy - len, color, 1.f);
	stroke_circle(cb, vg, cx, cy - len - r, r, color, 1.5f);
}
// 框选跑马线（offset 随时间递增即"蚂蚁线"动画）
inline void marquee(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color, float line_w = 1.f, float offset = 0.f)
{
	float x = std::min(x0, x1), y = std::min(y0, y1);
	dashed_rect(cb, vg, x, y, std::abs(x1 - x0), std::abs(y1 - y0), color, line_w, 4.f, 4.f, offset);
}
// 对齐参考线（垂直/水平虚线）
inline void guide_v(ovg_ctx_cb* cb, rvg_t* vg, float x, float y0, float y1, uint32_t color)
{
	dashed_line(cb, vg, x, y0, x, y1, color, 1.f, 4.f, 4.f);
}
inline void guide_h(ovg_ctx_cb* cb, rvg_t* vg, float y, float x0, float x1, uint32_t color)
{
	dashed_line(cb, vg, x0, y, x1, y, color, 1.f, 4.f, 4.f);
}
// 拖放插入指示（水平：粗线 + 两端朝下小三角；树/列表插入位置）
inline void insertion_marker(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, uint32_t color)
{
	line(cb, vg, x, y, x + w, y, color, 2.f, VG_LINE_CAP_BUTT);
	tri_glyph(cb, vg, x, y + 4.5f, 4.5f, 1, color);
	tri_glyph(cb, vg, x + w, y + 4.5f, 4.5f, 1, color);
}
// 垂直插入指示（Dock/列布局落点）
inline void insertion_marker_v(ovg_ctx_cb* cb, rvg_t* vg, float x, float y0, float y1, uint32_t color)
{
	line(cb, vg, x, y0, x, y1, color, 2.f, VG_LINE_CAP_BUTT);
	tri_glyph(cb, vg, x + 4.5f, y0, 4.5f, 0, color);
	tri_glyph(cb, vg, x + 4.5f, y1, 4.5f, 0, color);
}
// 尺寸标注：端界线 + 双向箭头 + 中线（ts 非空时在中点上方标注距离值）
inline void measure(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color, const text_style_t* ts = nullptr, float tick = 5.f, float head = 6.f)
{
	float dx = x1 - x0, dy = y1 - y0;
	float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-4f) return;
	float nx = dx / len, ny = dy / len;
	float px = -ny, py = nx;
	line(cb, vg, x0 - px * tick, y0 - py * tick, x0 + px * tick, y0 + py * tick, color, 1.f, VG_LINE_CAP_BUTT);
	line(cb, vg, x1 - px * tick, y1 - py * tick, x1 + px * tick, y1 + py * tick, color, 1.f, VG_LINE_CAP_BUTT);
	line(cb, vg, x0, y0, x1, y1, color, 1.f, VG_LINE_CAP_BUTT);
	arrow_head(cb, vg, x0, y0, nx, ny, head, color);
	arrow_head(cb, vg, x1, y1, -nx, -ny, head, color);
	if (ts)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.0f", len);
		draw_text(cb, vg, ts, (x0 + x1) * 0.5f + px * 14.f, (y0 + y1) * 0.5f + py * 14.f, 60, 16, buf);
	}
}

// ---------- 曲线编辑（P09、T02 缓动；对接 gui/anim.h 的 cubic_v_t） ----------
inline void bezier3(ovg_ctx_cb* cb, rvg_t* vg, const cubic_v_t& c, uint32_t color,
	float line_w = 1.5f)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, c.p0.x, c.p0.y);
	cb->curve_to(vg, c.p1.x, c.p1.y, c.p2.x, c.p2.y, c.p3.x, c.p3.y);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, VG_LINE_CAP_ROUND);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
inline void bezier2(ovg_ctx_cb* cb, rvg_t* vg, const quadratic_v_t& q, uint32_t color,
	float line_w = 1.5f)
{
	if (!cb || !vg) return;
	cb->new_path(vg);
	cb->move_to(vg, q.p0.x, q.p0.y);
	cb->quadratic_to(vg, q.p1.x, q.p1.y, q.p2.x, q.p2.y);
	cb->set_line_width(vg, line_w);
	cb->set_line_cap(vg, VG_LINE_CAP_ROUND);
	cb->set_source_color(vg, color);
	cb->stroke(vg);
}
// CPU 采样（命中测试、数值联动）
inline vec2 sample_bezier3(const cubic_v_t& c, float t)
{
	float u = 1.f - t;
	float a = u * u * u, b = 3.f * u * u * t, d = 3.f * u * t * t, e = t * t * t;
	return a * c.p0 + b * c.p1 + d * c.p2 + e * c.p3;
}
inline vec2 sample_bezier2(const quadratic_v_t& q, float t)
{
	float u = 1.f - t;
	return u * u * q.p0 + 2.f * u * t * q.p1 + t * t * q.p2;
}
// 曲线控制柄：锚点方块 + 切线虚线 + 控制点圆
inline void curve_handles(ovg_ctx_cb* cb, rvg_t* vg, const cubic_v_t& c, uint32_t line_color,
	uint32_t handle_color, float handle_r = 3.5f, float anchor_size = 5.f)
{
	dashed_line(cb, vg, c.p0.x, c.p0.y, c.p1.x, c.p1.y, line_color, 1.f, 3.f, 3.f);
	dashed_line(cb, vg, c.p3.x, c.p3.y, c.p2.x, c.p2.y, line_color, 1.f, 3.f, 3.f);
	selection_handle(cb, vg, c.p0.x, c.p0.y, anchor_size, 0xffffffffu, handle_color);
	selection_handle(cb, vg, c.p3.x, c.p3.y, anchor_size, 0xffffffffu, handle_color);
	fill_circle(cb, vg, c.p1.x, c.p1.y, handle_r, handle_color);
	fill_circle(cb, vg, c.p2.x, c.p2.y, handle_r, handle_color);
}
inline void anchor_handle(ovg_ctx_cb* cb, rvg_t* vg, vec2 p, uint32_t color,
	bool selected = false, float size = 5.f)
{
	if (selected)
		fill_rect(cb, vg, p.x - size * 0.5f, p.y - size * 0.5f, size, size, color);
	else
		selection_handle(cb, vg, p.x, p.y, size, 0xffffffffu, color);
}

// ---------- Timeline（T02 动画编辑器） ----------
enum class kf_state { hollow, solid, selected };
// 时间尺：t0~t1 时间区间，step 为小刻度间隔（大刻度 = 每 5 小格）。
// ts 非空时在大刻度处标注时间值。
inline void time_ruler(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	double t0, double t1, double step, uint32_t minor_color, uint32_t major_color,
	const text_style_t* ts = nullptr)
{
	if (!cb || !vg || w <= 0 || h <= 0 || t1 <= t0 || step <= 0) return;
	double range = t1 - t0;
	double t = std::ceil(t0 / step - 1e-9) * step;
	int i = (int)std::floor((t - t0) / step + 1e-9);
	for (; t <= t1 + 1e-9; t += step, ++i)
	{
		float px = x + (float)((t - t0) / range) * w;
		bool major = (i % 5) == 0;
		float len = major ? h * 0.75f : h * 0.4f;
		line(cb, vg, px, y, px, y + len, major ? major_color : minor_color, 1.f, VG_LINE_CAP_BUTT);
		if (major && ts)
		{
			char buf[48];
			std::snprintf(buf, sizeof(buf), "%g", t);
			draw_text(cb, vg, ts, px + 2, y, 48, h, buf);
		}
	}
}
// 播放头：竖线 + 顶部三角帽
inline void playhead(ovg_ctx_cb* cb, rvg_t* vg, float px, float y0, float y1, uint32_t color,
	float cap_w = 10.f, float cap_h = 9.f)
{
	line(cb, vg, px, y0 + cap_h * 0.8f, px, y1, color, 1.5f);
	fill_triangle(cb, vg, px - cap_w * 0.5f, y0, px + cap_w * 0.5f, y0, px, y0 + cap_h, color);
}
// 关键帧菱形：hollow 空心 / solid 实心 / selected 选中（高亮 + 白描边）
inline void keyframe(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float size,
	kf_state state, uint32_t color, uint32_t selected_color)
{
	vec2 pts[4] = {
		vec2(cx, cy - size), vec2(cx + size, cy),
		vec2(cx, cy + size), vec2(cx - size, cy)
	};
	if (state == kf_state::hollow)
	{
		stroke_polyline(cb, vg, pts, 4, color, 1.5f, VG_LINE_CAP_ROUND, VG_LINE_JOIN_ROUND, true);
	}
	else
	{
		fill_polygon(cb, vg, pts, 4, state == kf_state::selected ? selected_color : color);
		if (state == kf_state::selected)
			stroke_polyline(cb, vg, pts, 4, 0xffffffffu, 1.5f, VG_LINE_CAP_ROUND, VG_LINE_JOIN_ROUND, true);
	}
}
// 关键帧之间的插值连线
inline void keyframe_link(ovg_ctx_cb* cb, rvg_t* vg, float x0, float y0, float x1, float y1,
	uint32_t color)
{
	dashed_line(cb, vg, x0, y0, x1, y1, color, 1.f, 2.f, 3.f);
}
// 轨道底纹（奇偶行交替）
inline void track_lane(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	int index, uint32_t c0, uint32_t c1)
{
	fill_rect(cb, vg, x, y, w, h, (index & 1) ? c1 : c0);
}
// 空心区间条（动画区间、音频 clip）
inline void range_bar(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float r = 3.f, float line_w = 1.f)
{
	stroke_round_rect(cb, vg, x, y, w, h, r, color, line_w);
}

// ---------- 取色器（P05） ----------
// 色相条（0~360 度）
inline void hue_bar(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h)
{
	gradient_stop st[7];
	for (int i = 0; i < 7; ++i)
		st[i] = gradient_stop{ (float)i / 6.f, hsva((float)i * 60.f, 1.f, 1.f) };
	gradient_bar(cb, vg, x, y, w, h, st, 7);
}
// 透明度条：棋盘底 + color -> 透明 横向渐变
inline void alpha_bar(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, float cell = 5.f)
{
	checkerboard(cb, vg, x, y, w, h, cell, 0xffccccccu, 0xfff2f2f2u);
	fill_rect_gradient(cb, vg, x, y, w, h, color, with_alpha(color, 0), true);
}
// SV 双维盒：底为纯色相，横向叠白->透明，纵向叠透明->黑
inline void sv_box(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h, float hue_deg)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	fill_rect(cb, vg, x, y, w, h, hsva(hue_deg, 1.f, 1.f));
	gradient_stop wst[2] = { {0.f, 0xffffffffu}, {1.f, with_alpha(0xffffffffu, 0)} };
	vg_pattern_t* pw = linear_pattern(cb, vg, x, y, x + w, y, wst, 2);
	if (pw)
	{
		cb->rectangle(vg, x, y, w, h);
		cb->set_source(vg, pw);
		cb->fill(vg);
	}
	gradient_stop bst[2] = { {0.f, with_alpha(0xff000000u, 0)}, {1.f, 0xff000000u} };
	vg_pattern_t* pb = linear_pattern(cb, vg, x, y, x, y + h, bst, 2);
	if (pb)
	{
		cb->rectangle(vg, x, y, w, h);
		cb->set_source(vg, pb);
		cb->fill(vg);
	}
}
// 色轮（锥形渐变环，粗描边实现，同 fill_ring）
inline void color_wheel(ovg_ctx_cb* cb, rvg_t* vg, float cx, float cy, float r_outer, float r_inner)
{
	if (!cb || !vg || r_outer <= 0 || r_inner >= r_outer) return;
	vg_pattern_t* p = cb->new_pattern_sweep(vg, cx, cy, 0.f, kPi2);
	if (!p) return;
	for (int i = 0; i <= 12; ++i)
	{
		float r, g, b, a;
		color_to_f(hsva((float)i * 30.f, 1.f, 1.f), r, g, b, a);
		cb->pattern_add_color_stop(p, (float)i / 12.f, r, g, b, a);
	}
	cb->circle(vg, cx, cy, (r_outer + r_inner) * 0.5f);
	cb->set_line_width(vg, r_outer - r_inner);
	cb->set_source(vg, p);
	cb->stroke(vg);
}
// 色块（选中时外扩描边）
inline void swatch(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h, uint32_t color,
	bool selected = false, float r = 3.f, uint32_t sel_color = 0xffff8414u)
{
	fill_round_rect(cb, vg, x, y, w, h, r, color);
	if (selected)
		stroke_round_rect(cb, vg, x - 1.5f, y - 1.5f, w + 3.f, h + 3.f, r + 1.5f, sel_color, 1.5f);
}
// SV 盒光标：白色环 + 半透明深色晕边，任意背景可见
inline void sv_cursor(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float r = 5.f)
{
	if (!cb || !vg || r <= 0) return;
	stroke_circle(cb, vg, x, y, r + 1.5f, rgba(0, 0, 0, 200), 2.f);
	stroke_circle(cb, vg, x, y, r, 0xffffffffu, 2.f);
}
// 条形滑钮（色相/透明度条）：白底圆 + 深色描边，与 selection_handle 同款约定
inline void bar_thumb(ovg_ctx_cb* cb, rvg_t* vg, float x, float cy, float r = 6.f)
{
	if (!cb || !vg || r <= 0) return;
	fill_circle(cb, vg, x, cy, r, 0xffffffffu);
	stroke_circle(cb, vg, x, cy, r, rgba(0, 0, 0, 180), 1.5f);
}
// 透明预览色块：棋盘底（裁剪到圆角）+ swatch 叠加，参数对齐 swatch。
// 路径裁剪按 ovg 约定先 reset_clip(vg,0) 清模板；结束时 reset_clip(vg,1)
// 恢复全通过——save/restore 不含裁剪状态，调用方自有的路径裁剪不会被还原。
inline void swatch_alpha(ovg_ctx_cb* cb, rvg_t* vg, float x, float y, float w, float h,
	uint32_t color, bool selected = false, float r = 3.f, uint32_t sel_color = 0xffff8414u)
{
	if (!cb || !vg || w <= 0 || h <= 0) return;
	r = std::clamp(r, 0.f, std::min(w, h) * 0.5f);
	cb->reset_clip(vg, 0);
	if (r > 0) cb->rounded_rectangle(vg, x, y, w, h, r);
	else cb->rectangle(vg, x, y, w, h);
	cb->clip(vg);
	checkerboard(cb, vg, x, y, w, h);
	cb->reset_clip(vg, 1);
	swatch(cb, vg, x, y, w, h, color, selected, r, sel_color);
}

} // namespace ovg_tools
