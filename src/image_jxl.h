#pragma once

#include "image.h"

void InitJxl(int thread_count);
bool SaveJxlFile(const char* file_path, const Image& image, int cmp_level);
bool LoadJxlFile(const char* file_path, Image& r_image);



