#pragma once

#include "image.h"
#include "fileio.h"

void InitJxl(int thread_count);
bool SaveJxlFile(MyOStream& mem, const Image& image, int cmp_level);
bool LoadJxlFile(MyIStream &mem, Image& r_image);



