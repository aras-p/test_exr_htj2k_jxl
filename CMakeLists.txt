cmake_minimum_required (VERSION 3.24)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()

include(FetchContent)

option(BUILD_SHARED_LIBS "" OFF)

# libdeflate (needed by OpenEXR)
#FetchContent_Declare(
#    libdeflate
#    URL https://github.com/ebiggers/libdeflate/archive/refs/tags/v1.24.zip # latest (2025 May) at time of writing
#)
#option(LIBDEFLATE_BUILD_SHARED_LIB "" OFF)
#option(LIBDEFLATE_BUILD_GZIP "" OFF)
#FetchContent_MakeAvailable(libdeflate)

# Imath (needed by OpenEXR)
FetchContent_Declare(
    imath
    URL https://github.com/AcademySoftwareFoundation/Imath/archive/refs/tags/v3.1.12.zip # latest (2024 Sep) at time of writing
)
option(IMATH_INSTALL "" ON)
FetchContent_MakeAvailable(imath)

# OpenEXR
FetchContent_Declare(
	openexr
	URL https://github.com/AcademySoftwareFoundation/openexr/archive/refs/tags/v3.3.4.zip # latest (2025 Jun) at time of writing
)
option(BUILD_TESTING "" OFF)
option(OPENEXR_BUILD_TOOLS "" OFF)
option(OPENEXR_INSTALL "" OFF)
option(OPENEXR_INSTALL_TOOLS "" OFF)
option(OPENEXR_BUILD_EXAMPLES "" OFF)
option(OPENEXR_FORCE_INTERNAL_DEFLATE "" ON)
FetchContent_MakeAvailable(openexr)

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
)
set_property(TARGET test_exr_htj2k_jxl PROPERTY CXX_STANDARD 17)
set_property(TARGET test_exr_htj2k_jxl PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(test_exr_htj2k_jxl PRIVATE CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
