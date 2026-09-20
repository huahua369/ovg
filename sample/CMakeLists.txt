 
# ---- 源文件 ----
set(OVG_SRC
    main_vg.cpp 
    ../ovg/ovg_renderer_sdl3.cpp
) 

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# ---- 可执行文件 ----
add_executable(ovg ${OVG_SRC})
target_link_libraries(ovg PRIVATE libovg SDL3::SDL3 )
