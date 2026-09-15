 

find_package(EnTT CONFIG REQUIRED)

# ---- 源文件 ----
set(OVG_SRC
    main_vg.cpp 
) 
set(SRC
    main.cpp 
) 

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# ---- 可执行文件 ----
add_executable(ovg ${OVG_SRC})
target_link_libraries(ovg PRIVATE libovg libvgui)
add_executable(vgui ${SRC})
target_link_libraries(vgui PRIVATE libovg libvgui EnTT::EnTT)

  
