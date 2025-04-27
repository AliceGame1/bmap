cmake_minimum_required(VERSION 3.0)
project(test)

add_executable(page_cache_test ${CMAKE_CURRENT_SOURCE_DIR}/page_cache_test.cpp)
add_executable(bmap_test ${CMAKE_CURRENT_SOURCE_DIR}/bmap_test.cpp)


target_link_libraries(page_cache_test bptree)
target_link_libraries(bmap_test bptree)



