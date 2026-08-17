#pragma once

#include <SDL3/SDL_gpu.h>
#include <string>
#include <cstdint>
#include "filter_params.h"

// ============================================================
// FilterPipeline
// 封装 SDL_GPU 计算着色器管线的创建、参数上传与调度
// ============================================================

class FilterPipeline {
public:
    FilterPipeline(SDL_GPUDevice* device);
    ~FilterPipeline();

    // 禁止拷贝
    FilterPipeline(const FilterPipeline&) = delete;
    FilterPipeline& operator=(const FilterPipeline&) = delete;

    // ---- 初始化 ----
    bool initialize(
        const std::string& blurHCSPath,   // 水平模糊计算着色器
        const std::string& blurVCSPath,   // 垂直模糊计算着色器
        const std::string& colorAdjCSPath // 色彩调整计算着色器
    );

    // ---- 纹理管理 ----
    // 创建用于滤镜处理的 RGBA8 纹理
    SDL_GPUTexture* createTexture(int width, int height);
    void destroyTexture(SDL_GPUTexture* tex);

    // 从 CPU 像素数据上传到 GPU 纹理
    bool uploadPixels(SDL_GPUTexture* texture, const void* pixels, int width, int height);

    // 从 GPU 纹理读回到 CPU（用于保存 PNG）
    bool downloadPixels(SDL_GPUTexture* texture, void* outPixels, int width, int height);

    // ---- 参数设置 ----
    void setParams(const FilterParams& params) { m_params = params; }

    // ---- 核心：执行全套滤镜 ----
    // input   : 原始输入纹理
    // output  : 最终输出纹理
    // tempA   : 中间纹理（水平模糊结果）
    // tempB   : 中间纹理（垂直模糊结果）
    // 如果 enableBlur=0，则跳过模糊 Pass，直接从 input 做色彩调整
    bool execute(
        SDL_GPUTexture* input,
        SDL_GPUTexture* output,
        SDL_GPUTexture* tempA,
        SDL_GPUTexture* tempB
    );

    // ---- 单 Pass 快速版（使用 filter_all_in_one.slang） ----
    bool executeAllInOne(SDL_GPUTexture* input, SDL_GPUTexture* output);

    // ---- 获取默认参数（方便外部修改后回传） ----
    FilterParams& params() { return m_params; }

private:
    SDL_GPUDevice* m_device = nullptr;

    // 三个计算管线
    SDL_GPUComputePipeline* m_blurHPipeline   = nullptr;
    SDL_GPUComputePipeline* m_blurVPipeline   = nullptr;
    SDL_GPUComputePipeline* m_colorAdjPipeline = nullptr;
    SDL_GPUComputePipeline* m_allInOnePipeline = nullptr;

    // Uniform / Constant Buffer
    SDL_GPUBuffer* m_uniformBuffer = nullptr;

    FilterParams m_params;

    // ---- 内部方法 ----
    SDL_GPUComputePipeline* createComputePipeline(const std::string& csCodePath);
    bool createUniformBuffer();
    bool updateUniformBuffer();
    bool dispatchCompute(
        SDL_GPUComputePipeline* pipeline,
        SDL_GPUTexture* input,
        SDL_GPUTexture* output
    );
};
