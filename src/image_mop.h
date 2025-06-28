#pragma once

#include "image.h"
#include "fileio.h"

#ifdef INCLUDE_FORMAT_MOP

void InitMop(int thread_count);
bool SaveMopFile(MyOStream& mem, const Image& image, int cmp_level);
bool LoadMopFile(MyIStream& mem, Image& r_image);

#endif
