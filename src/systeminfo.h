#pragma once

#include <string>

std::string sysinfo_getplatform();
std::string sysinfo_getcpumodel();
std::string sysinfo_getcurtime();
unsigned int sysinfo_getcpuphysicalcores();
