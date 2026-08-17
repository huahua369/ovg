#pragma once

#include <string>
#include <vector>

// ============================================================
// ImageLoader
// 使用 stb_image 加载图片为 RGBA8 像素数据
// ============================================================

struct ImageData {
    int      width  = 0;
    int      height = 0;
    int      channels = 0;
    std::vector<uint8_t> pixels; // RGBA, 8-bit per channel

    void free();
};

class ImageLoader {
public:
    // 加载图片（自动检测格式：jpg/png/bmp/tga/webp/...）
    static bool load(const std::string& path, ImageData& out);

    // 保存为 PNG（需要 stb_image_write）
    static bool savePNG(const std::string& path, const ImageData& img);
};
