#ifndef __VMAN_VOLUME_H__
#define __VMAN_VOLUME_H__

#include <vector>
#include <list>
#include <map>
#include <set>
#include <string>
#include <time.h>
#include <tinythread.h>

#include "vman.h"
#include "Chunk.h"
#include "JobEntry.h"


namespace vman
{


/*
enum Statistic
{
    STATISTIC_CHUNK_GET_HITS = 0,
    STATISTIC_CHUNK_GET_MISSES,

    STATISTIC_CHUNK_LOAD_OPS,
    STATISTIC_CHUNK_SAVE_OPS,
    STATISTIC_CHUNK_UNLOAD_OPS,

    STATISTIC_READ_OPS,
    STATISTIC_WRITE_OPS,

    STATISTIC_MAX_LOADED_CHUNKS,
    STATISTIC_MAX_SCHEDULED_CHECKS,
    STATISTIC_MAX_ENQUEUED_JOBS,

    STATISTIC_COUNT
};
*/


class Volume
{
public:
    /**
     * @see vmanVolumeParameters
     */
    Volume( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir );
    ~Volume();

    /**
     * Is thread safe.
     * @return The maximum size a voxel may occupy.
     */
    int getMaxLayerVoxelSize() const;

    /**
     * Is thread safe.
     * @return The amount of voxel layers registered.
     */
    int getLayerCount() const;

    /**
     * Is thread safe.
     * @return A read only pointer to the layer definition
     * or `NULL` when something went wrong. (e.g. out of bounds)
     * @see getLayerCount
     */
    const vmanLayer* getLayer( int index ) const;

    /**
     * Searches for a layer with the given name and returns its index.
     * Is thread safe.
     * @return The layers index or `-1` on failure.
     */
    int getLayerIndexByName( const char* name ) const;

    /**
     * Is thread safe.
     * @return The edge length of the chunks cube.
     */
    int getChunkEdgeLength() const;

    /**
     * Is thread safe.
     * @return The amount of voxels a chunk contains.
     * `edgeLength^3`
     */
    int getVoxelsPerChunk() const;


    /**
     * Directory where the chunks are stored.
     * Is thread safe.
     * @return Base dir or `NULL` if saving to disk has been disabled.
     */
    const char* getBaseDir() const;


    /**
     * Generates the file name where a specific chunk could be stored.
     * Note that if the base dir is `NULL` the file name will be empty.
     * Is thread safe.
     * @see getBaseDir
     */
    std::string getChunkFileName( int chunkX, int chunkY, int chunkZ ) const;


    /**
     * Converts voxel to chunk coordinates.
     * Is thread safe.
     */
    void voxelToChunkCoordinates( const int voxelX, const int voxelY, const int voxelZ, int* chunkX, int* chunkY, int* chunkZ );

    /**
     * Converts a voxel selection to an chunk selection.
     * I.e. the chunks that include the given voxels.
     * Is thread safe.
     */
    void voxelToChunkSelection( const vmanSelection* voxelSelection, vmanSelection* chunkSelection );


    /**
     * Get the chunks of the given coordinates.
     *
     * Creates a chunk if it doesn't exists yet.
     * Chunks that need to be loaded from disk are locked.
     *
     * @param chunksOut
     * This array must have the size of the `chunkSelection`.
     * It is filled with the appropriate chunk pointers.
     * Access them with the Index3D() function.
     *
     * @param chunkSelection
     * The selection that shall be retrieved.
     *
     * @param chunksOut
     * An array where the chunk pointers are copied to.
     * Should obviously have enough space for alle chunks of chunkSelection.
     *
     * @param priority
     * Parameter used to sort the resulting io jobs.
     * TODO
     */
    void getSelection( const vmanSelection* chunkSelection, Chunk** chunksOut, int priority );

    /**
     * Checks if a chunk should be saved or unloaded and runs these actions.
     * Note that this function uses the chunks mutex.
     * Is thread safe.
     * @return `true` if the chunk was deleted.
     */
    bool checkChunk( ChunkId chunkId );


    /**
     * Writes all modified chunks to disk.
     * Is a no-op if saving to disk has been disabled.
     */
    void saveModifiedChunks();

    /**
     *
     */
    // void unloadUnusedChunks(); // TODO

    /**
     * For logging volume specific messages.
     * Is thread safe.
     */
    void log( vmanLogLevel level, const char* format, ... ) const;


    /**
     * Resets all statistics to zero.
     * Is thread safe.
     */
    //void resetStatistics();


    /**
     * Increments a statistic.
     * Is thread safe.
     */
    //void incStatistic( Statistic statistic, int amount = 1 );


    /**
     * Decrements a statistic.
     * Is thread safe.
     */
    //void decStatistic( Statistic statistic, int amount = 1 );


    /**
     * Sets the value if its greater than the current one.
     * Is thread safe.
     */
    //void minStatistic( Statistic statistic, int value );


    /**
     * Sets the value if its lower than the current one.
     * Is thread safe.
     */
    //void maxStatistic( Statistic statistic, int value );


    /**
     * Writes the current statistics to `statisticsDestination`.
     * @param statisticsDestination Statistics are written to this structure.
     * @return whether the operation succeeded. May return `false` even if statistics were enabled.
     */
    //bool getStatistics( vmanStatistics* statisticsDestination ) const;


    /**
     * Use this to lock the object while
     * using methods that aren't thread safe.
     */
    tthread::mutex* getMutex();


private:
    Volume( const Volume& volume );
    Volume& operator = ( const Volume& volume );


    bool chunkFileExists( int chunkX, int chunkY, int chunkZ );

    /**
     * Get the chunk of the given coordinates.
     *
     * Note that these are not "voxel coordinates".
     * `chunkX = voxelX / chunkEdgeLength`
     * Creates the chunk if it doesn't exists yet.
     * The function may block while loading a chunk from disk.
     * @param priority Priority when loading a chunk from disk.
     * @return The chunk for the given chunk coordinates.
     */
    Chunk* getChunkAt( int chunkX, int chunkY, int chunkZ, int priority );

    /**
     * Get the chunk with the given id.
     * @return The chunk with the given id or `NULL` if its not loaded/available.
     */
    Chunk* getLoadedChunkById( ChunkId id );


    std::vector<vmanLayer> m_Layers;
    int m_MaxLayerVoxelSize;

    int m_ChunkEdgeLength;

    std::map<ChunkId,Chunk*> m_ChunkMap; // Dimension
    std::string m_BaseDir;

    mutable tthread::mutex m_Mutex;
};

}

#endif
