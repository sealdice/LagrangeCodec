cmake_minimum_required(VERSION 3.24)
project(LagrangeCodec)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default builds keep producing the shared library as before. Static builds are
# used by the dedicated CI workflow (and by consumers who want a .a archive).
option(LAGRANGECODEC_BUILD_SHARED "Build LagrangeCodec as a shared library" ON)
if (ANDROID AND NOT LAGRANGECODEC_BUILD_SHARED)
    set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
endif()
if (LINUX AND NOT ANDROID)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wl,-Bsymbolic")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wl,-Bsymbolic")
endif()
if(MSVC)
    message(STATUS "MSVC compiler detected")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

include(FetchContent)
FetchContent_Declare(
        silk_fetch
        GIT_REPOSITORY "https://github.com/kn007/silk-v3-decoder.git"
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(silk_fetch)
message(STATUS "SILK source code will be downloaded to: ${silk_fetch_SOURCE_DIR}")

set(LAGRANGECODEC_ANDROID_NATIVE_PREFIX "" CACHE PATH "Prefix containing Android static FFmpeg/zlib built directly with the NDK")

if (ANDROID AND NOT LAGRANGECODEC_BUILD_SHARED AND LAGRANGECODEC_ANDROID_NATIVE_PREFIX)
    set(android_native_include_dir "${LAGRANGECODEC_ANDROID_NATIVE_PREFIX}/include")
    set(android_native_lib_dir "${LAGRANGECODEC_ANDROID_NATIVE_PREFIX}/lib")

    set(FFMPEG_INCLUDE_DIRS "${android_native_include_dir}")
    set(FFMPEG_LIBRARY_DIRS "${android_native_lib_dir}")
    set(FFMPEG_LIBRARIES
        "${android_native_lib_dir}/libavformat.a"
        "${android_native_lib_dir}/libavcodec.a"
        "${android_native_lib_dir}/libswresample.a"
        "${android_native_lib_dir}/libswscale.a"
        "${android_native_lib_dir}/libavutil.a"
    )

    foreach(required_file IN LISTS FFMPEG_LIBRARIES)
        if (NOT EXISTS "${required_file}")
            message(FATAL_ERROR "Missing Android FFmpeg archive: ${required_file}")
        endif()
    endforeach()

    if (NOT EXISTS "${android_native_lib_dir}/libz.a")
        message(FATAL_ERROR "Missing Android zlib archive: ${android_native_lib_dir}/libz.a")
    endif()

    add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${android_native_lib_dir}/libz.a"
        INTERFACE_INCLUDE_DIRECTORIES "${android_native_include_dir}"
    )
else()
    find_package(FFMPEG REQUIRED)
    find_package(ZLIB REQUIRED)
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")
if (LAGRANGECODEC_BUILD_SHARED)
    add_library(LagrangeCodec SHARED ${SOURCES})
else()
    add_library(LagrangeCodec STATIC ${SOURCES})
    # Shims are only needed for static-link consumers.
    file(GLOB SHIM_SOURCES CONFIGURE_DEPENDS "src/shim/*.c")
    target_sources(LagrangeCodec PRIVATE ${SHIM_SOURCES})
endif()

if (WIN32 AND LAGRANGECODEC_BUILD_SHARED)
    target_compile_definitions(LagrangeCodec PRIVATE LAGRANGECODEC_SHARED_BUILD)
endif()

if (ANDROID AND NOT LAGRANGECODEC_BUILD_SHARED)
    target_compile_options(LagrangeCodec PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
endif()

target_include_directories(LagrangeCodec
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include/public
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${silk_fetch_SOURCE_DIR}/silk/interface
        ${silk_fetch_SOURCE_DIR}/silk/include
        ${silk_fetch_SOURCE_DIR}/silk/src
)
file(GLOB SILK_SRC_FILES CONFIGURE_DEPENDS
        ${silk_fetch_SOURCE_DIR}/silk/src/*.c
        ${silk_fetch_SOURCE_DIR}/silk/src/*.S
)
target_sources(LagrangeCodec PRIVATE ${SILK_SRC_FILES})

target_include_directories(LagrangeCodec PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_link_directories(LagrangeCodec PUBLIC ${FFMPEG_LIBRARY_DIRS})
if (ANDROID AND NOT LAGRANGECODEC_BUILD_SHARED)
    set(ffmpeg_android_link_libraries "")
    set(use_ffmpeg_link_item ON)
    foreach(item IN LISTS FFMPEG_LIBRARIES)
        if (item STREQUAL "optimized" OR item STREQUAL "general")
            set(use_ffmpeg_link_item ON)
            continue()
        endif()
        if (item STREQUAL "debug")
            set(use_ffmpeg_link_item OFF)
            continue()
        endif()
        if (item STREQUAL "-pthread" OR item STREQUAL "-pthreads")
            continue()
        endif()
        if (use_ffmpeg_link_item)
            list(APPEND ffmpeg_android_link_libraries "${item}")
        endif()
        set(use_ffmpeg_link_item ON)
    endforeach()

    target_link_libraries(LagrangeCodec PUBLIC
        -Wl,--start-group
        ${ffmpeg_android_link_libraries}
        ZLIB::ZLIB
        m
        log
        dl
        atomic
        android
        -Wl,--end-group
    )
else()
    target_link_libraries(LagrangeCodec PUBLIC ${FFMPEG_LIBRARIES} ZLIB::ZLIB)
endif()

include(CTest)
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
