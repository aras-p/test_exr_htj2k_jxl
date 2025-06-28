cmake_minimum_required (VERSION 3.24)
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()

set(INCLUDE_FORMAT_EXR 1)
set(INCLUDE_FORMAT_JXL 1)
set(INCLUDE_FORMAT_MOP 1)

include(FetchContent)

option(BUILD_SHARED_LIBS "" OFF)

# OpenEXR
if (INCLUDE_FORMAT_EXR)
    FetchContent_Declare(
	    openexr
        URL https://github.com/AcademySoftwareFoundation/openexr/archive/45ee12752f9c26f601e21a5e7b782f6dbf857271.zip # 2025 Jun, post-3.3.4 with HTJ2K compression
    )
    option(BUILD_TESTING "" OFF)
    option(OPENEXR_BUILD_TOOLS "" OFF)
    option(OPENEXR_INSTALL "" OFF)
    option(OPENEXR_INSTALL_TOOLS "" OFF)
    option(OPENEXR_BUILD_EXAMPLES "" OFF)
    option(OPENEXR_FORCE_INTERNAL_DEFLATE "" ON)
    option(OPENEXR_FORCE_INTERNAL_OPENJPH "" ON)
    option(OPENEXR_FORCE_INTERNAL_IMATH "" ON)
    FetchContent_MakeAvailable(openexr)
endif()

# libjxl
if (INCLUDE_FORMAT_JXL)
    FetchContent_Declare(
        libjxl
        GIT_REPOSITORY https://github.com/libjxl/libjxl.git
        GIT_TAG main # 2025 Jun, latest at time of writing
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE TRUE
    )
    option(JPEGXL_ENABLE_FUZZERS "" OFF)
    option(JPEGXL_ENABLE_DEVTOOLS "" OFF)
    option(JPEGXL_ENABLE_TOOLS "" OFF)
    option(JPEGXL_ENABLE_JPEGLI "" OFF)
    option(JPEGXL_ENABLE_JPEGLI_LIBJPEG "" OFF)
    option(JPEGXL_ENABLE_DOXYGEN "" OFF)
    option(JPEGXL_ENABLE_MANPAGES "" OFF)
    option(JPEGXL_ENABLE_BENCHMARK "" OFF)
    option(JPEGXL_ENABLE_EXAMPLES "" OFF)
    option(JPEGXL_BUNDLE_LIBPNG "" OFF)
    option(JPEGXL_ENABLE_JNI "" OFF)
    option(JPEGXL_ENABLE_SJPEG "" OFF)
    option(JPEGXL_ENABLE_OPENEXR "" OFF)
    option(JPEGXL_ENABLE_TRANSCODE_JPEG "" OFF)
    FetchContent_MakeAvailable(libjxl)
endif()

# meshoptimizer
if (INCLUDE_FORMAT_MOP)
    FetchContent_Declare(
	    meshoptimizer
	    URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v0.24.zip # latest (2025 Jun) at time of writing
    )
    option(MESHOPT_INSTALL "" OFF)
    FetchContent_MakeAvailable(meshoptimizer)
endif()

project ("test_exr_htj2k_jxl")
set (SOURCES
    src/image.cpp
    src/image.h
    src/main.cpp
    src/fileio.cpp
    src/fileio.h
    src/systeminfo.cpp
    src/systeminfo.h
    src/systeminfo.cpp
)
set (LIBS )
set (DEFINES CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
if (INCLUDE_FORMAT_EXR)
    list(APPEND SOURCES src/image_exr.cpp src/image_exr.h)
    list(APPEND LIBS OpenEXR::OpenEXR)
    list(APPEND DEFINES INCLUDE_FORMAT_EXR)
endif()
if (INCLUDE_FORMAT_JXL)
    list(APPEND SOURCES src/image_jxl.cpp src/image_jxl.h)
    list(APPEND LIBS jxl_dec-obj jxl_enc-obj jxl_cms jxl_threads brotlidec brotlienc hwy)
    list(APPEND DEFINES INCLUDE_FORMAT_JXL)
endif()
if (INCLUDE_FORMAT_MOP)
    list(APPEND SOURCES src/image_mop.cpp src/image_mop.h)
    list(APPEND LIBS meshoptimizer)
    list(APPEND DEFINES INCLUDE_FORMAT_MOP)
endif()
add_executable (test_exr_htj2k_jxl ${SOURCES})
target_link_libraries(test_exr_htj2k_jxl PRIVATE ${LIBS})
set_property(TARGET test_exr_htj2k_jxl PROPERTY CXX_STANDARD 17)
target_compile_definitions(test_exr_htj2k_jxl PRIVATE ${DEFINES} $<$<CONFIG:Debug>:_DEBUG>)
