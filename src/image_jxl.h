#pragma once

#include "image.h"
#include "fileio.h"

#ifdef INCLUDE_FORMAT_JXL

void InitJxl(int thread_count);
bool SaveJxlFile(MyOStream& mem, const Image& image, int cmp_level);
bool LoadJxlFile(MyIStream &mem, Image& r_image);

#endif
