#ifndef __VMAN_MANAGER_H__
#define __VMAN_MANAGER_H__

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


class Manager
{
public:
    static bool Init( vmanLogFn logFn, bool enableStatistics );
    static void Deinit();
    static Manager* Singleton();

    /**
     * @see vmanManagerParameters
     */
    Manager( vmanLogFn logFn, bool enableStatistics );
    ~Manager();

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
    void scheduleCheck( CheckCause cause, Volume* volume, Chunk* chunk );

    /**
     * Flushes all scheduled chunks of a specific volume.
     */
    void flushScheduledChunksOfVolume( Volume* volume );

    /**
     * Adds a job to the job queue.
     * May eventually merge it with another job.
     * Is thread save.
     */
    void addJob( JobType type, int priority, Volume* volume, Chunk* chunk );

    /**
     * Flushes all scheduled jobs of a specific volume.
     */
    void flushEnqueuedJobsOfVolume( Volume* volume );

    /**
     * @return Whether jobs threads are being stopped.
     */
    bool isStoppingJobThreads() const;


    /**
     * For logging vman specific messages.
     * Is thread safe.
     */
    void log( vmanLogLevel level, const char* format, ... ) const;


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
    static Manager* s_Singleton;

    Manager( const Manager& manager );
    Manager& operator = ( const Manager& manager );

    static tthread::mutex   s_PanicMutex;
    static std::set<Manager*> s_PanicSet;

    void panicExit();



    mutable tthread::mutex m_Mutex;


    vmanLogFn m_LogFn;
    mutable tthread::mutex m_LogMutex;


    // --- Scheduled Checks ---

    int m_UnusedChunkTimeout;
    int m_ModifiedChunkTimeout;

    struct ScheduledCheck
    {
        time_t executionTime;
        ChunkId chunkId;
        Volume* volume;
    };

    /**
     * Internal version of `scheduleCheck` with time parameter.
     * Don't use this directly.
     */
    void scheduleCheck( Volume* volume, Chunk* chunk, double seconds );

    /**
     * This list needs its own mutex,
     * because its heavily used by the chunks.
     */
    std::list<ScheduledCheck> m_ScheduledChecks;

    mutable tthread::mutex m_ScheduledChecksMutex;

    tthread::condition_variable m_SchedulerReevaluateCondition;
    std::list<tthread::thread*> m_SchedulerThreads;

    tthread::atomic_int m_StopSchedulerThread;

    static void SchedulerThreadWrapper( void* managerInstance );
    void schedulerThreadFn();


    // --- Load/Save Jobs ---

    tthread::condition_variable m_NewJobCondition;

    /**
     * Tries to find a job with the given chunk id.
     * Returns the .end()-iterator if none has been found.
     */
    std::list<JobEntry>::iterator findJobByChunk( Chunk* chunk );

    /**
     * Finds a suitable job, removes it from the job list and returns it.
     */
    JobEntry getJob();

    /**
     * Execute job in the calling thread.
     */
    void processJob( JobEntry job );

    mutable tthread::mutex m_JobListMutex;
    std::list<JobEntry> m_JobList;
    int m_ActiveLoadJobs;
    int m_ActiveSaveJobs;

    std::list<tthread::thread*> m_JobThreads;
    tthread::atomic_int m_StopJobThreads;
    static void JobThreadWrapper(void* managerInstance);
    void jobThreadFn();
};


}

#endif
