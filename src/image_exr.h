#pragma once

#include "image.h"

void InitExr(int thread_count);
void SaveExrFile(const char* file_path, const Image& image, CompressorType cmp_type, int cmp_level);
bool LoadExrFile(const char* file_path, Image& r_image);
