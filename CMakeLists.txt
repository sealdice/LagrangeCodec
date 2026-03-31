cmake_minimum_required(VERSION 3.24)
project(LagrangeCodec)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default builds keep producing the shared library as before. Static builds are
# used by the dedicated CI workflow (and by consumers who want a .a archive).
option(LAGRANGECODEC_BUILD_SHARED "Build LagrangeCodec as a shared library" ON)
if (LINUX)
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

find_package(FFMPEG REQUIRED)

find_package(ZLIB REQUIRED)

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
target_link_libraries(LagrangeCodec PUBLIC ${FFMPEG_LIBRARIES} ZLIB::ZLIB)

include(CTest)
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
