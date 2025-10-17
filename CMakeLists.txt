if(NOT DEFINED CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation Directory")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()


project(axcl-pipeline)
set(CMAKE_CXX_STANDARD 17)


add_subdirectory(src/libdet)

find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

link_directories(${OpenCV_LIBRARY_DIRS})

include_directories(src)
include_directories(src/ffmpeg/include)
include_directories(/usr/include/axcl/)

# link_directories(/usr/lib/axcl/ffmpeg/)
set(AXCL_FFMPEG_DIR /usr/lib/axcl/ffmpeg)

add_executable(sample_demux_npu_rtsp src/sample_demux_npu_rtsp.cpp)
target_link_libraries(sample_demux_npu_rtsp
    ${AXCL_FFMPEG_DIR}/libavcodec.so
    ${AXCL_FFMPEG_DIR}/libavformat.so
    ${AXCL_FFMPEG_DIR}/libavutil.so
    ${AXCL_FFMPEG_DIR}/libswscale.so
    ${AXCL_FFMPEG_DIR}/libswresample.so
    ${OpenCV_LIBRARIES}
    det
)
install(TARGETS sample_demux_npu_rtsp
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)
# add_executable(sample_ffmpeg_vdec src/ffmpeg/vdec/sample_ffmpeg_vdec.c)
# target_link_libraries(sample_ffmpeg_vdec
#     ${AXCL_FFMPEG_DIR}/libavcodec.so
#     ${AXCL_FFMPEG_DIR}/libavformat.so
#     ${AXCL_FFMPEG_DIR}/libavutil.so
#     ${AXCL_FFMPEG_DIR}/libswscale.so
#     ${AXCL_FFMPEG_DIR}/libswresample.so
# )

# add_executable(sample_ffmpeg_venc src/ffmpeg/venc/sample_ffmpeg_venc.c)
# target_link_libraries(sample_ffmpeg_venc
#     ${AXCL_FFMPEG_DIR}/libavcodec.so
#     ${AXCL_FFMPEG_DIR}/libavformat.so
#     ${AXCL_FFMPEG_DIR}/libavutil.so
#     ${AXCL_FFMPEG_DIR}/libswscale.so
#     ${AXCL_FFMPEG_DIR}/libswresample.so
# )