 



# ---- 源文件 ----
set(SRC
    main.cpp 
) 

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# ---- 可执行文件 ----
add_executable(ovg ${SRC})
target_link_libraries(ovg PRIVATE libovg libvgui)

  
