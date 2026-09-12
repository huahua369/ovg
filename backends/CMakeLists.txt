 


set(OVGUI_SOURCES 
    ../gui/anim.cpp
    ../gui/win_core.cpp
    ../gui/vgui.cpp
    mapView.cpp
    ovg_renderer_sdl3.cpp
    vgui_sdl3.cpp
)
 

add_library(libvgui STATIC ${OVGUI_SOURCES})
target_link_libraries(libvgui PUBLIC SDL3::SDL3 glm::glm-header-only)
  