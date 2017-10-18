#include "filesystem.h"

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Commdlg.h>
#include <shlobj.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#undef near
#undef far
#undef interface
#endif

#include <cassert>

#ifdef _MSC_VER
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '\/'
#endif

namespace fnd
{
    namespace filesystem
    {
        /* Path implementation */

        Path::Path(const char* path)
        {
            Set(path);
        }

        void Path::Set(const char* path)
        {
            memset(_buffer, 0x0, MAX_PATH_LEN);
            _length = strlen(path);
            _length = _length > MAX_PATH_LEN ? MAX_PATH_LEN : _length;
            memcpy(_buffer, path, _length);

            Normalize();
        }

        void Path::Append(const char* subpath)
        {
            assert(_length < (MAX_PATH_LEN - 1));
            _buffer[_length] = PATH_SEPARATOR;
            _length += 1;

            size_t subLen = strlen(subpath);
            subLen = _length + subLen > MAX_PATH_LEN ? MAX_PATH_LEN - _length : subLen;
            memcpy(_buffer + _length, subpath, subLen);
            _length += subLen;

            Normalize();
        }

        void Path::Normalize()
        {
            if (_isNormalized) { return; }
            for (size_t i = 0; i < _length; ++i) {
                if (_buffer[i] == '\\' || _buffer[i] == '/') {
                    _buffer[i] = PATH_SEPARATOR;
                }
            }
            _isNormalized = true;
            if (IsDirectory()) {
                if (_buffer[_length - 1] != PATH_SEPARATOR) {
                    assert(_length < MAX_PATH_LEN);
                    _buffer[_length++] = PATH_SEPARATOR;                      
                }
            }
        }

        bool Path::IsValid()
        {
            Normalize();
            return IsDirectory() || IsFile();
        }

        bool Path::IsDirectory()
        {
#ifdef _MSC_VER
            Normalize();
            DWORD attribs = GetFileAttributesA(_buffer);
            return (attribs != INVALID_FILE_ATTRIBUTES &&
                (attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
            return false;   // @TODO 
#endif
        }

        bool Path::IsFile(FileInfo* outFileInfo)
        {
#ifdef _MSC_VER
            Normalize();
            DWORD attribs = GetFileAttributesA(_buffer);
            return (attribs != INVALID_FILE_ATTRIBUTES &&
                !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
            return false;   // @TODO 
#endif
        }

        bool Path::IsFile()
        {
            FileInfo fileInfo;
            return IsFile(&fileInfo);
        }
    }
}