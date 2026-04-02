cmake_minimum_required(VERSION 3.24)
project(LagrangeCodec)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default builds keep producing the shared library as before. Static builds are
# used by the dedicated CI workflow (and by consumers who want a .a archive).
option(LAGRANGECODEC_BUILD_SHARED "Build LagrangeCodec as a shared library" ON)
option(LAGRANGECODEC_BUILD_ANDROID_RUNTIME_SMOKE "Build the minimal Android runtime smoke executable" OFF)
set(LAGRANGECODEC_NATIVE_PREFIX "" CACHE PATH "Prefix containing prebuilt FFmpeg/zlib static libraries")
set(LAGRANGECODEC_LINK_ZLIB ON)
set(LAGRANGECODEC_TARGET_IS_ANDROID OFF)
if (ANDROID)
    set(LAGRANGECODEC_TARGET_IS_ANDROID ON)
elseif (DEFINED CMAKE_C_COMPILER_TARGET AND CMAKE_C_COMPILER_TARGET MATCHES "android")
    set(LAGRANGECODEC_TARGET_IS_ANDROID ON)
elseif (DEFINED CMAKE_CXX_COMPILER_TARGET AND CMAKE_CXX_COMPILER_TARGET MATCHES "android")
    set(LAGRANGECODEC_TARGET_IS_ANDROID ON)
endif()

if (LINUX AND NOT LAGRANGECODEC_TARGET_IS_ANDROID)
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
        URL "https://codeload.github.com/kn007/silk-v3-decoder/tar.gz/refs/heads/master"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(silk_fetch)
message(STATUS "SILK source code will be downloaded to: ${silk_fetch_SOURCE_DIR}")

set(FFMPEG_LINK_ITEMS "")
set(LAGRANGECODEC_NATIVE_SYSTEM_LIBS "")

if (LAGRANGECODEC_NATIVE_PREFIX)
    set(native_include_dir "${LAGRANGECODEC_NATIVE_PREFIX}/include")
    set(native_lib_dir "${LAGRANGECODEC_NATIVE_PREFIX}/lib")

    set(FFMPEG_INCLUDE_DIRS "${native_include_dir}")
    set(FFMPEG_LIBRARY_DIRS "${native_lib_dir}")
    if (EXISTS "${native_lib_dir}/libffmpeg.a")
        set(FFMPEG_LIBRARIES "${native_lib_dir}/libffmpeg.a")
        set(FFMPEG_LINK_ITEMS "${FFMPEG_LIBRARIES}")
        set(LAGRANGECODEC_LINK_ZLIB OFF)
    else()
        set(FFMPEG_LIBRARIES
            "${native_lib_dir}/libavformat.a"
            "${native_lib_dir}/libavcodec.a"
            "${native_lib_dir}/libswresample.a"
            "${native_lib_dir}/libswscale.a"
            "${native_lib_dir}/libavutil.a"
        )

        foreach(required_file IN LISTS FFMPEG_LIBRARIES)
            if (NOT EXISTS "${required_file}")
                message(FATAL_ERROR "Missing FFmpeg archive: ${required_file}")
            endif()
        endforeach()

        string(JOIN "," FFMPEG_GROUP_LIBRARIES ${FFMPEG_LIBRARIES})
        set(FFMPEG_LINK_ITEMS "$<LINK_GROUP:RESCAN,${FFMPEG_GROUP_LIBRARIES}>")

        set(zlib_archive "${native_lib_dir}/libz.a")
        if (NOT EXISTS "${zlib_archive}" AND EXISTS "${native_lib_dir}/libzlib.a")
            set(zlib_archive "${native_lib_dir}/libzlib.a")
        endif()

        if (EXISTS "${zlib_archive}")
            add_library(ZLIB::ZLIB STATIC IMPORTED GLOBAL)
            set_target_properties(ZLIB::ZLIB PROPERTIES
                IMPORTED_LOCATION "${zlib_archive}"
                INTERFACE_INCLUDE_DIRECTORIES "${native_include_dir}"
            )
        elseif (LAGRANGECODEC_TARGET_IS_ANDROID)
            set(LAGRANGECODEC_LINK_ZLIB OFF)
            list(APPEND LAGRANGECODEC_NATIVE_SYSTEM_LIBS z)
        else()
            message(FATAL_ERROR "Missing zlib archive under ${native_lib_dir}")
        endif()
    endif()

    if (LAGRANGECODEC_TARGET_IS_ANDROID)
        list(APPEND LAGRANGECODEC_NATIVE_SYSTEM_LIBS m atomic android mediandk)
    endif()
else()
    find_package(FFMPEG REQUIRED)
    find_package(ZLIB REQUIRED)
    set(FFMPEG_LINK_ITEMS ${FFMPEG_LIBRARIES})
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")
if (LAGRANGECODEC_BUILD_SHARED)
    add_library(LagrangeCodec SHARED ${SOURCES})
else()
    add_library(LagrangeCodec STATIC ${SOURCES})
    # Shims are only needed for static-link consumers.
    set(SHIM_SOURCES "")
    if (MINGW)
        list(APPEND SHIM_SOURCES "${CMAKE_SOURCE_DIR}/src/shim/mingw_time_shim.c")
    endif()
    if (LINUX AND NOT LAGRANGECODEC_TARGET_IS_ANDROID)
        list(APPEND SHIM_SOURCES
            "${CMAKE_SOURCE_DIR}/src/shim/linux_aarch64_atomic_shim.c"
            "${CMAKE_SOURCE_DIR}/src/shim/linux_fcntl64_shim.c"
        )
    endif()
    if (SHIM_SOURCES)
        target_sources(LagrangeCodec PRIVATE ${SHIM_SOURCES})
    endif()
endif()

if (WIN32 AND LAGRANGECODEC_BUILD_SHARED)
    target_compile_definitions(LagrangeCodec PRIVATE LAGRANGECODEC_SHARED_BUILD)
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
target_link_libraries(LagrangeCodec PUBLIC ${FFMPEG_LINK_ITEMS})
if (LAGRANGECODEC_LINK_ZLIB)
    target_link_libraries(LagrangeCodec PUBLIC ZLIB::ZLIB)
endif()
if (LAGRANGECODEC_NATIVE_SYSTEM_LIBS)
    target_link_libraries(LagrangeCodec PUBLIC ${LAGRANGECODEC_NATIVE_SYSTEM_LIBS})
endif()

if (LAGRANGECODEC_BUILD_ANDROID_RUNTIME_SMOKE)
    add_executable(android_runtime_smoke tests/android_runtime_smoke.cpp)
    target_include_directories(android_runtime_smoke PRIVATE ${PROJECT_SOURCE_DIR}/include/public)
    target_link_libraries(android_runtime_smoke PRIVATE LagrangeCodec)
    set_target_properties(android_runtime_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

include(CTest)
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
