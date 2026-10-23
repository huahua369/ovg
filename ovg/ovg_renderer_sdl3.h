#pragma once

#include <cstdint>
#include <queue>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <queue>
#include <vector>
#include "ovg.h"
// Forward declarations
struct ovg_device_t;		// 内部封装结构，包括：设备、空白纹理、shaderModules
struct ovg_ctx_t;
struct sdl3gpu_texture;		// 内部纹理包装器

struct ovg_sdl3_ctx {
	SDL_Window* window;
	SDL_GPUDevice* device;
};

// FBO 结构体 - SDL3 GPU 版本
struct vg_fbo_t {
	uint32_t width, height;
	glm::ivec2 display_size;
	ovg_ctx_t* ctx = 0;
	SDL_Window* window;
	SDL_GPUTexture* swapchain;			// 窗口主颜色纹理
	sdl3gpu_texture* colorTex;			// 主颜色纹理，和窗口互斥
	sdl3gpu_texture* colorTexMS;		// MSAA 解析前纹理（可选）
	sdl3gpu_texture* depthStencilTex;	// 深度+模板纹理
	SDL_GPUCommandBuffer* cmd;
	bool hasStencil;// 是否有模板附件
	bool upsize = true;
};

// 管道状态（SDL3 GPU 版本）
struct pipelinestate_p {
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUSampler* sampler;
	gem_info_t state = {};
};
// 创建主窗口和gpu设备
bool vg_sdl3_init(ovg_sdl3_ctx* g, int width, int height, bool is_vulkan);
// 设备创建与销毁
ovg_device_t* new_sdl3gpu_device(SDL_GPUDevice* gpuDevice);
void          free_sdl3gpu_device(ovg_device_t* dev);
// 渲染上下文创建与销毁
ovg_ctx_t* new_ovgctx_sdl3(ovg_device_t* dev, SDL_GPUTextureFormat colorFormat, SDL_GPUTextureFormat depthFormat, SDL_GPUSampleCount samples);
void       free_ovgctx_sdl3(ovg_ctx_t* ctx);

// FBO，创建与ovg_ctx兼容格式纹理，绑定窗口可选
vg_fbo_t new_vgfbo_sdl3(ovg_ctx_t* ctx, int width, int height, SDL_Window* window = nullptr);
void free_vgfbo_sdl3(vg_fbo_t* fbo);
void reset_vgfbo_sdl3(vg_fbo_t* fbo, int width, int height);
sdl3gpu_texture* new_texture_def(ovg_ctx_t* ctx, int w, int h, uint32_t format);

// 获取 FBO 颜色纹理：窗口 FBO 返回 swapchain，离屏 FBO 返回 colorTex。
// 主要用于离屏测试的 SDL_DownloadFromGPUTexture 读回。
SDL_GPUTexture* ovg_fbo_color_texture(vg_fbo_t* fbo);

// 采样调用方已有的 SDL_GPUTexture。ovg 只借用纹理，不释放它；后端创建
// linear + clamp 采样器并在 fini 时延迟释放。纹理必须带
// SDL_GPU_TEXTUREUSAGE_SAMPLER。
bool ovg_texture_init_native(ovg_ctx_t* ctx, ovg_texture_t* tex,
	SDL_GPUTexture* texture, int width, int height, vg_format_t format);
// 原地替换纹理指针/尺寸，保持 image.id 不变；旧纹理由调用方在
// ovg_wait_idle 之后释放。适用于 FBO 重建。
bool ovg_texture_update_native(ovg_ctx_t* ctx, ovg_texture_t* tex,
	SDL_GPUTexture* texture, int width, int height, vg_format_t format);
// 从 ctx 移除句柄并延迟释放采样器；不会释放借用的 SDL_GPUTexture。
void ovg_texture_fini_native(ovg_ctx_t* ctx, ovg_texture_t* tex);

// 获取fbo和cmd
int ovg_get_window_swapchain(ovg_ctx_t* ctx, vg_fbo_t* fbo, int count, bool wait);
// 绘制一帧，提交命令到GPU，消耗前面获取的cmd
void ovg_render_frame(ovg_ctx_t* ctx, ovg_draw_data_t* data, size_t count);
void ovg_wait_idle(ovg_ctx_t* ctx);

// 特效离屏资源统计（只读，用于验收/调预算）
typedef struct ovg_fx_stats_t {
	size_t pool_sets;        // 空闲缓存中的集合数
	size_t pending_sets;     // 已提交、等待 GPU fence 的集合数
	size_t pool_bytes;       // 空闲缓存字节数
	size_t pending_bytes;    // pending 字节数
	size_t frame_effects;    // 本帧真正生成纹理的 effect 数
	size_t frame_roi_pixels; // 本帧 ROI 像素总数
	size_t frame_bytes;      // 本帧使用的纹理字节数
} ovg_fx_stats_t;

void ovg_fx_get_stats(ovg_ctx_t* ctx, ovg_fx_stats_t* out);
// 设置池/待回收预算；传 0 表示恢复默认值（下限 1MB）
void ovg_fx_set_budget(ovg_ctx_t* ctx, size_t pool_bytes, size_t pending_bytes);
