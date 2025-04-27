cmake_minimum_required(VERSION 3.0)

project(bptree VERSION 1.0)

SET(CMAKE_COLOR_MAKEFILE    ON)
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "-Wall -g -fPIC -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=0 -DASIO_STANDALONE -Wno-deprecated -Wno-narrowing -fdiagnostics-color=always -fno-var-tracking-assignments -Wno-misleading-indentation -Wno-class-memaccess")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -g")
# 启用汇编支持
enable_language(ASM)

# 指定源文件
file(GLOB_RECURSE SOURCES
    src/*.cpp
)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

add_library(bptree ${SOURCES})
add_subdirectory(test)

target_link_libraries(bptree pthread)

