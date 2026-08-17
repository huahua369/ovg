#pragma once

#include <cstdint>

// ============================================================
// FilterParams —— 必须与 shaders/filter_common.slangh 中的
// ConstantBuffer<FilterParams> 内存布局完全一致！
// ============================================================
// Slang 侧：
//   float2 texelSize;    // offset  0, size 8
//   float2 textureSize;  // offset  8, size 8
//   float  blurRadius;   // offset 16, size 4
//   int    enableBlur;   // offset 20, size 4
//   int    _pad0;        // offset 24, size 4
//   int    _pad1;        // offset 28, size 4
//   float  brightness;   // offset 32, size 4
//   float  contrast;     // offset 36, size 4
//   float  grayscaleAmount; // offset 40, size 4
//   float  invertAmount;    // offset 44, size 4
//   float  opacity;       // offset 48, size 4
//   float  saturateAmount; // offset 52, size 4
//   float  sepiaAmount;    // offset 56, size 4
//   int    enableColorAdjust; // offset 60, size 4
// Total: 64 bytes, aligned to 16
// ============================================================

struct FilterParams {
    // ---- 纹理信息 (16 bytes) ----
    float texelSizeX      = 0.0f;
    float texelSizeY      = 0.0f;
    float textureSizeX     = 0.0f;
    float textureSizeY     = 0.0f;

    // ---- Blur (16 bytes) ----
    float blurRadius       = 0.0f;   // 0 = 不模糊
    int32_t enableBlur     = 0;
    int32_t _pad0          = 0;
    int32_t _pad1          = 0;

    // ---- Color Adjust (32 bytes) ----
    float brightness       = 1.0f;
    float contrast         = 1.0f;
    float grayscaleAmount  = 0.0f;
    float invertAmount     = 0.0f;
    float opacity          = 1.0f;
    float saturateAmount   = 1.0f;
    float sepiaAmount      = 0.0f;
    int32_t enableColorAdjust = 1;

    // ---- 便利方法 ----
    void reset() {
        blurRadius = 0.0f;
        enableBlur = 0;
        brightness = 1.0f;
        contrast = 1.0f;
        grayscaleAmount = 0.0f;
        invertAmount = 0.0f;
        opacity = 1.0f;
        saturateAmount = 1.0f;
        sepiaAmount = 0.0f;
        enableColorAdjust = 1;
    }

    void setTextureSize(int w, int h) {
        textureSizeX = (float)w;
        textureSizeY = (float)h;
        texelSizeX = 1.0f / (float)w;
        texelSizeY = 1.0f / (float)h;
    }
};

static_assert(sizeof(FilterParams) == 64, "FilterParams must be 64 bytes to match Slang constant buffer layout");
