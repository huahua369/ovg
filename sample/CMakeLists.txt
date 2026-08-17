 


# ---- 查找 SDL3 ----
find_package(SDL3 REQUIRED)

# ---- 源文件 ----
set(SRC
    ovg_main.cpp
    ${OVG_SOURCES}
)

# ---- 可执行文件 ----
add_executable(ovg ${SRC})

target_link_libraries(ovg PRIVATE SDL3::SDL3 libovg)
target_include_directories(ovg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  
