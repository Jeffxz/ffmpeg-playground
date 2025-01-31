cmake_minimum_required(VERSION 3.16)

project(transcode VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(FFmpeg 6.1 REQUIRED avformat avfilter avutil swscale swresample OPTIONAL_COMPONENTS avcodec)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
qt_standard_project_setup()

qt_add_executable(transcode
    main.cpp
)

target_link_libraries(transcode PRIVATE Qt6::Core)
target_link_libraries(transcode PRIVATE ${OpenCV_LIBS} )
target_link_libraries(
  transcode
  PRIVATE
    FFmpeg::avcodec
    FFmpeg::avfilter
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
)