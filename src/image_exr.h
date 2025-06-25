#pragma once

#include "image.h"
#include "fileio.h"

void InitExr(int thread_count);
void SaveExrFile(MyOStream& mem, const Image& image, CompressorType cmp_type, int cmp_level);
bool LoadExrFile(MyIStream& mem, Image& r_image);
