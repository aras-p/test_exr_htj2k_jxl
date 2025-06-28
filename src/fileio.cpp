#include "fileio.h"
#ifdef INCLUDE_FORMAT_EXR
#include "Iex.h"
#endif
#include <stdio.h>

MyIStream::MyIStream(const char* fileName)
	:
#ifdef INCLUDE_FORMAT_EXR
    IStream("<memory>"),
#endif
    _buffer(nullptr), _pos(0), _size(0), _owns_buffer(true)
{
    FILE* f = fopen(fileName, "rb");
    if (f == nullptr)
    {
#ifdef INCLUDE_FORMAT_EXR
        throw IEX_NAMESPACE::InputExc("Could not read file");
#endif
        return;
    }

    fseek(f, 0, SEEK_END);
    _size = ftell(f);
    fseek(f, 0, SEEK_SET);

    _buffer = new char[_size];
    fread((char*)_buffer, 1, _size, f);
    fclose(f);
}

MyIStream::~MyIStream()
{
    if (_owns_buffer)
        delete[] _buffer;
}

bool MyIStream::read (char c[/*n*/], int n)
{
    if (_pos + n > _size)
    {
#ifdef INCLUDE_FORMAT_EXR
        throw IEX_NAMESPACE::InputExc("Unexpected end of file.");
#endif
        return false;
    }
    memcpy(c, _buffer + _pos, n);
    _pos += n;
    return _pos != _size;
}

uint64_t MyIStream::tellg ()
{
    return _pos;
}

void MyIStream::seekg (uint64_t pos)
{
	if (_pos > _size)
	{
#ifdef INCLUDE_FORMAT_EXR
        throw IEX_NAMESPACE::InputExc("Invalid seek offset");
#endif
        return;
    }
    _pos = pos;
}

void MyIStream::clear ()
{
}

MyOStream::MyOStream()
    :
#ifdef INCLUDE_FORMAT_EXR
    OStream("<mem>"),
#endif
    _pos(0)
{
    _buffer.reserve(16 * 1024 * 1024);
}

void MyOStream::write (const char c[/*n*/], int n)
{
    if (_pos == _buffer.size())
    {
        _buffer.insert(_buffer.end(), c, c + n);
    }
    else
    {
        if (_pos + n > _buffer.size())
            _buffer.resize(_pos + n);
        memcpy(_buffer.data() + _pos, c, n);
    }
    _pos += n;
}

uint64_t MyOStream::tellp()
{
    return _pos;
}

void MyOStream::seekp (uint64_t pos)
{
    if (pos > _buffer.size())
    {
        printf("wat? seeking %zi but buffer size is %zi\n", (size_t)pos, _buffer.size());
    }
    _pos = pos;
}
