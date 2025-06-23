cmake_minimum_required (VERSION 3.24)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()

include(FetchContent)

option(BUILD_SHARED_LIBS "" OFF)

# OpenEXR
FetchContent_Declare(
	openexr
	#URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.3.4.zip # latest (2025 Jun) at time of writing
    URL https://github.com/AcademySoftwareFoundation/openexr/archive/ac31208e9433611003032c0a9f4ebcf050574f41.zip # 2025 May, beta with HTJ2K compression
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
    GIT_TAG a75b322e85fd # 2025 Jun, latest at time of writing
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
set_property(TARGET test_exr_htj2k_jxl PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(test_exr_htj2k_jxl PRIVATE CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
