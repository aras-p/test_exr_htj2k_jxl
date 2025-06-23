#include "fileio.h"
#include "Iex.h"
#include <sys/stat.h>
#ifdef _MSC_VER
#include <fileapi.h>
#endif

void TurnOffFileCache(FILE* f)
{
    /*
     //@TODO: looks like this turns off a whole bunch of caching somwhere in CRT level too, blergh
    int fd = fileno(f);
    int err = fcntl(fd, F_NOCACHE, 1);
    if (err != 0)
    {
        throw new std::string("Argh");
    }
     */
}

#ifdef _MSC_VER
static int roundUp(int numToRound, int multiple)
{
	return (numToRound + multiple - 1) & -multiple;
}
#endif

MyIStream::MyIStream(const char* fileName)
	: IStream(fileName)
{
#ifdef _MSC_VER
    _pos = 0;
    _size = 0;

	HANDLE file = CreateFileA(fileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		printf("Failed to open file %s: %x\n", fileName, GetLastError());
		throw IEX_NAMESPACE::InputExc("Could not read file");
	}
    DWORD sizeHi = 0;
	DWORD sizeLo = GetFileSize(file, &sizeHi);
    _size = (uint64_t(sizeHi) << 32) | sizeLo;
    _buffer = (char*)VirtualAlloc(NULL, sizeLo, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (_buffer == 0)
	{
		printf("Failed to alloc memory: %x\n", GetLastError());
		throw IEX_NAMESPACE::InputExc("Failed to alloc");
	}
	_pos = 0;
    DWORD read = 0;
    DWORD toRead = roundUp(sizeLo, 4096);
    BOOL ok = ReadFile(file, _buffer, toRead, &read, NULL);
    if (!ok)
    {
		printf("Failed to read file %s: %x\n", fileName, GetLastError());
		throw IEX_NAMESPACE::InputExc("Could not read file");
    }
    CloseHandle(file);
#else
    _file = fopen(fileName, "rb");
    if (!_file)
        throw IEX_NAMESPACE::InputExc("Could not open file");
	TurnOffFileCache(_file);
#endif
}

MyIStream::~MyIStream()
{
#ifdef _MSC_VER
    VirtualFree(_buffer, 0, MEM_RELEASE);
#else
    fclose(_file);
#endif
}


MyOStream::MyOStream(const char* fileName)
	: OStream(fileName), _file(0)
{
#ifdef _MSC_VER
	_buffer = (char*)VirtualAlloc(NULL, 1024*1024*1024, MEM_RESERVE, PAGE_READWRITE); // just reserve 1GB
	if (_buffer == 0)
	{
		printf("Failed to reserve memory: %x\n", GetLastError());
		throw IEX_NAMESPACE::InputExc("Failed to reserve");
	}
    _size = 0;

    _file = CreateFileA(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING, NULL);
    if (_file == INVALID_HANDLE_VALUE)
    {
        printf("Failed to open file %s: %x\n", fileName, GetLastError());
        throw IEX_NAMESPACE::InputExc("Could not write file");
    }
    _pos = 0;
#else
	_file = fopen(fileName, "wb");
	if (!_file)
		throw IEX_NAMESPACE::InputExc("Could not write file");
	TurnOffFileCache(_file);
#endif
}

MyOStream::~MyOStream()
{
#ifdef _MSC_VER
	DWORD written = 0;
	BOOL ok = WriteFile(_file, _buffer, (DWORD)roundUp(_size,4096), &written, NULL);
    if (!ok)
    {
        printf("ERROR: failed to write\n");
    }
    LARGE_INTEGER fileSize;
    fileSize.QuadPart = _size;
    SetFileInformationByHandle(_file, FileEndOfFileInfo, &fileSize, sizeof(fileSize));
    CloseHandle(_file);
#else
	fclose(_file);
#endif
}


bool MyIStream::read (char c[/*n*/], int n)
{
#ifdef _MSC_VER
    if (_pos + n > _size)
    {
		throw IEX_NAMESPACE::InputExc("Unexpected end of file.");
    }
    memcpy(c, _buffer + _pos, n);
    _pos += n;
    return _pos != _size;
#else
    if (n != static_cast<int>(fread (c, 1, n, _file)))
    {
        // fread() failed, but the return value does not distinguish
        // between I/O errors and end of file, so we call ferror() to
        // determine what happened.
        //if (ferror (_file))
        //    IEX_NAMESPACE::throwErrnoExc();
        //else
            throw IEX_NAMESPACE::InputExc ("Unexpected end of file.");
    }
    return feof (_file);
#endif
}
uint64_t MyIStream::tellg ()
{
#ifdef _MSC_VER
    return _pos;
#else
    return ftell (_file);
#endif
}
void MyIStream::seekg (uint64_t pos)
{
#ifdef _MSC_VER
	if (_pos > _size)
	{
		throw IEX_NAMESPACE::InputExc("Invalid seek offset");
    }
    _pos = pos;
#else
    clearerr (_file);
    fseek (_file, static_cast<long>(pos), SEEK_SET);
#endif
}
void MyIStream::clear ()
{
#ifdef _MSC_VER
#else
    clearerr (_file);
#endif
}
void MyOStream::write (const char c[/*n*/], int n)
{
#ifdef _MSC_VER
    VirtualAlloc(_buffer + _pos, n, MEM_COMMIT, PAGE_READWRITE);
    memcpy(_buffer + _pos, c, n);
    if (_pos + n > _size)
        _size = _pos + n;
    _pos += n;
#else
    clearerr (_file);
    if (n != static_cast<int>(fwrite (c, 1, n, _file)))
        //IEX_NAMESPACE::throwErrnoExc();
        throw IEX_NAMESPACE::InputExc ("Failed to write.");
#endif
}
uint64_t MyOStream::tellp()
{
#ifdef _MSC_VER
    return _pos;
#else
    return ftell (_file);
#endif
}
void MyOStream::seekp (uint64_t pos)
{
#ifdef _MSC_VER
    if (pos > _size)
    {
        printf("wat? seeking %i but buffer size is %i\n", (int)pos, (int)_size);
    }
    _pos = pos;
#else
    clearerr (_file);
    fseek (_file, static_cast<long>(pos), SEEK_SET);
#endif
}

size_t GetFileSize(const char* path)
{
    struct stat st = {};
    stat(path, &st);
    return st.st_size;
}
