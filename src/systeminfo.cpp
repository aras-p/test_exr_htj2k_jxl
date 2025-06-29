#define _CRT_SECURE_NO_WARNINGS
#include "systeminfo.h"

#include <time.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#ifdef _MSC_VER
#include <windows.h>
#endif
#ifdef __linux
#include <string.h>
#include <stdio.h>
#endif

std::string sysinfo_getplatform()
{
    #ifdef __APPLE__
    return "macOS";
    #elif defined _MSC_VER
    return "Windows";
    #elif defined __linux
    return "Linux";
    #else
    #error Unknown platform
    #endif
}

std::string sysinfo_getcpumodel()
{
    #ifdef __APPLE__
    char buffer[1000] = {0};
    size_t size = sizeof(buffer)-1;
    buffer[size] = 0;
    if (0 != sysctlbyname("machdep.cpu.brand_string", buffer, &size, NULL, 0))
        return "Unknown";
    return buffer;
    #elif defined _MSC_VER
	HKEY key;
	DWORD res = ::RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_QUERY_VALUE, &key);
	if (res == ERROR_SUCCESS)
	{
		char buffer[1000];
		DWORD dataType = 0;
		DWORD dataLength = sizeof(buffer);
		res = ::RegQueryValueExA(key, "ProcessorNameString", NULL, &dataType, (BYTE*)buffer, &dataLength);
		RegCloseKey(key);
		if (res == ERROR_SUCCESS && dataType == REG_SZ)
		{
			buffer[999] = 0;
			return buffer;
		}
	}
	return "Unknown";
    #elif defined __linux
	std::string result = "Unknown";

	FILE* file = fopen("/proc/cpuinfo", "r");
	if (!file)
		return result;

	char line[1024];
	while (fgets(line, sizeof(line), file))
	{
		if (strncmp(line, "model name\t: ", 13) == 0)
		{
			result = line + 13;
			if (!result.empty() && result.back() == '\n')
				result.pop_back();
			break;
		}
	}

	fclose(file);
	return result;
    #else
    #error Unknown platform
    #endif
}

std::string sysinfo_getcurtime()
{
    time_t rawtime;
    time(&rawtime);
    struct tm* timeInfo = localtime(&rawtime);
    char buf[1000];
    snprintf(buf, sizeof(buf), "%i%02i%02i", timeInfo->tm_year+1900, timeInfo->tm_mon+1, timeInfo->tm_mday);
    return buf;
}
