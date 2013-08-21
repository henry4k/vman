#ifndef __VMAN_UTIL_H__
#define __VMAN_UTIL_H__

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <tinythread.h>
#include <string>

#include "vman.h"

#if defined(_WIN32) || defined(__WIN32__)
    #if !defined(__WINDOWS__)
        #define __WINDOWS__
    #endif

	#undef FILE_TYPE_UNKNOWN
	// ^- tinythread has to include windows.h
#endif


namespace vman
{
    // --- Logic ---

    /*
    template<class T>
    inline bool InclusiveInside( T min, T value, T max )
    {
        return value >= min && value <= max;
    }

    template<class T>
    inline bool ExclusiveInside( T min, T value, T max )
    {
        return value > min && value < max;
    }
    */


    // --- byte order ---

    extern const bool IsLittleEndian;

    inline uint16_t EndianSwap( uint16_t n )
    {
        return (n<<8) | (n>>8);
    }
    inline int16_t EndianSwap( int16_t n ) { return EndianSwap(n); }

    inline uint32_t EndianSwap( uint32_t n )
    {
        return (n<<24) | (n>>24) | ((n>>8)&0xFF00) | ((n<<8)&0xFF0000);
    }
    inline int32_t EndianSwap( int32_t n ) { return EndianSwap(n); }

    inline uint16_t LittleEndian( uint16_t n ) { return IsLittleEndian ? n : EndianSwap(n); }
    inline  int16_t LittleEndian(  int16_t n ) { return IsLittleEndian ? n : EndianSwap(n); }
    inline uint32_t LittleEndian( uint32_t n ) { return IsLittleEndian ? n : EndianSwap(n); }
    inline  int32_t LittleEndian(  int32_t n ) { return IsLittleEndian ? n : EndianSwap(n); }

    inline uint16_t BigEndian( uint16_t n ) { return !IsLittleEndian ? n : EndianSwap(n); }
    inline  int16_t BigEndian(  int16_t n ) { return !IsLittleEndian ? n : EndianSwap(n); }
    inline uint32_t BigEndian( uint32_t n ) { return !IsLittleEndian ? n : EndianSwap(n); }
    inline  int32_t BigEndian(  int32_t n ) { return !IsLittleEndian ? n : EndianSwap(n); }


    // --- fs path ---

    extern const char DirSep;

    enum FileType
    {
        FILE_TYPE_INVALID,
        FILE_TYPE_UNKNOWN,
        FILE_TYPE_REGULAR,
        FILE_TYPE_DIRECTORY
    };

    FileType GetFileType( const char* path );
    bool MakeDirectory( const char* path );

    /**
     * Creates directory entries for all segments followed by an directory separator.
     * So that a file at the given path could be created.
     */
    bool MakePath( const char* path );


    // --- multi dimensional arrays --

    inline int Index2D(
        int w, int h,
        int x, int y
    )
    {
        return x + y*w;
    }

    inline int Index3D(
        int w, int h, int d,
        int x, int y, int z
    )
    {
        return x + y*w + z*w*h;
    }


    // --- Threads ---

    typedef tthread::lock_guard<tthread::mutex> lock_guard;


    // --- string ---

    std::string Format( const char* format, ... );

    std::string CoordsToString( int x, int y, int z );

    std::string SelectionToString( const vmanSelection* selection );


    // --- time ---

    time_t AddSeconds( const time_t tv, int seconds );
}

#endif
