#include "filter_pipeline.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_filesystem.h>
#include <vector>
#include <cstring>

// ============================================================
// 辅助：读取文件到字节数组
// ============================================================
static std::vector<uint8_t> readFile(const std::string& path) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Failed to open file: %s (%s)", path.c_str(), SDL_GetError());
        return {};
    }
    // 获取文件大小
    int64_t size = SDL_GetIOSize(io);
    if (size <= 0) {
        SDL_CloseIO(io);
        return {};
    }
    std::vector<uint8_t> data((size_t)size);
    size_t read = SDL_ReadIO(io, data.data(), (size_t)size);
    SDL_CloseIO(io);
    if ((int64_t)read != size) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Failed to read file: %s", path.c_str());
        return {};
    }
    return data;
}

// ============================================================
// 构造 / 析构
// ============================================================
FilterPipeline::FilterPipeline(SDL_GPUDevice* device)
    : m_device(device) {}

FilterPipeline::~FilterPipeline() {
    if (m_blurHPipeline)    SDL_ReleaseGPUComputePipeline(m_device, m_blurHPipeline);
    if (m_blurVPipeline)    SDL_ReleaseGPUComputePipeline(m_device, m_blurVPipeline);
    if (m_colorAdjPipeline) SDL_ReleaseGPUComputePipeline(m_device, m_colorAdjPipeline);
    if (m_allInOnePipeline) SDL_ReleaseGPUComputePipeline(m_device, m_allInOnePipeline);
    if (m_uniformBuffer)    SDL_ReleaseGPUBuffer(m_device, m_uniformBuffer);
}

// ============================================================
// 初始化：编译/加载三个计算着色器 + 创建 Uniform Buffer
// ============================================================
bool FilterPipeline::initialize(
    const std::string& blurHCSPath,
    const std::string& blurVCSPath,
    const std::string& colorAdjCSPath)
{
    m_blurHPipeline    = createComputePipeline(blurHCSPath);
    m_blurVPipeline    = createComputePipeline(blurVCSPath);
    m_colorAdjPipeline = createComputePipeline(colorAdjCSPath);

    if (!m_blurHPipeline || !m_blurVPipeline || !m_colorAdjPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create compute pipelines");
        return false;
    }

    return createUniformBuffer();
}

// ============================================================
// 创建计算管线（从编译好的 DXIL/SPIR-V 文件）
// ============================================================
SDL_GPUComputePipeline* FilterPipeline::createComputePipeline(const std::string& csCodePath) {
    auto code = readFile(csCodePath);
    if (code.empty()) return nullptr;

    SDL_GPUShaderCreateInfo shaderInfo = {};
    shaderInfo.code_size   = code.size();
    shaderInfo.code        = code.data();
    shaderInfo.entrypoint  = "cs_main";
    shaderInfo.format      = SDL_GPU_SHADERFORMAT_DXIL; // 默认 DXIL；SDL 会自动检测
    shaderInfo.stage       = SDL_GPU_SHADERSTAGE_COMPUTE;
    shaderInfo.num_samplers = 1;
    shaderInfo.num_storage_textures = 1;
    shaderInfo.num_storage_buffers  = 0;
    shaderInfo.num_uniform_buffers  = 1;

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &shaderInfo);
    if (!shader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateGPUShader failed for %s: %s",
            csCodePath.c_str(), SDL_GetError());
        return nullptr;
    }

    SDL_GPUComputePipelineCreateInfo pipeInfo = {};
    pipeInfo.shader               = shader;
    pipeInfo.num_samplers         = 1;
    pipeInfo.num_readonly_storage_textures = 1;
    pipeInfo.num_readonly_storage_buffers  = 0;
    pipeInfo.num_write_storage_textures    = 1;
    pipeInfo.num_write_storage_buffers     = 0;
    pipeInfo.num_uniform_buffers          = 1;

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(m_device, &pipeInfo);
    SDL_ReleaseGPUShader(m_device, shader); // pipeline 持有引用

    if (!pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateGPUComputePipeline failed for %s: %s",
            csCodePath.c_str(), SDL_GetError());
    }
    return pipeline;
}

// ============================================================
// 创建 Uniform (Constant) Buffer
// ============================================================
bool FilterPipeline::createUniformBuffer() {
    SDL_GPUBufferCreateInfo bufInfo = {};
    bufInfo.usage   = SDL_GPU_BUFFERUSAGE_GRAPHICS;
    bufInfo.size    = sizeof(FilterParams);
    m_uniformBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
    if (!m_uniformBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Failed to create uniform buffer: %s", SDL_GetError());
        return false;
    }
    return true;
}

// ============================================================
// 上传参数到 GPU
// ============================================================
bool FilterPipeline::updateUniformBuffer() {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return false;

    SDL_GPUTransferBufferCreateInfo transInfo = {};
    transInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transInfo.size  = sizeof(FilterParams);
    SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(m_device, &transInfo);
    if (!staging) { SDL_SubmitGPUCommandBuffer(cmd); return false; }

    void* ptr = SDL_MapGPUTransferBuffer(m_device, staging, false);
    memcpy(ptr, &m_params, sizeof(FilterParams));
    SDL_UnmapGPUTransferBuffer(m_device, staging);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = staging;
    src.offset = 0;
    SDL_GPUBufferLocation dst = {};
    dst.buffer = m_uniformBuffer;
    dst.offset = 0;
    SDL_UploadToGPUBuffer(copyPass, &src, &dst, sizeof(FilterParams), false);
    SDL_EndGPUCopyPass(copyPass);

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_device, staging);
    return true;
}

// ============================================================
// 创建纹理
// ============================================================
SDL_GPUTexture* FilterPipeline::createTexture(int width, int height) {
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type           = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format         = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.width          = (uint32_t)width;
    texInfo.height         = (uint32_t)height;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels     = 1;
    texInfo.usage          = SDL_GPU_TEXTUREUSAGE_SAMPLER
                           | SDL_GPU_TEXTUREUSAGE_COMPUTE
                           | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                           | SDL_GPU_TEXTUREUSAGE_TRANSFER_SRC
                           | SDL_GPU_TEXTUREUSAGE_TRANSFER_DST;
    return SDL_CreateGPUTexture(m_device, &texInfo);
}

void FilterPipeline::destroyTexture(SDL_GPUTexture* tex) {
    if (tex) SDL_ReleaseGPUTexture(m_device, tex);
}

// ============================================================
// 上传像素数据到纹理
// ============================================================
bool FilterPipeline::uploadPixels(SDL_GPUTexture* texture, const void* pixels, int width, int height) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return false;

    uint32_t byteSize = (uint32_t)(width * height * 4);
    SDL_GPUTransferBufferCreateInfo transInfo = {};
    transInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transInfo.size  = byteSize;
    SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(m_device, &transInfo);
    if (!staging) { SDL_SubmitGPUCommandBuffer(cmd); return false; }

    void* ptr = SDL_MapGPUTransferBuffer(m_device, staging, false);
    memcpy(ptr, pixels, byteSize);
    SDL_UnmapGPUTransferBuffer(m_device, staging);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = staging;
    src.offset = 0;
    SDL_GPUTextureTransferInfo dst = {};
    dst.texture = texture;
    dst.offset = 0;
    dst.w_pitch = (uint32_t)width;  // w_pitch in pixels for RGBA8
    dst.h_pitch = (uint32_t)height;
    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
    SDL_EndGPUCopyPass(copyPass);

    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(m_device, staging);
    return true;
}

// ============================================================
// 从纹理下载像素数据到 CPU
// ============================================================
bool FilterPipeline::downloadPixels(SDL_GPUTexture* texture, void* outPixels, int width, int height) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return false;

    uint32_t byteSize = (uint32_t)(width * height * 4);
    SDL_GPUTransferBufferCreateInfo transInfo = {};
    transInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transInfo.size  = byteSize;
    SDL_GPUTransferBuffer* staging = SDL_CreateGPUTransferBuffer(m_device, &transInfo);
    if (!staging) { SDL_SubmitGPUCommandBuffer(cmd); return false; }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {};
    src.texture = texture;
    src.offset = 0;
    src.w_pitch = (uint32_t)width;  // w_pitch in pixels for RGBA8
    src.h_pitch = (uint32_t)height;
    SDL_GPUTransferBufferLocation dst = {};
    dst.transfer_buffer = staging;
    dst.offset = 0;
    SDL_DownloadFromGPUTexture(copyPass, &src, &dst);
    SDL_EndGPUCopyPass(copyPass);

    SDL_SubmitGPUCommandBuffer(cmd);

    // 等待 GPU 完成
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(m_device, false, &fence, 1);
    SDL_ReleaseGPUFence(m_device, fence);

    void* ptr = SDL_MapGPUTransferBuffer(m_device, staging, true);
    memcpy(outPixels, ptr, byteSize);
    SDL_UnmapGPUTransferBuffer(m_device, staging);
    SDL_ReleaseGPUTransferBuffer(m_device, staging);
    return true;
}

// ============================================================
// 调度一次计算着色器
// ============================================================
bool FilterPipeline::dispatchCompute(
    SDL_GPUComputePipeline* pipeline,
    SDL_GPUTexture* input,
    SDL_GPUTexture* output)
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m_device);
    if (!cmd) return false;

    // 设置计算 Pass 的存储纹理读写列表
    SDL_GPUStorageTextureReadWriteBinding rwBindings[2] = {};
    rwBindings[0].texture = input;
    rwBindings[0].stage_flags = SDL_GPU_SHADERSTAGE_COMPUTE;
    rwBindings[1].texture = output;
    rwBindings[1].stage_flags = SDL_GPU_SHADERSTAGE_COMPUTE;

    SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(
        cmd, rwBindings, 2);

    SDL_BindGPUComputePipeline(cmd, pipeline);

    // 绑定 Uniform Buffer 到 slot 0
    SDL_BindGPUComputeUniformBuffers(cmd, 0, &m_uniformBuffer, 1);

    // 绑定只读存储纹理（输入）到 slot 0
    SDL_BindGPUComputeStorageTextures(cmd, 0, &input, 1);

    // 绑定读写存储纹理（输出）到 slot 1
    SDL_BindGPUComputeStorageTextures(cmd, 1, &output, 1);

    // 计算线程组数量（8x8 线程组）
    int w = (int)m_params.textureSizeX;
    int h = (int)m_params.textureSizeY;
    int groupsX = (w + 7) / 8;
    int groupsY = (h + 7) / 8;

    SDL_DispatchGPUCompute(cmd, (uint32_t)groupsX, (uint32_t)groupsY, 1);

    SDL_EndGPUComputePass(computePass);
    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

// ============================================================
// 执行全套滤镜（三 Pass）
// ============================================================
bool FilterPipeline::execute(
    SDL_GPUTexture* input,
    SDL_GPUTexture* output,
    SDL_GPUTexture* tempA,
    SDL_GPUTexture* tempB)
{
    if (!updateUniformBuffer()) return false;

    if (m_params.enableBlur) {
        // Pass 1: 水平模糊 input → tempA
        if (!dispatchCompute(m_blurHPipeline, input, tempA)) return false;
        // Pass 2: 垂直模糊 tempA → tempB
        if (!dispatchCompute(m_blurVPipeline, tempA, tempB)) return false;
        // Pass 3: 色彩调整 tempB → output
        if (!dispatchCompute(m_colorAdjPipeline, tempB, output)) return false;
    } else {
        // 跳过模糊，直接色彩调整 input → output
        if (!dispatchCompute(m_colorAdjPipeline, input, output)) return false;
    }
    return true;
}

// ============================================================
// 单 Pass 快速版
// ============================================================
bool FilterPipeline::executeAllInOne(SDL_GPUTexture* input, SDL_GPUTexture* output) {
    if (!m_allInOnePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "All-in-one pipeline not initialized. Call initialize() with all_in_one shader.");
        return false;
    }
    if (!updateUniformBuffer()) return false;
    return dispatchCompute(m_allInOnePipeline, input, output);
}
