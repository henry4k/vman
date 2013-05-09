#include <algorithm>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "Util.h"
#include "Access.h"
#include "World.h"


namespace vman
{


World::World( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir ) :
	m_Layers(&layers[0], &layers[layerCount]),
    m_MaxLayerVoxelSize(0),
	m_ChunkEdgeLength(chunkEdgeLength),
    m_BaseDir(), // Just to make it clear.
    m_StopThreads(false),
    m_UnusedChunkTimeout(0),
    m_ModifiedChunkTimeout(0),
    m_SchedulerThread(SchedulerThreadWrapper, this)
{
    if(baseDir != NULL)
        m_BaseDir = baseDir;

    for(int i = 0; i < m_Layers.size(); ++i)
    {
        const vmanLayer* layer = &m_Layers[i];
        
        assert(layer->name != NULL);
        assert(strlen(layer->name) > 0);
        assert(strlen(layer->name) <= VMAN_MAX_LAYER_NAME_LENGTH);
        assert(layer->voxelSize > 0);
        assert(layer->revision > 0);
        assert(layer->serializeFn != NULL);
        assert(layer->deserializeFn != NULL);
        
        if(layer->voxelSize > m_MaxLayerVoxelSize)
            m_MaxLayerVoxelSize = layer->voxelSize;
    }

    int workerCount = 4; // tthread::thread::hardware_concurrency() * 2; // This should do the trick at first.
    m_ThreadPool.resize(workerCount);
    for(int i = 0; i < workerCount; ++i)
    {
        m_ThreadPool[i] = new tthread::thread(JobThreadWrapper, this);
    }
}

World::~World()
{
    m_Mutex.lock();
    m_StopThreads = true;
    m_Mutex.unlock();


    assert(m_SchedulerThread.joinable());
    m_SchedulerReevaluateCondition.notify_all();
    m_SchedulerThread.join();


    m_NewJobCondition.notify_all();

    std::vector<tthread::thread*>::iterator i = m_ThreadPool.begin();
    for(; i != m_ThreadPool.end(); ++i)
    {
        if((*i)->joinable())
            (*i)->join();
        delete *i;
    }



    std::map<ChunkId,Chunk*>::const_iterator j = m_ChunkMap.begin();
    for(; j != m_ChunkMap.end(); ++j)
    {
        assert(j->second == NULL);
        delete j->second;
    }
}

int World::getLayerCount() const
{
	return m_Layers.size();
}

int World::getMaxLayerVoxelSize() const
{
    return m_MaxLayerVoxelSize;
}

int World::getVoxelsPerChunk() const
{
    return m_ChunkEdgeLength*m_ChunkEdgeLength*m_ChunkEdgeLength;
}

const vmanLayer* World::getLayer( int index ) const
{
	assert(index >= 0);
	assert(index < getLayerCount());
	return &m_Layers[index];
}

int World::getLayerIndexByName( const char* name ) const
{
    for(int i = 0; i < m_Layers.size(); ++i)
        if(strncmp(name, m_Layers[i].name, VMAN_MAX_LAYER_NAME_LENGTH) == 0)
            return i;
    return -1;
}

int World::getChunkEdgeLength() const
{
	return m_ChunkEdgeLength;
}

const char* World::getBaseDir() const
{
    if(m_BaseDir.empty())
        return NULL;
    else
        return m_BaseDir.c_str();
}

std::string World::getChunkFileName( int chunkX, int chunkY, int chunkZ ) const
{
    if(m_BaseDir.empty())
        return "";

    char buffer[256];
    sprintf(buffer,"%d-%d-%d",chunkX,chunkY,chunkZ);
    
    std::string r = m_BaseDir;
    r += DirSep;
    r += buffer;
    return r;
}


void World::log( LogLevel level, const char* format, ... ) const
{
    fprintf(stderr,"[VMAN %p ", (const void*)this);
    switch(level)
    {
        case LOG_DEBUG:   fprintf(stderr,"DEBUG] ");   break;
        case LOG_INFO:    fprintf(stderr,"INFO] ");    break;
        case LOG_WARNING: fprintf(stderr,"WARNING] "); break;
        case LOG_ERROR:   fprintf(stderr,"ERROR] ");   break;
    }

    va_list vl;
    va_start(vl,format);
    vfprintf(stderr, format, vl);
    va_end(vl);
}

/**
 * @param a divident
 * @param b divisor
 */
int CeilDiv( int a, int b )
{
    return a/b + ((a%b != 0) ? 1 : 0);
}

void World::voxelToChunkVolume( const vmanVolume* voxelVolume, vmanVolume* chunkVolume )
{
    chunkVolume->x = voxelVolume->x / m_ChunkEdgeLength;
    chunkVolume->y = voxelVolume->y / m_ChunkEdgeLength;
    chunkVolume->z = voxelVolume->z / m_ChunkEdgeLength;

    chunkVolume->w = CeilDiv(voxelVolume->w, m_ChunkEdgeLength);
    chunkVolume->h = CeilDiv(voxelVolume->h, m_ChunkEdgeLength);
    chunkVolume->d = CeilDiv(voxelVolume->d, m_ChunkEdgeLength);
}

void World::getVolume( const vmanVolume* chunkVolume, Chunk** chunksOut, int priority )
{
    assert(chunkVolume != NULL);
    assert(chunksOut != NULL);

    lock_guard guard(m_Mutex);
    
    for(int x = 0; x < chunkVolume->w; ++x)
    {
        for(int y = 0; y < chunkVolume->h; ++y)
        {
            for(int z = 0; z < chunkVolume->d; ++z)
            {
                Chunk* chunk = getChunkAt(
                    chunkVolume->x+x,
                    chunkVolume->y+y,
                    chunkVolume->z+z,
                    priority // TODO: Hmm...
                );

                chunksOut[ Index3D(
                    chunkVolume->w, chunkVolume->h, chunkVolume->d,
                    x, y, z
                ) ] = chunk;
            }
        }
    }
}

bool World::chunkFileExists( int chunkX, int chunkY, int chunkZ )
{
    if(m_BaseDir.empty())
        return false;

    FILE* f = fopen(getChunkFileName(chunkX, chunkY, chunkZ).c_str(), "rb");
    if(f == NULL)
        return false;
    fclose(f);
    return true;
}

Chunk* World::getChunkAt( int chunkX, int chunkY, int chunkZ, int priority )
{
	ChunkId id = Chunk::GenerateChunkId(chunkX, chunkY, chunkZ);
	
    std::map<ChunkId,Chunk*>::iterator it = m_ChunkMap.find(id);
	if(it != m_ChunkMap.end())
	{
		return it->second;
	}
	else
	{
        log(LOG_DEBUG, "Try loading chunk %d/%d/%d ..\n", chunkX, chunkY, chunkZ);

        Chunk* chunk = new Chunk(this, chunkX, chunkY, chunkZ);

        if(chunkFileExists(chunkX, chunkY, chunkZ))
            addJob(LOAD_JOB, priority, chunk);

		m_ChunkMap.insert( std::pair<ChunkId,Chunk*>(id,chunk) );
		return chunk;
	}
}



/* --- Scheduled Tasks --- */

void World::setUnusedChunkTimeout( int seconds )
{
    assert(seconds >= 0);
    m_UnusedChunkTimeout = seconds;
}

int World::getUnusedChunkTimeout() const
{
    return m_UnusedChunkTimeout;
}

void World::setModifiedChunkTimeout( int seconds )
{
    assert(seconds >= 0);
    m_ModifiedChunkTimeout = seconds;
}

int World::getModifiedChunkTimeout() const
{
    return m_ModifiedChunkTimeout;
}

void World::scheduleTask( ScheduledTaskType type, Chunk* chunk )
{
    double seconds = 0.0;
    switch(type)
    {
        case UNLOAD_UNUSED_TASK:
            seconds = getUnusedChunkTimeout();

        case SAVE_MODIFIED_TASK:
            seconds = getModifiedChunkTimeout();

        default:
            assert(false);
    }
    scheduleTask(type, chunk, seconds);
}

void World::scheduleTask( ScheduledTaskType type, Chunk* chunk, double seconds )
{
    assert(seconds >= 0.0);

    const time_t now = time(NULL);
    const struct tm* now_tv = gmtime(&now);
    struct tm tv;
    memcpy(&tv, &now_tv, sizeof(tv));
    tv.tm_sec += (int)seconds;

    ScheduledTask task;
    task.executionTime = mktime(&tv);
    task.type = type;
    task.chunk = chunk;
    
    m_ScheduledTasks.push_back(task);
}

void World::SchedulerThreadWrapper( void* worldInstance )
{
    reinterpret_cast<World*>(worldInstance)->schedulerThreadFn();
}

void World::schedulerThreadFn()
{
    static const double NO_WAIT_EPSILON = 0.1; // In seconds

    while(true)
    {
        lock_guard guard(m_Mutex);

        while(m_ScheduledTasks.empty())
            m_SchedulerReevaluateCondition.wait(m_Mutex);

        if(m_ScheduledTasks.empty() && m_StopThreads)
            break;
        
        const ScheduledTask task = m_ScheduledTasks.front();
        m_ScheduledTasks.pop_front();

        double waitTime = difftime(time(NULL), task.executionTime);
        if(m_StopThreads) // Don't wait when the threads should finish their jobs.
            waitTime = 0.0;
        
        if(waitTime > NO_WAIT_EPSILON)
        {
            const tthread::chrono::milliseconds waitMS(waitTime*1000);
            //m_SchedulerReevaluateCondition.wait_for(m_Mutex, waitMS);
        }

        switch(task.type)
        {
            case UNLOAD_UNUSED_TASK:
                if(task.chunk->isModified() == false)
                {
                    m_ChunkMap.erase(task.chunk->getId());
                    delete task.chunk;
                    break;
                }
                // If its still modified,
                // pass it through to the SAVE_MODIFIED_TASK routine.
                // Note that the save job should delete unreferenced chunks.
                // TODO: ^- Really?

            case SAVE_MODIFIED_TASK:
                addJob(SAVE_JOB, 0, task.chunk); // TODO: Should have minimum priority
                break;

            default:
                assert(false);
        }
    }
}




/* --- Load/Save Jobs --- */

World::JobEntry World::InvalidJob()
{
    JobEntry job;
    job.priority = 0;
    job.type = INVALID_JOB;
    job.chunk = NULL;
    return job;
}

World::JobEntry World::findJobByChunk( Chunk* chunk ) const
{
    std::list<JobEntry>::const_iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        if(i->chunk == chunk)
            return *i;
    }
    return InvalidJob();
}

void World::addJob( JobType type, int priority, Chunk* chunk )
{
    // Check if we're try to tear down already.
    assert(m_StopThreads == false);

    const JobEntry jobWithSameChunk = findJobByChunk(chunk);
    if(jobWithSameChunk.type != INVALID_JOB)
    {
        // TODO: Ohaaa ... :[]
        assert(!"Unimplemented!");
    }

    JobEntry job;
    job.priority = priority;
    job.type = type;
    job.chunk = chunk;

    // Sort in the job.
    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        // This has the neat side effect that,
        // if there are jobs with equal priority,
        // new jobs are inserted *after* the old ones.
        if(job.priority > i->priority)
        {
            m_JobList.insert(i, job);
            break;
        }
    }

    // If there is no job that has a lower priority like us ..
    if(i == m_JobList.end())
        m_JobList.push_back(job);
}

World::JobEntry World::getJob()
{
    if(m_JobList.empty())
        return InvalidJob();

    // Save and load job should be distributed equally on the threads.
    // I.e. if more save than load jobs run, the latter one should be picked.
    // (If one exists)
    
    JobType favoredJob;
    if(m_ActiveSaveJobs > m_ActiveLoadJobs)
        favoredJob = LOAD_JOB; // We favor a load job.
    else
        favoredJob = SAVE_JOB; // We favor a save job.

    // Try to find our favored job in the list.
    // The list is sorted from high to low priority.
    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        if(i->type == favoredJob)
        {
            const JobEntry job = *i;
            m_JobList.erase(i);
            return job;
        }
    }

    // The favored job type was not found. :(
    const JobEntry job = m_JobList.front();
    m_JobList.pop_front();
    return job;
}

void World::JobThreadWrapper(void* worldInstance)
{
    reinterpret_cast<World*>(worldInstance)->jobThreadFn();
}

void World::jobThreadFn()
{
    //log(LOG_DEBUG, "Thread started.\n");

    while(true)
    {
        JobEntry job = InvalidJob();
        
        {
            lock_guard guard(m_Mutex);
            // ^- Unlocks also when break is called.
            
            job = getJob();

            if(job.type == INVALID_JOB)
            {
                if(m_StopThreads)
                    break;

                //log(LOG_DEBUG, "Waiting for job ..\n");

                // Unlocks mutex while waiting for the condition
                m_NewJobCondition.wait(m_Mutex);
                job = getJob();

                //log(LOG_DEBUG, "Got job %p\n", job);
               
                if(job.type == INVALID_JOB)
                {
                    assert(m_StopThreads);
                    break;
                }
            }
        }
        
        switch(job.type)
        {
            case LOAD_JOB:
                job.chunk->loadFromFile();
                break;

            case SAVE_JOB:
                job.chunk->saveToFile();
                break;

            default:
                assert(false);
        }

        tthread::this_thread::yield();
    }

    //log(LOG_DEBUG, "Thread stopped.\n");
}


/** Forbidden Stuff **/

World::World( const World& world )
{
	assert(false);
}

World& World::operator = ( const World& world )
{
	assert(false);
    return *this;
}


}
