/*
 * ovg_renderer_sdl3.cpp
 * SDL3 GPU 矢量渲染后端 — "ref 动态切"版
 *
 * 核心设计（stencil 位平面）：
 *   bit0 = STENCIL_FILL_BIT (0x1) → 奇偶填充（INVERT 翻转）
 *   bit1 = STENCIL_CLIP_BIT (0x2) → 裁剪掩码（REPLACE 写入）
 *
 * 原则：
 *   - compareOp / compareMask / writeMask / passOp → pipeline 静态
 *   - ref → SDL_SetGPUStencilReference() 动态切
 *   - 每次 bind pipeline 后立刻 SetStencilReference
 */


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <map>
#include <vector>
#include <cstring>
#include <cassert>
#include <cmath>
#include "ovg_renderer_sdl3.h"
 // ========================================================================
 // 着色器 SPIR-V 数据（extern 声明，链接时从 spv 数组引入）
 // ========================================================================

#include "shaders/spv_c/a_vg.vert.h"
#include "shaders/spv_c/a_vg.frag.h"

#include "shaders/spv_c/a_base3d.vert.h"
#include "shaders/spv_c/a_base3d.frag.h"

#include "shaders/spv_c/a_base3d_mask.vert.h"
#include "shaders/spv_c/a_base3d_mask.frag.h"

#include "shaders/spv_c/a_base3d_dsc.vert.h"
#include "shaders/spv_c/a_base3d_dsc.frag.h"

#include "shaders/spv_c/a_base3d_inst.vert.h"
#include "shaders/spv_c/a_base3d_inst.frag.h"

#include "shaders/spv_c/a_base3d_dsc_inst.vert.h"
#include "shaders/spv_c/a_base3d_dsc_inst.frag.h"


// ========================================================================
// 内部数据结构
// ========================================================================
struct sdl3gpu_buffer {
	SDL_GPUBuffer* buffer = nullptr;
	SDL_GPUDevice* device = nullptr;
	uint32_t        size = 0;
	uint32_t        stride = 0;
	bool            isUniform = false;
};

struct sdl3gpu_texture {
	SDL_GPUTexture* texture = nullptr;
	SDL_GPUSampler* sampler = nullptr;
	SDL_GPUDevice* device = nullptr;
	SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
	int                  width = 0;
	int                  height = 0;
	bool                 hasStencil = false;
	uint32_t             references = 0;
};

struct shadermodule_vf {
	SDL_GPUShader* vert = nullptr;
	SDL_GPUShader* frag = nullptr;
};

struct pipelinestate_p_internal {
	SDL_GPUGraphicsPipeline* pipeline = nullptr;
	SDL_GPUSampler* defaultSampler = nullptr;
	gem_info_t               state = {};
};

// 混合参数
struct blend_params {
	SDL_GPUBlendFactor srcColor;
	SDL_GPUBlendFactor dstColor;
	SDL_GPUBlendOp     colorOp;
	SDL_GPUBlendFactor srcAlpha;
	SDL_GPUBlendFactor dstAlpha;
	SDL_GPUBlendOp     alphaOp;
	bool               blendEnable;
};

// 设备上下文
struct ovg_device_t {
	SDL_GPUDevice* gpuDevice = nullptr;

	shadermodule_vf    shaderModules[5] = {};

	sdl3gpu_texture* emptyTexture = nullptr;
	SDL_GPUShaderFormat supportedFormats = SDL_GPU_SHADERFORMAT_INVALID;
};

// 渲染上下文
struct ovg_ctx_t {
	ovg_device_t* device = nullptr;

	SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
	SDL_GPUSampleCount   samples = SDL_GPU_SAMPLECOUNT_1;

	// ─── 5 条 VG 专用管道 ───────────────────────
	SDL_GPUGraphicsPipeline* pipeOVER = nullptr;  // 正常绘制 (blend = OVER)
	SDL_GPUGraphicsPipeline* pipeSUB = nullptr;  // 差值绘制 (blend = SUB)
	SDL_GPUGraphicsPipeline* pipeCLEAR = nullptr;  // 清除操作 (logic op CLEAR)
	SDL_GPUGraphicsPipeline* pipePolyFill = nullptr;  // 奇偶填充 (stencil INVERT)
	SDL_GPUGraphicsPipeline* pipeClipping = nullptr;  // 裁剪掩码写入 (stencil REPLACE)

	// ─── 几何管线缓存 ─────────────────────────────
	std::map<uint64_t, pipelinestate_p_internal> geomPipelines;
	pipelinestate_p_internal* currentPipeline = nullptr;
	uint64_t                                     currentState = ~0ull;

	// ─── 缓冲区 ────────────────────────────────────
	sdl3gpu_buffer uboGrad;       // 渐变 UBO
	uint32_t        uboSize = 0;
	uint32_t        uboStride = 0;

	sdl3gpu_buffer vboVG;         // VG 顶点
	sdl3gpu_buffer iboVG;         // VG 索引
	sdl3gpu_buffer vboGeom;       // 几何顶点
	sdl3gpu_buffer iboGeom;       // 几何索引

	sdl3gpu_texture* currentTexture = nullptr;
	uint32_t         gradientOffset = 0;

	// ─── 视口 ──────────────────────────────────────
	int viewportW = 0;
	int viewportH = 0;

	// ─── 状态 ──────────────────────────────────────
	bool cmdStarted = false;
	int  status = 0;

	// ─── 当前活跃 RenderPass ──────────────────────
	SDL_GPURenderPass* currentRenderPass = nullptr;
	SDL_GPUCommandBuffer* currentCmdBuf = nullptr;

	// ─── 当前绑定的 VG 管道索引（用于自动设 ref）──
	int                currentVgPipeIndex = -1;  // 0=OVER,1=CLEAR,2=SUB,3=POLYFILL,4=CLIPPING
};

// ========================================================================
// 常量
// ========================================================================
#define VG_PTS_SIZE         1024
#define VG_VBO_SIZE         (VG_PTS_SIZE * 4)
#define VG_IBO_SIZE         (VG_VBO_SIZE * 6)

// Stencil 位平面
#define STENCIL_FILL_BIT    0x1   // bit0: 奇偶填充（INVERT 翻转）
#define STENCIL_CLIP_BIT    0x2   // bit1: 裁剪掩码（REPLACE 写入）
#define STENCIL_ALL_BIT     0x3   // bit0+bit1

// 顶点布局
#define OVG_VERTEX_SIZE     20   // pos.xy(8) + uv.xy(8) + color(4)
#define GEOM_VERTEX_SIZE    28   // pos.xyz(12) + uv.xy(8) + col0(4) + col1(4)

// VG 管道索引（与 currentVgPipeIndex 对应）
#define VG_PIPE_OVER     0
#define VG_PIPE_CLEAR    1
#define VG_PIPE_SUB      2
#define VG_PIPE_POLYFILL 3
#define VG_PIPE_CLIPPING 4

// ========================================================================
// 工具函数
// ========================================================================

inline size_t align_up(size_t val, size_t align)
{
	return (val + align - 1) / align * align;
}

static SDL_GPUShaderFormat detect_supported_shader_format(SDL_GPUDevice* dev) {
	SDL_GPUShaderFormat fmt = SDL_GetGPUShaderFormats(dev);
	if (fmt & SDL_GPU_SHADERFORMAT_SPIRV)  return SDL_GPU_SHADERFORMAT_SPIRV;
	if (fmt & SDL_GPU_SHADERFORMAT_DXIL)   return SDL_GPU_SHADERFORMAT_DXIL;
	if (fmt & SDL_GPU_SHADERFORMAT_MSL)    return SDL_GPU_SHADERFORMAT_MSL;
	return SDL_GPU_SHADERFORMAT_INVALID;
}

// ========================================================================
// 着色器编译
// ========================================================================
static SDL_GPUShader* compile_shader(
	SDL_GPUDevice* device,
	SDL_GPUShaderStage   stage,
	const uint32_t* code,
	size_t               codeSize,
	const char* entryPoint)
{
	SDL_GPUShaderFormat fmt = detect_supported_shader_format(device);
	if (fmt == SDL_GPU_SHADERFORMAT_INVALID) {
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "No supported shader format!");
		return nullptr;
	}

	SDL_GPUShaderCreateInfo sci = {};
	sci.stage = stage;
	sci.format = fmt;
	sci.code_size = codeSize;
	sci.code = (const Uint8*)code;
	sci.entrypoint = entryPoint;
	sci.num_samplers = 0;
	sci.num_uniform_buffers = 0;
	sci.num_storage_buffers = 0;
	sci.num_storage_textures = 0;

	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &sci);
	if (!shader) {
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create %s shader: %s",
			stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertex" : "fragment",
			SDL_GetError());
	}
	return shader;
}

// ========================================================================
// 缓冲区管理
// ========================================================================
static void create_uniform_buffer(ovg_device_t* dev, sdl3gpu_buffer* buf, uint32_t stride, uint32_t count) {
	buf->device = dev->gpuDevice;
	buf->stride = stride;
	buf->size = align_up(stride * count, 256);
	buf->isUniform = true;

	SDL_GPUBufferCreateInfo info = {};
	info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
	info.size = buf->size;

	buf->buffer = SDL_CreateGPUBuffer(dev->gpuDevice, &info);
	assert(buf->buffer && "Failed to create uniform buffer");
}

static void create_vertex_buffer(ovg_device_t* dev, sdl3gpu_buffer* buf, uint32_t size, uint32_t stride) {
	buf->device = dev->gpuDevice;
	buf->stride = stride;
	buf->size = align_up(size, 256);

	SDL_GPUBufferCreateInfo info = {};
	info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	info.size = buf->size;

	buf->buffer = SDL_CreateGPUBuffer(dev->gpuDevice, &info);
	assert(buf->buffer && "Failed to create vertex buffer");
}

static void create_index_buffer(ovg_device_t* dev, sdl3gpu_buffer* buf, uint32_t size) {
	buf->device = dev->gpuDevice;
	buf->stride = sizeof(uint32_t);
	buf->size = size;

	SDL_GPUBufferCreateInfo info = {};
	info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	info.size = buf->size;

	buf->buffer = SDL_CreateGPUBuffer(dev->gpuDevice, &info);
	assert(buf->buffer && "Failed to create index buffer");
}

static void resize_buffer(sdl3gpu_buffer* buf, uint32_t newSize) {
	if (buf->size >= newSize) return;

	SDL_GPUBufferUsageFlags usage;
	if (buf->isUniform) {
		usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
	}
	else {
		usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
	}

	SDL_GPUBufferCreateInfo info = {};
	info.usage = usage;
	info.size = align_up(newSize, 256);;

	SDL_GPUBuffer* newBuf = SDL_CreateGPUBuffer(buf->device, &info);
	assert(newBuf && "Failed to resize buffer");

	// 复制旧数据
	//SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(buf->device);
	//if (cmd) {
	//	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
	//	if (copyPass) {
	//		// 源：旧缓冲区
	//		SDL_GPUBufferRegion srcRegion = {};
	//		srcRegion.buffer = buf->buffer;
	//		srcRegion.offset = 0;
	//		srcRegion.size = buf->size;

	//		// 目的：新缓冲区
	//		SDL_GPUBufferRegion dstRegion = {};
	//		dstRegion.buffer = newBuf;
	//		dstRegion.offset = 0;
	//		dstRegion.size = buf->size;

	//		SDL_CopyGPUBuffer(copyPass, &srcRegion, &dstRegion);
	//		SDL_EndGPUCopyPass(copyPass);
	//	}
	//	SDL_SubmitGPUCommandBuffer(cmd);
	//	SDL_WaitForGPUIdle(buf->device);
	//}

	SDL_ReleaseGPUBuffer(buf->device, buf->buffer);
	buf->buffer = newBuf;
	buf->size = info.size;
}

static void upload_buffer_data(sdl3gpu_buffer* buf, const void* data, uint32_t offset, uint32_t size) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(buf->device);
	if (!cmd) return;
	if (buf->size < size) {
		resize_buffer(buf, size + buf->size * 0.5);
	}
	SDL_GPUTransferBufferCreateInfo tbi = {};
	tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tbi.size = size;
	SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(buf->device, &tbi);
	if (!staging) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return;
	}

	void* mapped = SDL_MapGPUTransferBuffer(buf->device, staging, false);
	if (mapped) {
		memcpy((char*)mapped + offset, data, size);
		SDL_UnmapGPUTransferBuffer(buf->device, staging);
	}

	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
	if (copyPass) {
		SDL_GPUTransferBufferLocation srcLoc = {};
		srcLoc.transfer_buffer = staging;
		srcLoc.offset = offset;

		SDL_GPUBufferRegion dstRegion = {};
		dstRegion.buffer = buf->buffer;
		dstRegion.offset = offset;
		dstRegion.size = size;

		SDL_UploadToGPUBuffer(copyPass, &srcLoc, &dstRegion, false);
		SDL_EndGPUCopyPass(copyPass);
	}

	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_WaitForGPUIdle(buf->device);
	SDL_ReleaseGPUTransferBuffer(buf->device, staging);
}

static void destroy_buffer(sdl3gpu_buffer* buf) {
	if (buf->buffer) {
		SDL_ReleaseGPUBuffer(buf->device, buf->buffer);
		buf->buffer = nullptr;
	}
	buf->size = 0;
}

// ========================================================================
// 纹理管理
// ========================================================================
static sdl3gpu_texture* create_texture(
	ovg_device_t* dev,
	SDL_GPUTextureFormat  format,
	int                   width,
	int                   height,
	SDL_GPUTextureUsageFlags extraUsage = 0)
{
	sdl3gpu_texture* tex = new sdl3gpu_texture();
	//tex->device = dev->gpuDevice;
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | extraUsage;

	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	assert(tex->texture && "Failed to create texture");

	return tex;
}

static sdl3gpu_texture* create_msaa_texture(
	ovg_device_t* dev,
	SDL_GPUTextureFormat format,
	int                  width,
	int                  height,
	SDL_GPUSampleCount   samples)
{
	sdl3gpu_texture* tex = new sdl3gpu_texture();
	//tex->device = dev->gpuDevice;
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = samples;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	assert(tex->texture && "Failed to create MSAA texture");

	return tex;
}

static sdl3gpu_texture* create_depth_stencil_texture(
	ovg_device_t* dev,
	SDL_GPUTextureFormat format,
	int                  width,
	int                  height,
	SDL_GPUSampleCount   samples)
{
	sdl3gpu_texture* tex = new sdl3gpu_texture();
	//tex->device = dev->gpuDevice;
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->hasStencil = true;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = samples;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;

	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	assert(tex->texture && "Failed to create depth/stencil texture");

	return tex;
}

static void create_sampler(
	sdl3gpu_texture* tex,
	SDL_GPUFilter         minFilter,
	SDL_GPUFilter         magFilter,
	SDL_GPUSamplerMipmapMode mipmapMode,
	SDL_GPUSamplerAddressMode addressMode)
{
	if (tex->sampler) {
		SDL_ReleaseGPUSampler(tex->device, tex->sampler);
	}

	SDL_GPUSamplerCreateInfo sci = {};
	sci.min_filter = minFilter;
	sci.mag_filter = magFilter;
	sci.mipmap_mode = mipmapMode;
	sci.address_mode_u = addressMode;
	sci.address_mode_v = addressMode;
	sci.address_mode_w = addressMode;
	sci.max_anisotropy = 1.0f;

	tex->sampler = SDL_CreateGPUSampler(tex->device, &sci);
}

static void destroy_texture(sdl3gpu_texture* tex) {
	if (!tex) return;
	tex->references--;
	if (tex->references > 0) return;

	if (tex->sampler) {
		SDL_ReleaseGPUSampler(tex->device, tex->sampler);
		tex->sampler = nullptr;
	}
	if (tex->texture) {
		SDL_ReleaseGPUTexture(tex->device, tex->texture);
		tex->texture = nullptr;
	}
	delete tex;
}

// ========================================================================
// 混合模式转换
// ========================================================================
static void set_blend_params(blend_params& bp, blendMode_e mode) {
	// 默认值（normal）
	bp.srcColor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	bp.dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	bp.colorOp = SDL_GPU_BLENDOP_ADD;
	bp.srcAlpha = SDL_GPU_BLENDFACTOR_ONE;
	bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	bp.alphaOp = SDL_GPU_BLENDOP_ADD;
	bp.blendEnable = true;

	switch (mode) {
	case blendMode_e::none:
		bp.srcColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ZERO;
		bp.colorOp = SDL_GPU_BLENDOP_ADD;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ZERO;
		bp.alphaOp = SDL_GPU_BLENDOP_ADD;
		bp.blendEnable = false;
		break;
	case blendMode_e::normal:
		// 默认值已是 normal
		break;
	case blendMode_e::normal_prem:
		bp.srcColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case blendMode_e::additive:
		bp.srcColor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ZERO;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case blendMode_e::additive_prem:
		bp.srcColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ZERO;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case blendMode_e::multiply:
		bp.srcColor = SDL_GPU_BLENDFACTOR_DST_COLOR;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ZERO;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case blendMode_e::modulate:
		bp.srcColor = SDL_GPU_BLENDFACTOR_ZERO;
		bp.dstColor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ZERO;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case blendMode_e::screen:
		bp.srcColor = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstColor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
		bp.srcAlpha = SDL_GPU_BLENDFACTOR_ONE;
		bp.dstAlpha = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
		break;
	default:
		break;
	}
}

// ========================================================================
// 着色器模块管理
// ========================================================================
static void init_shader_modules(ovg_device_t* dev) {
	struct { const uint32_t* v; size_t vlen; const uint32_t* f; size_t flen; } shaders[5] = {
		{a_base3d_vert,       sizeof(a_base3d_vert),         a_base3d_frag,          sizeof(a_base3d_frag)},
		{a_base3d_mask_vert,    sizeof(a_base3d_mask_vert),    a_base3d_mask_frag,     sizeof(a_base3d_mask_frag)},
		{a_base3d_dsc_vert,     sizeof(a_base3d_dsc_vert),     a_base3d_dsc_frag,     sizeof(a_base3d_dsc_frag)},
		{a_base3d_inst_vert,    sizeof(a_base3d_inst_vert),    a_base3d_inst_frag,    sizeof(a_base3d_inst_frag)},
		{a_base3d_dsc_inst_vert, sizeof(a_base3d_dsc_inst_vert),a_base3d_dsc_inst_frag, sizeof(a_base3d_dsc_inst_frag)},
	};

	for (int i = 0; i < 5; i++) {
		dev->shaderModules[i].vert = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_VERTEX, shaders[i].v, shaders[i].vlen, "main");
		dev->shaderModules[i].frag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT, shaders[i].f, shaders[i].flen, "main");
	}
}

static void destroy_shader_modules(ovg_device_t* dev) {
	for (int i = 0; i < 5; i++) {
		if (dev->shaderModules[i].vert) SDL_ReleaseGPUShader(dev->gpuDevice, dev->shaderModules[i].vert);
		if (dev->shaderModules[i].frag) SDL_ReleaseGPUShader(dev->gpuDevice, dev->shaderModules[i].frag);
		dev->shaderModules[i] = {};
	}
}

// ========================================================================
// 管道创建辅助
// ========================================================================
struct vg_pipeline_inputs {
	SDL_GPUShader* vertShader;
	SDL_GPUShader* fragShader;
	SDL_GPUTextureFormat colorFormat;
	SDL_GPUTextureFormat depthFormat;
	SDL_GPUSampleCount   samples;
	SDL_GPUPrimitiveType topology;
	bool                 depthTestEnable;
	bool                 depthWriteEnable;
	bool                 stencilTestEnable;
	//bool                 logicOpEnable;
	//SDL_GPULogicOp       logicOp;
	blendMode_e          blendMode;
	float                lineWidth;
	uint32_t             vertexStride;
	uint32_t             numAttributes;
	SDL_GPUVertexAttribute attributes[4];
	// 模板状态
	SDL_GPUDepthStencilState ds;
	//SDL_GPUDepthStencilState stencilBack;
	SDL_GPUStencilOpState stencilFront;
	SDL_GPUStencilOpState stencilBack;
};

static SDL_GPUGraphicsPipeline* create_graphics_pipeline(
	ovg_device_t* dev,
	const vg_pipeline_inputs* inputs)
{
	blend_params bp = {};
	set_blend_params(bp, inputs->blendMode);

	SDL_GPUColorTargetDescription colorTarget = {};
	colorTarget.format = inputs->colorFormat;
	colorTarget.blend_state.enable_blend = bp.blendEnable;
	colorTarget.blend_state.src_color_blendfactor = bp.srcColor;
	colorTarget.blend_state.dst_color_blendfactor = bp.dstColor;
	colorTarget.blend_state.color_blend_op = bp.colorOp;
	colorTarget.blend_state.src_alpha_blendfactor = bp.srcAlpha;
	colorTarget.blend_state.dst_alpha_blendfactor = bp.dstAlpha;
	colorTarget.blend_state.alpha_blend_op = bp.alphaOp;
	colorTarget.blend_state.enable_color_write_mask = true;
	colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
		SDL_GPU_COLORCOMPONENT_G |
		SDL_GPU_COLORCOMPONENT_B |
		SDL_GPU_COLORCOMPONENT_A;

	SDL_GPUDepthStencilState dsState = {};
	dsState.enable_depth_test = inputs->depthTestEnable;
	dsState.enable_depth_write = inputs->depthWriteEnable;
	dsState.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	dsState.enable_stencil_test = inputs->stencilTestEnable;
	dsState.front_stencil_state = inputs->stencilFront;
	dsState.back_stencil_state = inputs->stencilBack;

	SDL_GPURasterizerState rasterState = {};
	rasterState.fill_mode = SDL_GPU_FILLMODE_FILL;
	rasterState.cull_mode = SDL_GPU_CULLMODE_NONE;
	rasterState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	rasterState.enable_depth_bias = false;
	rasterState.enable_depth_clip = false;

	SDL_GPUMultisampleState msState = {};
	msState.sample_count = inputs->samples;
	msState.sample_mask = 0;// 0xFFFFFFFF;

	SDL_GPUVertexBufferDescription vertexBufferDesc = {};
	vertexBufferDesc.slot = 0;
	vertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	vertexBufferDesc.pitch = inputs->vertexStride;

	SDL_GPUVertexInputState vertexInput = {};
	vertexInput.num_vertex_buffers = 1;
	vertexInput.vertex_buffer_descriptions = &vertexBufferDesc;
	vertexInput.num_vertex_attributes = inputs->numAttributes;
	vertexInput.vertex_attributes = (SDL_GPUVertexAttribute*)inputs->attributes;

	SDL_GPUGraphicsPipelineCreateInfo pci = {};
	pci.vertex_shader = inputs->vertShader;
	pci.fragment_shader = inputs->fragShader;
	pci.vertex_input_state = vertexInput;
	pci.primitive_type = inputs->topology;
	pci.rasterizer_state = rasterState;
	pci.multisample_state = msState;
	pci.depth_stencil_state = dsState;
	//pci.num_color_targets = 1;
	//pci.color_target_descriptions = &colorTarget;
	//pci.enable_primitive_restart = false;
	pci.target_info.has_depth_stencil_target = (inputs->depthFormat != SDL_GPU_TEXTUREFORMAT_INVALID);
	pci.target_info.depth_stencil_format = inputs->depthFormat;
	pci.target_info.num_color_targets = 1;
	pci.target_info.color_target_descriptions = &colorTarget;

	SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(dev->gpuDevice, &pci);
	if (!pipeline) {
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create graphics pipeline: %s", SDL_GetError());
	}
	return pipeline;
}

// ========================================================================
// ★ 核心：5 条 VG 管道初始化（ref 动态切方案）
// ========================================================================
//
// 位平面分配：
//   bit0 (STENCIL_FILL_BIT = 0x1) → 奇偶填充，由 pipePolyFill 的 INVERT 翻转
//   bit1 (STENCIL_CLIP_BIT = 0x2) → 裁剪掩码，由 pipeClipping 的 REPLACE 写入
//
// 动态 ref 切换：
//   - 填充阶段：ref 无关（INVERT 不依赖 ref）
//   - 裁剪阶段：ref = STENCIL_CLIP_BIT (0x2)，写入掩码
//   - 绘制阶段：ref = STENCIL_CLIP_BIT (0x2)，通过测试
//
// ========================================================================
static void init_vg_pipelines(ovg_ctx_t* ctx) {
	ovg_device_t* dev = ctx->device;

	// 编译 VG 着色器
	SDL_GPUShader* vgVert = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_VERTEX, vg_vert, sizeof(vg_vert), "main");
	SDL_GPUShader* vgFrag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT, vg_frag, sizeof(vg_frag), "main");

	// 公共顶点属性: pos(2) + uv(2) + color(1) = 20 bytes
	SDL_GPUVertexAttribute vgAttrs[3] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,        0},   // pos
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,        8},   // uv
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16},   // color (RGBA8 UNORM)
	};

	// ═══════════════════════════════════════════════════════════════
	// ① pipePolyFill — 奇偶填充
	//   职责：翻转 bit0（INVERT），不关心 ref
	//   使用：TRIANGLE_FAN，compareOp=ALWAYS（保证所有像素都通过）
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = vgFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = true;
		inputs.blendMode = blendMode_e::none;  // 只写 stencil，不写颜色
		inputs.vertexStride = OVG_VERTEX_SIZE;
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// ★ 核心 stencil 状态
		//   compareOp = ALWAYS → 所有像素都通过模板测试
		//   passOp    = INVERT  → 通过后翻转 bit0
		//   writeMask = FILL_BIT → 只动 bit0，不动 bit1（裁剪位）
		//   ref 无关（因为 ALWAYS 不比较 ref）
		inputs.ds.compare_mask = STENCIL_FILL_BIT;     // 0x1
		inputs.ds.write_mask = STENCIL_FILL_BIT;     // 0x1
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_INVERT;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipePolyFill = create_graphics_pipeline(dev, &inputs);
	}

	// ═══════════════════════════════════════════════════════════════
	// ② pipeClipping — 裁剪掩码写入
	//   职责：把通过的像素 bit1 写成 ref（= STENCIL_CLIP_BIT = 0x2）
	//   使用：TRIANGLE_LIST，compareOp=ALWAYS
	//   绘制前需：SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT)
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = vgFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = true;
		inputs.blendMode = blendMode_e::none;  // 只写 stencil
		inputs.vertexStride = OVG_VERTEX_SIZE;
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// ★ 核心 stencil 状态
		//   compareOp = ALWAYS → 所有像素通过
		//   passOp    = REPLACE → 通过后 stencil = ref
		//   writeMask = CLIP_BIT → 只动 bit1
		//   ref = STENCIL_CLIP_BIT (0x2) → 动态设置
		//
		// 注意：compare_mask 设成 FILL_BIT(0x1) 意味着只比较 bit0
		//       但 compareOp=ALWAYS 使比较结果恒为真
		//       所以 compare_mask 实际上不影响通过/不通过
		//       它只影响"哪些位参与比较"——这里设成 ALL_BIT 更清晰
		inputs.ds.compare_mask = STENCIL_ALL_BIT;      // 0x3（参与比较全部位）
		inputs.ds.write_mask = STENCIL_CLIP_BIT;     // 0x2（只写 bit1）
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_REPLACE;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		//inputs.ds.reference = 0;  // 初始值，实际由 SetStencilReference 动态覆盖
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeClipping = create_graphics_pipeline(dev, &inputs);
	}

	// ═══════════════════════════════════════════════════════════════
	// ③ pipeOVER — 正常绘制（受裁剪影响）
	//   职责：只画 stencil & CLIP_BIT == CLIP_BIT 的像素
	//   使用：TRIANGLE_LIST，compareOp=EQUAL
	//   绘制前需：SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT)
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = vgFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = true;
		inputs.blendMode = blendMode_e::normal;  // SRC_ALPHA / ONE_MINUS_SRC_ALPHA
		inputs.vertexStride = OVG_VERTEX_SIZE;
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// ★ 核心 stencil 状态
		//   compareOp = EQUAL → (stencil & compare_mask) == (ref & compare_mask)
		//   compare_mask = CLIP_BIT → 只比 bit1
		//   writeMask = 0 → 不改 stencil
		//   ref = STENCIL_CLIP_BIT (0x2) → 动态设置
		inputs.ds.compare_mask = STENCIL_CLIP_BIT;      // 0x2
		inputs.ds.write_mask = 0x0;                   // 不改 stencil
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_EQUAL;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		//inputs.stencilFront.reference = 0;  // 动态覆盖
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeOVER = create_graphics_pipeline(dev, &inputs);
	}

	// ═══════════════════════════════════════════════════════════════
	// ④ pipeSUB — 差值绘制（受裁剪影响，blend 不同）
	//   stencil 状态与 pipeOVER 完全相同
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = vgFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = true;
		inputs.blendMode = blendMode_e::none;  // 用逻辑操作 SUBTRACT 模拟
		inputs.vertexStride = OVG_VERTEX_SIZE;
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// stencil 与 OVER 完全一致
		inputs.ds.compare_mask = STENCIL_CLIP_BIT;
		inputs.ds.write_mask = 0x0;
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_EQUAL;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		//inputs.stencilFront.reference = 0;
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeSUB = create_graphics_pipeline(dev, &inputs);
	}

	// ═══════════════════════════════════════════════════════════════
	// ⑤ pipeCLEAR — 清除操作
	//   职责：无视 stencil，直接写颜色（logic op CLEAR）
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = vgFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = false;  // ★ 关 stencil
		inputs.blendMode = blendMode_e::none;
		inputs.vertexStride = OVG_VERTEX_SIZE;
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// stencil 状态无关（已禁用）
		//inputs.stencilFront.compare_mask = 0xFF;
		//inputs.stencilFront.write_mask = 0x0;
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeCLEAR = create_graphics_pipeline(dev, &inputs);
	}

	// 释放 VG 着色器（管道已持有引用）
	SDL_ReleaseGPUShader(dev->gpuDevice, vgVert);
	SDL_ReleaseGPUShader(dev->gpuDevice, vgFrag);
}

// ========================================================================
// 几何管道创建
// ========================================================================
static pipelinestate_p_internal create_geom_pipeline(
	ovg_device_t* dev,
	const gem_info_t* info)
{
	pipelinestate_p_internal result = {};

	int shaderIdx = info->shader;
	if (shaderIdx < 0 || shaderIdx >= 5) shaderIdx = 0;

	SDL_GPUShader* vert = dev->shaderModules[shaderIdx].vert;
	SDL_GPUShader* frag = dev->shaderModules[shaderIdx].frag;

	SDL_GPUVertexAttribute geomAttrs[4] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,         0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,        12},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,  20},
		{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,  24},
	};

	SDL_GPUColorTargetDescription colorTarget = {};
	colorTarget.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	colorTarget.blend_state.enable_color_write_mask = true;
	colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
		SDL_GPU_COLORCOMPONENT_G |
		SDL_GPU_COLORCOMPONENT_B |
		SDL_GPU_COLORCOMPONENT_A;

	blend_params bp = {};
	set_blend_params(bp, (blendMode_e)info->blendMode);
	colorTarget.blend_state.enable_blend = bp.blendEnable;
	colorTarget.blend_state.src_color_blendfactor = bp.srcColor;
	colorTarget.blend_state.dst_color_blendfactor = bp.dstColor;
	colorTarget.blend_state.color_blend_op = bp.colorOp;
	colorTarget.blend_state.src_alpha_blendfactor = bp.srcAlpha;
	colorTarget.blend_state.dst_alpha_blendfactor = bp.dstAlpha;
	colorTarget.blend_state.alpha_blend_op = bp.alphaOp;

	SDL_GPUDepthStencilState dsState = {};
	dsState.enable_depth_test = (info->flags & (uint8_t)depth_stencil_State::d_depthtest_enable) != 0;
	dsState.enable_depth_write = (info->flags & (uint8_t)depth_stencil_State::d_depthwrite_enable) != 0;
	dsState.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	dsState.enable_stencil_test = (info->flags & (uint8_t)depth_stencil_State::d_stenciltest_enable) != 0;
	dsState.compare_mask = 0xFF;
	dsState.write_mask = 0xFF;
	dsState.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	dsState.front_stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
	dsState.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
	dsState.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
	dsState.back_stencil_state = dsState.front_stencil_state;

	SDL_GPURasterizerState rasterState = {};
	rasterState.fill_mode = (SDL_GPUFillMode)info->polygon;
	rasterState.cull_mode = SDL_GPU_CULLMODE_NONE;
	rasterState.front_face = info->frontFace ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
	rasterState.enable_depth_clip = false;

	SDL_GPUMultisampleState msState = {};
	msState.sample_count = SDL_GPU_SAMPLECOUNT_1;
	msState.sample_mask = 0;// 0xFFFFFFFF;

	SDL_GPUVertexBufferDescription vbDesc = {};
	vbDesc.slot = 0;
	vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	vbDesc.pitch = GEOM_VERTEX_SIZE;

	SDL_GPUVertexInputState vertexInput = {};
	vertexInput.num_vertex_buffers = 1;
	vertexInput.vertex_buffer_descriptions = &vbDesc;
	vertexInput.num_vertex_attributes = 3 + shaderIdx;
	vertexInput.vertex_attributes = geomAttrs;

	SDL_GPUGraphicsPipelineCreateInfo pci = {};
	pci.vertex_shader = vert;
	pci.fragment_shader = frag;
	pci.vertex_input_state = vertexInput;
	pci.primitive_type = (SDL_GPUPrimitiveType)info->topology;
	pci.rasterizer_state = rasterState;
	pci.multisample_state = msState;
	pci.depth_stencil_state = dsState;
	//pci.num_color_targets = 1;
	//pci.color_target_descriptions = &colorTarget;
	//pci.enable_primitive_restart = false;
	pci.target_info.has_depth_stencil_target = true;
	pci.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
	pci.target_info.num_color_targets = 1;
	pci.target_info.color_target_descriptions = &colorTarget;

	result.pipeline = SDL_CreateGPUGraphicsPipeline(dev->gpuDevice, &pci);
	result.state = *info;

	SDL_GPUSamplerCreateInfo sci = {};
	sci.min_filter = SDL_GPU_FILTER_LINEAR;
	sci.mag_filter = SDL_GPU_FILTER_LINEAR;
	sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sci.max_anisotropy = 1.0f;
	result.defaultSampler = SDL_CreateGPUSampler(dev->gpuDevice, &sci);

	return result;
}

// ========================================================================
// 管道状态管理
// ========================================================================
static pipelinestate_p_internal* get_geom_pipeline(ovg_ctx_t* ctx, const gem_info_t* info) {
	uint64_t key = *(const uint64_t*)info;

	if (ctx->currentPipeline && ctx->currentState == key) {
		return ctx->currentPipeline;
	}

	ctx->currentState = key;
	auto it = ctx->geomPipelines.find(key);
	if (it == ctx->geomPipelines.end()) {
		pipelinestate_p_internal p = create_geom_pipeline(ctx->device, info);
		ctx->geomPipelines[key] = p;
		ctx->currentPipeline = &ctx->geomPipelines[key];
	}
	else {
		ctx->currentPipeline = &it->second;
	}
	return ctx->currentPipeline;
}

// ========================================================================
// ★ 核心：VG 管道绑定 + 自动设置 stencil ref
// ========================================================================
//
// 每次 bind pipeline 后立刻设置正确的 stencil reference：
//   pipePolyFill → ref 无关（ALWAYS），设 0 即可
//   pipeClipping → ref = STENCIL_CLIP_BIT (0x2)，REPLACE 写入
//   pipeOVER     → ref = STENCIL_CLIP_BIT (0x2)，EQUAL 比较
//   pipeSUB      → ref = STENCIL_CLIP_BIT (0x2)，EQUAL 比较
//   pipeCLEAR    → stencil 已禁用，ref 无关
//
// ========================================================================
void ovg_bind_vg_pipeline(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass, int operatorType) {
	if (!ctx || !pass) return;

	SDL_GPUGraphicsPipeline* pipe = ctx->pipeOVER;
	int pipeIndex = VG_PIPE_OVER;

	switch (operatorType) {
	case 0:  pipe = ctx->pipeOVER;     pipeIndex = VG_PIPE_OVER;      break;  // VG_OPERATOR_OVER
	case 1:  pipe = ctx->pipeCLEAR;    pipeIndex = VG_PIPE_CLEAR;     break;  // VG_OPERATOR_CLEAR
	case 2:  pipe = ctx->pipeSUB;      pipeIndex = VG_PIPE_SUB;       break;  // VG_OPERATOR_DIFFERENCE
	default: pipe = ctx->pipeOVER;     pipeIndex = VG_PIPE_OVER;      break;
	}

	SDL_BindGPUGraphicsPipeline(pass, pipe);
	ctx->currentVgPipeIndex = pipeIndex;

	// ★ 根据管道类型自动设置正确的 stencil reference
	switch (pipeIndex) {
	case VG_PIPE_CLIPPING:
		// REPLACE 写入：ref = STENCIL_CLIP_BIT → 通过的像素 bit1 = 1
		SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);  // 0x2
		break;

	case VG_PIPE_OVER:
	case VG_PIPE_SUB:
		// EQUAL 比较：只画 bit1 == 1 的像素
		SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);  // 0x2
		break;

	case VG_PIPE_POLYFILL:
		// INVERT 翻转：ref 无关（ALWAYS），设 0 即可
		SDL_SetGPUStencilReference(pass, 0);
		break;

	case VG_PIPE_CLEAR:
		// stencil 已禁用，ref 无关
		SDL_SetGPUStencilReference(pass, 0);
		break;
	}
}

// ========================================================================
// ★ 显式设置 stencil ref（高级用法）
// ========================================================================
//
// 允许调用者在绑定管道后手动覆盖 ref 值。
// 典型场景：多级裁剪（bit2=0x4, bit3=0x8...）
//
// ⚠️ 注意：SDL3 GPU 不能动态改 compareMask，
// 所以多级裁剪的 compare_mask 必须提前在管道中设好。
// 这里只提供 ref 切换能力。
//
// ========================================================================
void ovg_set_stencil_reference(ovg_ctx_t* ctx, SDL_GPURenderPass* pass, uint8_t ref) {
	if (!ctx || !pass) return;
	SDL_SetGPUStencilReference(pass, ref);
}

// ========================================================================
// 设备创建与销毁
// ========================================================================
ovg_device_t* new_sdl3gpu_device(SDL_GPUDevice* gpuDevice) {
	if (!gpuDevice) return nullptr;

	ovg_device_t* dev = new ovg_device_t();
	dev->gpuDevice = gpuDevice;

	dev->supportedFormats = detect_supported_shader_format(gpuDevice);

	init_shader_modules(dev);

	// 创建默认空纹理
	dev->emptyTexture = create_texture(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 16, 16,
		SDL_GPU_TEXTUREUSAGE_SAMPLER |
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
	create_sampler(dev->emptyTexture,
		SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_NEAREST,
		SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
		SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);

	// 用白色填充空纹理
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
	if (cmd) {
		SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
		if (copyPass) {
			uint32_t white = 0xFFFFFFFF;
			SDL_GPUTransferBufferCreateInfo tbi = {};
			tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
			tbi.size = sizeof(uint32_t);
			SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(gpuDevice, &tbi);
			if (staging) {
				void* mapped = SDL_MapGPUTransferBuffer(gpuDevice, staging, false);
				if (mapped) {
					memcpy(mapped, &white, sizeof(white));
					SDL_UnmapGPUTransferBuffer(gpuDevice, staging);
				}
				SDL_GPUTextureTransferInfo tex_src = {};
				tex_src.transfer_buffer = staging;
				tex_src.rows_per_layer = dev->emptyTexture->height;
				tex_src.pixels_per_row = dev->emptyTexture->width;

				SDL_GPUTextureRegion tex_dst = {};
				tex_dst.texture = dev->emptyTexture->texture;
				tex_dst.x = 0;
				tex_dst.y = 0;
				tex_dst.w = dev->emptyTexture->width;
				tex_dst.h = dev->emptyTexture->height;
				tex_dst.d = 1;
				SDL_UploadToGPUTexture(copyPass, &tex_src, &tex_dst, false);
				SDL_EndGPUCopyPass(copyPass);
				SDL_SubmitGPUCommandBuffer(cmd);
				SDL_WaitForGPUIdle(gpuDevice);
				SDL_ReleaseGPUTransferBuffer(gpuDevice, staging);
			}
			else {
				SDL_EndGPUCopyPass(copyPass);
				SDL_SubmitGPUCommandBuffer(cmd);
			}
		}
		else {
			SDL_SubmitGPUCommandBuffer(cmd);
		}
	}

	return dev;
}

void free_sdl3gpu_device(ovg_device_t* dev) {
	if (!dev) return;

	if (dev->emptyTexture) {
		destroy_texture(dev->emptyTexture);
		dev->emptyTexture = nullptr;
	}
	destroy_shader_modules(dev);
	delete dev;
}

// ========================================================================
// 渲染上下文创建与销毁
// ========================================================================
ovg_ctx_t* new_ovgctx_sdl3(ovg_device_t* dev, SDL_GPUTextureFormat colorFormat, SDL_GPUTextureFormat depthFormat, SDL_GPUSampleCount samples) {
	if (!dev || !dev->gpuDevice) return nullptr;

	ovg_ctx_t* ctx = new ovg_ctx_t();
	ctx->device = dev;
	ctx->colorFormat = colorFormat;
	ctx->depthFormat = depthFormat;
	ctx->samples = samples;

	create_uniform_buffer(dev, &ctx->uboGrad, 256, 8);
	ctx->uboSize = 256 * 8;
	ctx->uboStride = 256;

	create_vertex_buffer(dev, &ctx->vboVG, VG_VBO_SIZE * OVG_VERTEX_SIZE, OVG_VERTEX_SIZE);
	create_index_buffer(dev, &ctx->iboVG, VG_IBO_SIZE * sizeof(uint32_t));
	create_vertex_buffer(dev, &ctx->vboGeom, VG_VBO_SIZE * GEOM_VERTEX_SIZE, GEOM_VERTEX_SIZE);
	create_index_buffer(dev, &ctx->iboGeom, VG_IBO_SIZE * sizeof(uint32_t));

	init_vg_pipelines(ctx);

	return ctx;
}

void free_ovgctx_sdl3(ovg_ctx_t* ctx) {
	if (!ctx) return;

	if (ctx->pipeOVER)     SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, ctx->pipeOVER);
	if (ctx->pipeSUB)      SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, ctx->pipeSUB);
	if (ctx->pipeCLEAR)    SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, ctx->pipeCLEAR);
	if (ctx->pipePolyFill) SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, ctx->pipePolyFill);
	if (ctx->pipeClipping) SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, ctx->pipeClipping);

	for (auto& [key, p] : ctx->geomPipelines) {
		if (p.pipeline)      SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, p.pipeline);
		if (p.defaultSampler) SDL_ReleaseGPUSampler(ctx->device->gpuDevice, p.defaultSampler);
	}
	ctx->geomPipelines.clear();

	destroy_buffer(&ctx->uboGrad);
	destroy_buffer(&ctx->vboVG);
	destroy_buffer(&ctx->iboVG);
	destroy_buffer(&ctx->vboGeom);
	destroy_buffer(&ctx->iboGeom);

	delete ctx;
}

// ========================================================================
// FBO 管理
// ========================================================================
vg_fbo_t new_vgfbo_sdl3(ovg_ctx_t* ctx, int width, int height) {
	vg_fbo_t fbo = {};
	fbo.width = width;
	fbo.height = height;
	if (!ctx || !ctx->device) return fbo;

	ovg_device_t* dev = ctx->device;

	fbo.colorTex = create_texture(dev, ctx->colorFormat, width, height,
		SDL_GPU_TEXTUREUSAGE_SAMPLER |
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);

	if (ctx->samples > SDL_GPU_SAMPLECOUNT_1) {
		fbo.colorTexMS = create_msaa_texture(dev, ctx->colorFormat, width, height, ctx->samples);
	}

	fbo.depthStencilTex = create_depth_stencil_texture(dev, ctx->depthFormat, width, height, ctx->samples);
	fbo.hasStencil = true;

	return fbo;
}

void free_vgfbo_sdl3(vg_fbo_t* fbo) {
	if (!fbo) return;
	if (fbo->colorTex) { destroy_texture((sdl3gpu_texture*)fbo->colorTex);     fbo->colorTex = nullptr; }
	if (fbo->colorTexMS) { destroy_texture((sdl3gpu_texture*)fbo->colorTexMS);   fbo->colorTexMS = nullptr; }
	if (fbo->depthStencilTex) { destroy_texture((sdl3gpu_texture*)fbo->depthStencilTex); fbo->depthStencilTex = nullptr; }
}

// ========================================================================
// MSAA 解析
// ========================================================================
void ovg_resolve_msaa_sdl3(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, vg_fbo_t* fbo) {
	if (!ctx || !cmdBuf || !fbo) return;
	if (!fbo->colorTexMS || !fbo->colorTex) return;
	return;
	SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
	if (!copyPass) return;

	SDL_GPUTextureRegion srcRegion = {};
	srcRegion.texture = ((sdl3gpu_texture*)fbo->colorTexMS)->texture;
	srcRegion.w = fbo->width;
	srcRegion.h = fbo->height;
	srcRegion.d = 1;

	SDL_GPUTextureRegion dstRegion = {};
	dstRegion.texture = ((sdl3gpu_texture*)fbo->colorTex)->texture;
	dstRegion.w = fbo->width;
	dstRegion.h = fbo->height;
	dstRegion.d = 1;

	//SDL_ResolveGPUTexture(copyPass, &srcRegion, &dstRegion);
	SDL_EndGPUCopyPass(copyPass);
}

// ========================================================================
// 帧生命周期
// ========================================================================
SDL_GPUCommandBuffer* ovg_begin_frame(ovg_ctx_t* ctx, vg_fbo_t* fbo, bool clearAll) {
	if (!ctx || !fbo) return nullptr;

	SDL_GPUCommandBuffer* cmdBuf = ctx->currentCmdBuf;
	if (!cmdBuf) return nullptr;

	sdl3gpu_texture* colorTex = (sdl3gpu_texture*)fbo->colorTex;
	sdl3gpu_texture* colorTexMS = (sdl3gpu_texture*)fbo->colorTexMS;
	sdl3gpu_texture* depthStencil = (sdl3gpu_texture*)fbo->depthStencilTex;

	if (!colorTex || !depthStencil) {
		SDL_SubmitGPUCommandBuffer(cmdBuf);
		return nullptr;
	}



	SDL_GPUTexture* renderColorTex = colorTexMS ? colorTexMS->texture : colorTex->texture;

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = renderColorTex;
	colorTargetInfo.load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	colorTargetInfo.store_op = (fbo && fbo->colorTexMS && fbo->colorTex) ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_STORE;
	colorTargetInfo.clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
	//colorTargetInfo.layer_count = 1;
	colorTargetInfo.resolve_mip_level = 1;
	colorTargetInfo.resolve_layer = 1;
	colorTargetInfo.resolve_texture = colorTex->texture;

	SDL_GPUDepthStencilTargetInfo depthTargetInfo = {};
	depthTargetInfo.texture = depthStencil->texture;
	depthTargetInfo.load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depthTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
	depthTargetInfo.stencil_load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depthTargetInfo.stencil_store_op = SDL_GPU_STOREOP_STORE;
	depthTargetInfo.clear_depth = 1.0f;
	depthTargetInfo.clear_stencil = 0;

	SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTargetInfo, 1, &depthTargetInfo);
	if (!renderPass) {
		SDL_SubmitGPUCommandBuffer(cmdBuf);
		return nullptr;
	}

	ctx->currentRenderPass = renderPass;
	ctx->currentVgPipeIndex = -1;

	// 设置视口
	SDL_GPUViewport viewport = {};
	viewport.x = 0.0f; viewport.y = 0.0f;
	viewport.w = (float)fbo->width; viewport.h = (float)fbo->height;
	viewport.min_depth = 0.0f; viewport.max_depth = 1.0f;
	SDL_Rect rc = { 0, 0, fbo->width, fbo->height };
	SDL_SetGPUViewport(renderPass, &viewport);
	SDL_SetGPUScissor(renderPass, &rc);

	ctx->cmdStarted = true;
	ctx->viewportW = fbo->width;
	ctx->viewportH = fbo->height;

	// 绑定 VG 顶点/索引缓冲区
	SDL_GPUBufferBinding vboBinding = {};
	vboBinding.buffer = ctx->vboVG.buffer;
	vboBinding.offset = 0;
	SDL_BindGPUVertexBuffers(renderPass, 0, &vboBinding, 1);

	SDL_GPUBufferBinding iboBinding = {};
	iboBinding.buffer = ctx->iboVG.buffer;
	iboBinding.offset = 0;
	SDL_BindGPUIndexBuffer(renderPass, &iboBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

	return cmdBuf;
}

SDL_GPURenderPass* ovg_get_current_render_pass(ovg_ctx_t* ctx) {
	if (!ctx) return nullptr;
	return ctx->currentRenderPass;
}

void ovg_end_frame(ovg_ctx_t* ctx, vg_fbo_t* fbo) {
	if (!ctx) return;

	SDL_GPUCommandBuffer* actualCmd = ctx->currentCmdBuf;

	if (ctx->currentRenderPass) {
		SDL_EndGPURenderPass(ctx->currentRenderPass);
		ctx->currentRenderPass = nullptr;
	}

	if (fbo && fbo->colorTexMS && fbo->colorTex) {
		ovg_resolve_msaa_sdl3(ctx, actualCmd, fbo);
	}

	ctx->cmdStarted = false;
	ctx->currentCmdBuf = nullptr;
	ctx->currentVgPipeIndex = -1;
}

// ========================================================================
// 纹理绑定
// ========================================================================
void ovg_bind_texture(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass, sdl3gpu_texture* tex) {
	if (!ctx || !pass) return;

	sdl3gpu_texture* actualTex = tex ? tex : ctx->device->emptyTexture;
	ctx->currentTexture = actualTex;

	SDL_GPUTextureSamplerBinding binding = {};
	binding.texture = actualTex->texture;
	binding.sampler = actualTex->sampler ? actualTex->sampler : ctx->device->emptyTexture->sampler;
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
}

// 绑定 UBO（渐变数据）
void ovg_bind_ubo(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass, uint32_t offset) {
	if (!ctx || !pass) return;

	SDL_GPUBufferBinding uboBinding = {};
	uboBinding.buffer = ctx->uboGrad.buffer;
	uboBinding.offset = offset;
	SDL_BindGPUVertexStorageBuffers(pass, 0, &ctx->uboGrad.buffer, 1);
}

// ========================================================================
// 几何管道绑定
// ========================================================================
void ovg_bind_geom_pipeline(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass, const gem_info_t* info) {
	if (!ctx || !pass || !info) return;

	pipelinestate_p_internal* p = get_geom_pipeline(ctx, info);
	if (p && p->pipeline) {
		SDL_BindGPUGraphicsPipeline(pass, p->pipeline);
	}
}

// ========================================================================
// 绘制命令
// ========================================================================
void ovg_draw_indexed(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass,
	uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset) {
	if (!ctx || !pass) return;
	SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, indexOffset, vertexOffset, 0);
}

void ovg_draw_arrays(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass,
	uint32_t vertexCount, uint32_t vertexOffset) {
	if (!ctx || !pass) return;
	SDL_DrawGPUPrimitives(pass, vertexCount, 1, vertexOffset, 0);
}

// ========================================================================
// 数据上传
// ========================================================================
void ovg_upload_vbo(ovg_ctx_t* ctx, const void* data, uint32_t offset, uint32_t size) {
	if (!ctx || !data) return;
	upload_buffer_data(&ctx->vboVG, data, offset, size);
}

void ovg_upload_ibo(ovg_ctx_t* ctx, const void* data, uint32_t offset, uint32_t size) {
	if (!ctx || !data) return;
	upload_buffer_data(&ctx->iboVG, data, offset, size);
}

void ovg_upload_ubo(ovg_ctx_t* ctx, const void* data, uint32_t offset, uint32_t size) {
	if (!ctx || !data) return;
	upload_buffer_data(&ctx->uboGrad, data, offset, size);
}

void ovg_upload_geom_vbo(ovg_ctx_t* ctx, const void* data, uint32_t offset, uint32_t size) {
	if (!ctx || !data) return;
	upload_buffer_data(&ctx->vboGeom, data, offset, size);
}

void ovg_upload_geom_ibo(ovg_ctx_t* ctx, const void* data, uint32_t offset, uint32_t size) {
	if (!ctx || !data) return;
	upload_buffer_data(&ctx->iboGeom, data, offset, size);
}

// ========================================================================
// 几何缓冲区绑定
// ========================================================================
void ovg_bind_geom_buffers(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass,
	uint32_t vboOffset, uint32_t iboOffset) {
	if (!ctx || !pass) return;

	SDL_GPUBufferBinding vboBinding = {};
	vboBinding.buffer = ctx->vboGeom.buffer;
	vboBinding.offset = vboOffset;
	SDL_BindGPUVertexBuffers(pass, 0, &vboBinding, 1);

	SDL_GPUBufferBinding iboBinding = {};
	iboBinding.buffer = ctx->iboGeom.buffer;
	iboBinding.offset = iboOffset;
	SDL_BindGPUIndexBuffer(pass, &iboBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

// ========================================================================
// 视口与裁剪
// ========================================================================
void ovg_set_viewport(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass,
	float x, float y, float w, float h) {
	if (!ctx || !pass) return;
	SDL_GPUViewport vp = {};
	vp.x = x; vp.y = y; vp.w = w; vp.h = h;
	vp.min_depth = 0.0f; vp.max_depth = 1.0f;
	SDL_SetGPUViewport(pass, &vp);
}

void ovg_set_scissor(SDL_GPURenderPass* pass, int x, int y, int w, int h) {
	if (!pass) return;
	SDL_Rect rc = { x, y, w, h };
	SDL_SetGPUScissor(pass, &rc);
}

void ovg_reset_scissor(ovg_ctx_t* ctx, SDL_GPURenderPass* pass) {
	if (!ctx || !pass) return;
	SDL_Rect rc = { 0, 0, ctx->viewportW, ctx->viewportH };
	SDL_SetGPUScissor(pass, &rc);
}

// ========================================================================
// 命令缓冲区管理
// ========================================================================
SDL_GPUCommandBuffer* ovg_get_command_buffer(ovg_ctx_t* ctx) {
	if (!ctx || !ctx->device) return nullptr;
	return SDL_AcquireGPUCommandBuffer(ctx->device->gpuDevice);
}

void ovg_submit_command_buffer(SDL_GPUCommandBuffer* cmdBuf) {
	if (!cmdBuf) return;
	SDL_SubmitGPUCommandBuffer(cmdBuf);
}

void ovg_wait_idle(ovg_ctx_t* ctx) {
	if (!ctx || !ctx->device) return;
	SDL_WaitForGPUIdle(ctx->device->gpuDevice);
}

// ========================================================================
// 一次性绘制入口
// ========================================================================
void ovg_draw_sdl3(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, vg_fbo_t* fbo, bool clearAll) {
	if (!ctx || !cmdBuf || !fbo) return;

	sdl3gpu_texture* colorTex = (sdl3gpu_texture*)fbo->colorTex;
	sdl3gpu_texture* colorTexMS = (sdl3gpu_texture*)fbo->colorTexMS;
	sdl3gpu_texture* depthStencil = (sdl3gpu_texture*)fbo->depthStencilTex;
	if (!colorTex || !depthStencil) return;

	SDL_GPUTexture* renderColorTex = colorTexMS ? colorTexMS->texture : colorTex->texture;

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = renderColorTex;
	colorTargetInfo.load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	colorTargetInfo.store_op = (colorTexMS && colorTex) ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_STORE;
	colorTargetInfo.clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };


	SDL_GPUDepthStencilTargetInfo depthTargetInfo = {};
	depthTargetInfo.texture = depthStencil->texture;
	depthTargetInfo.load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depthTargetInfo.store_op = SDL_GPU_STOREOP_STORE;
	depthTargetInfo.stencil_load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depthTargetInfo.stencil_store_op = SDL_GPU_STOREOP_STORE;
	depthTargetInfo.clear_depth = 1.0f;
	depthTargetInfo.clear_stencil = 0;

	SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdBuf, &colorTargetInfo, 1, &depthTargetInfo);
	if (!renderPass) return;

	SDL_GPUViewport viewport = {};
	viewport.x = 0.0f; viewport.y = 0.0f;
	viewport.w = (float)fbo->width; viewport.h = (float)fbo->height;
	viewport.min_depth = 0.0f; viewport.max_depth = 1.0f;
	SDL_SetGPUViewport(renderPass, &viewport);
	ovg_set_scissor(renderPass, 0, 0, fbo->width, fbo->height);

	ctx->cmdStarted = true;
	ctx->viewportW = fbo->width;
	ctx->viewportH = fbo->height;
	ctx->currentRenderPass = renderPass;
	ctx->currentCmdBuf = cmdBuf;

	// 绑定 VG 缓冲区
	SDL_GPUBufferBinding vboBinding = {};
	vboBinding.buffer = ctx->vboVG.buffer;
	vboBinding.offset = 0;
	SDL_BindGPUVertexBuffers(renderPass, 0, &vboBinding, 1);

	SDL_GPUBufferBinding iboBinding = {};
	iboBinding.buffer = ctx->iboVG.buffer;
	iboBinding.offset = 0;
	SDL_BindGPUIndexBuffer(renderPass, &iboBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

	// 注意：调用者可通过 ctx->currentRenderPass 继续录制
	// 这里不自动结束，让调用者控制生命周期

	SDL_EndGPURenderPass(renderPass);
	ctx->currentRenderPass = nullptr;

	if (colorTexMS && colorTex) {
		//ovg_resolve_msaa_sdl3(ctx, cmdBuf, fbo);
	}
}
void ovg_draw_data(ovg_ctx_t* ctx, vg_fbo_t* fbo, ovg_draw_data_t* data)
{
	if (!ctx || !fbo || !fbo->colorTex || !data || !data->count)return;
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device->gpuDevice);
	if (!cmd)return;
	ctx->currentCmdBuf = cmd;
	ovg_upload_vbo(ctx, data->vg_vertex, 0, data->v_count * sizeof(ovgVertex));
	ovg_upload_ibo(ctx, data->vg_indices, 0, data->i_count * sizeof(uint32_t));

	ovg_begin_frame(ctx, fbo, true);

	ovg_end_frame(ctx, fbo);
}
