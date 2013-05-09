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
     * Timeout after that unreferenced chunks are unloaded.
     * `0` disables this functionallity.
     */
    void setUnusedChunkTimeout( int seconds );

    /**
     * Timeout after that unreferenced chunks are unloaded.
     * @return `0` if disabled.
     */
    int getUnusedChunkTimeout() const;

    /**
     * Timeout after that modified chunks are saved to disk.
     * `0` disables this functionallity.
     */
    void setModifiedChunkTimeout( int seconds );

    /**
     * Timeout after that modified chunks are saved to disk.
     * @return `0` if disabled.
     */
    int getModifiedChunkTimeout() const;

    /**
     * Writes all modified chunks to disk.
     * Is a no-op if saving to disk has been disabled.
     */
    //void saveModifiedChunks(); // TODO

    /**
     * 
     */
    // void unloadUnusedChunks(); // TODO


    enum ScheduledTaskType
    {
        UNLOAD_UNUSED_TASK,
        SAVE_MODIFIED_TASK
    };

    /**
     * Schedules tasks that will be run in the future.
     * `scheduled_time = now + wait_duration`
     * While the duration is defined by the tasks type.
     * E.g. for the `UNLOAD_UNUSED_TASK` it uses getUnusedChunkTimeout().
     */
    void scheduleTask( ScheduledTaskType type, Chunk* chunk );


    /**
     * For logging vman specific messages.
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
     * Wipes a chunk out of existence.
     */
    void eraseChunk( Chunk* chunk );

    
    /**
     * Save or unload all unused chunks.
     * Modified chunsk are just saved instead and once the save job is done the next call of to this function will unload them.
     * This is okay, because this functions is intended to be called regulary.
     */
    void unloadUnusedChunks();


	std::vector<vmanLayer> m_Layers;
    int m_MaxLayerVoxelSize;
	int m_ChunkEdgeLength;
    int m_ServiceSleepTime;

    std::map<ChunkId,Chunk*> m_ChunkMap; // Dimension
    std::string m_BaseDir;
	
    tthread::mutex m_Mutex;
    bool m_StopThreads;

    typedef tthread::lock_guard<tthread::mutex> lock_guard;


    // --- Scheduled Tasks ---
    
    int m_UnusedChunkTimeout;
    int m_ModifiedChunkTimeout;

    struct ScheduledTask
    {
        time_t executionTime;
        ScheduledTaskType type;
        Chunk* chunk;
    };

    /**
     * Internal version of `scheduleTask` with time parameter.
     * Don't use this directly.
     */
    void scheduleTask( ScheduledTaskType type, Chunk* chunk, double seconds );

    std::list<ScheduledTask> m_ScheduledTasks;
    tthread::thread m_SchedulerThread;
    tthread::condition_variable m_SchedulerReevaluateCondition;
    static void SchedulerThreadWrapper( void* worldInstance );
    void schedulerThreadFn();


    // --- Load/Save Jobs ---

    enum JobType
    {
        INVALID_JOB,
        LOAD_JOB,
        SAVE_JOB
    };

    struct JobEntry
    {
        int priority;
        JobType type;
        Chunk* chunk;
    };

    static JobEntry InvalidJob();

    tthread::condition_variable m_NewJobCondition;

    /**
     * Tries to find a job with the given chunk id.
     * Returns an invalid job if nothing was found.
     */
    JobEntry findJobByChunk( Chunk* chunk ) const;

    /**
     * Adds a job to the job queue.
     * May eventually merge it with another job.
     */
    void addJob( JobType type, int priority, Chunk* chunk );
    
    /**
     * Finds a suitable job, removes it from the job list and returns it.
     */
    JobEntry getJob();

    std::list<JobEntry> m_JobList;
    int m_ActiveLoadJobs;
    int m_ActiveSaveJobs;
    
    std::vector<tthread::thread*> m_ThreadPool;
    static void JobThreadWrapper(void* worldInstance);
    void jobThreadFn();
};

}

#endif
