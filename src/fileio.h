#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <vector>
#include "ImfIO.h"

class MyIStream : public Imf::IStream
{
public:
    MyIStream(const char* fileName);
    MyIStream(const char* buffer, size_t size)
        : IStream("<mem>"), _buffer(buffer), _pos(0), _size(size), _owns_buffer(false) {}
    ~MyIStream();
    virtual bool read(char c[], int n);
    virtual uint64_t tellg();
    virtual void seekg(uint64_t pos);
    virtual void clear();

    const char* data() const { return _buffer; }
    size_t size() const { return _size; }

    template<typename T>
    void read(T& v) { read((char*)&v, sizeof(v)); }

private:
    const char* _buffer;
	size_t _pos;
    size_t _size;
    bool _owns_buffer = false;
};

class MyOStream : public Imf::OStream
{
public:
    MyOStream();
    ~MyOStream() {}

    virtual void write(const char c[/*n*/], int n);
    virtual uint64_t tellp();
    virtual void seekp(uint64_t pos);

    template<typename T>
    void write(const T& v) { write((const char*)&v, sizeof(v)); }

    const char* data() const { return _buffer.data(); }
    size_t size() const { return _buffer.size(); }
private:
    std::vector<char> _buffer;
    size_t _pos;
};
