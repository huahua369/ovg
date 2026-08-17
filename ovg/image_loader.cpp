#include "image_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <SDL3/SDL_log.h>

// ============================================================
// 加载图片
// ============================================================
bool ImageLoader::load(const std::string& path, ImageData& out) {
    int w, h, ch;
    stbi_set_flip_vertically_on_load(1); // 匹配 GPU 纹理坐标
    uint8_t* data = stbi_load(path.c_str(), &w, &h, &ch, 4); // 强制 4 通道
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "stbi_load failed: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }
    out.width    = w;
    out.height   = h;
    out.channels = 4;
    out.pixels.assign(data, data + w * h * 4);
    stbi_image_free(data);
    return true;
}

// ============================================================
// 保存 PNG
// ============================================================
bool ImageLoader::savePNG(const std::string& path, const ImageData& img) {
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(
        path.c_str(),
        img.width,
        img.height,
        4,
        img.pixels.data(),
        img.width * 4
    );
    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "stbi_write_png failed: %s", path.c_str());
        return false;
    }
    return true;
}
