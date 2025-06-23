#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "ImfIO.h"
#ifdef _MSC_VER
#include <wtypes.h>
#endif

size_t GetFileSize(const char* path);

void TurnOffFileCache(FILE* f);

class MyIStream : public Imf::IStream
{
public:
    MyIStream(const char* fileName);
    ~MyIStream();
    virtual bool read(char c[/*n*/], int n);
    virtual uint64_t tellg();
    virtual void seekg(uint64_t pos);
    virtual void clear();
private:
#ifdef _MSC_VER
    char* _buffer;
	size_t _pos;
    size_t _size;
#else
    FILE* _file;
#endif
};
class MyOStream : public Imf::OStream
{
public:
    MyOStream(const char* fileName);
    ~MyOStream();
    virtual void write(const char c[/*n*/], int n);
    virtual uint64_t tellp();
    virtual void seekp(uint64_t pos);
private:
#ifdef _MSC_VER
    char* _buffer;
    size_t _size;
    size_t _pos;
    HANDLE _file;
#else
    FILE* _file;
#endif
};
