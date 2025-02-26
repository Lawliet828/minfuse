cmake_minimum_required(VERSION 3.18)
project(minfuse)

# 静态链接标准库
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()

# Import mimalloc (if installed)
find_package(mimalloc 1.9 NO_SYSTEM_ENVIRONMENT_PATH QUIET)
if(mimalloc_FOUND)
    message(STATUS "Found mimalloc at: ${MIMALLOC_LIBRARY_DIR} (${MIMALLOC_VERSION_DIR})")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")

# pkg-config --cflags-only-I fuse3 命令可以查看头文件路径
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
# 标准输出为 /usr/local/include/fuse3
message(STATUS "FUSE3_INCLUDE_DIRS: ${FUSE3_INCLUDE_DIRS}")
message(STATUS "FUSE3_LIBRARY_DIRS: ${FUSE3_LIBRARY_DIRS}")
message(STATUS "FUSE3_LIBRARIES: ${FUSE3_LIBRARIES}")
include_directories(${FUSE3_INCLUDE_DIRS})

add_executable(minfuse minfuse.cc)

target_compile_definitions(minfuse PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# 寻找并链接静态 libfuse3
find_library(FUSE3_STATIC_LIB NAMES fuse3.a libfuse3.a fuse3 PATHS ${FUSE3_LIBRARY_DIRS} NO_DEFAULT_PATH)

if(FUSE3_STATIC_LIB)
    message(STATUS "Found static libfuse3: ${FUSE3_STATIC_LIB}")
    target_link_libraries(minfuse
        PRIVATE
            ${FUSE3_STATIC_LIB}
            spdlog::spdlog
    )
else()
    message(FATAL_ERROR "Static libfuse3 not found.")
endif()

# if(mimalloc_FOUND)
#     target_link_libraries(minfuse PUBLIC mimalloc-static)
# endif()