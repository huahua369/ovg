 


# ---- 查找 lib ----
find_package(SDL3 REQUIRED)
find_package(harfbuzz CONFIG REQUIRED) 
find_package(Fontconfig REQUIRED)

# ---- 源文件 ----
set(SRC
    main.cpp
    ${OVG_SOURCES}
)
set(SRC_TEXT
    ../ovg/ovg_fonts.cpp  
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# ---- 可执行文件 ----
add_executable(ovg ${SRC})
target_link_libraries(ovg PRIVATE SDL3::SDL3 libovg)
add_executable(test_text ${SRC_TEXT})
target_link_libraries(test_text PRIVATE SDL3::SDL3 libovg)
  
