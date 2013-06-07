#ifndef __VMAN_H__
#define __VMAN_H__

// VMAN_SHARED: Define this if your linking vman dynamically.
// VMAN_BUILDING_SHARED: Define this when actually building the shared library.

#if defined(VMAN_SHARED)
    #if defined(_WIN32) || defined(__CYGWIN__)
        #if defined(VMAN_BUILDING_SHARED)
            #if defined(__GNUC__)
                #define VMAN_API __attribute__ ((dllexport))
            #else
                #define VMAN_API __declspec(dllexport)
            #endif
        #else
            #if defined(__GNUC__)
                #define VMAN_API __attribute__ ((dllimport))
            #else
                #define VMAN_API __declspec(dllimport)
            #endif
        #endif
    #else
        #if (__GNUC__ >= 4)
            #define VMAN_API __attribute__ ((visibility ("default")))
        #else
            #define VMAN_API
        #endif
    #endif
#else
    #define VMAN_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif


// -- Status --

typedef enum
{
	VMAN_NO_ERROR = 0,
	VMAN_OUT_OF_MEMORY
} vmanError;

VMAN_API int vmanGetError();

enum
{
    VMAN_MAX_LAYER_NAME_LENGTH = 31
};

// -- Layer --

typedef struct
{
    /**
     * Name of the layer, used to identify it.
     * May use up to VMAN_MAX_LAYER_NAME_LENGTH characters.
     */
	const char* name;
	
    /**
     * Bytes single voxel of this layer occupies.
     */
    int voxelSize;
    
    /**
     * Revision number 
     */
    int revision;

    /**
     * Used to convert voxels in a portable representation. (e.g. when saving them to disk)
     * Serialize to little endian, since most target machines use it and results in a noop there.
     * @count Amount of voxels that are affected. Length of source and destination is computed by `bytes*count`.
     */
    void (*serializeFn)( const void* source, void* destination, int count );

    /**
     * Used to convert voxels from their portable representation. (e.g. when loading them from disk)
     * Deserialize from little endian, since most target machines use it and results in a noop there.
     * @count Amount of voxels that are affected. Length of source and destination is computed by `bytes*count`.
     */
    void (*deserializeFn)( const void* source, void* destination, int count );

    // ...
} vmanLayer;


// -- World --

typedef void* vmanWorld;

/**
 * Creates a new world object.
 * @param layers Array that describes the data layers available to each voxel.
 * @param layerCount Amount of elements stored in layers.
 * @param chunkEdgeLength Defines the volume used for the internal chunks. Dont change this later on!
 * @param baseDir Chunks are stored here. If NULL nothing is stored to disk.
 * @return NULL when something went wrong.
 */
VMAN_API vmanWorld vmanCreateWorld( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir );

/**
 * Deletes the given world object and all its allocated resources.
 */
VMAN_API void vmanDeleteWorld( const vmanWorld world );


// -- Volume --

typedef struct
{
	int x, y, z;
	int w, h, d;
} vmanVolume;


// -- Access --

typedef enum
{
	VMAN_READ_ACCESS  = 1,
	VMAN_WRITE_ACCESS = 2
} vmanAccessMode;

typedef void* vmanAccess;

/**
 * Creates an access object, which provides r/w access to the world.
 * Will preload chunks as soon as a valid volume is set.
 * Initially the volume will be invalid and all r/w operations will fail.
 * @return NULL when something went wrong.
 */
VMAN_API vmanAccess vmanCreateAccess( const vmanWorld world );

/**
 * Deleting a locked access object will cause an error!
 */
VMAN_API void vmanDeleteAccess( const vmanAccess access );

/**
 * Updates the volume.
 * At this point the affected chunks will be precached and preloaded.
 * Setting the volume to NULL renders it invalid and will prevent any r/w operations.
 */
VMAN_API void vmanSetAccessVolume( vmanAccess access, const vmanVolume* volume );

/**
 * Locks access to the specified volume.
 * May block when intersecting chunks are already locked by other access objects.
 * May also block while affected chunks are loaded from disk.
 * Multiple access objects may read simultaneously from the same chunk.
 * @param mode Access mode bitmask.
 * @see vmanAccessMode
 */
VMAN_API void vmanLockAccess( vmanAccess access, int mode );


/**
 * Behaves like vmanLockAccess, except that it returns 0 if the volume is already locked.
 * Returns a positive value on success.
 * @see vmanLockAccess
 */
VMAN_API int vmanTryLockAccess( vmanAccess access, int mode );


/**
 * Unlocks access.
 */
VMAN_API void vmanUnlockAccess( vmanAccess access );

/**
 * @return A read only pointer to the voxel data in the specified layer.
 * Will return NULL if the voxel lies outside the volume or
 * an incomplatible access mode has been selected.
 */
VMAN_API const void* vmanReadVoxelLayer( const vmanAccess access, int x, int y, int z, int layer );

/**
 * @return A pointer to the voxel data in the specified layer.
 * Will return NULL if the voxel lies outside the volume or
 * an incomplatible access mode has been selected.
 * Read operations may yield undefined values if write only is active.
 */
VMAN_API void* vmanReadWriteVoxelLayer( const vmanAccess access, int x, int y, int z, int layer );


#ifdef __cplusplus
}
#endif

#endif
