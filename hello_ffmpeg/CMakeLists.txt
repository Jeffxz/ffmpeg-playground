cmake_minimum_required(VERSION 3.16)

project(hello_ffmpeg VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(FFmpeg 6.1 REQUIRED avformat avutil swscale swresample OPTIONAL_COMPONENTS avcodec)
find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

qt_add_executable(hello_ffmpeg
    main.cpp
)

target_link_libraries(hello_ffmpeg PRIVATE Qt6::Core)
target_link_libraries(
  hello_ffmpeg
  PRIVATE
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
)