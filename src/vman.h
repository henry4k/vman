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


/**
 * Call this function on abnormal or abprupt program termination.
 */
VMAN_API void vmanPanicExit();


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


// -- Statistics --

typedef struct
{
    int chunkGetHits;
    int chunkGetMisses;
    
    int chunkLoadOps;
    int chunkSaveOps;
    int chunkUnloadOps;

    int readOps;
    int writeOps;

    int maxLoadedChunks;
    int maxScheduledChecks;
    int maxEnqueuedJobs;
} vmanStatistics;


// -- Volume --

typedef enum
{
    VMAN_LOG_DEBUG = 0,
    VMAN_LOG_INFO,
    VMAN_LOG_WARNING,
    VMAN_LOG_ERROR
} vmanLogLevel;

typedef struct
{
    /**
     * Array that describes the data layers available to each voxel.
     */
    const vmanLayer* layers;

    /**
     * Amount of elements stored in `layers`.
     */
    int layerCount;

    /**
     * Defines the selection used for the internal chunks.
     * Don't change this later on!
     */
    int chunkEdgeLength;

    /**
     * Chunks are stored here.
     * May be `NULL`, then nothing is saved to disk.
     */
    const char* baseDir;

    /**
     * Whether statistics should be enabled.
     */
    bool enableStatistics;

    /**
     * Callback for log messages.
     * If `NULL` vman uses its internal logging function.
     */
    void (*logFn)( vmanLogLevel level, const char* message );

} vmanVolumeParameters;


/**
 * Initializes a volume parameter structure.
 */
VMAN_API void vmanInitVolumeParameters( vmanVolumeParameters* parameters );



typedef void* vmanVolume;


/**
 * Creates a new volume object.
 * @param parameters Parameter structure. Should be initialized using vmanInitVolumeParameters before.
 * @return `NULL` when something went wrong.
 */
VMAN_API vmanVolume vmanCreateVolume( const vmanVolumeParameters* parameters );


/**
 * Deletes the given volume object and all its allocated resources.
 */
VMAN_API void vmanDeleteVolume( const vmanVolume volume );


/**
 * Timeout after that unreferenced chunks are unloaded.
 * Negative values disable this behaviour.
 */
VMAN_API void vmanSetUnusedChunkTimeout( const vmanVolume volume, int seconds );


/**
 * Timeout after that modified chunks are saved to disk.
 * Negative values disable this behaviour.
 */
VMAN_API void vmanSetModifiedChunkTimeout( const vmanVolume volume, int seconds );


/**
 * Resets all statistics to zero.
 */
VMAN_API void vmanResetStatistics( const vmanVolume volume );


/**
 * Writes the current statistics to `statisticsDestination`.
 * @param statisticsDestination Statistics are written to this structure.
 * @return whether the operation succeeded. May return `false` even if statistics were enabled.
 */
VMAN_API bool vmanGetStatistics( const vmanVolume volume, vmanStatistics* statisticsDestination );


// -- Selection --

typedef struct
{
    int x, y, z;
    int w, h, d;
} vmanSelection;


// -- Access --

typedef enum
{
    VMAN_READ_ACCESS  = 1,
    VMAN_WRITE_ACCESS = 2
} vmanAccessMode;

typedef void* vmanAccess;

/**
 * Creates an access object, which provides r/w access to the volume.
 * Will preload chunks as soon as a valid selection is set.
 * Initially the selection will be invalid and all r/w operations will fail.
 * @return NULL when something went wrong.
 */
VMAN_API vmanAccess vmanCreateAccess( const vmanVolume volume );

/**
 * Deleting a locked access object will cause an error!
 */
VMAN_API void vmanDeleteAccess( const vmanAccess access );

/**
 * Updates the selection.
 * At this point the affected chunks will be precached and preloaded.
 * Setting the selection to NULL renders it invalid and will prevent any r/w operations.
 */
VMAN_API void vmanSelect( vmanAccess access, const vmanSelection* selection );

/**
 * Locks access to the specified selection.
 * May block when intersecting chunks are already locked by other access objects.
 * May also block while affected chunks are loaded from disk.
 * Multiple access objects may read simultaneously from the same chunk.
 * @param mode Access mode bitmask.
 * @see vmanAccessMode
 */
VMAN_API void vmanLockAccess( vmanAccess access, int mode );


/**
 * Behaves like vmanLockAccess, except that it returns 0 if the selection is already locked.
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
 * Will return NULL if the voxel lies outside the selection or
 * an incomplatible access mode has been selected.
 */
VMAN_API const void* vmanReadVoxelLayer( const vmanAccess access, int x, int y, int z, int layer );

/**
 * @return A pointer to the voxel data in the specified layer.
 * Will return NULL if the voxel lies outside the selection or
 * an incomplatible access mode has been selected.
 * Read operations may yield undefined values if write only is active.
 */
VMAN_API void* vmanReadWriteVoxelLayer( const vmanAccess access, int x, int y, int z, int layer );


#ifdef __cplusplus
}
#endif

#endif
