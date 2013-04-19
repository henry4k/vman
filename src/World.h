#ifndef __VMAN_WORLD_H__
#define __VMAN_WORLD_H__

#include <vector>
#include <list>
#include <map>
#include <string>
#include <time.h>
#include <tinythread.h>

#include "vman.h"
#include "Chunk.h"


namespace vman
{


class Access;


enum LogLevel
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};


class World
{
public:
	/**
	 * @param layers Pointer to an array of layers.
	 * @param layerCount The arrays length.
	 * @param chunkEdgeLength Internal chunk edge length.
     * @param baseDir May be `NULL`, then nothing is stored on disk.
	 */
	World( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir );
	~World();


	/**
	 * @return The edge length of the chunks cube.
	 */
	int getChunkEdgeLength() const;

    /**
     * @return The amount of voxels a chunk contains.
     * `edgeLength^3`
     */
    int getVoxelsPerChunk() const;

    /**
     * @return The maximum size a voxel may occupy.
     */
    int getMaxLayerVoxelSize() const;


	/**
	 * @return The amount of voxel layers registered.
	 */
	int getLayerCount() const;

	/**
	 * @return A read only pointer to the layer definition
	 * or `NULL` when something went wrong. (e.g. out of bounds)
	 * @see getLayerCount
	 */
	const vmanLayer* getLayer( int index ) const;

    /**
     * Searches for a layer with the given name and returns its index.
     * @return The layers index or `-1` on failure.
     */
    int getLayerIndexByName( const char* name ) const;


    /**
     * Directory where the chunks are stored.
     * @return Base dir or `NULL` if saving to disk has been disabled.
     */
    const char* getBaseDir() const;


    /**
     * Generates the file name where a specific chunk could be stored.
     * Note that if the base dir is `NULL` the file name will be empty.
     * @see getBaseDir
     */
    std::string getChunkFileName( int chunkX, int chunkY, int chunkZ ) const;


    /**
     * Converts a voxel volume to an chunk volume.
     * I.e. the chunks that include the given voxels.
     */
    void voxelToChunkVolume( const vmanVolume* voxelVolume, vmanVolume* chunkVolume );


	/**
     * Get the chunks of the given coordinates.
	 * 
	 * Creates a chunk if it doesn't exists yet.
     * Chunks that need to be loaded from disk are locked.
     *
     * @param chunksOut
     * This array must have the size of the `chunkVolume`.
     * It is filled with the appropriate chunk pointers.
     * Access them with the Index3D() function.
     *
     * @param chunkVolume 
     * The volume that shall be retrieved.
     *
     * @param chunksOut
     * An array where the chunk pointers are copied to.
     * Should obviously have enough space for alle chunks of chunkVolume.
     *
     * @param priority
     * Parameter used to sort the resulting io jobs.
     * TODO
	 */
    void getVolume( const vmanVolume* chunkVolume, Chunk** chunksOut, int priority );


    /**
     * Whether to automatically save modified chunks in regular intervals.
     */
    void setAutoSave( bool enable );

    /**
     * Timout after that unreferenced chunks may be unloaded.
     * `0` disables this functionallity.
     */
    void setUnusedChunkTimeout( int seconds );

    /**
     * Should be less than `unusedChunkTimeout`.
     * TODO
     */
    void setServiceSleepTime( int seconds );

    /**
     * Writes all modified chunks to disk.
     * Is a no-op if saving to disk has been disabled.
     */
    void saveModifiedChunks();

	/**
	 *
	 */
	void addAccess( Access* access );
	
	/**
	 *
	 */
	void removeAccess( Access* access );

    /**
     *
     */
    void log( LogLevel level, const char* format, ... ) const;


private:
	World( const World& world );
	World& operator = ( const World& world );


    bool chunkFileExists( int chunkX, int chunkY, int chunkZ );

	/**
     * Get the chunk of the given coordinates.
	 * 
     * Note that these are not "voxel coordinates".
	 *- `chunkX = voxelX / chunkEdgeLength`
	 * Creates the chunk if it doesn't exists yet.
     * The function may block while loading a chunk from disk.
     * @param priority Priority when loading a chunk from disk.
	 * @return The chunk for the given chunk coordinates.
	 */
	Chunk* getChunkAt( int chunkX, int chunkY, int chunkZ, int priority );

    
    /**
     * Save or unload all unused chunks.
     * Modified chunsk are just saved instead and once the save job is done the next call of to this function will unload them.
     * This is okay, because this functions is intended to be called regulary.
     */
    void unloadUnusedChunks();


	std::vector<vmanLayer> m_Layers;
    int m_MaxLayerVoxelSize;
	int m_ChunkEdgeLength;
    bool m_AutoSave;
    int m_UnusedChunkTimeout;
    int m_ServiceSleepTime;

    std::map<ChunkId,Chunk*> m_ChunkMap; // Dimension
    std::string m_BaseDir;
	
    std::list<Access*> m_AccessObjects; // Dimension ?

    tthread::mutex m_Mutex;
    bool m_StopThreads;

    /**
     * Regulary runs saveModifiedChunks() and unloadUnusedChunks().
     */
    tthread::thread m_ServiceThread;
    static void ServiceThreadWrapper( void* worldInstance );
    void serviceThreadFn();

    typedef tthread::lock_guard<tthread::mutex> lock_guard;


    enum JobType
    {
        LOAD_JOB = 0,
        SAVE_JOB = 1
    };

    struct JobEntry
    {
        int priority;
        JobType jobType;
        ChunkId chunkId;
        Chunk* chunk;
    };

    tthread::condition_variable m_NewJobCondition;

    /**
     * Tries to find a job with the given chunk id.
     * Returns NULL if no job was found.
     */
    JobEntry* findJobByChunkId( ChunkId chunkId );

    /**
     * Adds a job to the job queue.
     * May eventually merge it with another job.
     * @param job The job pointer becomes invalid by calling this function.
     */
    void addJob( JobEntry* job );
    
    /**
     * Finds a suitable job, removes it from the job list and returns it.
     * So you have to delete it when you're done with it!
     */
    JobEntry* getJob();

    std::list<JobEntry*> m_JobList;
    int m_ActiveLoadJobs;
    int m_ActiveSaveJobs;
    
    std::vector<tthread::thread*> m_ThreadPool;
    static void JobThreadWrapper(void* worldInstance);
    void jobThreadFn();
};

}

#endif
