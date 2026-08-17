#!/bin/bash
# ============================================================
# build.sh —— 编译 Slang 着色器 + C++ 代码
# ============================================================

set -e

# ---- 配置 ----
BUILD_DIR="build"
SHADER_DIR="shaders"
SLANGC=${SLANGC:-slangc}

# 检测 SDL3
SDL_CFLAGS=$(sdl3-config --cflags 2>/dev/null || pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS=$(sdl3-config --libs 2>/dev/null || pkg-config --libs sdl3 2>/dev/null)

if [ -z "$SDL_CFLAGS" ] || [ -z "$SDL_LIBS" ]; then
    echo "ERROR: SDL3 not found. Install via:"
    echo "  macOS:  brew install sdl3"
    echo "  Linux:  sudo apt install libsdl3-dev"
    exit 1
fi

echo "=== SDL3 found ==="
echo "  CFLAGS: $SDL_CFLAGS"
echo "  LIBS:   $SDL_LIBS"
echo ""

# ---- 创建构建目录 ----
mkdir -p "$BUILD_DIR"

# ============================================================
# 1. 编译 Slang 计算着色器
# ============================================================
echo "=== Compiling Slang Shaders ==="

# 检测目标平台
if [[ "$OSTYPE" == "linux-gnu"* ]] || [[ "$OSTYPE" == "darwin"* ]]; then
    # Linux/macOS: 优先 Vulkan (SPIR-V)
    TARGET="spirv"
    PROFILE="glslcompute"
    EXT="spv"
    FORMAT_FLAG="-target spirv"
    echo "  Target: SPIR-V (Vulkan)"
elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    # Windows: DXIL
    TARGET="dxil"
    PROFILE="cs_6_0"
    EXT="dxil"
    FORMAT_FLAG="-target dxil -profile cs_6_0"
    echo "  Target: DXIL (DirectX 12)"
else
    echo "  Unknown OS, defaulting to SPIR-V"
    TARGET="spirv"
    PROFILE="glslcompute"
    EXT="spv"
    FORMAT_FLAG="-target spirv"
fi

# 编译三个计算着色器
for shader in blur_horizontal blur_vertical color_adjust filter_all_in_one; do
    SRC="$SHADER_DIR/${shader}.slang"
    DST="$BUILD_DIR/${shader}.${EXT}"
    echo "  Compiling $SRC -> $DST"
    $SLANGC $FORMAT_FLAG "$SRC" -o "$DST"
done

echo ""
echo "=== Shader compilation done ==="
echo ""

# ============================================================
# 2. 编译 C++ 代码
# ============================================================
echo "=== Compiling C++ ==="

CXX=${CXX:-clang++}
CXXFLAGS="-std=c++20 -O2 -Wall -Wextra"

# 平台特定链接库
case "$OSTYPE" in
    linux-gnu*)
        PLATFORM_LIBS="-ldl -lpthread"
        ;;
    darwin*)
        PLATFORM_LIBS="-framework Cocoa -framework Metal"
        ;;
    *)
        PLATFORM_LIBS=""
        ;;
esac

$CXX $CXXFLAGS \
    main.cpp \
    filter_pipeline.cpp \
    image_loader.cpp \
    $SDL_CFLAGS \
    $CXXFLAGS \
    -o "$BUILD_DIR/ovg" \
    $SDL_LIBS \
    $PLATFORM_LIBS

echo ""
echo "=== Build Complete ==="
echo ""
echo "Usage:"
echo "  $BUILD_DIR/ovg <input_image.jpg>"
echo ""
echo "Controls:"
echo "  B       Toggle Blur          I  Toggle Invert"
echo "  UP/DOWN Adjust Blur Radius   S  Toggle Saturate"
echo "  G       Toggle Grayscale     O  Toggle Opacity"
echo "  P       Toggle Sepia        Ctrl+P Save PNG"
echo "  C       Toggle Contrast     R  Reset"
echo "  F       Fullscreen          Esc Quit"
echo ""
