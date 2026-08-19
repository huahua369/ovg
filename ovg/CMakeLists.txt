 
FILE(GLOB_RECURSE TESS2_SRC 
  tess2/src/*.c 
)
set(OVG_SOURCES 
    ovg.cpp
    ovg_renderer_sdl3.cpp
    ${TESS2_SRC}
    #filter_pipeline.cpp
    #image_loader.cpp
)

find_package(glm CONFIG REQUIRED)
add_library(libovg STATIC ${OVG_SOURCES})
target_link_libraries(libovg
  PUBLIC glm::glm-header-only
)

target_include_directories(libovg PRIVATE "tess2/Include"})

# ---- 着色器编译（自定义命令）----
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(BUILD_DIR ${EXECUTABLE_OUTPUT_PATH})

# 检测 slangc
find_program(SLANGC slangc)
if(NOT SLANGC)
    message(WARNING "slangc not found - shaders won't be compiled automatically")
else()
    # 检测目标格式
    if(enableDX12)
        set(SLANG_TARGET "dxil")
        set(SLANG_PROFILE "cs_6_0")
        set(SLANG_EXT "dxil")
    else()
        set(SLANG_TARGET "spirv")
        set(SLANG_PROFILE "glsl_450")
        set(SLANG_EXT "spv")
    endif()

    set(SHADER_FILES
        blur_horizontal
        blur_vertical
        color_adjust
        filter_all_in_one
    )

    foreach(SHADER ${SHADER_FILES})
        set(SRC ${SHADER_DIR}/${SHADER}.slang)
        set(DST ${BUILD_DIR}/${SHADER}.${SLANG_EXT})
        add_custom_command(
            OUTPUT ${DST}
            COMMAND ${SLANGC} -target ${SLANG_TARGET} -profile ${SLANG_PROFILE} ${SRC} -o ${DST}
            DEPENDS ${SRC} ${SHADER_DIR}/filter_common.slang.h
            COMMENT "Compiling ${SHADER}.slang -> ${SHADER}.${SLANG_EXT}"
        )
        list(APPEND COMPILED_SHADERS ${DST})
    endforeach()

    # Blit VS/FS
    add_custom_command(
        OUTPUT ${BUILD_DIR}/blit_vs.${SLANG_EXT}
        COMMAND ${SLANGC} -target ${SLANG_TARGET} -profile vs_6_0 ${SHADER_DIR}/blit_vs.slang -o ${BUILD_DIR}/blit_vs.${SLANG_EXT}
        DEPENDS ${SHADER_DIR}/blit_vs.slang
        COMMENT "Compiling blit_vs"
    )
    add_custom_command(
        OUTPUT ${BUILD_DIR}/blit_fs.${SLANG_EXT}
        COMMAND ${SLANGC} -target ${SLANG_TARGET} -profile ps_6_0 ${SHADER_DIR}/blit_fs.slang -o ${BUILD_DIR}/blit_fs.${SLANG_EXT}
        DEPENDS ${SHADER_DIR}/blit_fs.slang
        COMMENT "Compiling blit_fs"
    )
    list(APPEND COMPILED_SHADERS ${BUILD_DIR}/blit_vs.${SLANG_EXT} ${BUILD_DIR}/blit_fs.${SLANG_EXT})

    add_custom_target(shaders ALL DEPENDS ${COMPILED_SHADERS}) 
    add_dependencies(libovg shaders)
endif()
