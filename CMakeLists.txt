cmake_minimum_required (VERSION 3.24)
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()

include(FetchContent)

option(BUILD_SHARED_LIBS "" OFF)

# OpenEXR
FetchContent_Declare(
	openexr
	#URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.3.4.zip # latest (2025 Jun) at time of writing
    #URL https://github.com/AcademySoftwareFoundation/openexr/archive/ac31208e9433611003032c0a9f4ebcf050574f41.zip # 2025 May, beta with HTJ2K compression

    # 2025 June, beta with HTJ2K compression, and a patch that fixes Windows perf issue (https://github.com/AcademySoftwareFoundation/openexr/pull/2061)
    URL https://github.com/sandflow/openexr-ht/archive/1103c6d112a5a5beed8e8ac06156bbde2170e58d.zip
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

# libjxl
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

project ("test_exr_htj2k_jxl")
add_executable (test_exr_htj2k_jxl
    src/image.cpp
    src/image.h
    src/image_exr.cpp
    src/image_exr.h
    src/image_jxl.cpp
    src/image_jxl.h
    src/main.cpp
    src/fileio.cpp
    src/fileio.h
    src/systeminfo.cpp
    src/systeminfo.h
    src/systeminfo.cpp
)
target_link_libraries(test_exr_htj2k_jxl PRIVATE
    OpenEXR::OpenEXR
    jxl_dec-obj jxl_enc-obj jxl_cms jxl_threads brotlidec brotlienc hwy
)
set_property(TARGET test_exr_htj2k_jxl PROPERTY CXX_STANDARD 17)
target_compile_definitions(test_exr_htj2k_jxl PRIVATE CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX $<$<CONFIG:Debug>:_DEBUG>)
