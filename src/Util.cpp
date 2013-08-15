#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "Util.h"

#if defined(__WINDOWS__)
	#define WIN32_LEAN_AND_MEAN
	#define NOGDI
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

namespace vman
{


bool IsLittleEndian_()
{
	const int i = 1;
	return ( (*(const char*)&i) != 0 );
}
const bool IsLittleEndian = IsLittleEndian_();



#if defined(__WINDOWS__)
const char DirSep = '\\';
#else
const char DirSep = '/';
#endif



#if defined(__WINDOWS__)
    FileType GetFileType( const char* path )
    {
        DWORD type = GetFileAttributesA(path);
        if(type == INVALID_FILE_ATTRIBUTES)
            return FILE_TYPE_INVALID;

        if(type & FILE_ATTRIBUTE_DIRECTORY)
            return FILE_TYPE_DIRECTORY;
        else
            return FILE_TYPE_REGULAR;
    }

    bool MakeDirectory( const char* path )
    {
        return CreateDirectory(path, NULL) == TRUE;
    }
#else
    FileType GetFileType( const char* path )
    {
        struct stat info;
        if(stat(path, &info) == -1)
            return FILE_TYPE_INVALID;
        if(info.st_mode & S_IFREG)
            return FILE_TYPE_REGULAR;
        else if(info.st_mode & S_IFDIR)
            return FILE_TYPE_DIRECTORY;
        else
            return FILE_TYPE_UNKNOWN;
    }

    bool MakeDirectory( const char* path )
    {
        return mkdir(path, 0777) == 0;
    }
#endif

bool MakePath( const char* path_ )
{
    char path[256];

    strncpy(path, path_, sizeof(path)-1);
    path[sizeof(path)-1] = '\0';

    for(int i = 0; path[i] != '\0'; ++i)
    {
        if(path[i] == '/' || path[i] == '\\')
        {
            path[i] = '\0';
            const FileType fileType = GetFileType(path);
            switch(GetFileType(path))
            {
                case FILE_TYPE_INVALID:
                    if(MakeDirectory(path) == false)
                        return false;
                    break;

                case FILE_TYPE_DIRECTORY:
                    // everything fine
                    break;

                default:
                    return false;
            }
            path[i] = DirSep;
        }
    }

    /*
    const FileType fileType = GetFileType(path);
    if(fileType == FILE_TYPE_INVALID)
        if(MakeDirectory(path) == false)
            return false;
    */
    return true;
}


std::string Format( const char* format, ... )
{
    static const int BUFFER_SIZE = 256;
    char buffer[BUFFER_SIZE];

    va_list vl;
    va_start(vl, format);
    int charsWritten = vsprintf(buffer, format, vl);
    va_end(vl);

    assert(charsWritten >= 0);
    assert(charsWritten < BUFFER_SIZE);
    return buffer;
}


std::string CoordsToString( int x, int y, int z )
{
    return Format("%d|%d|%d",
        x, y, z
    );
}

std::string VolumeToString( const vmanVolume* volume )
{
    return Format("%s => %s (%s)",
        CoordsToString(
            volume->x,
            volume->y,
            volume->z
        ).c_str(),
        CoordsToString(
            volume->x + volume->w - 1,
            volume->y + volume->h - 1,
            volume->z + volume->d - 1
        ).c_str(),
        CoordsToString(
            volume->w,
            volume->h,
            volume->d
        ).c_str()
    );
}

//tthread::mutex g_AddSecondsMutex;
time_t AddSeconds( const time_t tv, int seconds )
{
	/*
    lock_guard guard(g_AddSecondsMutex);

    struct tm helper;
    gmtime_r(&tv, &helper);

    helper.tm_sec += seconds;

    return mktime(&helper);
	*/
	return tv+seconds;
}

}
