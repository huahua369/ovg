/*
 * ovg_example_sdl3.cpp
 * SDL3 GPU 矢量渲染后端使用示例
 *
 * 展示完整的初始化、渲染循环和清理流程
 */

#include "ovg_renderer_sdl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstring>

// ========================================================================
// 示例：初始化 SDL3 + GPU 设备
// ========================================================================
static SDL_GPUDevice* init_sdl3_gpu() {
    // 优先选择 Vulkan 后端（支持 SPIR-V 着色器）
    SDL_GPUShaderFormat preferredFormats =
        SDL_GPU_SHADERFORMAT_SPIRV;

    SDL_GPUDevice* gpuDevice = SDL_CreateGPUDevice(
        preferredFormats,
        true,   // debug mode
        "vg-renderer"  // app name
    );

    if (!gpuDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create GPU device: %s", SDL_GetError());
        return nullptr;
    }

    // 检查 SPIR-V 支持
    SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(gpuDevice);
    if (!(supported & SDL_GPU_SHADERFORMAT_SPIRV)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SPIR-V not supported! Falling back may be needed.");
        // 可以在这里尝试 DXIL 或 MSL
    }

    return gpuDevice;
}

// ========================================================================
// 示例：创建窗口和交换链
// ========================================================================
struct WindowContext {
    SDL_Window*      window    = nullptr;
    SDL_GPUDevice*   gpuDevice = nullptr;
    SDL_GPUTexture*  swapchain = nullptr;
    uint32_t         swapchainWidth  = 0;
    uint32_t         swapchainHeight = 0;
};

static bool acquire_swapchain(WindowContext* wc) {
    // 获取当前交换链纹理
    wc->swapchain = SDL_AcquireGPUSwapchainTexture(
        wc->gpuDevice, wc->window,
        &wc->swapchainWidth, &wc->swapchainHeight
    );
    return wc->swapchain != nullptr;
}

// ========================================================================
// 示例：绘制一个时钟（使用 OVG API）
// ========================================================================
// 注意：这里展示的是如何使用 SDL3 GPU 后端的 OVG 命令
// 实际 OVG 命令录制由 ovg_renderer_sdl3.cpp 内部处理

struct ClockDrawData {
    float cx, cy, radius;
    int   hour, minute, second;
};

static void draw_clock_vg_commands(
    ovg_ctx_t*        ctx,
    SDL_GPUCommandBuffer* cmdBuf,
    SDL_GPURenderPass*   pass,
    const ClockDrawData& clock)
{
    // 在这个 renderPass 内，OVG 内部会发出：
    // - bind pipeline (pipeOVER for normal alpha blending)
    // - bind vertex/index buffers
    // - set viewport & scissor
    // - draw calls for each path

    // 实际使用中，你需要：
    // 1. 用 OVG 的 path API 构建路径
    // 2. 调用 fill() / stroke()
    // 3. OVG 内部录制 Vulkan/SDL3 GPU 命令
    // 4. 这里只是示意

    (void)ctx; (void)cmdBuf; (void)pass; (void)clock;

    /*
    // 伪代码 - 实际 OVG 用法：
    ovg_new_path(ctx);
    ovg_circle(ctx, clock.cx, clock.cy, clock.radius);
    ovg_set_source_color(ctx, 0xFFFFFFFF);  // 白色
    ovg_fill(ctx);

    // 刻度
    for (int i = 0; i < 60; i++) {
        float angle = i * (2.0f * M_PI / 60.0f) - M_PI / 2.0f;
        float r1 = (i % 5 == 0) ? clock.radius * 0.76f : clock.radius * 0.83f;
        float r2 = (i % 5 == 0) ? clock.radius * 0.89f : clock.radius * 0.88f;
        float x1 = clock.cx + r1 * cosf(angle);
        float y1 = clock.cy + r1 * sinf(angle);
        float x2 = clock.cx + r2 * cosf(angle);
        float y2 = clock.cy + r2 * sinf(angle);

        ovg_new_path(ctx);
        ovg_move_to(ctx, x1, y1);
        ovg_line_to(ctx, x2, y2);
        ovg_set_line_width(ctx, (i % 5 == 0) ? 2.0f : 1.0f);
        ovg_set_source_color(ctx, (i % 5 == 0) ? 0xFF000000 : 0x88000000);
        ovg_stroke(ctx);
    }

    // 时针
    float hr_ang = (clock.hour % 12) * 30.0f + clock.minute * 0.5f;
    hr_ang = glm::radians(hr_ang - 90.0f);
    ovg_save(ctx);
    ovg_translate(ctx, clock.cx, clock.cy);
    ovg_rotate(ctx, hr_ang);
    ovg_rounded_rectangle(ctx, -3, -clock.radius * 0.48f, 6, clock.radius * 0.48f, 2);
    ovg_set_source_color(ctx, 0xFF000000);
    ovg_fill(ctx);
    ovg_restore(ctx);

    // 分针
    float min_ang = clock.minute * 6.0f + clock.second * 0.1f;
    min_ang = glm::radians(min_ang - 90.0f);
    ovg_save(ctx);
    ovg_translate(ctx, clock.cx, clock.cy);
    ovg_rotate(ctx, min_ang);
    ovg_rounded_rectangle(ctx, -2, -clock.radius * 0.68f, 4, clock.radius * 0.68f, 1.5f);
    ovg_set_source_color(ctx, 0xFF000000);
    ovg_fill(ctx);
    ovg_restore(ctx);

    // 秒针
    float sec_ang = clock.second * 6.0f;
    sec_ang = glm::radians(sec_ang - 90.0f);
    ovg_save(ctx);
    ovg_translate(ctx, clock.cx, clock.cy);
    ovg_rotate(ctx, sec_ang);
    ovg_rounded_rectangle(ctx, -1, -clock.radius * 0.80f, 2, clock.radius * 0.80f, 1);
    ovg_set_source_color(ctx, 0xFFFF0000);  // 红色
    ovg_fill(ctx);
    ovg_restore(ctx);
    */
}

// ========================================================================
// 主渲染循环
// ========================================================================
static void render_frame(
    ovg_device_t*  ovgDev,
    ovg_ctx_t*     ovgCtx,
    WindowContext* wc,
    const ClockDrawData& clock)
{
    // 1. 获取交换链（需要在 Acquire 之前）
    if (!acquire_swapchain(wc)) return;

    // 2. 创建 FBO 包装器
    vg_fbo_t fbo = {};
    fbo.width  = wc->swapchainWidth;
    fbo.height = wc->swapchainHeight;
    fbo.colorTex = (void*)wc->swapchain;
    fbo.colorTexMS = nullptr;
    fbo.depthStencilTex = nullptr;
    fbo.hasStencil = false;

    // 3. 开始 OVG 帧 → 内部 Acquire CommandBuffer + BeginRenderPass
    SDL_GPUCommandBuffer* cmdBuf = ovg_begin_frame(ovgCtx, &fbo, true);

    if (cmdBuf) {
        // 4. ✅ 获取当前活跃的 RenderPass
        SDL_GPURenderPass* pass = ovg_get_current_render_pass(ovgCtx);

        if (pass) {
            // 5. 在 RenderPass 内录制所有绘制命令
            // draw_clock_vg_commands(ovgCtx, cmdBuf, pass, clock);

            // 示例：绑定管道并绘制
            // ovg_bind_vg_pipeline(ovgCtx, cmdBuf, pass, 0);  // OVER
            // ovg_draw_indexed(ovgCtx, cmdBuf, pass, indexCount, 0, 0);
        }

        // 6. 结束帧 → 内部 EndRenderPass + MSAA Resolve + Submit
        ovg_end_frame(ovgCtx, cmdBuf, &fbo);
    }
}

// ========================================================================
// 完整示例 main
// ========================================================================
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GPU)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    // 创建 GPU 设备
    SDL_GPUDevice* gpuDevice = init_sdl3_gpu();
    if (!gpuDevice) {
        SDL_Quit();
        return -1;
    }

    // 创建窗口
    SDL_Window* window = SDL_CreateWindow(
        "OVG SDL3 GPU Clock",
        800, 600,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window creation failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_Quit();
        return -1;
    }

    // 关联窗口和 GPU 设备
    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Claim window failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_Quit();
        return -1;
    }

    // 创建 OVG 设备上下文
    SDL_GPUCommandBuffer* initCmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
    ovg_device_t* ovgDev = new_sdl3gpu_device(gpuDevice, initCmd);
    if (!ovgDev) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create OVG device");
        SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_Quit();
        return -1;
    }

    // 创建 OVG 渲染上下文
    // 使用 4x MSAA 提升质量
    ovg_ctx_t* ovgCtx = new_ovgctx_sdl3(
        ovgDev,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        SDL_GPU_SAMPLECOUNT_4
    );
    if (!ovgCtx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create OVG context");
        free_sdl3gpu_device(ovgDev);
        SDL_Quit();
        return -1;
    }

    // 窗口上下文
    WindowContext wc = {};
    wc.window    = window;
    wc.gpuDevice = gpuDevice;

    // 时钟数据
    ClockDrawData clock = {};
    clock.cx = 400.0f;
    clock.cy = 300.0f;
    clock.radius = 200.0f;
    clock.hour   = 10;
    clock.minute = 30;
    clock.second = 45;

    // 主循环
    bool running = true;
    SDL_Event event;
    Uint64 lastTime = SDL_GetTicks();

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }

        // 更新时间
        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - lastTime) / 1000.0f;
        lastTime = now;

        clock.second = (int)(now / 1000) % 60;
        clock.minute = ((int)(now / 60000)) % 60;
        clock.hour   = (((int)(now / 3600000)) % 12);

        // 渲染帧
        render_frame(ovgDev, ovgCtx, &wc, clock);

        // 控制帧率
        SDL_Delay(16);  // ~60 FPS
    }

    // 等待 GPU 空闲
    SDL_WaitForGPUIdle(gpuDevice);

    // 清理
    free_ovgctx_sdl3(ovgCtx);
    free_sdl3gpu_device(ovgDev);

    SDL_ReleaseWindowFromGPUDevice(gpuDevice, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(gpuDevice);
    SDL_Quit();

    return 0;
}
