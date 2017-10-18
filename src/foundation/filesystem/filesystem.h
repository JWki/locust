#pragma once
#include "../int_types.h"

namespace fnd
{
    namespace filesystem
    {
        static const size_t MAX_PATH_LEN = 1024;

        typedef uint64_t FileTime;

        struct FileInfo;

        struct Path
        {
            bool    _isNormalized = false;
            size_t  _length = 0;
            char    _buffer[MAX_PATH_LEN] = "";
        
            operator const char* () { return _buffer; }
            operator char* () { return _buffer; }

            Path() = default;
            Path(const char* path);

            void Set(const char* path);
            void Append(const char* subpath);

            /* Normalizes path separators, directory separator, etc */
            void    Normalize();

            bool    IsDirectory();
            bool    IsFile();
            bool    IsFile(FileInfo* outFileInfo);
            bool    IsValid();
        };

        struct FileInfo
        {
            Path        path;
            FileTime    lastModifiedTime = 0;
        };
    }
}
