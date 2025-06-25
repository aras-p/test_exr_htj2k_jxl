#include "fileio.h"
#include "Iex.h"
#include <stdio.h>

MyIStream::MyIStream(const char* fileName)
	: IStream("<memory>"), _buffer(nullptr), _pos(0), _size(0), _owns_buffer(true)
{
    FILE* f = fopen(fileName, "rb");
    if (f == nullptr)
    {
        throw IEX_NAMESPACE::InputExc("Could not read file");
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
		throw IEX_NAMESPACE::InputExc("Unexpected end of file.");
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
		throw IEX_NAMESPACE::InputExc("Invalid seek offset");
    }
    _pos = pos;
}

void MyIStream::clear ()
{
}

MyOStream::MyOStream()
    : OStream("<mem>"), _pos(0)
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
