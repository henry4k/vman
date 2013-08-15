#ifndef __VMAN_WORLD_H__
#define __VMAN_WORLD_H__

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


class Access;


enum LogLevel
{
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

enum Statistic
{
	STATISTIC_CHUNK_GET_HITS = 0,
	STATISTIC_CHUNK_GET_MISSES,

	STATISTIC_CHUNK_LOAD_OPS,
	STATISTIC_CHUNK_SAVE_OPS,
	STATISTIC_CHUNK_UNLOAD_OPS,
	
	STATISTIC_VOLUME_READ_HITS,
	STATISTIC_VOLUME_WRITE_HITS,
	
	STATISTIC_MAX_LOADED_CHUNKS,
	STATISTIC_MAX_SCHEDULED_CHECKS,
	STATISTIC_MAX_ENQUEUED_JOBS,

	STATISTIC_COUNT
};


class World
{
public:
	/**
	 * @param layers Pointer to an array of layers.
	 * @param layerCount The arrays length.
	 * @param chunkEdgeLength Internal chunk edge length.
     * @param baseDir May be `NULL`, then nothing is stored on disk.
     * @param enableStatistics Whether statistics should be enabled.
	 */
	World( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir, bool enabledStatistics );
	~World();


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
     * Converts a voxel volume to an chunk volume.
     * I.e. the chunks that include the given voxels.
     * Is thread safe.
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
     * Negative values disable this behaviour.
     */
    void setUnusedChunkTimeout( int seconds );

    /**
     * Timeout after that unreferenced chunks are unloaded.
     * @return Timeout or `-1` if disabled.
     */
    int getUnusedChunkTimeout() const;

    /**
     * Timeout after that modified chunks are saved to disk.
     * Negative values disable this behaviour.
     */
    void setModifiedChunkTimeout( int seconds );

    /**
     * Timeout after that modified chunks are saved to disk.
     * @return Timeout or `-1` if disabled.
     */
    int getModifiedChunkTimeout() const;

    /**
     * Writes all modified chunks to disk.
     * Is a no-op if saving to disk has been disabled.
     */
	void saveModifiedChunks();

    /**
     *
     */
    // void unloadUnusedChunks(); // TODO


    enum CheckCause
    {
        CHECK_CAUSE_UNUSED,
        CHECK_CAUSE_MODIFIED
    };

    /**
     * Schedules tasks that will be run in the future.
     * `scheduled_time = now + wait_duration`
     * While the duration is defined by the tasks type.
     * E.g. for the `CHECK_CAUSE_UNUSED` it uses getUnusedChunkTimeout().
     */
    void scheduleCheck( CheckCause cause, Chunk* chunk );


    /**
     * For logging vman specific messages.
     * Is thread safe.
     */
    void log( LogLevel level, const char* format, ... ) const;


	/**
	 * Resets all statistics to zero.
	 * Is thread safe.
	 */
	void resetStatistics();


	/**
	 * Increments a statistic.
	 * Is thread safe.
	 */
	void incStatistic( Statistic statistic, int amount = 1 );


	/**
	 * Decrements a statistic.
	 * Is thread safe.
	 */
	void decStatistic( Statistic statistic, int amount = 1 );


	/**
	 * Sets the value if its greater than the current one.
	 * Is thread safe.
	 */
	void minStatistic( Statistic statistic, int value );


	/**
	 * Sets the value if its lower than the current one.
	 * Is thread safe.
	 */
	void maxStatistic( Statistic statistic, int value );


	/**
     * Writes the current statistics to `statisticsDestination`.
     * @param statisticsDestination Statistics are written to this structure.
     * @return whether the operation succeeded. May return `false` even if statistics were enabled.
	 */
	bool getStatistics( vmanStatistics* statisticsDestination ) const;


    /**
     * Use this to lock the object while
     * using methods that aren't thread safe.
     */
    tthread::mutex* getMutex();


	/**
	 * Call this function on abnormal or abprupt program termination.
	 */
	static void PanicExit();
	

private:
	World( const World& world );
	World& operator = ( const World& world );

	static tthread::mutex   s_PanicMutex;
	static std::set<World*> s_PanicWorldSet;
	
	void panicExit();


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


	/**
	 * Checks if a chunk should be saved or unloaded and runs these actions.
	 * Note that this function uses the chunks mutex.
     * @return `true` if the chunk was deleted.
     */
	bool checkChunk( Chunk* chunk );


	std::vector<vmanLayer> m_Layers;
    int m_MaxLayerVoxelSize;
	int m_ChunkEdgeLength;

    std::map<ChunkId,Chunk*> m_ChunkMap; // Dimension
    std::string m_BaseDir;

    mutable tthread::mutex m_Mutex;


    mutable tthread::mutex m_LogMutex;


	// --- Statistics ---
	
	bool m_StatisticsEnabled; // thread safe (is only set in the constructor)
	tthread::atomic_int m_Statistics[STATISTIC_COUNT];


    // --- Scheduled Checks ---

    int m_UnusedChunkTimeout;
    int m_ModifiedChunkTimeout;

    struct ScheduledCheck
    {
        time_t executionTime;
        ChunkId chunkId;
    };

    /**
     * Internal version of `scheduleCheck` with time parameter.
     * Don't use this directly.
     */
    void scheduleCheck( Chunk* chunk, double seconds );

    /**
     * This list needs its own mutex,
     * because its heavily used by the chunks.
     */
    std::list<ScheduledCheck> m_ScheduledChecks;
    
	mutable tthread::mutex m_ScheduledChecksMutex;
    
	tthread::condition_variable m_SchedulerReevaluateCondition;
    tthread::thread* m_SchedulerThread;

	tthread::atomic_int m_StopSchedulerThread;

    static void SchedulerThreadWrapper( void* worldInstance );
    void schedulerThreadFn();


    // --- Load/Save Jobs ---

    tthread::condition_variable m_NewJobCondition;

    /**
     * Tries to find a job with the given chunk id.
     * Returns the .end()-iterator if none has been found.
     */
    std::list<JobEntry>::iterator findJobByChunk( Chunk* chunk );

    /**
     * Adds a job to the job queue.
     * May eventually merge it with another job.
     */
    void addJob( JobType type, int priority, Chunk* chunk );

    /**
     * Finds a suitable job, removes it from the job list and returns it.
     */
    JobEntry getJob();

    mutable tthread::mutex m_JobListMutex;
    std::list<JobEntry> m_JobList;
    int m_ActiveLoadJobs;
    int m_ActiveSaveJobs;

    std::vector<tthread::thread*> m_JobThreads;
	tthread::atomic_int m_StopJobThreads;
    static void JobThreadWrapper(void* worldInstance);
    void jobThreadFn();
};

}

#endif
