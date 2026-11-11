cmake_minimum_required(VERSION 3.16)
project(protodroid_example_plugin LANGUAGES C)

add_library(protodroid_example SHARED plugin.c)
target_include_directories(protodroid_example PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../include")
target_compile_features(protodroid_example PRIVATE c_std_11)
set_target_properties(protodroid_example PROPERTIES PREFIX "lib" OUTPUT_NAME "protodroid_example")
