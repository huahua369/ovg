/*
 * ovg_renderer_sdl3.cpp
 * SDL3 GPU 矢量渲染后端


2026/10/16	增加compute特效shader
2026/10/13	支持渲染到多fbo,使用不同数据
2026/9/9	支持矢量图/位图/文本混合渲染
2026/8/31	后端支持普通三角形渲染


shader规则：
每set的binding排列
 Category 1: read resources
	  samplerCount,
	  storageTextureCount,
	  storageBufferCount,
 Category 2: write resources
	  writeStorageTextureCount,
	  writeStorageBufferCount,
 Category 3: uniform buffers
	  uniformBufferCount

顶点	set=0[num_samplers, num_storageTextures, num_storageBuffers]
	set=1[num_uniformBuffers]
像素	set=2[num_samplers, num_storageTextures, num_storageBuffers]
	set=3[numUniformBuffers]
计算	set=0[num_samplers, num_readonly_storage_textures, num_readonly_storage_buffers]
	set=1[num_readwrite_storage_textures, num_readwrite_storage_buffers]
	set=2[num_uniform_buffers]


 */


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cassert>
#include <cmath>
// 仅 vg_sdl3_init 的 is_vulkan 分支用到 Vulkan 特性结构；头不可用时跳过该分支
#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define OVG_HAS_VULKAN_HEADERS 1
#else
#define OVG_HAS_VULKAN_HEADERS 0
#endif
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

#include "shaders/spv_c/blur.spv.h" 
#include "shaders/spv_c/effect_present.vert.h"
#include "shaders/spv_c/effect_present.frag.h"
#include "shaders/spv_c/effect_mask.frag.h"
#include "shaders/spv_c/noise.spv.h" 
#include "shaders/spv_c/displace.spv.h" 
#include "shaders/spv_c/blit.vert.h"
#include "shaders/spv_c/blit.frag.h"


#define FULLSCREEN_BIT         0x10000000
#define SRCTYPE_MASK           0x000000FF
// ========================================================================
// 内部数据结构
// ========================================================================


typedef struct sdl3gpu_texture sdl3gpu_texture;

struct fx_texture_set {
	sdl3gpu_texture* mask = nullptr;
	sdl3gpu_texture* tmp = nullptr;
	sdl3gpu_texture* out = nullptr;
	int width = 0;
	int height = 0;
	size_t bytes = 0;			// 本套纹理的显存估算，池按字节限额
	bool need_blur = false;		// false 时 tmp/out 为空，仅分配 mask
	uint64_t last_use = 0;		// 最近使用的帧号，池淘汰用
};

// 特效纹理池：按 (w,h,need_blur) 分桶复用，超预算时按 LRU 销毁。
// 这里只约束空闲缓存；本帧正在使用的纹理由 keep_bytes/keep_sets 保护。
static constexpr size_t FX_POOL_MAX_SETS = 64;
static constexpr size_t FX_POOL_MAX_BYTES = 64ull << 20;
// 已提交但 GPU 尚未完成的集合上限：GPU 落后时 pending 会逐帧增长，
// 超过该预算就等待 fence 完成并回收，保证显存占用有界。
static constexpr size_t FX_PENDING_MAX_BYTES = 96ull << 20;

struct fx_result_t {
	sdl3gpu_texture* tex = nullptr;		// blurred coverage
	sdl3gpu_texture* mask = nullptr;	// original coverage
	glm::vec4 rect = {};	// x,y,w,h in device pixels
	glm::vec4 color = {};	// straight RGBA
	glm::vec2 offset = {};
	float spread = 0.0f;
	float opacity = 1.0f;
	int mode = 0;			// 0 outer, 1 inner, 2 shadow
};

struct render_item_t {
	ovg_draw_data_t data = {};
	std::vector<fx_result_t> fx;
};

struct sdl3gpu_texture {
	SDL_GPUTexture* texture = nullptr;
	SDL_GPUSampler* sampler = nullptr;
	SDL_GPUDevice* device = nullptr;
	SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
	int                  width = 0;
	int                  height = 0;
	uint32_t             references = 0;
	void* user_ptr = 0;
	uint32_t id = 0;
	bool                 hasStencil = false;
	// 原生纹理由调用方持有，包装器只借用 SDL_GPUTexture。
	bool                 owns_texture = true;
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
	SDL_GPUComputePipeline* comp_pipe[10] = {};
	sdl3gpu_texture* emptyTexture = nullptr;
	SDL_GPUShaderFormat supportedFormats = SDL_GPU_SHADERFORMAT_INVALID;

};
class gpu_buffer_cx
{
public:
	SDL_GPUBuffer* buf = 0;
	SDL_GPUDevice* dev = 0;
	SDL_GPUBufferCreateInfo info = {};	// 保存了分配大小
	size_t size = 0;					// 正在用的大小
public:
	gpu_buffer_cx();
	~gpu_buffer_cx();
	void init(SDL_GPUDevice* d, const SDL_GPUBufferCreateInfo* createinfo);
	void init(SDL_GPUDevice* d, uint32_t usage, size_t capacity);
	void init_graphics(SDL_GPUDevice* d, size_t capacity, bool vbo, bool ibo, bool indirect);
	void init_storage(SDL_GPUDevice* d, size_t capacity, bool comp_write, bool indirect);
	void resize(size_t newsize);

private:

};

class OvgGpuBuffers {
public:
	SDL_GPUDevice* _device = nullptr;

	gpu_buffer_cx _vbo = {};
	gpu_buffer_cx _ibo = {};
	gpu_buffer_cx _ssbo = {};

	SDL_GPUTransferBuffer* _staging = nullptr;
	size_t _stagingSize = 0;
	size_t vbo_ps = 0;
	size_t ibo_ps = 0;
	size_t ssbo_ps = 0;
	char* mapdt = 0;
	size_t _offset = 0;
public:
	OvgGpuBuffers() = default;
	~OvgGpuBuffers();

	// 初始化（只调用一次）
	void init(SDL_GPUDevice* device);
	// 总顶点大小、索引大小、ssbo大小
	void begin(size_t vcs, size_t ics, size_t ssbocs);
	// 分片添加数据，返回offset,
	uint32_t add_vbo(const void* data, uint32_t size);
	uint32_t add_ibo(const void* data, uint32_t size);
	uint32_t add_ssbo(const void* data, uint32_t size);

	void end(SDL_GPUCommandBuffer* cmd);


	// 绑定接口（RenderPass 内）
	void bindVBO(SDL_GPURenderPass* pass, uint32_t offset = 0);
	void bindIBO(SDL_GPURenderPass* pass, uint32_t offset = 0);
	// 偏移需要自行在ubo指定
	void bindSSBO(SDL_GPURenderPass* pass, bool is_vertex);
	void bind_v_ssbo(SDL_GPURenderPass* pass, SDL_GPUBuffer** storage_buffers, uint32_t num_bindings);
	void bind_f_ssbo(SDL_GPURenderPass* pass, SDL_GPUBuffer** storage_buffers, uint32_t num_bindings);

	// getter（用于创建 pipeline / bind）
	SDL_GPUBuffer* vbo() const { return _vbo.buf; }
	SDL_GPUBuffer* ibo() const { return _ibo.buf; }
private:
};

struct upload_desc_t {
	void* pixels;     /* CPU 像素数据，调用方持有生命周期 */
	uint32_t width;
	uint32_t height;
	uint32_t stride;     /* 每行字节数（0 = 按 bpp * width 自动算） */
	vg_format_t format;     /* 像素格式 */
	sdl3gpu_texture* dst_texture;/* 目标 GPU 纹理 */
	uint32_t x, y;       /* 目标区域起点（纹理空间） */
	uint32_t w, h;       /* 目标区域尺寸（0 = 整张） */
	vg_image_t* img;
	bool is_copy_rect;		// 是否从区域复制
};

// 渲染上下文
// 渐变 fragment uniform 缓存键：命中则该命令跳过 708B 拷贝/3x3 求逆/stop 排序/推送。
// 键含帧号——录制器可在帧间修改 stop 内容（指针不变），同帧内录制已结束、内容冻结。
struct grad_cache_key_t {
	const void* src = nullptr;      // pattern->data；nullptr 表示纯色零渐变
	uint32_t    frame = 0;
	int         type = 0;
	bool        hasMatrix = false;
	glm::mat3x2 patmat = glm::mat3x2(1.0f);
	glm::mat3x2 vmat = glm::mat3x2(1.0f);
	bool operator==(const grad_cache_key_t& o) const {
		return src == o.src && frame == o.frame && type == o.type && hasMatrix == o.hasMatrix
			&& patmat == o.patmat && vmat == o.vmat;
	}
};

struct ovg_ctx_t {
	ovg_device_t* device = nullptr;

	SDL_GPUTextureFormat colorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
	SDL_GPUSampleCount   samples = SDL_GPU_SAMPLECOUNT_1;

	// ─── 5 条 VG 专用管道 ───────────────────────
	SDL_GPUGraphicsPipeline* pipeOVER = nullptr;  // 正常绘制 (blend = OVER)
	SDL_GPUGraphicsPipeline* pipeSUB = nullptr;  // 差值绘制 (blend = SUB)
	SDL_GPUGraphicsPipeline* pipeCLEAR = nullptr;  // 清除操作 (logic op CLEAR) 
	SDL_GPUGraphicsPipeline* pipeClipping = nullptr;  // 裁剪掩码写入 (stencil REPLACE)

	SDL_GPUGraphicsPipeline* pipeStencilClear = nullptr; // 清 0
	SDL_GPUGraphicsPipeline* pipeEffectMask = nullptr;   // 特效掩码写入离屏纹理
	SDL_GPUGraphicsPipeline* pipeEffectComposite = nullptr; // 特效纹理叠加到主画布
	std::set<SDL_GPUGraphicsPipeline*> freepipe;
	// ─── 几何管线缓存 ─────────────────────────────
	std::map<uint64_t, pipelinestate_p_internal> geomPipelines;
	pipelinestate_p_internal* currentPipeline = nullptr;
	uint64_t                                     currentState = ~0ull;

	// ─── 缓冲区 ────────────────────────────────────
	OvgGpuBuffers* gpubuf = 0;
	// 纹理管理
	vg_unordered_map<uint32_t, sdl3gpu_texture*> textures;
	// 释放列队
	std::queue<sdl3gpu_texture*> texq;
	uint32_t next_image_id = 0;
	sdl3gpu_texture* currentTexture = nullptr;
	uint32_t         gradientOffset = 0;
	// 缓冲用
	std::vector<upload_desc_t> _uploads;
	// ─── 特效资源 ────────────────────────────────
	SDL_GPUSampler* fxSampler = nullptr;
	bool fxSupported = false;
	SDL_GPUTextureFormat fxMaskFormat = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
	SDL_GPUTextureFormat fxBlurFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
	size_t fxPoolBudget = FX_POOL_MAX_BYTES;
	size_t fxPendingBudget = FX_PENDING_MAX_BYTES;
	size_t fxFrameBytes = 0;		// 本帧使用的特效纹理字节数
	size_t fxPrevFrameBytes = 0;	// 上一帧使用的字节数，用于动态 pending 上限
	std::map<uint64_t, std::vector<fx_texture_set>> fxPool;
	std::vector<fx_texture_set> fxPending;
	std::vector<fx_texture_set> fxUsed;
	uint64_t fxFrame = 0;		// 帧号，用于特效纹理池 LRU 淘汰
	size_t fxStatEffects = 0;	// 本帧生成纹理的 effect 数
	size_t fxStatRoiPixels = 0;	// 本帧 ROI 像素总数
	struct per_item_t {
		uint32_t src_offset;       /* CPU 源像素起始字节（原始纹理坐标系） */
		uint32_t gpu_offset;       /* transfer buffer 中的字节偏移 */
		uint32_t pixels_per_row;   /* 传给 SDL：源 buffer 每行像素数（0=紧密） */
		uint32_t copy_h;           /* 拷贝行数（= 脏矩形高） */
		uint32_t dirty_w;          /* 脏矩形宽 */
		uint32_t dirty_h;          /* 脏矩形高 */
		uint32_t bpp;              /* 每像素字节 */
		uint32_t stride;           /* 原始纹理每行字节（含 padding） */
		uint32_t row_pitch;        /* memcpy 每行字节数（= dirty_w * bpp） */
		uint32_t gpu_row_bytes;    /* buffer 里每行实际字节（紧密 or 对齐后） */
	};
	std::vector<per_item_t> _items;
	std::vector<render_item_t> _fbo_data;
	// ─── 视口 ──────────────────────────────────────
	int viewportW = 0;
	int viewportH = 0;

	// ─── 状态 ──────────────────────────────────────
	bool cmdStarted = false;
	int  status = 0;

	// ─── 当前活跃 RenderPass ──────────────────────
	SDL_GPURenderPass* pass = nullptr;
	SDL_GPUCommandBuffer* currentCmdBuf = nullptr;

	// ─── 当前绑定的 VG 管道索引（用于自动设 ref）──
	int currentVgPipeIndex = -1;  // VG_PIPE_OVER=0, VG_PIPE_CLEAR=1, VG_PIPE_SUB=2, VG_PIPE_CLIPPING=3

	// ─── 纹理延迟释放 fence（避免每帧 WaitForGPUIdle 全局卡顿）───
	SDL_GPUFence* tex_free_fence = nullptr;

	// ─── 顶点/索引绑定缓存（减少每命令重复绑定）───
	SDL_GPUBuffer* last_vbo = nullptr;
	SDL_GPUBuffer* last_ibo = nullptr;
	uint32_t last_vbo_off = 0;
	uint32_t last_ibo_off = 0;
	bool     bind_dirty = true;   // 新 render pass 或 gpubuf 重建后置 true
	// ─── 渐变 fragment uniform 缓存（draw_vg 每命令去重）───
	grad_cache_key_t gradKey;
	bool             gradDirty = true;  // slot0 被其他数据占用（特效/掩码/新 pass）时置位
};

// ========================================================================
// 常量
// ========================================================================


// 顶点布局
#define OVG_VERTEX_SIZE     20   // pos.xy(8) + uv.xy(8) + color(4)
#define GEOM_VERTEX_SIZE    28   // pos.xyz(12) + uv.xy(8) + col0(4) + col1(4)

// ========================================================================
// 工具函数
// ========================================================================

inline size_t align_up(size_t val, size_t align)
{
	return (val + align - 1) / align * align;
}
//inline uint32_t align_up(uint32_t v, uint32_t a) {
//	return (v + a - 1) & ~(a - 1);
//}
/*
	VG_FORMAT_RGBA8 = 0,
	VG_FORMAT_BGRA8,
	VG_FORMAT_RGBA8_SRGB, //SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
	VG_FORMAT_BGRA8_SRGB,
	VG_FORMAT_RGBA16F,
	VG_FORMAT_RGBA32F,
*/
inline SDL_GPUTextureFormat vg_to_sdl_format(vg_format_t fmt) {
	switch (fmt) {
	case VG_FORMAT_RGBA8:   return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	case VG_FORMAT_BGRA8:   return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	case VG_FORMAT_RGBA8_SRGB:   return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
	case VG_FORMAT_BGRA8_SRGB:   return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
	case VG_FORMAT_RGBA16F: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
	case VG_FORMAT_RGBA32F: return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	default:                return SDL_GPU_TEXTUREFORMAT_INVALID;
	}
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
static SDL_GPUShader* compile_shader(SDL_GPUDevice* device, SDL_GPUShaderStage stage, const uint32_t* code, size_t codeSize, const glm::ivec4& num, const char* entryPoint)
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
	sci.num_samplers = num.x;
	sci.num_uniform_buffers = num.y;
	sci.num_storage_buffers = num.z;
	sci.num_storage_textures = num.w;

	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &sci);
	if (!shader) {
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create %s shader: %s",
			stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vertex" : "fragment",
			SDL_GetError());
	}
	return shader;
}
SDL_GPUComputePipeline* new_compute_pipe(SDL_GPUDevice* device, size_t code_size, const void* code, const char* entrypoint
	, const glm::uvec2& tex_uniform_count, const glm::uvec4& rw, const glm::uvec3& count, SDL_GPUShaderFormat format)
{
	SDL_GPUComputePipelineCreateInfo cp = {
	.code_size = code_size,
	.code = (const Uint8*)code,
	.entrypoint = entrypoint,
	.format = format,
	.num_samplers = tex_uniform_count.x,
	.num_readonly_storage_textures = rw.x,
	.num_readonly_storage_buffers = rw.y,
	.num_readwrite_storage_textures = rw.z,
	.num_readwrite_storage_buffers = rw.w,
	.num_uniform_buffers = tex_uniform_count.y,
	.threadcount_x = count.x,
	.threadcount_y = count.y,
	.threadcount_z = count.z,
	};
	SDL_GPUComputePipeline* computePipeline = SDL_CreateGPUComputePipeline(device, &cp);
	return computePipeline;
}

#define MAX_COUNT_PASS_RES 8
#define MAX_COUNT_PASS_RES2 16
// 计算着色器调用封装
class compute_pass
{
public:
	SDL_GPUCommandBuffer* cmd = 0;
	SDL_GPUComputePass* pass = 0;
	// 写入目标资源
	std::array<SDL_GPUStorageTextureReadWriteBinding, MAX_COUNT_PASS_RES> rw_textures = {};
	std::array<SDL_GPUStorageBufferReadWriteBinding, MAX_COUNT_PASS_RES> rw_buffers = {};
	// 使用资源
	std::array<SDL_GPUTextureSamplerBinding, MAX_COUNT_PASS_RES2> textures = {};
	std::array<SDL_GPUTexture*, MAX_COUNT_PASS_RES> storage_textures = {};
	std::array<SDL_GPUBuffer*, MAX_COUNT_PASS_RES> storage_buffers = {};
	glm::uvec2 rw_count = {};
	glm::uvec3 count = {};
public:
	compute_pass();
	~compute_pass();
	void clear();
	// 添加读写资源，保留资源则cycle=true
	void add_rw_texture(SDL_GPUTexture* texture, uint32_t mip_level, uint32_t layer, bool cycle);
	void add_rw_buffer(SDL_GPUBuffer* buffer, bool cycle);
	// 开始构建pass
	void begin(SDL_GPUCommandBuffer* c);
	void bind_pipe(SDL_GPUComputePipeline* compute_pipeline);
	void push_texture(SDL_GPUTexture* texture, SDL_GPUSampler* sampler);
	void push_storage_texture(SDL_GPUTexture* storage_textures);
	void push_storage_buffers(SDL_GPUBuffer* storage_buffers);
	void push_uniformdata(uint32_t slot_index, const void* data, uint32_t length);
	// 执行
	void dispatch(const glm::uvec3& gcount);
	void dispatch_indirect(SDL_GPUBuffer* buffer, uint32_t offset);
	// 结束pass
	void end();
private:

};


compute_pass::compute_pass()
{}

compute_pass::~compute_pass()
{
	end();
}

void compute_pass::clear()
{
	cmd = 0;
	pass = 0;
	rw_textures = {};
	rw_buffers = {};
	textures = {};
	storage_textures = {};
	storage_buffers = {};
	rw_count = {};
	count = {};
}

void compute_pass::add_rw_texture(SDL_GPUTexture* texture, uint32_t mip_level, uint32_t layer, bool cycle)
{
	if (rw_count.x < rw_textures.size())
		rw_textures[rw_count.x++] = { texture,mip_level,layer,cycle };
}

void compute_pass::add_rw_buffer(SDL_GPUBuffer* buffer, bool cycle)
{
	if (rw_count.y < rw_buffers.size())
		rw_buffers[rw_count.y++] = { buffer,cycle };
}

void compute_pass::begin(SDL_GPUCommandBuffer* c)
{
	cmd = c;
	if (!pass && (rw_count.x > 0 || rw_count.y > 0))
	{
		pass = SDL_BeginGPUComputePass(c, rw_count.x > 0 ? rw_textures.data() : nullptr, rw_count.x, rw_count.y ? rw_buffers.data() : nullptr, rw_count.y);
	}
}
void compute_pass::bind_pipe(SDL_GPUComputePipeline* compute_pipeline) {
	if (pass)
		SDL_BindGPUComputePipeline(pass, compute_pipeline);
}
void compute_pass::push_texture(SDL_GPUTexture* texture, SDL_GPUSampler* sampler)
{
	if (texture && sampler && count.x < textures.size()) { textures[count.x++] = { texture, sampler }; }
}

void compute_pass::push_storage_texture(SDL_GPUTexture* storage_texture)
{
	if (storage_texture && count.x < storage_textures.size()) { storage_textures[count.y++] = storage_texture; }
}

void compute_pass::push_storage_buffers(SDL_GPUBuffer* storage_buffer)
{
	if (storage_buffer && count.x < storage_buffers.size()) { storage_buffers[count.z++] = storage_buffer; }
}

void compute_pass::push_uniformdata(uint32_t slot_index, const void* data, uint32_t length) {
	if (cmd && data && length)
		SDL_PushGPUComputeUniformData(cmd, slot_index, data, length);
}

void compute_pass::dispatch(const glm::uvec3& gcount)
{
	if (pass)
	{
		if (count.x > 0)
			SDL_BindGPUComputeSamplers(pass, 0, textures.data(), count.x);
		if (count.y > 0)
			SDL_BindGPUComputeStorageTextures(pass, 0, storage_textures.data(), count.y);
		if (count.z > 0)
			SDL_BindGPUComputeStorageBuffers(pass, 0, storage_buffers.data(), count.z);
		SDL_DispatchGPUCompute(pass, gcount.x, gcount.y, gcount.z);
	}
}
void compute_pass::dispatch_indirect(SDL_GPUBuffer* buffer, uint32_t offset)
{
	if (pass)
	{
		if (count.x > 0)
			SDL_BindGPUComputeSamplers(pass, 0, textures.data(), count.x);
		if (count.y > 0)
			SDL_BindGPUComputeStorageTextures(pass, 0, storage_textures.data(), count.y);
		if (count.z > 0)
			SDL_BindGPUComputeStorageBuffers(pass, 0, storage_buffers.data(), count.z);
		SDL_DispatchGPUComputeIndirect(pass, buffer, offset);
	}
}
void compute_pass::end() {
	if (pass) { SDL_EndGPUComputePass(pass); pass = 0; }
}


// ========================================================================
// 缓冲区管理
// ========================================================================



gpu_buffer_cx::gpu_buffer_cx()
{}

gpu_buffer_cx::~gpu_buffer_cx()
{
	if (buf)
		SDL_ReleaseGPUBuffer(dev, buf);
	buf = 0;
}
void gpu_buffer_cx::init(SDL_GPUDevice* d, const SDL_GPUBufferCreateInfo* createinfo)
{
	if (d && createinfo) {
		dev = d;
		buf = SDL_CreateGPUBuffer(dev, createinfo);
		if (!buf)
			SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU buffer: %s", SDL_GetError());
		info = *createinfo;
	}
}
void gpu_buffer_cx::init(SDL_GPUDevice* d, uint32_t usage, size_t capacity)
{
	SDL_GPUBufferCreateInfo c = {};
	if ((!d && !dev) || !usage)return;
	c.usage = usage;
	c.size = align_up(capacity, 256);
	if (d)
		dev = d;
	buf = SDL_CreateGPUBuffer(dev, &c);
	if (!buf)
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create GPU buffer: %s", SDL_GetError());
	info = c;
	size = c.size;
}
void gpu_buffer_cx::init_graphics(SDL_GPUDevice* d, size_t capacity, bool vbo, bool ibo, bool indirect)
{
	uint32_t usage = 0;
	if (vbo)usage |= SDL_GPU_BUFFERUSAGE_VERTEX;
	if (ibo)usage |= SDL_GPU_BUFFERUSAGE_INDEX;
	if (indirect)usage |= SDL_GPU_BUFFERUSAGE_INDIRECT;
	init(d, usage, capacity);
}
void gpu_buffer_cx::init_storage(SDL_GPUDevice* d, size_t capacity, bool comp_write, bool indirect)
{
	uint32_t usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
	if (comp_write)usage |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
	if (indirect)usage |= SDL_GPU_BUFFERUSAGE_INDIRECT;
	init(d, usage, capacity);
}
void gpu_buffer_cx::resize(size_t newsize)
{
	size = newsize;
	if (info.size < newsize && info.usage) {
		newsize = align_up(0.5 * info.size + newsize, 256);
		if (buf)
			SDL_ReleaseGPUBuffer(dev, buf);
		info.size = newsize;
		buf = SDL_CreateGPUBuffer(dev, &info);
		if (!buf)
			SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to resize GPU buffer: %s", SDL_GetError());
	}
}

OvgGpuBuffers::~OvgGpuBuffers()
{
	if (_staging)
		SDL_ReleaseGPUTransferBuffer(_device, _staging);

}

void OvgGpuBuffers::init(SDL_GPUDevice* device)
{
	_device = device;
	_vbo.init(_device, SDL_GPU_BUFFERUSAGE_VERTEX, 1024);
	_ibo.init(_device, SDL_GPU_BUFFERUSAGE_INDEX, 1024);
	_ssbo.init(_device, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ, 1024);
}

void OvgGpuBuffers::begin(size_t vcs, size_t ics, size_t ssbocs)
{
	vcs = align_up(vcs, 64);
	ics = align_up(ics, 64);
	ssbocs = align_up(ssbocs, 64);
	size_t ss = vcs + ics + ssbocs;
	if (_device && ss > _stagingSize)
	{
		ss = align_up(ss, 256);
		SDL_GPUTransferBufferCreateInfo tbi = {};
		tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		tbi.size = ss;
		auto nb = SDL_CreateGPUTransferBuffer(_device, &tbi);
		if (nb) {
			if (_staging)
			{
				SDL_ReleaseGPUTransferBuffer(_device, _staging);
			}
			_stagingSize = ss;
			_staging = nb;
		}
	}
	_vbo.resize(vcs);
	_ibo.resize(ics);
	_ssbo.resize(ssbocs);
	mapdt = (char*)SDL_MapGPUTransferBuffer(_device, _staging, true);
	vbo_ps = ibo_ps = ssbo_ps = 0;
	if (!mapdt)return;
}

uint32_t OvgGpuBuffers::add_vbo(const void* data, uint32_t size)
{
	assert(mapdt);
	if (!mapdt || !size || !data)return 0;
	auto dst = mapdt + vbo_ps;
	memcpy(dst, data, size);
	auto ret = vbo_ps;
	vbo_ps += size;
	return ret;
}

uint32_t OvgGpuBuffers::add_ibo(const void* data, uint32_t size)
{
	assert(mapdt);
	if (!mapdt || !size || !data)return 0;
	auto dst = mapdt + ibo_ps + _vbo.size;
	memcpy(dst, data, size);
	auto ret = ibo_ps;
	ibo_ps += size;
	return ret;
}

uint32_t OvgGpuBuffers::add_ssbo(const void* data, uint32_t size)
{
	assert(mapdt);
	if (!mapdt || !size || !data)return 0;
	auto dst = mapdt + ssbo_ps + _vbo.size + _ibo.size;
	memcpy(dst, data, size);
	auto ret = ssbo_ps;
	ssbo_ps += size;
	return ret;
}

void OvgGpuBuffers::end(SDL_GPUCommandBuffer* cmd)
{
	if (mapdt) {
		SDL_UnmapGPUTransferBuffer(_device, _staging);
		mapdt = nullptr;
	}
	if (!(_vbo.size + _ibo.size + _ssbo.size > 0))return;
	auto copyPass = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTransferBufferLocation tbl = { .transfer_buffer = _staging, .offset = 0 };
	SDL_GPUBufferRegion vbr = { .buffer = _vbo.buf, .offset = 0, .size = (uint32_t)_vbo.size };
	if (vbr.size)
	{
		SDL_UploadToGPUBuffer(copyPass, &tbl, &vbr, true);
	}
	SDL_GPUBufferRegion ibr = { .buffer = _ibo.buf, .offset = 0, .size = (uint32_t)_ibo.size };
	if (ibr.size) {
		tbl.offset = vbr.size;
		SDL_UploadToGPUBuffer(copyPass, &tbl, &ibr, true);
	}
	SDL_GPUBufferRegion ubr = { .buffer = _ssbo.buf, .offset = 0, .size = (uint32_t)_ssbo.size };
	if (ubr.size) {
		tbl.offset += ibr.size;
		SDL_UploadToGPUBuffer(copyPass, &tbl, &ubr, true);
	}
	SDL_EndGPUCopyPass(copyPass);
}

void OvgGpuBuffers::bindVBO(SDL_GPURenderPass* pass, uint32_t offset)
{
	SDL_GPUBufferBinding binding = {};
	binding.buffer = _vbo.buf;
	binding.offset = offset;
	SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
}

void OvgGpuBuffers::bindIBO(SDL_GPURenderPass* pass, uint32_t offset)
{
	SDL_GPUBufferBinding binding = {};
	binding.buffer = _ibo.buf;
	binding.offset = offset;
	SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void OvgGpuBuffers::bindSSBO(SDL_GPURenderPass* pass, bool is_vertex)
{
	if (is_vertex)
		SDL_BindGPUVertexStorageBuffers(pass, 0, &_ssbo.buf, 1);
	else
		SDL_BindGPUFragmentStorageBuffers(pass, 0, &_ssbo.buf, 1);
}

void OvgGpuBuffers::bind_v_ssbo(SDL_GPURenderPass* pass, SDL_GPUBuffer** storage_buffers, uint32_t num_bindings)
{
	SDL_BindGPUVertexStorageBuffers(pass, 0, storage_buffers, num_bindings);
}

void OvgGpuBuffers::bind_f_ssbo(SDL_GPURenderPass* pass, SDL_GPUBuffer** storage_buffers, uint32_t num_bindings)
{
	SDL_BindGPUFragmentStorageBuffers(pass, 0, storage_buffers, num_bindings);
}

/* 带缓存的 VBO/IBO 绑定：同一 render pass 内偏移与缓冲未变化时跳过重复绑定 */
void bind_vbo_cached(ovg_ctx_t* ctx, SDL_GPURenderPass* pass, uint32_t offset)
{
	SDL_GPUBuffer* buf = ctx->gpubuf ? ctx->gpubuf->vbo() : nullptr;
	if (!ctx->bind_dirty && buf == ctx->last_vbo && offset == ctx->last_vbo_off)
		return;
	SDL_GPUBufferBinding binding = {};
	binding.buffer = buf;
	binding.offset = offset;
	SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
	ctx->last_vbo = buf;
	ctx->last_vbo_off = offset;
	// bind_dirty 由 IBO 绑定负责清除：新 render pass 里 VBO/IBO 都必须重新绑定，
	// 若在这里提前清除，IBO 缓存命中时会跳过绑定。
}

void bind_ibo_cached(ovg_ctx_t* ctx, SDL_GPURenderPass* pass, uint32_t offset)
{
	SDL_GPUBuffer* buf = ctx->gpubuf ? ctx->gpubuf->ibo() : nullptr;
	if (!ctx->bind_dirty && buf == ctx->last_ibo && offset == ctx->last_ibo_off)
		return;
	SDL_GPUBufferBinding binding = {};
	binding.buffer = buf;
	binding.offset = offset;
	SDL_BindGPUIndexBuffer(pass, &binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
	ctx->last_ibo = buf;
	ctx->last_ibo_off = offset;
	ctx->bind_dirty = false;
}


// ========================================================================
// 纹理管理
// ========================================================================
static sdl3gpu_texture* new_texture(sdl3gpu_texture* tex, ovg_device_t* dev, SDL_GPUTextureFormat  format, int width, int height, SDL_GPUTextureUsageFlags extraUsage = 0)
{
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.usage = extraUsage ? extraUsage : SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

	tex->device = dev->gpuDevice;
	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	if (!tex->texture)
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create texture: %s", SDL_GetError());

	return tex;
}

static sdl3gpu_texture* new_msaa_texture(sdl3gpu_texture* tex, ovg_device_t* dev, SDL_GPUTextureFormat format, int width, int height, SDL_GPUSampleCount samples)
{
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = samples;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;

	tex->device = dev->gpuDevice;
	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	if (!tex->texture)
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create MSAA texture: %s", SDL_GetError());

	return tex;
}

static sdl3gpu_texture* new_depth_stencil_texture(sdl3gpu_texture* tex, ovg_device_t* dev, SDL_GPUTextureFormat format, int width, int height, SDL_GPUSampleCount samples)
{
	tex->format = format;
	tex->width = width;
	tex->height = height;
	tex->hasStencil = true;
	tex->references = 1;

	SDL_GPUTextureCreateInfo info = {};
	info.format = format;
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.width = (Uint32)width;
	info.height = (Uint32)height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = samples;
	info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	tex->device = dev->gpuDevice;
	tex->texture = SDL_CreateGPUTexture(dev->gpuDevice, &info);
	if (!tex->texture)
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create depth/stencil texture: %s", SDL_GetError());

	return tex;
}
sdl3gpu_texture* new_texture(ovg_device_t* dev, SDL_GPUTextureFormat  format, int width, int height, SDL_GPUTextureUsageFlags extraUsage = 0)
{
	auto p = new sdl3gpu_texture();
	if (p)
	{
		new_texture(p, dev, format, width, height, extraUsage);
	}
	return p;
}
static sdl3gpu_texture* new_msaa_texture(ovg_device_t* dev, SDL_GPUTextureFormat format, int width, int height, SDL_GPUSampleCount samples)
{
	auto p = new sdl3gpu_texture();
	if (p)
	{
		new_msaa_texture(p, dev, format, width, height, samples);
	}
	return p;
}
static sdl3gpu_texture* new_depth_stencil_texture(ovg_device_t* dev, SDL_GPUTextureFormat format, int width, int height, SDL_GPUSampleCount samples)
{
	auto p = new sdl3gpu_texture();
	if (p)
	{
		new_depth_stencil_texture(p, dev, format, width, height, samples);
	}
	return p;
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
	if (tex->texture && tex->owns_texture) {
		SDL_ReleaseGPUTexture(tex->device, tex->texture);
		tex->texture = nullptr;
	}
	delete tex;
}

// 释放一组特效离屏纹理（mask/tmp/out），创建不完整或池淘汰时调用
static void free_fx_set(fx_texture_set& s) {
	if (s.mask) { destroy_texture(s.mask); s.mask = nullptr; }
	if (s.tmp) { destroy_texture(s.tmp); s.tmp = nullptr; }
	if (s.out) { destroy_texture(s.out); s.out = nullptr; }
}

// ========================================================================
// 混合模式转换
// ========================================================================

#if 1
#define SDL_BLENDMODE_NONE_FULL(blend) \
    do { \
        (blend).enable_blend = false; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_BLEND_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_BLEND_PREMULTIPLIED_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_ADD_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_ADD_PREMULTIPLIED_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_MOD_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_MUL_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

#define SDL_BLENDMODE_SCREEN_FULL(blend) \
    do { \
        (blend).enable_blend = true; \
        (blend).src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR; \
        (blend).color_blend_op = SDL_GPU_BLENDOP_ADD; \
        (blend).src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; \
        (blend).dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR; \
        (blend).alpha_blend_op = SDL_GPU_BLENDOP_ADD; \
    } while(0)

void gpu_set_blend(SDL_GPUColorTargetBlendState& blend, blendMode_e bm)
{
	blend = {};
	switch (bm)
	{
	case blendMode_e::none:
		SDL_BLENDMODE_NONE_FULL(blend);
		break;
	case blendMode_e::normal:
		SDL_BLENDMODE_BLEND_FULL(blend);
		break;
	case blendMode_e::additive:
		SDL_BLENDMODE_ADD_FULL(blend);
		break;
	case blendMode_e::normal_prem:
		SDL_BLENDMODE_BLEND_PREMULTIPLIED_FULL(blend);
		break;
	case blendMode_e::additive_prem:
		SDL_BLENDMODE_ADD_PREMULTIPLIED_FULL(blend);
		break;
	case blendMode_e::multiply:
		SDL_BLENDMODE_MUL_FULL(blend);
		break;
	case blendMode_e::modulate:
		SDL_BLENDMODE_MOD_FULL(blend);
		break;
	case blendMode_e::screen:
		SDL_BLENDMODE_SCREEN_FULL(blend);
		break;
	default:
		SDL_BLENDMODE_NONE_FULL(blend);
		break;
	}

	blend.color_write_mask =
		SDL_GPU_COLORCOMPONENT_R |
		SDL_GPU_COLORCOMPONENT_G |
		SDL_GPU_COLORCOMPONENT_B |
		SDL_GPU_COLORCOMPONENT_A;
}
#endif // 1

// ========================================================================
// 着色器模块管理
// ========================================================================
static void init_shader_modules(ovg_device_t* dev) {
	struct { const uint32_t* v; size_t vlen; const uint32_t* f; size_t flen; } shaders[5] = {
		{ a_base3d_vert,       sizeof(a_base3d_vert),         a_base3d_frag,          sizeof(a_base3d_frag) },
		{ a_base3d_mask_vert,    sizeof(a_base3d_mask_vert),    a_base3d_mask_frag,     sizeof(a_base3d_mask_frag) },
		{ a_base3d_dsc_vert,     sizeof(a_base3d_dsc_vert),     a_base3d_dsc_frag,     sizeof(a_base3d_dsc_frag) },
		{ a_base3d_inst_vert,    sizeof(a_base3d_inst_vert),    a_base3d_inst_frag,    sizeof(a_base3d_inst_frag) },
		{ a_base3d_dsc_inst_vert, sizeof(a_base3d_dsc_inst_vert),a_base3d_dsc_inst_frag, sizeof(a_base3d_dsc_inst_frag) },
	};
	/*
	.num_readonly_storage_textures = rw.x,
	.num_readonly_storage_buffers = rw.y,
	.num_readwrite_storage_textures = rw.z,
	.num_readwrite_storage_buffers = rw.w,
	*/
	struct { const uint32_t* c; size_t vlen; glm::uvec2 tex_uniform_count; glm::uvec4 rw; } comp_shaders[] = {
		{ a_blur_comp, sizeof(a_blur_comp),{ 1,1 },{ 0,0,1,0 } },
		{ a_displace_comp, sizeof(a_displace_comp),{ 2,1 },{ 0,0,1,0 } },
		{ a_noise_comp, sizeof(a_noise_comp),{ 0,1 },{ 0,0,1,0 } },
	};
	struct num_sm {
		glm::ivec4 vnum = {};	//num_samplers = num.x;	num_uniform_buffers = num.y;	num_storage_buffers = num.z;	num_storage_textures = num.w;
		glm::ivec4 fnum = {};
	};
	num_sm nums[5] = { { glm::ivec4(0,1,0,0),glm::ivec4(1,0,0,0) }
		,{ glm::ivec4(0,1,0,0),glm::ivec4(2,1,0,0) }
		,{ glm::ivec4(0,1,0,0),glm::ivec4(1,0,0,0) }
		,{ glm::ivec4(0,1,1,0),glm::ivec4(1,0,0,0) }
		,{ glm::ivec4(0,1,1,0),glm::ivec4(1,0,0,0) }
	};
	for (int i = 0; i < 5; i++) {
		dev->shaderModules[i].vert = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_VERTEX, shaders[i].v, shaders[i].vlen, nums[i].vnum, "main");
		dev->shaderModules[i].frag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT, shaders[i].f, shaders[i].flen, nums[i].fnum, "main");
	}
	glm::uvec3 ccount = { 8,8,1 };
	for (int i = 0; i < (int)(sizeof(comp_shaders) / sizeof(comp_shaders[0])); i++)
	{
		auto& it = comp_shaders[i];
		dev->comp_pipe[i] = new_compute_pipe(dev->gpuDevice, it.vlen, it.c, "main", it.tex_uniform_count, it.rw, ccount, dev->supportedFormats);
	}
}

static void destroy_shader_modules(ovg_device_t* dev) {
	for (int i = 0; i < 5; i++) {
		if (dev->shaderModules[i].vert) SDL_ReleaseGPUShader(dev->gpuDevice, dev->shaderModules[i].vert);
		if (dev->shaderModules[i].frag) SDL_ReleaseGPUShader(dev->gpuDevice, dev->shaderModules[i].frag);
		dev->shaderModules[i] = {};
	}
	for (int i = 0; i < 10; i++) {
		auto& p = dev->comp_pipe[i];
		if (p)
			SDL_ReleaseGPUComputePipeline(dev->gpuDevice, p);
		p = 0;
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
	SDL_GPUColorComponentFlags color_write_mask;
	bool                 depthTestEnable;
	bool                 depthWriteEnable;
	bool                 stencilTestEnable;
	bool                 enable_color_write_mask;
	bool                 logicOpEnable;
	SDL_GPUBlendOp       logicOp;
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

static SDL_GPUGraphicsPipeline* create_graphics_pipeline(ovg_device_t* dev, const vg_pipeline_inputs* inputs, ovg_ctx_t* ctx)
{
	blend_params bp = {};
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

	gpu_set_blend(colorTarget.blend_state, inputs->blendMode);
	if (inputs->logicOpEnable)
		colorTarget.blend_state.alpha_blend_op = colorTarget.blend_state.color_blend_op = inputs->logicOp;
	SDL_GPUDepthStencilState dsState = {};
	dsState.enable_depth_test = inputs->depthTestEnable;
	dsState.enable_depth_write = inputs->depthWriteEnable;
	dsState.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	dsState.enable_stencil_test = inputs->stencilTestEnable;
	dsState.front_stencil_state = inputs->stencilFront;
	dsState.back_stencil_state = inputs->stencilBack;
	dsState.compare_mask = inputs->ds.compare_mask;
	dsState.write_mask = inputs->ds.write_mask;
	SDL_GPURasterizerState rasterState = {};
	rasterState.fill_mode = SDL_GPU_FILLMODE_FILL;
	rasterState.cull_mode = SDL_GPU_CULLMODE_NONE;
	rasterState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	rasterState.enable_depth_bias = false;
	rasterState.enable_depth_clip = false;
	colorTarget.blend_state.enable_color_write_mask = inputs->enable_color_write_mask;
	if (inputs->enable_color_write_mask)
		colorTarget.blend_state.color_write_mask = inputs->color_write_mask;
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
	if (ctx)
		ctx->freepipe.insert(pipeline);
	return pipeline;
}

// ========================================================================
// ★ 核心：5 条 VG 管道初始化（ref 动态切方案）
// ========================================================================
//
//   bit1 (STENCIL_CLIP_BIT = 0x1) → 裁剪掩码，由 pipeClipping 的 REPLACE 写入
//
// 动态 ref 切换：
//   - 填充阶段：ref 无关（INVERT 不依赖 ref）
//   - 裁剪阶段：ref = STENCIL_CLIP_BIT，写入掩码
//   - 绘制阶段：ref = STENCIL_CLIP_BIT，通过测试
//
// ========================================================================
static void init_vg_pipelines(ovg_ctx_t* ctx) {
	ovg_device_t* dev = ctx->device;

	glm::ivec4 vnum = { 0,1,0,0 };	//num_samplers = num.x;	num_uniform_buffers = num.y;	num_storage_buffers = num.z;	num_storage_textures = num.w;
	glm::ivec4 fnum = { 1,1,0,0 };
	// 编译 VG 着色器
	SDL_GPUShader* vgVert = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_VERTEX, vg_vert, sizeof(vg_vert), vnum, "main");
	SDL_GPUShader* vgFrag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT, vg_frag, sizeof(vg_frag), fnum, "main");
	// mask 专用 FS：只输出填充 alpha，无 sampler/UBO
	glm::ivec4 fmask = { 0,0,0,0 };
	SDL_GPUShader* effectMaskFrag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT,
		a_effect_mask_frag, sizeof(a_effect_mask_frag), fmask, "main");

	// 公共顶点属性: pos(2) + uv(2) + color(1) = 20 bytes
	SDL_GPUVertexAttribute vgAttrs[3] = {
		{ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,        0 },   // pos
		{ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,        8 },   // uv
		{ 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 16 },	// color (RGBA8 UNORM)
	};

	// ═══════════════════════════════════════════════════════════════
	// ② pipeClipping — 裁剪掩码写入
	//   职责：把通过的像素 bit1 写成 ref（= STENCIL_CLIP_BIT = 0x1）
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
		inputs.blendMode = blendMode_e::none;
		inputs.vertexStride = sizeof(ovgVertex);
		inputs.numAttributes = 3;
		inputs.enable_color_write_mask = true;// 只写 stencil
		inputs.color_write_mask = 0;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// ★ 核心 stencil 状态 
		inputs.ds.compare_mask = STENCIL_CLIP_BIT;
		inputs.ds.write_mask = STENCIL_CLIP_BIT;
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_REPLACE;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilBack = inputs.stencilFront;
		ctx->pipeClipping = create_graphics_pipeline(dev, &inputs, ctx);
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
		inputs.blendMode = blendMode_e::normal_prem;
		inputs.vertexStride = sizeof(ovgVertex);
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));

		// ★ 核心 stencil 状态
		//   compareOp = EQUAL → (stencil & compare_mask) == (ref & compare_mask)
		//   compare_mask = CLIP_BIT → 只比 bit1
		//   writeMask = 0 → 不改 stencil
		//   ref = STENCIL_CLIP_BIT (0x1) → 动态设置
		inputs.ds.compare_mask = STENCIL_CLIP_BIT;
		inputs.ds.write_mask = 0x0;                   // 不改 stencil
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_EQUAL;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		//inputs.stencilFront.reference = 0;  // 动态覆盖
		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeOVER = create_graphics_pipeline(dev, &inputs, ctx);
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
		inputs.blendMode = blendMode_e::normal_prem;  // 用逻辑操作 SUBTRACT 模拟
		inputs.vertexStride = sizeof(ovgVertex);
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
		inputs.logicOp = SDL_GPU_BLENDOP_SUBTRACT;
		inputs.logicOpEnable = true;
		ctx->pipeSUB = create_graphics_pipeline(dev, &inputs, ctx);
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
		inputs.vertexStride = sizeof(ovgVertex);
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

		inputs.enable_color_write_mask = true;
		ctx->pipeCLEAR = create_graphics_pipeline(dev, &inputs, ctx);
	}
	// todo 管线
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
		inputs.blendMode = blendMode_e::none;
		inputs.vertexStride = sizeof(ovgVertex);
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));
		inputs.enable_color_write_mask = true;
		inputs.color_write_mask = 0;

		// ★ 清 stencil 核心
		inputs.ds.compare_mask = 0xFF;
		inputs.ds.write_mask = 0xFF;

		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_REPLACE;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_REPLACE;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_REPLACE;

		inputs.stencilBack = inputs.stencilFront;

		ctx->pipeStencilClear = create_graphics_pipeline(dev, &inputs, ctx);
	}
	// ═══════════════════════════════════════════════════════════════
	// ⑥ 特效管线：掩码离屏渲染 + 特效层叠加
	// ═══════════════════════════════════════════════════════════════
	{
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = vgVert;
		inputs.fragShader = effectMaskFrag;
		inputs.colorFormat = ctx->fxMaskFormat;
		inputs.depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		inputs.samples = SDL_GPU_SAMPLECOUNT_1;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = false;
		inputs.blendMode = blendMode_e::none;
		inputs.vertexStride = sizeof(ovgVertex);
		inputs.numAttributes = 3;
		memcpy(inputs.attributes, vgAttrs, sizeof(vgAttrs));
		inputs.enable_color_write_mask = true;
		inputs.color_write_mask = SDL_GPU_COLORCOMPONENT_R;
		ctx->pipeEffectMask = create_graphics_pipeline(dev, &inputs, ctx);
	}
	{
		SDL_GPUShader* epVert = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_VERTEX,
			a_effect_present_vert, sizeof(a_effect_present_vert), glm::ivec4(0, 0, 0, 0), "main");
		SDL_GPUShader* epFrag = compile_shader(dev->gpuDevice, SDL_GPU_SHADERSTAGE_FRAGMENT,
			a_effect_present_frag, sizeof(a_effect_present_frag), glm::ivec4(2, 1, 0, 0), "main");
		vg_pipeline_inputs inputs = {};
		inputs.vertShader = epVert;
		inputs.fragShader = epFrag;
		inputs.colorFormat = ctx->colorFormat;
		inputs.depthFormat = ctx->depthFormat;
		inputs.samples = ctx->samples;
		inputs.topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		inputs.depthTestEnable = false;
		inputs.depthWriteEnable = false;
		inputs.stencilTestEnable = false;
		inputs.blendMode = blendMode_e::normal_prem;
		inputs.vertexStride = 0;
		inputs.numAttributes = 0;
		inputs.ds.compare_mask = STENCIL_CLIP_BIT;
		inputs.ds.write_mask = 0x0;
		inputs.stencilFront.compare_op = SDL_GPU_COMPAREOP_EQUAL;
		inputs.stencilFront.pass_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilFront.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
		inputs.stencilBack = inputs.stencilFront;
		ctx->pipeEffectComposite = create_graphics_pipeline(dev, &inputs, ctx);
		if (epVert) SDL_ReleaseGPUShader(dev->gpuDevice, epVert);
		if (epFrag) SDL_ReleaseGPUShader(dev->gpuDevice, epFrag);
	}
	// 释放 VG 着色器（管道已持有引用）
	SDL_ReleaseGPUShader(dev->gpuDevice, vgVert);
	SDL_ReleaseGPUShader(dev->gpuDevice, vgFrag);
	if (effectMaskFrag) SDL_ReleaseGPUShader(dev->gpuDevice, effectMaskFrag);
}

// ========================================================================
// 几何管道创建
// ========================================================================
static pipelinestate_p_internal create_geom_pipeline(ovg_ctx_t* ctx, const gem_info_t* info)
{
	pipelinestate_p_internal result = {};
	auto dev = ctx->device;
	int shaderIdx = info->shader;
	if (shaderIdx < 0 || shaderIdx >= 5) shaderIdx = 0;
	bool doublesided = info->shader == ST_INSTANCE_DOUBLESIDED || info->shader == ST_DOUBLESIDED;
	SDL_GPUShader* vert = dev->shaderModules[shaderIdx].vert;
	SDL_GPUShader* frag = dev->shaderModules[shaderIdx].frag;

	SDL_GPUVertexAttribute geomAttrs[4] = {
		{ 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,        0 },
		{ 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,       12 },
		{ 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,  20 },
		{ 3, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,  24 },
	};

	SDL_GPUColorTargetDescription colorTarget = {};
	colorTarget.format = ctx->colorFormat;
	colorTarget.blend_state.enable_color_write_mask = true;
	colorTarget.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
		SDL_GPU_COLORCOMPONENT_G |
		SDL_GPU_COLORCOMPONENT_B |
		SDL_GPU_COLORCOMPONENT_A;

	blend_params bp = {};
	colorTarget.blend_state.enable_blend = bp.blendEnable;
	colorTarget.blend_state.src_color_blendfactor = bp.srcColor;
	colorTarget.blend_state.dst_color_blendfactor = bp.dstColor;
	colorTarget.blend_state.color_blend_op = bp.colorOp;
	colorTarget.blend_state.src_alpha_blendfactor = bp.srcAlpha;
	colorTarget.blend_state.dst_alpha_blendfactor = bp.dstAlpha;
	colorTarget.blend_state.alpha_blend_op = bp.alphaOp;
	gpu_set_blend(colorTarget.blend_state, (blendMode_e)info->blendMode);

	SDL_GPUDepthStencilState dsState = {};
	dsState.enable_depth_test = (info->flags & (uint8_t)depth_stencil_State::d_depthtest_enable) != 0;
	dsState.enable_depth_write = (info->flags & (uint8_t)depth_stencil_State::d_depthwrite_enable) != 0;
	dsState.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	dsState.enable_stencil_test = (info->flags & (uint8_t)depth_stencil_State::d_stenciltest_enable) != 0;
	dsState.compare_mask = 0xFF;
	dsState.write_mask = 0;
	dsState.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	dsState.front_stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
	dsState.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
	dsState.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
	dsState.back_stencil_state = dsState.front_stencil_state;

	SDL_GPURasterizerState rasterState = {};
	rasterState.fill_mode = (SDL_GPUFillMode)info->polygon;
	rasterState.cull_mode = SDL_GPU_CULLMODE_NONE;
	rasterState.front_face = info->frontFace ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	rasterState.enable_depth_clip = false;

	SDL_GPUMultisampleState msState = {};
	msState.sample_count = ctx->samples;
	msState.sample_mask = 0;// 0xFFFFFFFF;

	SDL_GPUVertexBufferDescription vbDesc = {};
	vbDesc.slot = 0;
	vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	vbDesc.pitch = doublesided ? sizeof(geomVertex2) : sizeof(geomVertex1);

	SDL_GPUVertexInputState vertexInput = {};
	vertexInput.num_vertex_buffers = 1;
	vertexInput.vertex_buffer_descriptions = &vbDesc;
	vertexInput.num_vertex_attributes = 3 + (doublesided ? 1 : 0);
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
	pci.target_info.depth_stencil_format = ctx->depthFormat;
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
		pipelinestate_p_internal p = create_geom_pipeline(ctx, info);
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
//   pipeClipping → ref = STENCIL_CLIP_BIT，REPLACE 写入
//   pipeOVER     → ref = STENCIL_CLIP_BIT，EQUAL 比较
//   pipeSUB      → ref = STENCIL_CLIP_BIT，EQUAL 比较
//   pipeCLEAR    → stencil 已禁用，ref 无关
//
// ========================================================================
void ovg_bind_vg_pipeline(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* pass, int operatorType) {
	if (!ctx || !pass) return;

	SDL_GPUGraphicsPipeline* pipe = ctx->pipeOVER;
	int pipeIndex = 0;
	auto ot = (vg_operator_t)operatorType;
	switch (ot) {
	case vg_operator_t::VG_OPERATOR_OVER:  pipe = ctx->pipeOVER;     pipeIndex = VG_PIPE_OVER;      break;  // VG_OPERATOR_OVER
	case vg_operator_t::VG_OPERATOR_CLEAR:  pipe = ctx->pipeCLEAR;    pipeIndex = VG_PIPE_CLEAR;     break;  // VG_OPERATOR_CLEAR
	case vg_operator_t::VG_OPERATOR_DIFFERENCE:  pipe = ctx->pipeSUB;      pipeIndex = VG_PIPE_SUB;       break;  // VG_OPERATOR_DIFFERENCE
	default:  break;
	}

	SDL_BindGPUGraphicsPipeline(pass, pipe);
	ctx->currentVgPipeIndex = pipeIndex;

	// ★ 根据管道类型自动设置正确的 stencil reference
	switch (pipeIndex) {
	case VG_PIPE_CLIPPING:
		// REPLACE 写入：ref = STENCIL_CLIP_BIT → 通过的像素 bit1 = 1
		SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);
		break;

	case VG_PIPE_OVER:
	case VG_PIPE_SUB:
		// EQUAL 比较：只画 bit1 == 1 的像素
		SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);
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
	dev->emptyTexture = new_texture(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 16, 16,
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
			tbi.size = sizeof(uint32_t) * 16 * 16;
			SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(gpuDevice, &tbi);
			if (staging) {
				void* mapped = SDL_MapGPUTransferBuffer(gpuDevice, staging, false);
				if (mapped) {
					auto dd = (char*)mapped;
					for (int i = 0; i < 16 * 16; i++)
						memcpy(dd + i * sizeof(int), &white, sizeof(white));
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
	ctx->gpubuf = new OvgGpuBuffers();
	ctx->gpubuf->init(dev->gpuDevice);
	bool mask_r8 = SDL_GPUTextureSupportsFormat(dev->gpuDevice,
		SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
	bool mask_rgba8 = SDL_GPUTextureSupportsFormat(dev->gpuDevice,
		SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
	bool blur_r16 = SDL_GPUTextureSupportsFormat(dev->gpuDevice,
		SDL_GPU_TEXTUREFORMAT_R16_FLOAT, SDL_GPU_TEXTURETYPE_2D,
		SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
	// mask 只需要 alpha；R8 不可用时退回 RGBA8（mask shader 会把 alpha 写到所有通道）
	ctx->fxMaskFormat = mask_r8 ? SDL_GPU_TEXTUREFORMAT_R8_UNORM
		: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ctx->fxBlurFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
	ctx->fxSupported = (mask_r8 || mask_rgba8) && blur_r16;
	if (ctx->fxSupported) {
		SDL_GPUSamplerCreateInfo sci = {};
		sci.min_filter = SDL_GPU_FILTER_NEAREST;
		sci.mag_filter = SDL_GPU_FILTER_NEAREST;
		sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sci.max_anisotropy = 1.0f;
		ctx->fxSampler = SDL_CreateGPUSampler(dev->gpuDevice, &sci);
	}
	ctx->textures[0] = dev->emptyTexture;
	ctx->next_image_id++;
	init_vg_pipelines(ctx);

	return ctx;
}

void free_ovgctx_sdl3(ovg_ctx_t* ctx) {
	if (!ctx) return;
	SDL_WaitForGPUIdle(ctx->device->gpuDevice);
	// 释放 fence 与延迟销毁队列中的纹理
	if (ctx->tex_free_fence) {
		SDL_ReleaseGPUFence(ctx->device->gpuDevice, ctx->tex_free_fence);
		ctx->tex_free_fence = nullptr;
	}
	for (; ctx->texq.size();)
	{
		auto p = ctx->texq.front();
		if (p && p->id > 0)
		{
			ctx->textures.erase(p->id);
			destroy_texture(p);
		}
		ctx->texq.pop();
	}
	for (auto p : ctx->freepipe)
	{
		if (p) SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, p);
	}
	for (auto& [k, v] : ctx->textures) {
		if (v) destroy_texture(v);
	}
	for (auto& [key, p] : ctx->geomPipelines) {
		if (p.pipeline)      SDL_ReleaseGPUGraphicsPipeline(ctx->device->gpuDevice, p.pipeline);
		if (p.defaultSampler) SDL_ReleaseGPUSampler(ctx->device->gpuDevice, p.defaultSampler);
	}
	for (auto& [key, list] : ctx->fxPool) {
		for (auto& s : list) free_fx_set(s);
	}
	for (auto& s : ctx->fxPending) free_fx_set(s);
	for (auto& s : ctx->fxUsed) free_fx_set(s);
	ctx->fxPool.clear();
	ctx->fxPending.clear();
	ctx->fxUsed.clear();
	if (ctx->fxSampler) {
		SDL_ReleaseGPUSampler(ctx->device->gpuDevice, ctx->fxSampler);
		ctx->fxSampler = nullptr;
	}
	ctx->textures.clear();
	ctx->freepipe.clear();
	ctx->geomPipelines.clear();

	delete ctx->gpubuf;

	delete ctx;
}

// ========================================================================
// FBO 管理
// ========================================================================
vg_fbo_t new_vgfbo_sdl3(ovg_ctx_t* ctx, int width, int height, SDL_Window* window) {
	vg_fbo_t fbo = {};
	fbo.width = width;
	fbo.height = height;
	if (!ctx || !ctx->device) return fbo;
	fbo.ctx = ctx;
	ovg_device_t* dev = ctx->device;
	if (window)
		fbo.window = window;
	else
		fbo.colorTex = new_texture(dev, ctx->colorFormat, width, height, SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);

	if (ctx->samples > SDL_GPU_SAMPLECOUNT_1) {
		fbo.colorTexMS = new_msaa_texture(dev, ctx->colorFormat, width, height, ctx->samples);
	}

	fbo.depthStencilTex = new_depth_stencil_texture(dev, ctx->depthFormat, width, height, ctx->samples);
	fbo.hasStencil = true;

	return fbo;
}

SDL_GPUTexture* ovg_fbo_color_texture(vg_fbo_t* fbo) {
	if (!fbo) return nullptr;
	if (fbo->swapchain) return fbo->swapchain;
	if (fbo->colorTex) return ((sdl3gpu_texture*)fbo->colorTex)->texture;
	return nullptr;
}

void free_vgfbo_sdl3(vg_fbo_t* fbo) {
	if (!fbo) return;
	if (fbo->colorTex) { destroy_texture((sdl3gpu_texture*)fbo->colorTex);     fbo->colorTex = nullptr; }
	if (fbo->colorTexMS) { destroy_texture((sdl3gpu_texture*)fbo->colorTexMS);   fbo->colorTexMS = nullptr; }
	if (fbo->depthStencilTex) { destroy_texture((sdl3gpu_texture*)fbo->depthStencilTex); fbo->depthStencilTex = nullptr; }
}
void reset_vgfbo_sdl3(vg_fbo_t* fbo, int width, int height) {
	if (fbo) {
		free_vgfbo_sdl3(fbo);
		*fbo = new_vgfbo_sdl3(fbo->ctx, width, height, fbo->window);
		fbo->display_size = { width, height };
		fbo->upsize = true;
	}
}

void set_viewport2d(SDL_GPURenderPass* pass, const glm::ivec2& size) {
	SDL_GPUViewport view = { 0,0,0,0,0,1 };
	view.x = 0;
	view.y = size.y;
	view.w = size.x;
	view.h = -size.y;
	SDL_SetGPUViewport(pass, &view);
}
void set_viewport3d(SDL_GPURenderPass* pass, const glm::ivec2& size) {
	SDL_GPUViewport view = { 0,0,0,0,0,1 };
	view.x = 0;
	view.y = 0;
	view.w = size.x;
	view.h = size.y;
	SDL_SetGPUViewport(pass, &view);
}
// ========================================================================
// 帧生命周期
// ========================================================================
bool ovg_begin_pass(ovg_ctx_t* ctx, vg_fbo_t* fbo, bool clearAll) {
	if (!ctx || !fbo) return false;

	SDL_GPUCommandBuffer* cmdBuf = ctx->currentCmdBuf;
	if (!cmdBuf) return false;
	if (ctx->pass) {
		SDL_EndGPURenderPass(ctx->pass);
		ctx->pass = nullptr;
	}
	ctx->bind_dirty = true;   // 新 render pass，绑定状态失效
	ctx->gradDirty = true;    // fragment slot0 需重新推送
	sdl3gpu_texture* colorTex = (sdl3gpu_texture*)fbo->colorTex;
	sdl3gpu_texture* colorTexMS = (sdl3gpu_texture*)fbo->colorTexMS;
	sdl3gpu_texture* depthStencil = (sdl3gpu_texture*)fbo->depthStencilTex;

	auto ct = fbo->swapchain ? fbo->swapchain : colorTex->texture;
	if (!ct || !depthStencil) {
		//SDL_SubmitGPUCommandBuffer(cmdBuf);
		return false;
	}


	SDL_GPUTexture* renderColorTex = colorTexMS ? colorTexMS->texture : ct;

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = renderColorTex;
	colorTargetInfo.load_op = clearAll ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	colorTargetInfo.store_op = (fbo && fbo->colorTexMS && ct) ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
	colorTargetInfo.clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
	//colorTargetInfo.layer_count = 1;
	colorTargetInfo.resolve_mip_level = 0;
	colorTargetInfo.resolve_layer = 0;
	colorTargetInfo.resolve_texture = ct;

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
		//SDL_SubmitGPUCommandBuffer(cmdBuf);
		return false;
	}
	fbo->cmd = cmdBuf;
	ctx->pass = renderPass;
	ctx->currentVgPipeIndex = -1;

	// 设置视口 
	SDL_Rect rc = { 0, 0, fbo->width, fbo->height };
	set_viewport2d(renderPass, glm::ivec2(fbo->width, fbo->height));
	SDL_SetGPUScissor(renderPass, &rc);
	ctx->cmdStarted = true;
	ctx->viewportW = fbo->width;
	ctx->viewportH = fbo->height;


	return true;
}

void ovg_end_pass(ovg_ctx_t* ctx, vg_fbo_t* fbo) {
	if (!ctx || !fbo) return;
	SDL_GPUCommandBuffer* actualCmd = ctx->currentCmdBuf;

	if (ctx->pass) {
		SDL_EndGPURenderPass(ctx->pass);
		ctx->pass = nullptr;
	}
	fbo->cmd = nullptr;
	ctx->cmdStarted = false;
	ctx->currentCmdBuf = nullptr;
	ctx->currentVgPipeIndex = -1;
}
SDL_GPURenderPass* ovg_get_current_render_pass(ovg_ctx_t* ctx) {
	if (!ctx) return nullptr;
	return ctx->pass;
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

void deferred_free_advance(ovg_ctx_t* r);

void ovg_wait_idle(ovg_ctx_t* ctx) {
	if (!ctx || !ctx->device) return;
	SDL_WaitForGPUIdle(ctx->device->gpuDevice);
	deferred_free_advance(ctx);   // idle 后 fence 必已完成，可安全释放队列中的纹理
}

void ovg_sort_gradient_stops(vg_gradient_t* grad, float* stops, uint32_t count) {
	auto colors = grad->colors;
	for (uint32_t i = 1; i < count; i++) {
		float   key_stop = stops[i];
		auto key_color = colors[i];
		int j = (int)i - 1;
		while (j >= 0 && stops[j] > key_stop) {
			stops[j + 1] = stops[j];
			colors[j + 1] = colors[j];
			j--;
		}
		stops[j + 1] = key_stop;
		colors[j + 1] = key_color;
	}
}
void ovg_mul_pat(vg_gradient_t* grad, int type, const glm::mat3& mat, const glm::mat3& pat_mat, bool hasMatrix) {
	glm::vec3 cp0[2] = { glm::vec3(grad->cp[0].x, grad->cp[0].y,1.0f) ,glm::vec3(grad->cp[0].z, grad->cp[0].w,1.0f) };
	glm::vec3 cp1[2] = { glm::vec3(grad->cp[1].x, grad->cp[1].y,1.0f) ,glm::vec3(grad->cp[1].z, grad->cp[1].w,1.0f) };
	auto m = mat * glm::inverse(pat_mat);
	cp0[0] = m * cp0[0];
	if (type == vg_pattern_type_t::VG_PATTERN_TYPE_LINEAR) {
		cp0[1] = m * cp0[1];
	}
	else {
		cp1[0] = m * cp1[0];
		// radii
		cp0[1].z = 0.0;
		cp0[1] = m * cp0[1];
		cp1[1].z = 0.0;
		cp1[1] = m * cp1[1];
	}
	grad->cp[0] = glm::vec4(glm::vec2(cp0[0]), glm::vec2(cp0[1]));
	grad->cp[1] = glm::vec4(glm::vec2(cp1[0]), glm::vec2(cp1[1]));
	ovg_sort_gradient_stops(grad, grad->stops, grad->count);
}

SDL_Rect set_scissor_sdl3(vg_fbo_t* fbo, SDL_GPURenderPass* pass, glm::vec4* scissor)
{
	SDL_Rect r = { 0,0,-1,-1 };
	if (pass)
	{
		r = { 0,0,(int)fbo->width,(int)fbo->height };
		if (scissor)
		{
			r.x = (int)scissor->x; r.y = (int)scissor->y;
			r.w = (int)scissor->z;
			if (r.x < 0)
			{
				r.w += r.x;
				r.x = 0;
			}
			r.h = (int)scissor->w;
			if (r.y < 0)
			{
				r.h += r.y;
				r.y = 0;
			}
			if (r.w < 1) r.w = 1;
			if (r.h < 1) r.h = 1;
		}
		SDL_SetGPUScissor(pass, &r);
	}
	return r;
}

void cmd_draw_full_screen_quad_sdl3(vg_fbo_t* fbo, SDL_GPURenderPass* pass, vgcmd_t* c, glm::vec4* scissor, SDL_Rect* clip, push_constants_t& pc)
{
#if defined(_DEBUG)
	SDL_PushGPUDebugGroup(fbo->cmd, "_draw_full_screen_quad");
#endif
	SDL_Rect bounds = { 0,0,fbo->width, fbo->height };
	if (scissor) {
		SDL_Rect r = { (int32_t)glm::max((int)scissor->x, 0), (int32_t)glm::max((int)scissor->y, 0),
					 (int32_t)glm::max((int)scissor->z - (int32_t)scissor->x + 1, 1),
					   (int32_t)glm::max((int)scissor->w - (int32_t)scissor->y + 1, 1) };
		SDL_SetGPUScissor(pass, &r);
	}
	uint32_t firstVertIdx = c->full_screen_quad;
	if (c->state)
		pc.fsq_patternType = c->state->pushConsts.fsq_patternType;
	pc.fsq_patternType |= FULLSCREEN_BIT;
	SDL_PushGPUVertexUniformData(fbo->cmd, 0, &pc, sizeof(pc));
	SDL_DrawGPUPrimitives(pass, 3, 1, firstVertIdx, 0);
	// 不必恢复 pc：本函数返回后调用方不会再用 pc 画别的，下条命令会重推自己的 uniform
	if (scissor)
		SDL_SetGPUScissor(pass, clip && clip->w > 0 && clip->h > 0 ? clip : &bounds);
#if defined(_DEBUG) 
	SDL_PopGPUDebugGroup(fbo->cmd);
#endif
}

void draw_ct(SDL_GPURenderPass* pass, vgcmd_t* c)
{
	if (c->index.y > 0)
	{
		SDL_DrawGPUIndexedPrimitives(pass, c->index.y, 1, c->index.x, static_cast<int32_t>(c->vertex.x), 0);
	}
	else
	{
		SDL_DrawGPUPrimitives(pass, c->vertex.y, 1, c->vertex.x, 0);
	}
}
static void draw_effect_quad(vg_fbo_t* fbo, SDL_GPURenderPass* pass, const fx_result_t* fx, SDL_Rect* clip)
{
	if (!fbo || !pass || !fx || !fx->tex) return;
	ovg_ctx_t* ctx = fbo->ctx;
	if (!ctx || !ctx->pipeEffectComposite || !ctx->fxSampler) return;
	SDL_BindGPUGraphicsPipeline(pass, ctx->pipeEffectComposite);

	int x0 = (int)fx->rect.x;
	int y0 = (int)fx->rect.y;
	int x1 = x0 + (int)fx->rect.z;
	int y1 = y0 + (int)fx->rect.w;
	if (clip && clip->w > 0 && clip->h > 0) {
		x0 = std::max(x0, clip->x);
		y0 = std::max(y0, clip->y);
		x1 = std::min(x1, clip->x + clip->w);
		y1 = std::min(y1, clip->y + clip->h);
	}
	x0 = std::max(x0, 0); y0 = std::max(y0, 0);
	x1 = std::min(x1, (int)fbo->width); y1 = std::min(y1, (int)fbo->height);
	if (x1 <= x0 || y1 <= y0) return;
	SDL_Rect rc = { x0, y0, x1 - x0, y1 - y0 };
	SDL_SetGPUScissor(pass, &rc);

	SDL_GPUTextureSamplerBinding b[2] = {
		{ fx->mask->texture, ctx->fxSampler },
		{ fx->tex->texture, ctx->fxSampler }
	};
	SDL_BindGPUFragmentSamplers(pass, 0, b, 2);
	struct fx_present_params_t {
		int mode;
		float spread;
		float opacity;
		float pad0;
		float offset[2];
		float color[4];
		float rect[4];
	};
	static_assert(sizeof(fx_present_params_t) == 56, "fx_present_params_t must match Slang layout");
	fx_present_params_t pu = {};
	pu.mode = fx->mode;
	pu.spread = fx->spread;
	pu.opacity = fx->opacity;
	pu.offset[0] = fx->offset.x; pu.offset[1] = fx->offset.y;
	pu.color[0] = fx->color.x; pu.color[1] = fx->color.y;
	pu.color[2] = fx->color.z; pu.color[3] = fx->color.w;
	pu.rect[0] = fx->rect.x; pu.rect[1] = fx->rect.y;
	pu.rect[2] = fx->rect.z; pu.rect[3] = fx->rect.w;
	SDL_PushGPUFragmentUniformData(ctx->currentCmdBuf, 0, &pu, sizeof(pu));
	ctx->gradDirty = true;   // slot0 已被特效参数占用
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

	SDL_Rect full = { 0, 0, (int)fbo->width, (int)fbo->height };
	SDL_SetGPUScissor(pass, (clip && clip->w > 0 && clip->h > 0) ? clip : &full);
}

void draw_vg(vg_fbo_t* fbo, SDL_GPURenderPass* pass, vgcmd_t* c, SDL_Rect* cuclip, const glm::uvec2& offset, const fx_result_t* fx = nullptr)
{
	push_constants_t pc = {};
	auto t = c->state;
	bind_vbo_cached(fbo->ctx, pass, offset.x);
	bind_ibo_cached(fbo->ctx, pass, offset.y);
	SDL_GPUTextureSamplerBinding binding = { .texture = fbo->ctx->device->emptyTexture->texture,	.sampler = fbo->ctx->device->emptyTexture->sampler, };

	if (fx && fx->tex && fx->mask && fx->mode != 1)
		draw_effect_quad(fbo, pass, fx, cuclip);

	if (t) {
		int smax = std::max(fbo->width, fbo->height);
		pc = t->pushConsts;
		pc.size = { fbo->width,fbo->height };
		ovg_bind_vg_pipeline(fbo->ctx, fbo->cmd, pass, (int)t->curOperator);
		if (t->pattern && t->pattern->type != vg_pattern_type_t::VG_PATTERN_TYPE_SOLID) {
			pc.source = { smax,smax,0,0 };
		}
		glm::mat3x3 inv(pc.mat);
		// 单位矩阵快速路径：跳过 3x3 求逆
		if (inv == glm::mat3x3(1.0f))
			pc.matInv = glm::mat3x3(1.0f);
		else
			pc.matInv = glm::inverse(inv);
		if (t->pattern)
		{
			pc.fsq_patternType = (pc.fsq_patternType & FULLSCREEN_BIT) + t->pattern->type;
			if (t->pattern->type == vg_pattern_type_t::VG_PATTERN_TYPE_SURFACE) {
				auto tex = (sdl3gpu_texture*)t->pattern->data;
				if (tex)
				{
					binding.texture = tex->texture;
					binding.sampler = tex->sampler;
					pc.source.x;// 在vg设置了
					pc.source.y;
					pc.source.z = (float)fbo->width;
					pc.source.w = (float)fbo->height;
					glm::mat3x3 mat;
					if (t->pattern->hasMatrix) {
						mat = t->pattern->matrix;
						pc.matInv = pc.matInv * mat;
					}
				}
			}
		}
		if (t->pattern && t->pattern->data) {
			auto src = (vg_gradient_t*)t->pattern->data;
			grad_cache_key_t key;
			key.src = src;
			key.frame = (uint32_t)fbo->ctx->fxFrame;
			key.type = (int)t->pattern->type;
			key.hasMatrix = t->pattern->hasMatrix;
			key.patmat = t->pattern->hasMatrix ? t->pattern->matrix : glm::mat3x2(1.0f);
			key.vmat = pc.mat;
			if (fbo->ctx->gradDirty || !(fbo->ctx->gradKey == key)) {
				vg_gradient_t gr = *src;
				ovg_mul_pat(&gr, key.type, pc.mat, key.patmat, key.hasMatrix);
				fbo->ctx->gradKey = key;
				fbo->ctx->gradDirty = false;
				SDL_PushGPUFragmentUniformData(fbo->cmd, 0, &gr, sizeof(vg_gradient_t));
			}
		}
		else {
			// 纯色：零渐变与变换无关，键保持默认（除帧号外全零），同一帧内只推一次
			grad_cache_key_t key;
			key.frame = (uint32_t)fbo->ctx->fxFrame;
			if (fbo->ctx->gradDirty || !(fbo->ctx->gradKey == key)) {
				static const vg_gradient_t gr = {};
				fbo->ctx->gradKey = key;
				fbo->ctx->gradDirty = false;
				SDL_PushGPUFragmentUniformData(fbo->cmd, 0, &gr, sizeof(vg_gradient_t));
			}
		}
		SDL_PushGPUVertexUniformData(fbo->cmd, 0, &pc, sizeof(pc));
	}
	else {
		pc.mat = glm::mat3x2(1.0);
		glm::mat3x3 inv = pc.mat;
		pc.matInv = glm::inverse(inv);
		ovg_bind_vg_pipeline(fbo->ctx, fbo->cmd, pass, (int)vg_operator_t::VG_OPERATOR_SOURCE);
		SDL_PushGPUVertexUniformData(fbo->cmd, 0, &pc, sizeof(pc));
	}
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	switch (c->type) {
	case 0:
	case 1:
		draw_ct(pass, c);
		if (fx && fx->tex && fx->mask && fx->mode == 1)
			draw_effect_quad(fbo, pass, fx, cuclip);
		break;
	case 2:
	{
		const int bw = c->bounds.z;
		const int bh = c->bounds.w;
		if (bw != 0 && bh != 0) {
			*cuclip = set_scissor_sdl3(fbo, pass, (bw < 0 || bh < 0) ? nullptr : &c->bounds);
			break;
		}
		if (c->vertex.y > 0 || c->index.y > 0) {
#if defined(_DEBUG)
			SDL_PushGPUDebugGroup(fbo->cmd, "clip");
#endif 
			SDL_BindGPUGraphicsPipeline(pass, fbo->ctx->pipeClipping);
			SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);
			draw_ct(pass, c);
#if defined(_DEBUG)
			SDL_PopGPUDebugGroup(fbo->cmd);
#endif
		}
		else if (c->full_screen_quad >= 0) {
			SDL_BindGPUGraphicsPipeline(pass, fbo->ctx->pipeStencilClear);
			SDL_SetGPUStencilReference(pass, c->ref);
			cmd_draw_full_screen_quad_sdl3(fbo, pass, c, nullptr, 0, pc);
			SDL_SetGPUStencilReference(pass, STENCIL_CLIP_BIT);
		}
	}
	break;
	case 3:
	{
		cmd_draw_full_screen_quad_sdl3(fbo, pass, c, nullptr, 0, pc);
	}
	break;
	case 4:
	{
		//SDL_ClearGPUColorTarget(cmd, pass, &colorTarget);
		//SDL_ClearGPUDepthStencilTarget(cmd, pass, &depthStencilTarget);
	}
	break;
	}
}

void draw_geom(vg_fbo_t* fbo, SDL_GPURenderPass* pass, geom_cmd_t* c, const glm::uvec3& offset)
{
	if (!c)return;
	pipelinestate_p_internal* pipe = get_geom_pipeline(fbo->ctx, &c->state);
	if (pipe && pipe->pipeline) {
		SDL_BindGPUGraphicsPipeline(pass, pipe->pipeline);
	}
	bind_vbo_cached(fbo->ctx, pass, c->v_offset ? offset.y : offset.x);
	bind_ibo_cached(fbo->ctx, pass, offset.z);
	if (c->instance_count > 1)
		fbo->ctx->gpubuf->bindSSBO(pass, true);
	SDL_GPUTextureSamplerBinding binding = { .texture = fbo->ctx->device->emptyTexture->texture,	.sampler = fbo->ctx->device->emptyTexture->sampler, };
	if (c->texture) {
		auto texid = (vg_image_t*)c->texture;
		auto it = fbo->ctx->textures.find(texid->id);
		if (it != fbo->ctx->textures.end() && it->second)
		{
			binding.texture = it->second->texture;
			binding.sampler = it->second->sampler;
		}
	}
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	if (c->texture_mask) {
		auto tex = (sdl3gpu_texture*)c->texture_mask;
		if (tex)
		{
			binding.texture = tex->texture;
			binding.sampler = tex->sampler;
		}
		SDL_BindGPUFragmentSamplers(pass, 1, &binding, 1);
		SDL_PushGPUFragmentUniformData(fbo->cmd, 0, &c->mask_time, sizeof(float) * 2);
		fbo->ctx->gradDirty = true;   // slot0 已被掩码参数占用
	}
	struct PushConsts
	{
		glm::mat4 mvp;
		uint32_t instance_pos;
	};
	PushConsts pc = { c->mat, c->instance_ssbo_pos };
	SDL_PushGPUVertexUniformData(fbo->cmd, 0, &pc, sizeof(PushConsts));
	if (c->firstIndex >= 0)
	{
		SDL_DrawGPUIndexedPrimitives(pass, c->elemCount, c->instance_count > 1 ? c->instance_count : 1, c->firstIndex, c->vertexOffset, 0);
	}
	else
	{
		SDL_DrawGPUPrimitives(pass, c->elemCount, c->instance_count > 1 ? c->instance_count : 1, c->vertexOffset, 0);
	}
}

int ovg_get_window_swapchain(ovg_ctx_t* ctx, vg_fbo_t* fbo, int count, bool wait) {

	if (!ctx || !fbo || count < 1 || !(fbo->window || fbo->colorTex))return 0;
	if (ctx->currentCmdBuf) {
		SDL_SubmitGPUCommandBuffer(ctx->currentCmdBuf); ctx->currentCmdBuf = 0;
	}
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->device->gpuDevice);
	if (!cmd)return 0;
	int n = 0;
	uint32_t sw = 0, sh = 0;
	for (size_t i = 0; i < count; i++, fbo++)
	{
		SDL_GPUTexture* swapchain = NULL;
		if (fbo->window)
		{
			bool aq = wait ? SDL_WaitAndAcquireGPUSwapchainTexture(cmd, fbo->window, &swapchain, &sw, &sh) : SDL_AcquireGPUSwapchainTexture(cmd, fbo->window, &swapchain, &sw, &sh);
			fbo->display_size = { sw,sh };
			if (!aq || !sw || !sh || !swapchain) {
				continue;
			}
			if (fbo->width != sw || fbo->height != sh)
			{
				SDL_WaitForGPUSwapchain(ctx->device->gpuDevice, fbo->window);
				reset_vgfbo_sdl3(fbo, sw, sh);
			}
		}
		if (swapchain || fbo->colorTex)
			n++;
		fbo->swapchain = swapchain;
	}
	if (n < 1)
	{
		SDL_CancelGPUCommandBuffer(cmd);
		cmd = 0;
	}
	ctx->currentCmdBuf = cmd;
	return n;
}



/* 批量上传结果 */
typedef struct {
	uint32_t           submitted;  /* 实际提交的数量 */
	uint32_t           skipped;    /* 因 size==0 / 非法而跳过的数量 */
} upload_result_t;

/*
 * 批量上传：只创建一个 SDL_GPUTransferBuffer，所有纹理数据连续拷贝，
 * 用 offset 寻址，一次 CopyPass + 一次 Submit 全部推上去。
 *
 * 要求：
 *   - descs[i].pixels 必须在本次调用期间保持有效（在 Submit 之前）
 *   - 所有 dst_texture 属于同一个 SDL_GPUDevice
 */
upload_result_t upload_pixels_batched(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd, const upload_desc_t* descs, uint32_t count);

uint32_t format_bpp(vg_format_t fmt) {
	switch (fmt) {
	case VG_FORMAT_RGBA8:
	case VG_FORMAT_BGRA8:
	case VG_FORMAT_RGBA8_SRGB:
	case VG_FORMAT_BGRA8_SRGB:return 4;
	case VG_FORMAT_RGBA16F: return 8;   /* R16G16B16A16_FLOAT = 8 字节 */
	case VG_FORMAT_RGBA32F: return 16;  /* R32G32B32A32_FLOAT = 16 字节 */
	default: return 4;
	}
}

upload_result_t upload_pixels_batched(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd, const upload_desc_t* descs, uint32_t count)
{
	upload_result_t result = { 0, 0 };
	SDL_GPUDevice* device = ctx->device->gpuDevice;
	if (!device || !descs || count == 0) return result;

	const uint32_t align = 256;

	auto& items = ctx->_items;
	items.resize(count);

	uint32_t total_size = 0;

	/* ── 1. 预扫描：计算每张图的源偏移、buffer 布局、对齐 ── */
	for (uint32_t i = 0; i < count; i++) {
		const upload_desc_t* d = &descs[i];

		/* 非法条目跳过 */
		if (!d->pixels || !d->dst_texture || d->width == 0 || d->height == 0) {
			items[i].copy_h = 0;
			result.skipped++;
			continue;
		}

		uint32_t bpp = format_bpp(d->format);
		uint32_t stride = (d->stride != 0) ? d->stride : (d->width * bpp);

		/* 目标区域（纹理空间），0 表示整张 */
		uint32_t dst_x = d->x;
		uint32_t dst_y = d->y;
		uint32_t dst_w = (d->w != 0) ? d->w : d->width;
		uint32_t dst_h = (d->h != 0) ? d->h : d->height;

		/* 裁剪到纹理边界，防止越界 */
		if (dst_x + dst_w > d->width)  dst_w = d->width - dst_x;
		if (dst_y + dst_h > d->height) dst_h = d->height - dst_y;
		if (dst_w == 0 || dst_h == 0) {
			items[i].copy_h = 0;
			result.skipped++;
			continue;
		}

		bool is_full_upload = (dst_x == 0 && dst_y == 0 &&
			dst_w == d->width && dst_h == d->height);

		uint32_t dirty_pitch = dst_w * bpp;  /* 每行有效像素字节数 */

		if (is_full_upload) {
			/* ── 整图上传：紧密打包，pixels_per_row = 0 ── */
			items[i].src_offset = 0;
			items[i].gpu_offset = total_size;
			items[i].pixels_per_row = 0;          /* 驱动按 region.w 推导行距 */
			items[i].copy_h = d->height;
			items[i].dirty_w = d->width;
			items[i].dirty_h = d->height;
			items[i].bpp = bpp;
			items[i].stride = stride;
			items[i].row_pitch = d->width * bpp;
			items[i].gpu_row_bytes = d->width * bpp;  /* 紧密：行距 = 行宽 */
			total_size += d->width * bpp * d->height;  /* 紧密打包，只预留实际写入量（原 stride*height 会多算 padding） */
		}
		else {
			/* ── 脏矩形上传：buffer 每行对齐 256 字节 ── */
			uint32_t aligned_pitch = align_up(dirty_pitch, align);

			items[i].src_offset = dst_y * stride + dst_x * bpp;
			items[i].gpu_offset = total_size;
			items[i].pixels_per_row = aligned_pitch / bpp;  /* ← 关键：源 buffer 真实行像素宽 */
			items[i].copy_h = dst_h;
			items[i].dirty_w = dst_w;
			items[i].dirty_h = dst_h;
			items[i].bpp = bpp;
			items[i].stride = stride;
			items[i].row_pitch = dirty_pitch;          /* memcpy 只拷有效部分 */
			items[i].gpu_row_bytes = aligned_pitch;        /* buffer 行距 = 对齐后 */
			total_size += aligned_pitch * dst_h;
		}
	}

	if (total_size == 0) return result;

	/* ── 2. 创建唯一一个 transfer buffer ── */
	SDL_GPUTransferBufferCreateInfo ti = {
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = total_size,
	};
	SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device, &ti);
	if (!tb) return result;

	/* ── 3. 映射一次，逐张拷贝（各自按 gpu_offset / gpu_row_bytes 落位） ── */
	uint8_t* base = (uint8_t*)SDL_MapGPUTransferBuffer(device, tb, false);
	if (base) {
		for (uint32_t i = 0; i < count; i++) {
			if (items[i].copy_h == 0) continue;

			const upload_desc_t* d = &descs[i];
			const ovg_ctx_t::per_item_t* it = &items[i];

			uint8_t* src = (uint8_t*)d->pixels + it->src_offset;   /* 原始纹理脏区起点 */
			uint8_t* dst = base + it->gpu_offset;                    /* buffer 脏区起点 */

			/* 逐行拷贝：源按 stride 跳行，目的按 gpu_row_bytes 跳行 */
			for (uint32_t row = 0; row < it->copy_h; row++) {
				SDL_memcpy(
					dst + row * it->gpu_row_bytes,
					src + row * it->stride,
					it->row_pitch
				);
			}
		}
		SDL_UnmapGPUTransferBuffer(device, tb);
	}

	/* ── 4. 一个 CopyPass，全部上传（offset 寻址） ── */
	bool own_cmd = (cmd == NULL);
	if (own_cmd) cmd = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);

	for (uint32_t i = 0; i < count; i++) {
		if (items[i].copy_h == 0) continue;

		const upload_desc_t* d = &descs[i];
		const ovg_ctx_t::per_item_t* it = &items[i];

		SDL_GPUTextureTransferInfo src = {
			.transfer_buffer = tb,
			.offset = it->gpu_offset,
			.pixels_per_row = it->pixels_per_row,   /* 0 或 aligned_pitch/bpp */
			.rows_per_layer = 0,
		};
		SDL_GPUTextureRegion dst = {
			.texture = d->dst_texture->texture,
			.x = d->x,  .y = d->y,  .z = 0,
			.w = it->dirty_w,
			.h = it->copy_h,
			.d = 1,
		};
		SDL_UploadToGPUTexture(cp, &src, &dst, false);
		result.submitted++;
	}

	SDL_EndGPUCopyPass(cp);
	if (own_cmd) SDL_SubmitGPUCommandBuffer(cmd);

	/* ── 5. 提交后释放 transfer buffer（GPU 已读完，引用计数回收） ── */
	SDL_ReleaseGPUTransferBuffer(device, tb);
	return result;
}
/* ---- 延迟释放 ---- */

void deferred_free_push(ovg_ctx_t* ctx, sdl3gpu_texture* tex) {
	ctx->texq.push(tex);
}

static void recycle_fx_sets(ovg_ctx_t* ctx);
static size_t fx_pending_bytes(const ovg_ctx_t* ctx);

void deferred_free_advance(ovg_ctx_t* r) {
	if (!r) return;
	if (r->texq.empty() && r->fxPending.empty()) return;
	// 依赖上一帧的 fence：未 signaled 则 GPU 可能仍在使用纹理
	if (r->tex_free_fence && !SDL_QueryGPUFence(r->device->gpuDevice, r->tex_free_fence))
	{
		// 特效纹理待回收集合超过预算时强制等待，避免 GPU 落后时显存逐帧增长。
		// 上限至少为上一帧工作集的 2 倍，避免大 ROI 场景每帧都等待。
		size_t cap = std::max(r->fxPendingBudget, r->fxPrevFrameBytes * 2);
		if (fx_pending_bytes(r) <= cap)
			return;
		SDL_GPUFence* fences[1] = { r->tex_free_fence };
		SDL_WaitForGPUFences(r->device->gpuDevice, true, fences, 1);
	}
	if (r->tex_free_fence) {
		SDL_ReleaseGPUFence(r->device->gpuDevice, r->tex_free_fence);
		r->tex_free_fence = nullptr;
	}
	for (; r->texq.size();)
	{
		auto p = r->texq.front();
		if (p && p->id > 0)
		{
			r->textures.erase(p->id);
			destroy_texture(p);
		}
		r->texq.pop();
	}
	recycle_fx_sets(r);
}

int build_devres(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd, ovg_draw_data_t* kd) {
	int errornum = 0;
	for (size_t y = 0; y < kd->pipeinfo_count; y++)
	{
		auto it = kd->pipeinfo + y;
		if (it)
		{
			pipelinestate_p_internal* p = get_geom_pipeline(ctx, it);
			if (!p) {
				errornum++;
			}
		}
	}
	auto& uploads = ctx->_uploads;
	uploads.clear();
	if (kd->image_desc_count > uploads.capacity()) {
		uploads.reserve(kd->image_desc_count);
	}
	for (size_t i = 0; i < kd->image_desc_count; i++)
	{
		auto it = kd->image_desc[i];
		auto tex = ctx->textures[it->img->id];
		// 原生纹理由 ovg_texture_fini_native 管理生命周期；image_update /
		// image_destroy 不得覆盖或释放调用方的 SDL_GPUTexture。
		if (tex && !tex->owns_texture)
		{
			it->is_destroy = false;
			it->dirty = false;
			continue;
		}
		if (tex && it->is_destroy && tex != ctx->device->emptyTexture)
		{
			ctx->texq.push(tex);
			it->is_destroy = false;  // 只入队一次，避免每帧重复销毁
			it->dirty = false;
			it->img->id = 0;         // 句柄失效，等待下一次 image_update 重建
			continue;
		}
		if (it->img->id == 0)tex = 0;
		if (!tex && !it->dirty)continue;   // 无纹理且无更新（销毁后的残留项）→ 跳过
		if (!tex) {
			tex = new_texture_def(ctx, it->width, it->height, it->format);
			if (tex)
			{
				create_sampler(tex, SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_NEAREST, SDL_GPU_SAMPLERMIPMAPMODE_NEAREST, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
				it->img->id = ctx->next_image_id++;
				tex->id = it->img->id;
				ctx->textures[it->img->id] = tex;
			}
			else {
				it->img->id = 0;
			}
		}
		if (!tex || !it->dirty)continue;   // 纹理已存在且本帧无更新 → 跳过上传
		upload_desc_t d = {};
		d.pixels = (void*)it->pixels;     /* CPU 像素数据，调用方持有生命周期 */
		d.width = it->width;
		d.height = it->height;
		d.stride = it->stride;     /* 每行字节数（0 = 按 bpp * width 自动算） */
		d.format = (vg_format_t)it->format;     /* 像素格式 */
		d.dst_texture = tex;/* 目标 GPU 纹理 */
		d.x = it->x, d.y = it->y;       /* 目标区域起点（纹理空间） */
		d.w = it->w, d.h = it->h;       /* 目标区域尺寸（0 = 整张） */
		d.is_copy_rect = !it->is_copy;
		d.img = it->img;
		uploads.push_back(d);
		it->dirty = false;   // 已排队上传，清除脏标记
	}
	upload_result_t hr = upload_pixels_batched(ctx, cmd, uploads.data(), uploads.size());
	return errornum;
}

// ========================================================================
// 矢量特效：掩码 -> 模糊 -> 合成
// ========================================================================
struct fx_blur_params_t {
	glm::vec2 texel;
	float sigma;
	int axis;
};

static size_t fx_format_bytes(SDL_GPUTextureFormat f)
{
	switch (f) {
	case SDL_GPU_TEXTUREFORMAT_R8_UNORM: return 1;
	case SDL_GPU_TEXTUREFORMAT_R16_FLOAT: return 2;
	case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return 4;
	case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT: return 8;
	case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT: return 16;
	default: return 4;
	}
}

static size_t fx_set_bytes(const ovg_ctx_t* ctx, int w, int h, bool need_blur)
{
	size_t px = (size_t)w * (size_t)h;
	size_t bytes = px * fx_format_bytes(ctx->fxMaskFormat);
	if (need_blur)
		bytes += px * fx_format_bytes(ctx->fxBlurFormat) * 2;
	return bytes;
}

static sdl3gpu_texture* new_effect_texture(ovg_ctx_t* ctx, int w, int h,
	SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage)
{
	return new_texture(ctx->device, format, w, h, usage);
}

static uint64_t fx_pool_key(int w, int h, bool need_blur)
{
	uint64_t key = ((uint64_t)(uint32_t)w << 32) | (uint32_t)h;
	if (need_blur) key |= (1ull << 63);
	return key;
}

static size_t fx_pool_bytes(const ovg_ctx_t* ctx)
{
	size_t total = 0;
	for (auto& kv : ctx->fxPool)
		for (auto& s : kv.second) total += s.bytes;
	return total;
}

static size_t fx_pending_bytes(const ovg_ctx_t* ctx)
{
	size_t total = 0;
	for (auto& s : ctx->fxPending) total += s.bytes;
	return total;
}

// 池超过预算时按 last_use 淘汰最久未使用的集合。
// keep_bytes/keep_sets：本帧工作集，避免把刚用到的纹理淘汰掉造成抖动。
// 预算只约束空闲缓存，允许本帧工作集暂时超过预算。
static void trim_fx_pool(ovg_ctx_t* ctx, size_t keep_bytes, size_t keep_sets)
{
	size_t total = fx_pool_bytes(ctx);
	size_t count = 0;
	for (auto& kv : ctx->fxPool) count += kv.second.size();
	while ((total > ctx->fxPoolBudget && total > keep_bytes) ||
		(count > FX_POOL_MAX_SETS && count > keep_sets)) {
		auto victim_it = ctx->fxPool.end();
		size_t victim_idx = 0;
		uint64_t oldest = UINT64_MAX;
		for (auto it = ctx->fxPool.begin(); it != ctx->fxPool.end(); ++it) {
			auto& list = it->second;
			for (size_t i = 0; i < list.size(); i++) {
				if (list[i].last_use < oldest) {
					oldest = list[i].last_use;
					victim_it = it;
					victim_idx = i;
				}
			}
		}
		if (victim_it == ctx->fxPool.end()) break;
		auto& list = victim_it->second;
		size_t victim_bytes = list[victim_idx].bytes;
		free_fx_set(list[victim_idx]);
		list[victim_idx] = list.back();
		list.pop_back();
		if (list.empty()) ctx->fxPool.erase(victim_it);
		total -= victim_bytes;
		count--;
	}
}

static fx_texture_set acquire_fx_set(ovg_ctx_t* ctx, int w, int h, bool need_blur)
{
	fx_texture_set s = {};
	uint64_t key = fx_pool_key(w, h, need_blur);
	auto it = ctx->fxPool.find(key);
	if (it != ctx->fxPool.end() && !it->second.empty()) {
		s = it->second.back();
		it->second.pop_back();
	}
	else {
		s.width = w;
		s.height = h;
		s.need_blur = need_blur;
		s.bytes = fx_set_bytes(ctx, w, h, need_blur);
		s.mask = new_effect_texture(ctx, w, h, ctx->fxMaskFormat,
			SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
		if (need_blur) {
			s.tmp = new_effect_texture(ctx, w, h, ctx->fxBlurFormat,
				SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
			s.out = new_effect_texture(ctx, w, h, ctx->fxBlurFormat,
				SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE);
		}
	}
	// 任一必需纹理创建失败（如显存不足）时释放已创建的纹理，避免泄漏
	bool ok = s.mask && s.mask->texture;
	if (need_blur)
		ok = ok && s.tmp && s.tmp->texture && s.out && s.out->texture;
	if (!ok) {
		free_fx_set(s);
		return fx_texture_set{};
	}
	s.last_use = ctx->fxFrame;
	ctx->fxUsed.push_back(s);
	ctx->fxFrameBytes += s.bytes;
	return s;
}

static void recycle_fx_sets(ovg_ctx_t* ctx)
{
	size_t used_bytes = 0;
	for (auto& s : ctx->fxPending) used_bytes += s.bytes;
	size_t used_sets = ctx->fxPending.size();
	for (auto& s : ctx->fxPending) {
		bool ok = s.mask && s.mask->texture;
		if (s.need_blur)
			ok = ok && s.tmp && s.tmp->texture && s.out && s.out->texture;
		if (ok) {
			s.last_use = ctx->fxFrame;
			ctx->fxPool[fx_pool_key(s.width, s.height, s.need_blur)].push_back(s);
		}
		else {
			free_fx_set(s);
		}
	}
	ctx->fxPending.clear();
	trim_fx_pool(ctx, used_bytes, used_sets);
}

static void render_effect_mask(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd,
	ovg_draw_data_t* kd, vgcmd_t* c, sdl3gpu_texture* mask,
	const glm::ivec4& roi)
{
	if (!ctx || !cmd || !kd || !c || !c->state || !mask || roi.z <= 0 || roi.w <= 0)
		return;
	SDL_GPUColorTargetInfo ct = {};
	ct.texture = mask->texture;
	ct.load_op = SDL_GPU_LOADOP_CLEAR;
	ct.store_op = SDL_GPU_STOREOP_STORE;
	ct.clear_color = { 0, 0, 0, 0 };
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
	if (!pass) return;

	// mask 纹理本身就是 ROI 大小：viewport 用 ROI 尺寸，几何平移到 ROI 原点
	set_viewport2d(pass, glm::ivec2(roi.z, roi.w));
	SDL_Rect sc = { 0, 0, roi.z, roi.w };
	SDL_SetGPUScissor(pass, &sc);
	SDL_BindGPUGraphicsPipeline(pass, ctx->pipeEffectMask);

	ctx->bind_dirty = true;
	bind_vbo_cached(ctx, pass, kd->vg_offset.x);
	bind_ibo_cached(ctx, pass, kd->vg_offset.y);

	push_constants_t pc = c->state->pushConsts;
	pc.size = { (float)roi.z, (float)roi.w };
	// 设备空间后平移 p' = p - roi.xy；平移位于 mat3x2 的第三列。
	pc.mat[2].x -= (float)roi.x;
	pc.mat[2].y -= (float)roi.y;
	pc.source = { 1, 1, 1, 1 };
	pc.fsq_patternType = VG_PATTERN_TYPE_SOLID;
	pc.opacity = 1.0f;
	SDL_PushGPUVertexUniformData(cmd, 0, &pc, sizeof(pc));

	if (c->index.y > 0)
		SDL_DrawGPUIndexedPrimitives(pass, c->index.y, 1, c->index.x, c->vertex.x, 0);
	else
		SDL_DrawGPUPrimitives(pass, c->vertex.y, 1, c->vertex.x, 0);

	SDL_EndGPURenderPass(pass);
	ctx->bind_dirty = true;
}

static void dispatch_fx_compute(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd,
	SDL_GPUComputePipeline* pipe, sdl3gpu_texture* dst,
	sdl3gpu_texture* src0, sdl3gpu_texture* src1,
	const void* params, uint32_t params_size, int w, int h)
{
	if (!ctx || !cmd || !pipe || !dst || !src0 || w <= 0 || h <= 0)
		return;
	compute_pass cp;
	cp.add_rw_texture(dst->texture, 0, 0, false);
	cp.begin(cmd);
	if (!cp.pass) return;
	cp.bind_pipe(pipe);
	cp.push_texture(src0->texture, ctx->fxSampler);
	if (src1) cp.push_texture(src1->texture, ctx->fxSampler);
	cp.push_uniformdata(0, params, params_size);
	cp.dispatch(glm::uvec3((uint32_t)((w + 7) / 8), (uint32_t)((h + 7) / 8), 1));
	cp.end();
}

static bool fx_roi_from_bounds(const vgcmd_t* c, const vg_fbo_t* fbo, glm::ivec4& roi)
{
	if (!c || !fbo || !(c->bounds.z > 0.0f) || !(c->bounds.w > 0.0f))
		return false;
	// 先在 float 域裁剪，避免负坐标/超大坐标直接转 int 溢出
	float fx0 = std::max(c->bounds.x, 0.0f);
	float fy0 = std::max(c->bounds.y, 0.0f);
	float fx1 = std::min(c->bounds.x + c->bounds.z, (float)fbo->width);
	float fy1 = std::min(c->bounds.y + c->bounds.w, (float)fbo->height);
	if (!(fx1 > fx0) || !(fy1 > fy0))
		return false;
	int x0 = (int)floorf(fx0);
	int y0 = (int)floorf(fy0);
	int x1 = (int)ceilf(fx1);
	int y1 = (int)ceilf(fy1);
	if (x1 <= x0 || y1 <= y0)
		return false;
	roi = glm::ivec4(x0, y0, x1 - x0, y1 - y0);
	return true;
}

static void prepare_effect(ovg_ctx_t* ctx, SDL_GPUCommandBuffer* cmd,
	ovg_draw_data_t* kd, vgcmd_t* c, vg_fbo_t* fbo, fx_result_t* out)
{
	if (!out) return;
	out->tex = nullptr;
	if (!ctx || !ctx->fxSupported || !cmd || !kd || !c || !c->state || !fbo)
		return;
	const vg_effect_t& e = c->state->effect;
	if (c->ref < 0 || e.type == VG_EFFECT_NONE || e.opacity <= 0.0f)
		return;
	if (((e.color >> 24) & 0xFF) == 0)
		return;
	if (c->index.y == 0 && c->vertex.y == 0)
		return;

	glm::ivec4 roi;
	if (!fx_roi_from_bounds(c, fbo, roi))
		return;
	const int w = roi.z;
	const int h = roi.w;
	if (w <= 0 || h <= 0) return;
	const bool need_blur = e.sigma > 0.01f;

	fx_texture_set set = acquire_fx_set(ctx, w, h, need_blur);
	if (!set.mask || !set.mask->texture) return;
	if (need_blur && (!set.tmp || !set.tmp->texture || !set.out || !set.out->texture)) return;

	ctx->fxStatEffects++;
	ctx->fxStatRoiPixels += (size_t)w * (size_t)h;
	render_effect_mask(ctx, cmd, kd, c, set.mask, roi);

	if (need_blur) {
		fx_blur_params_t bp = {};
		bp.texel = { 1.0f / w, 1.0f / h };
		bp.sigma = e.sigma;
		bp.axis = 0;
		dispatch_fx_compute(ctx, cmd, ctx->device->comp_pipe[0], set.tmp,
			set.mask, nullptr, &bp, sizeof(bp), w, h);
		bp.axis = 1;
		dispatch_fx_compute(ctx, cmd, ctx->device->comp_pipe[0], set.out,
			set.tmp, nullptr, &bp, sizeof(bp), w, h);
	}

	sdl3gpu_texture* blurred = need_blur ? set.out : set.mask;
	out->tex = blurred;
	out->mask = set.mask;
	out->rect = glm::vec4((float)roi.x, (float)roi.y, (float)w, (float)h);
	out->color = glm::vec4(
		(float)(e.color & 0xFF) / 255.0f,
		(float)((e.color >> 8) & 0xFF) / 255.0f,
		(float)((e.color >> 16) & 0xFF) / 255.0f,
		(float)((e.color >> 24) & 0xFF) / 255.0f);
	out->offset = e.offset;
	out->spread = e.spread;
	out->opacity = e.opacity;
	out->mode = (e.type == VG_EFFECT_OUTER_GLOW) ? 0 :
		(e.type == VG_EFFECT_INNER_GLOW) ? 1 : 2;
}

void ovg_render_frame(ovg_ctx_t* ctx, ovg_draw_data_t* data, size_t count)
{
	if (!ctx /*|| !fbo */ || !data || !count)return;

	auto cmd = ctx->currentCmdBuf;
	if (!cmd) {
		cmd = SDL_AcquireGPUCommandBuffer(ctx->device->gpuDevice);

	}
	if (!cmd)return;
	ctx->fxFrame++;
	ctx->fxPrevFrameBytes = ctx->fxFrameBytes;
	ctx->fxFrameBytes = 0;
	ctx->fxStatEffects = 0;
	ctx->fxStatRoiPixels = 0;
	deferred_free_advance(ctx);   // 帧首尝试释放：此时 fence 为上一帧所提交，可能已完成
	ctx->fxUsed.clear();
	size_t total_vbo = 0;
	size_t total_ibo = 0;
	size_t total_ssbo = 0;
	int errornum = 0;
	auto& tfb = ctx->_fbo_data;
	tfb.reserve(count); tfb.clear();
	for (size_t i = 0; i < count; i++) {
		auto kd = &data[i];
		if (!kd->fbo || !(kd->fbo->window || kd->fbo->colorTex))continue;
		render_item_t item;
		item.data = *kd;
		tfb.push_back(std::move(item));
		total_vbo += data[i].v_count * sizeof(ovgVertex);
		total_vbo += data[i].v1_count * sizeof(geomVertex1);
		total_vbo += data[i].v2_count * sizeof(geomVertex2);

		total_ibo += data[i].i_count * sizeof(uint32_t);
		total_ibo += data[i].ig_count * sizeof(uint32_t);
		total_ssbo += data[i].instance_count * sizeof(glm::mat4);
		errornum += build_devres(ctx, cmd, kd);
	}
	if (tfb.empty()) {
		SDL_CancelGPUCommandBuffer(cmd);
		ctx->currentCmdBuf = nullptr;
		return;
	}
	if (errornum)
		SDL_LogError(SDL_LOG_CATEGORY_GPU, "ovg_render_frame: %d device resource(s) failed to build", errornum);
	ctx->currentCmdBuf = cmd;
	ctx->gpubuf->begin(total_vbo, total_ibo, total_ssbo);
	// 按 fbo 首次出现顺序聚组，减少 render pass 切换（不依赖指针值大小）
	{
		std::unordered_map<vg_fbo_t*, size_t> fbo_order;
		for (auto& it : tfb)
			fbo_order.emplace(it.data.fbo, fbo_order.size());
		std::stable_sort(tfb.begin(), tfb.end(), [&fbo_order](const render_item_t& a, const render_item_t& b) {
			return fbo_order[a.data.fbo] < fbo_order[b.data.fbo];
		});
	}
	for (size_t i = 0; i < tfb.size(); i++) {
		auto kd = &tfb[i].data;
		kd->vg_offset.x = ctx->gpubuf->add_vbo(kd->vg_vertex, kd->v_count * sizeof(ovgVertex));
		kd->vg_offset.y = ctx->gpubuf->add_ibo(kd->vg_indices, kd->i_count * sizeof(uint32_t));
		kd->geom_offset.x = ctx->gpubuf->add_vbo(kd->vertex1, kd->v1_count * sizeof(geomVertex1));
		kd->geom_offset.y = ctx->gpubuf->add_vbo(kd->vertex2, kd->v2_count * sizeof(geomVertex2));
		kd->geom_offset.z = ctx->gpubuf->add_ibo(kd->geom_indices, kd->ig_count * sizeof(uint32_t));
		ctx->gpubuf->add_ssbo(kd->instance_data, kd->instance_count * sizeof(glm::mat4));
	}
	ctx->gpubuf->end(cmd);

	// 主 RenderPass 之前先完成所有特效的离屏掩码/模糊/合成
	if (ctx->fxSupported) {
		for (size_t d = 0; d < tfb.size(); d++) {
			auto& item = tfb[d];
			auto kd = &item.data;
			int maxslot = -1;
			for (size_t i = 0; i < kd->count; i++) {
				auto& it = kd->d[i];
				if ((cmd_type_e)it.g.stype != cmd_type_e::DRAW_VG) continue;
				auto& cc = it.vg;
				if ((cc.type == 0 || cc.type == 1) && cc.state &&
					cc.state->effect.type != VG_EFFECT_NONE && cc.ref >= 0)
					maxslot = std::max(maxslot, (int)cc.ref);
			}
			if (maxslot < 0) continue;
			item.fx.assign((size_t)maxslot + 1, fx_result_t{});
			for (size_t i = 0; i < kd->count; i++) {
				auto& it = kd->d[i];
				if ((cmd_type_e)it.g.stype != cmd_type_e::DRAW_VG) continue;
				auto& cc = it.vg;
				if ((cc.type == 0 || cc.type == 1) && cc.state &&
					cc.state->effect.type != VG_EFFECT_NONE && cc.ref >= 0)
					prepare_effect(ctx, cmd, kd, &cc, kd->fbo, &item.fx[(size_t)cc.ref]);
			}
		}
	}
	vg_fbo_t* current_fbo = nullptr;

	SDL_Rect cuClip = {};
	for (size_t d = 0; d < tfb.size(); d++)
	{
		auto& item = tfb[d];
		auto kd = &item.data;
		vg_fbo_t* fbo = kd->fbo;
		if (fbo != current_fbo) {
			if (current_fbo) {
				ovg_end_pass(ctx, current_fbo);
			}
			current_fbo = fbo;
			ovg_begin_pass(ctx, fbo, true);
		}
		SDL_GPURenderPass* pass = ctx->pass;
		if (!pass) continue;
		for (size_t i = 0; i < kd->count; i++)
		{
			auto& it = kd->d[i];
			switch ((cmd_type_e)it.g.stype) {
			case cmd_type_e::DRAW_VG:
			{
				const fx_result_t* fx = nullptr;
				if (it.vg.state && it.vg.state->effect.type != VG_EFFECT_NONE &&
					it.vg.ref >= 0 && (size_t)it.vg.ref < item.fx.size())
					fx = &item.fx[(size_t)it.vg.ref];
				draw_vg(fbo, pass, &it.vg, &cuClip, kd->vg_offset, fx);
			}
			break;
			case cmd_type_e::DRAW_GEOM:
			{
				draw_geom(fbo, pass, &it.g, kd->geom_offset);
			}
			break;
			}
		}
	}
	if (current_fbo) {
		ovg_end_pass(ctx, current_fbo);
	}
	if (ctx->tex_free_fence) {
		SDL_ReleaseGPUFence(ctx->device->gpuDevice, ctx->tex_free_fence);
		ctx->tex_free_fence = nullptr;
	}
	ctx->tex_free_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	ctx->currentCmdBuf = 0;
	for (auto& s : ctx->fxUsed)
		ctx->fxPending.push_back(s);
	ctx->fxUsed.clear();
}

void ovg_fx_get_stats(ovg_ctx_t* ctx, ovg_fx_stats_t* out)
{
	if (!out) return;
	*out = {};
	if (!ctx) return;
	size_t pool_sets = 0;
	for (auto& kv : ctx->fxPool) pool_sets += kv.second.size();
	out->pool_sets = pool_sets;
	out->pending_sets = ctx->fxPending.size();
	out->pool_bytes = fx_pool_bytes(ctx);
	out->pending_bytes = fx_pending_bytes(ctx);
	out->frame_effects = ctx->fxStatEffects;
	out->frame_roi_pixels = ctx->fxStatRoiPixels;
	out->frame_bytes = ctx->fxFrameBytes;
}

void ovg_fx_set_budget(ovg_ctx_t* ctx, size_t pool_bytes, size_t pending_bytes)
{
	if (!ctx) return;
	// 0 表示恢复默认；下限 1MB，避免误设成 0 后每帧强制等待
	ctx->fxPoolBudget = pool_bytes ? std::max<size_t>(pool_bytes, 1ull << 20) : FX_POOL_MAX_BYTES;
	ctx->fxPendingBudget = pending_bytes ? std::max<size_t>(pending_bytes, 1ull << 20) : FX_PENDING_MAX_BYTES;
}

sdl3gpu_texture* new_texture_def(ovg_ctx_t* ctx, int w, int h, uint32_t format)
{
	SDL_GPUTextureFormat f = vg_to_sdl_format((vg_format_t)format);
	auto p = new_texture(ctx->device, f, w, h, 0);
	return p;
}

bool ovg_texture_init_native(ovg_ctx_t* ctx, ovg_texture_t* out,
	SDL_GPUTexture* texture, int width, int height, vg_format_t format)
{
	if (!ctx || !ctx->device || !out || !texture || width <= 0 || height <= 0)
		return false;
	if (out->image.id != 0)
		return false;
	const SDL_GPUTextureFormat fmt = vg_to_sdl_format(format);
	if (fmt == SDL_GPU_TEXTUREFORMAT_INVALID)
		return false;

	auto* tex = new sdl3gpu_texture();
	if (!tex)
		return false;
	tex->texture = texture;
	tex->device = ctx->device->gpuDevice;
	tex->format = fmt;
	tex->width = width;
	tex->height = height;
	tex->references = 1;
	tex->owns_texture = false;
	create_sampler(tex, SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
		SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE);
	if (!tex->sampler)
	{
		delete tex;
		return false;
	}

	uint32_t id = ctx->next_image_id++;
	if (id == 0)
		id = ctx->next_image_id++;
	tex->id = id;
	ctx->textures[id] = tex;

	out->image.id = id;
	out->image.width = width;
	out->image.height = height;
	out->image.valid = false;
	out->image.copy_status = false;
	return true;
}

bool ovg_texture_update_native(ovg_ctx_t* ctx, ovg_texture_t* tex,
	SDL_GPUTexture* texture, int width, int height, vg_format_t format)
{
	if (!ctx || !tex || tex->image.id == 0 || !texture || width <= 0 || height <= 0)
		return false;
	auto it = ctx->textures.find(tex->image.id);
	if (it == ctx->textures.end() || !it->second)
		return false;
	sdl3gpu_texture* wrap = it->second;
	if (wrap->owns_texture)
		return false;
	const SDL_GPUTextureFormat fmt = vg_to_sdl_format(format);
	if (fmt == SDL_GPU_TEXTUREFORMAT_INVALID)
		return false;

	wrap->texture = texture;
	wrap->format = fmt;
	wrap->width = width;
	wrap->height = height;
	tex->image.width = width;
	tex->image.height = height;
	tex->image.valid = false;
	return true;
}

void ovg_texture_fini_native(ovg_ctx_t* ctx, ovg_texture_t* tex)
{
	if (!ctx || !tex || tex->image.id == 0)
		return;
	auto it = ctx->textures.find(tex->image.id);
	if (it != ctx->textures.end())
	{
		sdl3gpu_texture* wrap = it->second;
		ctx->textures.erase(it);
		if (wrap)
			ctx->texq.push(wrap);
	}
	tex->image.id = 0;
	tex->image.width = 0;
	tex->image.height = 0;
	tex->image.valid = false;
}

bool vg_sdl3_init(ovg_sdl3_ctx* g, int width, int height, bool is_vulkan) {
	SDL_Init(SDL_INIT_VIDEO);

	SDL_PropertiesID props = SDL_CreateProperties();
#if OVG_HAS_VULKAN_HEADERS
	if (is_vulkan)
	{
		SDL_GPUVulkanOptions vo = {};
		VkPhysicalDeviceScalarBlockLayoutFeatures scalarFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
			.pNext = NULL,
			.scalarBlockLayout = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features enabledFeatures12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

		enabledFeatures12.scalarBlockLayout = VK_TRUE;
		// 1b. Vulkan 1.1 复合特性（包含 shaderDrawParameters） 
		VkPhysicalDeviceVulkan11Features vk11Features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,	   .pNext = &enabledFeatures12, };
		// 其他 1.1 特性默认由驱动填充，我们只关心 shaderDrawParameters
		vk11Features.shaderDrawParameters = VK_TRUE;
		// 以下字段留 0，让 SDL/Vulkan 使用默认值
		vk11Features.storageBuffer16BitAccess = VK_FALSE;
		vk11Features.uniformAndStorageBuffer16BitAccess = VK_FALSE;
		vk11Features.storagePushConstant16 = VK_FALSE;
		vk11Features.storageInputOutput16 = VK_FALSE;
		vk11Features.multiview = VK_FALSE;
		vk11Features.multiviewGeometryShader = VK_FALSE;
		vk11Features.multiviewTessellationShader = VK_FALSE;
		vk11Features.variablePointersStorageBuffer = VK_FALSE;
		vk11Features.variablePointers = VK_FALSE;
		vk11Features.protectedMemory = VK_FALSE;
		vk11Features.samplerYcbcrConversion = VK_TRUE;
		const char* devext[] = { VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
				VK_KHR_MAINTENANCE_5_EXTENSION_NAME,VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME };
		const char* insext[] = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
		// 2. 填充 SDL_GPUVulkanOptions
		SDL_GPUVulkanOptions vkOpts = {
			.vulkan_api_version = VK_API_VERSION_1_2,  // 必须 >= 1.2 才能启用 scalarBlockLayout
			.feature_list = &vk11Features,             // pNext 链头
			.vulkan_10_physical_device_features = NULL, // 不需要额外 1.0 特性
			.device_extension_count = 3,
			.device_extension_names = devext,
			.instance_extension_count = 1,
			.instance_extension_names = insext,
		};
		SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vkOpts);
		SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	}
#endif // OVG_HAS_VULKAN_HEADERS
	bool debugmode = false;
#ifdef _DEBUG
	debugmode = true;
#endif // _DEBUG

	// 3. 通过属性创建 GPU 设备
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugmode);

	SDL_GPUDevice* device = g->device = SDL_CreateGPUDeviceWithProperties(props);
	SDL_DestroyProperties(props);
	if (!g->device) {
		SDL_Log("GPU device create failed: %s", SDL_GetError());
		return false;
	}
	g->window = SDL_CreateWindow("SDL3 GPU Vector Graphics",
		width, height,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN |
		SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!g->window) return false;
	SDL_ClaimWindowForGPUDevice(g->device, g->window);
	SDL_ShowWindow(g->window);
	return true;
}

