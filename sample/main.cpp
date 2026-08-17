/* ============================================================
// main.cpp
// SDL_GPU + Slang 计算着色器 —— CSS Filter 完整示例
//
// 功能：
//   - 加载图片到 GPU 纹理
//   - 通过 SDL_GPU 计算着色器执行全套 CSS filter
//   - 将结果渲染到屏幕
//   - 实时调整参数（键盘交互）
//   - 保存结果到 PNG
//
// 编译后的着色器路径通过命令行参数或硬编码指定
sdl3 gpu规则
set0 vert 纹理
set1 vert ubo
set2 frag 纹理
set3 frag ubo
// ============================================================*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <chrono>

#include "filter_pipeline.h"
#include "filter_params.h"
#include "image_loader.h"

// ============================================================
// 配置
// ============================================================
struct AppConfig {
    int   windowWidth  = 1280;
    int   windowHeight = 720;
    const char* title  = "SDL_GPU CSS Filter Demo";
};

// ============================================================
// 全局状态
// ============================================================
struct AppState {
    SDL_Window*     window      = nullptr;
    SDL_GPUDevice*  device      = nullptr;
    SDL_GPUSwapchainComposition swapchainFmt = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;

    // 滤镜管线
    FilterPipeline*  filter      = nullptr;

    // 纹理
    SDL_GPUTexture*  inputTex    = nullptr;
    SDL_GPUTexture*  tempTexA   = nullptr;
    SDL_GPUTexture*  tempTexB   = nullptr;
    SDL_GPUTexture*  outputTex  = nullptr;

    // 图形管线（用于把 outputTex 画到屏幕）
    SDL_GPUGraphicsPipeline* blitPipeline = nullptr;
    SDL_GPUSampler*          linearSampler = nullptr;

    // 参数
    FilterParams    params;
    bool            needsRecompute = true;

    // 图片信息
    int             imgWidth  = 0;
    int             imgHeight = 0;

    // 退出标志
    bool            quit = false;

    // 全屏 / 窗口
    bool            fullscreen = false;
};

// ============================================================
// 创建用于渲染到屏幕的 Graphics Pipeline
// 使用预编译的 blit_vs / blit_fs 着色器
// ============================================================
static bool createBlitPipeline(AppState& app, const std::string& shaderDir) {
    SDL_GPUDevice* device = app.device;

    // 检测着色器文件扩展名
    const char* ext = "dxil";
    SDL_GPUShaderFormat fmt = SDL_GPU_SHADERFORMAT_DXIL;
#if defined(__linux__) || defined(__APPLE__)
    ext = "spv";
    fmt = SDL_GPU_SHADERFORMAT_SPIRV;
#endif

    auto readBytes = [](const std::string& path) -> std::vector<uint8_t> {
        SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
        if (!io) return {};
        int64_t sz = SDL_GetIOSize(io);
        if (sz <= 0) { SDL_CloseIO(io); return {}; }
        std::vector<uint8_t> buf((size_t)sz);
        SDL_ReadIO(io, buf.data(), (size_t)sz);
        SDL_CloseIO(io);
        return buf;
    };

    // 加载 VS
    std::string vsPath = shaderDir + "/blit_vs." + ext;
    auto vsCode = readBytes(vsPath);
    if (vsCode.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load %s", vsPath.c_str());
        return false;
    }

    SDL_GPUShaderCreateInfo vsInfo = {};
    vsInfo.code_size   = vsCode.size();
    vsInfo.code        = vsCode.data();
    vsInfo.entrypoint  = "main";
    vsInfo.format      = fmt;
    vsInfo.stage       = SDL_GPU_SHADERSTAGE_VERTEX;
    vsInfo.num_samplers = 0;
    vsInfo.num_storage_textures = 0;
    vsInfo.num_storage_buffers  = 0;
    vsInfo.num_uniform_buffers  = 0;
    SDL_GPUShader* vs = SDL_CreateGPUShader(device, &vsInfo);
    if (!vs) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CreateGPUShader(VS) failed: %s", SDL_GetError());
        return false;
    }

    // 加载 FS
    std::string fsPath = shaderDir + "/blit_fs." + ext;
    auto fsCode = readBytes(fsPath);
    if (fsCode.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load %s", fsPath.c_str());
        SDL_ReleaseGPUShader(device, vs);
        return false;
    }

    SDL_GPUShaderCreateInfo fsInfo = {};
    fsInfo.code_size   = fsCode.size();
    fsInfo.code        = fsCode.data();
    fsInfo.entrypoint  = "main";
    fsInfo.format      = fmt;
    fsInfo.stage       = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fsInfo.num_samplers = 1;
    fsInfo.num_storage_textures = 0;
    fsInfo.num_storage_buffers  = 0;
    fsInfo.num_uniform_buffers  = 0;
    SDL_GPUShader* fs = SDL_CreateGPUShader(device, &fsInfo);
    if (!fs) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CreateGPUShader(FS) failed: %s", SDL_GetError());
        SDL_ReleaseGPUShader(device, vs);
        return false;
    }

    // 创建图形管线
    SDL_GPUColorAttachmentDescription colorAtt = {};
    colorAtt.format            = (SDL_GPUTextureFormat)app.swapchainFmt;
    colorAtt.blend_state = {};
    colorAtt.blend_state.enable_blend = false;

    SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {};
    pipeInfo.vertex_shader   = vs;
    pipeInfo.fragment_shader = fs;
    pipeInfo.primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeInfo.depth_stencil_state.enable_depth_test = false;
    pipeInfo.depth_stencil_state.enable_depth_write = false;
    pipeInfo.num_color_attachments = 1;
    pipeInfo.color_attachments    = &colorAtt;
    pipeInfo.target_info = {};
    pipeInfo.target_info.num_color_attachments = 1;
    pipeInfo.target_info.color_attachment_formats = &colorAtt.format;

    app.blitPipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeInfo);
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);

    if (!app.blitPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    // 创建线性采样器
    SDL_GPUSamplerCreateInfo sampInfo = {};
    sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    app.linearSampler = SDL_CreateGPUSampler(device, &sampInfo);

    SDL_Log("Blit pipeline created successfully");
    return app.linearSampler != nullptr;
}

// ============================================================
// 初始化 SDL_GPU
// ============================================================
static bool initGPU(AppState& app, const AppConfig& cfg) {
    // 创建 GPU 设备（优先 Vulkan，其次 D3D12，再 Metal）
    SDL_GPUShaderFormat formats =
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXIL  |
        SDL_GPU_SHADERFORMAT_MSL;

    app.device = SDL_CreateGPUDevice(formats, true, nullptr);
    if (!app.device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // 创建窗口
    app.window = SDL_CreateWindow(
        cfg.title,
        cfg.windowWidth,
        cfg.windowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!app.window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // 关联窗口与 GPU 设备
    if (!SDL_ClaimWindowForGPUDevice(app.device, app.window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    // 查询交换链格式
    app.swapchainFmt = SDL_GetGPUSwapchainTextureFormat(app.device, app.window);
    SDL_Log("Swapchain format: %d", (int)app.swapchainFmt);

    return true;
}

// ============================================================
// 加载图片并上传到 GPU
// ============================================================
static bool loadImage(AppState& app, const std::string& path) {
    ImageData img;
    if (!ImageLoader::load(path, img)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load image: %s", path.c_str());
        return false;
    }

    app.imgWidth  = img.width;
    app.imgHeight = img.height;

    // 设置参数中的纹理尺寸
    app.params.setTextureSize(img.width, img.height);

    // 创建输入纹理
    app.inputTex = app.filter->createTexture(img.width, img.height);
    if (!app.inputTex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create input texture");
        return false;
    }

    // 上传像素
    if (!app.filter->uploadPixels(app.inputTex, img.pixels.data(), img.width, img.height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload pixels");
        return false;
    }

    // 创建中间纹理和输出纹理
    app.tempTexA = app.filter->createTexture(img.width, img.height);
    app.tempTexB = app.filter->createTexture(img.width, img.height);
    app.outputTex = app.filter->createTexture(img.width, img.height);

    if (!app.tempTexA || !app.tempTexB || !app.outputTex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create intermediate textures");
        return false;
    }

    SDL_Log("Image loaded: %dx%d, %d channels", img.width, img.height, img.channels);
    return true;
}

// ============================================================
// 执行滤镜计算
// ============================================================
static void runFilters(AppState& app) {
    if (!app.needsRecompute) return;

    auto start = std::chrono::high_resolution_clock::now();

    app.filter->setParams(app.params);
    bool ok = app.filter->execute(
        app.inputTex,
        app.outputTex,
        app.tempTexA,
        app.tempTexB
    );

    auto end = std::chrono::high_resolution_clock::now();
    auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    if (ok) {
        SDL_Log("Filter executed in %lld us (%.2f ms)", us, us / 1000.0f);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Filter execution failed");
    }

    app.needsRecompute = false;
}

// ============================================================
// 渲染到屏幕（全屏 blit outputTex → swapchain）
// ============================================================
static void render(AppState& app) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(app.device);
    if (!cmd) return;

    SDL_GPUTexture* swapchain = nullptr;
    uint32_t w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, app.window, &swapchain, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (!swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    if (app.blitPipeline && app.linearSampler) {
        // ---- Graphics Pass: 全屏三角形 blit ----
        SDL_GPUColorAttachment att = {};
        att.texture     = swapchain;
        att.clear_color = { 0.08f, 0.08f, 0.12f, 1.0f };
        att.load_op     = SDL_GPU_LOADOP_CLEAR;
        att.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPUGraphicsPass* gfxPass = SDL_BeginGPUGraphicsPass(cmd, &att, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(cmd, app.blitPipeline);

        // 绑定 outputTex 到 t0
        SDL_BindGPUFragmentSamplers(cmd, 0, app.linearSampler, 1);
        // 注意：SDL_GPU 中纹理绑定需要通过 SDL_BindGPUFragmentStorageTextures
        // 或设置 pipeline 的 layout。这里用最简方式：
        SDL_BindGPUFragmentStorageTextures(cmd, 0, &app.outputTex, 1);

        SDL_DrawGPUPrimitives(cmd, 3, 1, 0, 0);
        SDL_EndGPUGraphicsPass(gfxPass);
    } else {
        // ---- Fallback: 直接 Copy ----
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);

        // 计算等比缩放
        float scale = fminf((float)w / (float)app.imgWidth, (float)h / (float)app.imgHeight);
        int dstW = (int)((float)app.imgWidth * scale);
        int dstH = (int)((float)app.imgHeight * scale);

        SDL_GPUTextureTransferInfo src = {};
        src.texture  = app.outputTex;
        src.offset   = 0;
        src.w_pitch  = (uint32_t)app.imgWidth;  // pixels, not bytes
        src.h_pitch  = (uint32_t)app.imgHeight;

        SDL_GPUTextureTransferInfo dst = {};
        dst.texture  = swapchain;
        dst.offset   = 0;
        dst.w_pitch  = (uint32_t)dstW;  // pixels
        dst.h_pitch  = (uint32_t)dstH;

        SDL_CopyGPUTextureToTexture(copyPass, &src, &dst, false);
        SDL_EndGPUCopyPass(copyPass);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
}

// ============================================================
// 保存输出纹理为 PNG
// ============================================================
static void saveOutput(AppState& app, const std::string& path) {
    std::vector<uint8_t> pixels(app.imgWidth * app.imgHeight * 4);
    if (app.filter->downloadPixels(app.outputTex, pixels.data(), app.imgWidth, app.imgHeight)) {
        ImageData img;
        img.width    = app.imgWidth;
        img.height   = app.imgHeight;
        img.channels = 4;
        img.pixels   = std::move(pixels);
        if (ImageLoader::savePNG(path, img)) {
            SDL_Log("Saved output to %s", path.c_str());
        }
    }
}

// ============================================================
// 打印当前参数状态
// ============================================================
static void printParams(const FilterParams& p) {
    SDL_Log("--- Filter Params ---");
    SDL_Log("  blur:       %s (radius=%.1f)", p.enableBlur ? "ON" : "OFF", p.blurRadius);
    SDL_Log("  brightness: %.2f", p.brightness);
    SDL_Log("  contrast:   %.2f", p.contrast);
    SDL_Log("  grayscale:  %.2f", p.grayscaleAmount);
    SDL_Log("  invert:     %.2f", p.invertAmount);
    SDL_Log("  opacity:    %.2f", p.opacity);
    SDL_Log("  saturate:   %.2f", p.saturateAmount);
    SDL_Log("  sepia:      %.2f", p.sepiaAmount);
    SDL_Log("---------------------");
}

// ============================================================
// 键盘事件处理
// ============================================================
static void handleKeyEvent(AppState& app, SDL_KeyboardEvent* ev) {
    if (ev->type != SDL_EVENT_KEY_DOWN) return;

    bool ctrl = (ev->mod & SDL_KMOD_CTRL) != 0;
    int scancode = ev->scancode;

    switch (scancode) {
        // ---- Blur ----
        case SDL_SCANCODE_B:
            app.params.enableBlur = !app.params.enableBlur;
            SDL_Log("Blur: %s", app.params.enableBlur ? "ON" : "OFF");
            break;
        case SDL_SCANCODE_UP:
            if (app.params.enableBlur) {
                app.params.blurRadius = fminf(app.params.blurRadius + 1.0f, 16.0f);
                SDL_Log("Blur radius: %.0f", app.params.blurRadius);
            }
            break;
        case SDL_SCANCODE_DOWN:
            if (app.params.enableBlur) {
                app.params.blurRadius = fmaxf(app.params.blurRadius - 1.0f, 0.0f);
                SDL_Log("Blur radius: %.0f", app.params.blurRadius);
            }
            break;

        // ---- Grayscale ----
        case SDL_SCANCODE_G:
            app.params.grayscaleAmount = app.params.grayscaleAmount > 0 ? 0.0f : 1.0f;
            SDL_Log("Grayscale: %s", app.params.grayscaleAmount > 0 ? "ON" : "OFF");
            break;

        // ---- Invert ----
        case SDL_SCANCODE_I:
            app.params.invertAmount = app.params.invertAmount > 0 ? 0.0f : 1.0f;
            SDL_Log("Invert: %s", app.params.invertAmount > 0 ? "ON" : "OFF");
            break;

        // ---- Sepia ----
        case SDL_SCANCODE_P:
            if (ctrl) {
                saveOutput(app, "output.png");
            } else {
                app.params.sepiaAmount = app.params.sepiaAmount > 0 ? 0.0f : 1.0f;
                SDL_Log("Sepia: %s", app.params.sepiaAmount > 0 ? "ON" : "OFF");
            }
            break;

        // ---- Contrast (+/-) ----
        case SDL_SCANCODE_C:
            app.params.contrast = (app.params.contrast == 1.0f) ? 1.5f : 1.0f;
            SDL_Log("Contrast: %.2f", app.params.contrast);
            break;

        // ---- Saturate (+/-) ----
        case SDL_SCANCODE_S:
            app.params.saturateAmount = (app.params.saturateAmount == 1.0f) ? 2.0f : 1.0f;
            SDL_Log("Saturate: %.2f", app.params.saturateAmount);
            break;

        // ---- Opacity ----
        case SDL_SCANCODE_O:
            app.params.opacity = (app.params.opacity == 1.0f) ? 0.5f : 1.0f;
            SDL_Log("Opacity: %.2f", app.params.opacity);
            break;

        // ---- Brightness ----
        case SDL_SCANCODE_L:
            app.params.brightness = (app.params.brightness == 1.0f) ? 1.3f : 1.0f;
            SDL_Log("Brightness: %.2f", app.params.brightness);
            break;

        // ---- Reset ----
        case SDL_SCANCODE_R:
            app.params.reset();
            SDL_Log("Params reset to default");
            break;

        // ---- Print ----
        case SDL_SCANCODE_Q:
            printParams(app.params);
            break;

        // ---- Quit ----
        case SDL_SCANCODE_ESCAPE:
            app.quit = true;
            break;

        // ---- Fullscreen ----
        case SDL_SCANCODE_F:
            app.fullscreen = !app.fullscreen;
            SDL_SetWindowFullscreen(app.window, app.fullscreen);
            break;
    }
    app.needsRecompute = true;
}

// ============================================================
// 主循环
// ============================================================
static void mainLoop(AppState& app) {
    SDL_Event event;
    while (!app.quit) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    app.quit = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    handleKeyEvent(app, &event.key);
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    SDL_Log("Window resized: %dx%d", event.window.data1, event.window.data2);
                    break;
            }
        }

        // 执行滤镜（如果参数变化）
        runFilters(app);

        // 渲染到屏幕
        render(app);

        // 简单帧率控制
        SDL_Delay(16); // ~60 FPS
    }
}

// ============================================================
// 清理资源
// ============================================================
static void cleanup(AppState& app) {
    if (app.filter) {
        delete app.filter;
        app.filter = nullptr;
    }
    if (app.inputTex)  app.filter->destroyTexture(app.inputTex);
    if (app.tempTexA)  app.filter->destroyTexture(app.tempTexA);
    if (app.tempTexB)  app.filter->destroyTexture(app.tempTexB);
    if (app.outputTex) app.filter->destroyTexture(app.outputTex);
    if (app.linearSampler) SDL_ReleaseGPUSampler(app.device, app.linearSampler);
    if (app.blitPipeline)  SDL_ReleaseGPUGraphicsPipeline(app.device, app.blitPipeline);
    if (app.window) {
        SDL_ReleaseWindowFromGPUDevice(app.device, app.window);
        SDL_DestroyWindow(app.window);
    }
    if (app.device) SDL_DestroyGPUDevice(app.device);
}

// ============================================================
// 打印使用说明
// ============================================================
static void printUsage(const char* progName) {
    printf("\n");
    printf("SDL_GPU CSS Filter Demo\n");
    printf("=======================\n");
    printf("Usage: %s <input_image> [options]\n\n", progName);
    printf("Options:\n");
    printf("  --shader-dir <path>    Directory containing compiled .dxil/.spv shaders\n");
    printf("  --all-in-one           Use single-pass filter (lower quality, faster)\n");
    printf("  --help                 Show this help\n\n");
    printf("Controls:\n");
    printf("  B       Toggle Blur          I  Toggle Invert\n");
    printf("  UP/DOWN Adjust Blur Radius   S  Toggle Saturate (1x/2x)\n");
    printf("  G       Toggle Grayscale     O  Toggle Opacity (100%%/50%%)\n");
    printf("  P       Toggle Sepia        Ctrl+P Save PNG\n");
    printf("  C       Toggle Contrast     L  Toggle Brightness\n");
    printf("  R       Reset all           Q  Print params\n");
    printf("  F       Fullscreen          Esc Quit\n\n");
}

// ============================================================
// main
// ============================================================
int main(int argc, char* argv[]) {
    // 解析命令行
    std::string imagePath;
    std::string shaderDir = "build";
    bool useAllInOne = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--shader-dir" && i + 1 < argc) {
            shaderDir = argv[++i];
        } else if (arg == "--all-in-one") {
            useAllInOne = true;
        } else if (arg[0] != '-') {
            imagePath = arg;
        }
    }

    if (imagePath.empty()) {
        printUsage(argv[0]);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No input image specified!");
        return 1;
    }

    //SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    AppConfig  cfg;
    AppState   app;

    // 1. 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // 2. 初始化 GPU
    if (!initGPU(app, cfg)) {
        SDL_Quit();
        return 1;
    }

    // 3. 创建滤镜管线
    app.filter = new FilterPipeline(app.device);
    std::string blurHPath = shaderDir + "/blur_h.dxil";
    std::string blurVPath = shaderDir + "/blur_v.dxil";
    std::string colorAdjPath = shaderDir + "/color.dxil";
    std::string allInOnePath = shaderDir + "/filter_all_in_one.dxil";

    if (useAllInOne) {
        // 单 Pass 模式只需要 all_in_one
        if (!app.filter->initialize(allInOnePath, allInOnePath, allInOnePath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init filter pipeline (all-in-one)");
            cleanup(app);
            SDL_Quit();
            return 1;
        }
    } else {
        if (!app.filter->initialize(blurHPath, blurVPath, colorAdjPath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to init filter pipeline");
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Make sure shaders are compiled in: %s", shaderDir.c_str());
            cleanup(app);
            SDL_Quit();
            return 1;
        }
    }

    // 4. 创建 Blit 管线（渲染到屏幕用）
    if (!createBlitPipeline(app, shaderDir)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Blit pipeline creation failed, will use copy pass only");
    }

    // 5. 加载图片
    if (!loadImage(app, imagePath)) {
        cleanup(app);
        SDL_Quit();
        return 1;
    }

    // 6. 初始参数
    app.params.reset();
    app.params.enableBlur = 1;
    app.params.blurRadius = 5.0f;
    app.needsRecompute = true;

    SDL_Log("========================================");
    SDL_Log("  SDL_GPU CSS Filter Demo");
    SDL_Log("  Image: %s (%dx%d)", imagePath.c_str(), app.imgWidth, app.imgHeight);
    SDL_Log("  Mode:  %s", useAllInOne ? "All-in-One (single pass)" : "Multi-Pass (separable blur)");
    SDL_Log("  Press 'H' in-app for controls, 'Q' to print params");
    SDL_Log("========================================");

    // 7. 进入主循环
    mainLoop(app);

    // 8. 清理
    cleanup(app);
    SDL_Quit();

    SDL_Log("Goodbye!");
    return 0;
}
