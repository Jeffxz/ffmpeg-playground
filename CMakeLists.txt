cmake_minimum_required(VERSION 3.16)

project(helloffmpeg VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

add_subdirectory(decode_video)
add_subdirectory(encode_video)
add_subdirectory(generate_video)
add_subdirectory(hello_ffmpeg)
add_subdirectory(hello_world)
add_subdirectory(list_dir)
add_subdirectory(read_callback)
add_subdirectory(sandbox)
add_subdirectory(scale_video)
add_subdirectory(transcode)
add_subdirectory(video2image)