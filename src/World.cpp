#include <algorithm>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <set>

#include "Util.h"
#include "Access.h"
#include "World.h"


namespace vman
{

World::World( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir, bool enableStatistics ) :
	m_Layers(&layers[0], &layers[layerCount]),
    m_MaxLayerVoxelSize(0),
	m_ChunkEdgeLength(chunkEdgeLength),
	m_ChunkMap(),
    m_BaseDir(), // Just to make it clear.
    m_Mutex(),
    m_LogMutex(),
	m_StatisticsEnabled(enableStatistics),

    m_UnusedChunkTimeout(4),
    m_ModifiedChunkTimeout(3),
    m_ScheduledChecks(),
    m_ScheduledChecksMutex(),
    m_SchedulerReevaluateCondition(),
    m_SchedulerThread(NULL),
	m_StopSchedulerThread(0),

    m_NewJobCondition(),
	m_JobListMutex(),
    m_JobList(),
    m_ActiveLoadJobs(0),
    m_ActiveSaveJobs(0),
    m_JobThreads(),
	m_StopJobThreads(0)
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

	resetStatistics();

    int workerCount = 4; // tthread::thread::hardware_concurrency() * 2; // This should do the trick at first.
    if(m_BaseDir.empty())
        workerCount = 0;

    m_JobThreads.resize(workerCount);
    for(int i = 0; i < workerCount; ++i)
    {
        m_JobThreads[i] = new tthread::thread(JobThreadWrapper, this, "VMAN Job Worker");
    }

    m_SchedulerThread = new tthread::thread(SchedulerThreadWrapper, this, "VMAN Scheduler");

	s_PanicMutex.lock();
	s_PanicWorldSet.insert(this);
	s_PanicMutex.unlock();
}

World::~World()
{
    // DEBUG START
    m_ScheduledChecksMutex.lock();
    log(LOG_DEBUG, "%d scheduled checks.\n",
        m_ScheduledChecks.size()
    );
    m_ScheduledChecksMutex.unlock();
    // DEBUG END

    m_StopSchedulerThread = 1;
    assert(m_SchedulerThread->joinable());
	{
		m_SchedulerReevaluateCondition.notify_all();
		m_SchedulerThread->join();
		log(LOG_DEBUG, "Joined Scheduler Thread!\n");
	}
	delete m_SchedulerThread;
	m_SchedulerThread = NULL;

	saveModifiedChunks();

    // DEBUG START
    m_JobListMutex.lock();
    log(LOG_DEBUG, "%d enqueued jobs.\n",
        m_JobList.size()
    );
    m_JobListMutex.unlock();
    // DEBUG END

	m_StopJobThreads = 1;
    m_NewJobCondition.notify_all();
    std::vector<tthread::thread*>::iterator i = m_JobThreads.begin();
    for(; i != m_JobThreads.end(); ++i)
    {
        if((*i)->joinable())
            (*i)->join();
        delete *i;
    }


    std::map<ChunkId,Chunk*>::const_iterator j = m_ChunkMap.begin();
    for(; j != m_ChunkMap.end(); ++j)
    {
        assert(j->second != NULL);
        delete j->second;
    }

	s_PanicMutex.lock();
	s_PanicWorldSet.erase(this);
	s_PanicMutex.unlock();
}

tthread::mutex   World::s_PanicMutex;
std::set<World*> World::s_PanicWorldSet;

void World::PanicExit()
{
	printf("World::PanicExit called\n");

	lock_guard guard(s_PanicMutex);
	std::set<World*>::const_iterator i = s_PanicWorldSet.begin();
	for(; i != s_PanicWorldSet.end(); ++i)
	{
		printf("world %p\n", (void*)*i);
		(*i)->panicExit();
	}
	s_PanicWorldSet.clear();
}

void World::panicExit()
{
	saveModifiedChunks();
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
    sprintf(buffer,"%d_%d_%d",chunkX,chunkY,chunkZ);

    std::string r = m_BaseDir;
    r += DirSep;
    r += buffer;
    return r;
}

void World::log( LogLevel level, const char* format, ... ) const
{
    lock_guard guard(m_LogMutex);

	FILE* logfile = NULL;
    switch(level)
    {
		case LOG_WARNING:
		case LOG_ERROR:
			logfile = stderr;

		default:
			logfile = stdout;
    }

    fprintf(logfile,"[VMAN world %p from %s ", (const void*)this, tthread::this_thread::get_name().c_str());
    switch(level)
    {
        case LOG_DEBUG:   fprintf(logfile, "DEBUG] ");   break;
        case LOG_INFO:    fprintf(logfile, "INFO] ");    break;
        case LOG_WARNING: fprintf(logfile, "WARNING] "); break;
        case LOG_ERROR:   fprintf(logfile, "ERROR] ");   break;
    }

    va_list vl;
    va_start(vl,format);
    vfprintf(logfile, format, vl);
    va_end(vl);
}

void World::resetStatistics()
{
	if(m_StatisticsEnabled)
		for(int i = 0; i < STATISTIC_COUNT; ++i)
			m_Statistics[i] = 0;
}

void World::incStatistic( Statistic statistic, int amount )
{
	if(m_StatisticsEnabled)
		m_Statistics[statistic].fetch_add(amount);
}

void World::decStatistic( Statistic statistic, int amount )
{
	if(m_StatisticsEnabled)
		m_Statistics[statistic].fetch_sub(amount);
}

void World::maxStatistic( Statistic statistic, int value )
{
	if(m_StatisticsEnabled)
		if(value > m_Statistics[statistic])
			m_Statistics[statistic] = value;
}

void World::minStatistic( Statistic statistic, int value )
{
	if(m_StatisticsEnabled)
		if(m_Statistics[statistic] > value)
			m_Statistics[statistic] = value;
}

bool World::getStatistics( vmanStatistics* statisticsDestination ) const
{
	assert(statisticsDestination != NULL);

	if(m_StatisticsEnabled == false)
		return false;

	statisticsDestination->chunkGetHits = m_Statistics[STATISTIC_CHUNK_GET_HITS];
	statisticsDestination->chunkGetMisses = m_Statistics[STATISTIC_CHUNK_GET_MISSES];

	statisticsDestination->chunkLoadOps = m_Statistics[STATISTIC_CHUNK_LOAD_OPS];
	statisticsDestination->chunkSaveOps = m_Statistics[STATISTIC_CHUNK_SAVE_OPS];
	statisticsDestination->chunkUnloadOps = m_Statistics[STATISTIC_CHUNK_UNLOAD_OPS];

	statisticsDestination->volumeReadHits = m_Statistics[STATISTIC_VOLUME_READ_HITS];
	statisticsDestination->volumeWriteHits = m_Statistics[STATISTIC_VOLUME_WRITE_HITS];

	statisticsDestination->maxLoadedChunks = m_Statistics[STATISTIC_MAX_LOADED_CHUNKS];
	statisticsDestination->maxScheduledChecks = m_Statistics[STATISTIC_MAX_SCHEDULED_CHECKS];
	statisticsDestination->maxEnqueuedJobs = m_Statistics[STATISTIC_MAX_ENQUEUED_JOBS];

	return true;
}


void World::voxelToChunkCoordinates( const int voxelX, const int voxelY, const int voxelZ, int* chunkX, int* chunkY, int* chunkZ )
{
    *chunkX = (int)floor( float(voxelX) / float(m_ChunkEdgeLength) );
    *chunkY = (int)floor( float(voxelY) / float(m_ChunkEdgeLength) );
    *chunkZ = (int)floor( float(voxelZ) / float(m_ChunkEdgeLength) );
}

void World::voxelToChunkVolume( const vmanVolume* voxelVolume, vmanVolume* chunkVolume )
{
    voxelToChunkCoordinates(
        voxelVolume->x,
        voxelVolume->y,
        voxelVolume->z,

        &chunkVolume->x,
        &chunkVolume->y,
        &chunkVolume->z
    );

    int maxChunkX, maxChunkY, maxChunkZ;
    voxelToChunkCoordinates(
        voxelVolume->x + voxelVolume->w,
        voxelVolume->y + voxelVolume->h,
        voxelVolume->z + voxelVolume->d,

        &maxChunkX,
        &maxChunkY,
        &maxChunkZ
    );

    chunkVolume->w = maxChunkX - chunkVolume->x + 1;
    chunkVolume->h = maxChunkY - chunkVolume->y + 1;
    chunkVolume->d = maxChunkZ - chunkVolume->z + 1;
}

void World::getVolume( const vmanVolume* chunkVolume, Chunk** chunksOut, int priority )
{
    assert(chunkVolume != NULL);
    assert(chunksOut != NULL);

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
    const std::string fileName = getChunkFileName(chunkX, chunkY, chunkZ);
    return GetFileType(fileName.c_str()) == FILE_TYPE_REGULAR;
}

// TODO: Make sure that chunks returned by this get referenced .. or they may become zombies.
Chunk* World::getChunkAt( int chunkX, int chunkY, int chunkZ, int priority )
{
	ChunkId id = Chunk::GenerateChunkId(chunkX, chunkY, chunkZ);

    Chunk* chunk = getLoadedChunkById(id);

	if(chunk == NULL)
	{
		incStatistic(STATISTIC_CHUNK_GET_MISSES);

        chunk = new Chunk(this, chunkX, chunkY, chunkZ);

        if(chunkFileExists(chunkX, chunkY, chunkZ))
        {
            log(LOG_DEBUG, "Try loading chunk %s ..\n",
                CoordsToString(chunkX, chunkY, chunkZ).c_str()
            );
			lock_guard jobListGuard(m_JobListMutex);
            addJob(LOAD_JOB, priority, chunk);
        }

		m_ChunkMap.insert( std::pair<ChunkId,Chunk*>(id,chunk) );

		maxStatistic(STATISTIC_MAX_LOADED_CHUNKS, m_ChunkMap.size());
	}
	else
	{
		incStatistic(STATISTIC_CHUNK_GET_HITS);
	}
	return chunk;
}

Chunk* World::getLoadedChunkById( ChunkId id )
{
    std::map<ChunkId,Chunk*>::iterator it = m_ChunkMap.find(id);
	if(it != m_ChunkMap.end())
	{
	    assert(id == it->second->getId());
		return it->second;
	}
	else
	{
	    return NULL;
	}
}

bool World::checkChunk( Chunk* chunk )
{
    chunk->getMutex()->lock();
    
    bool unloadChunk = chunk->isUnused();
    bool saveChunk = false;
    
    if(chunk->isModified() && m_BaseDir.empty() == false)
    {
		// Don't save if automatic saving has been disabled
		if(getModifiedChunkTimeout() < 0)
			saveChunk = false;
	    // Save immediately
		else if(getModifiedChunkTimeout() == 0 || m_StopJobThreads.load())
			saveChunk = true;
		// Timeout triggered
		else if(difftime(time(NULL), chunk->getModificationTime()) >= getModifiedChunkTimeout())
			saveChunk = true;
    }

    if(saveChunk)
    {
		lock_guard jobListGuard(m_JobListMutex);
        addJob(SAVE_JOB, 0, chunk); // TODO: Should have minimum priority
    }
    else if(unloadChunk && chunk->isModified() == false)
    {
		incStatistic(STATISTIC_CHUNK_UNLOAD_OPS);
		log(LOG_DEBUG, "Unloading chunk %s ...\n", chunk->toString().c_str());
        m_ChunkMap.erase(chunk->getId());
        chunk->getMutex()->unlock();
        delete chunk;
        return true;
    }
    
    chunk->getMutex()->unlock();
    return false;
}

void World::saveModifiedChunks()
{
	if(m_BaseDir.empty())
		return;

	lock_guard jobListGuard(m_JobListMutex);

    std::map<ChunkId,Chunk*>::const_iterator i = m_ChunkMap.begin();
    for(; i != m_ChunkMap.end(); ++i)
    {
		Chunk* chunk = i->second;
        assert(chunk != NULL);

		lock_guard chunkGuard(*chunk->getMutex());

		if(chunk->isModified())
			addJob(SAVE_JOB, 0, chunk); // TODO: Should have minimum priority
    }
}



/* --- Scheduled Tasks --- */

void World::setUnusedChunkTimeout( int seconds )
{
    m_UnusedChunkTimeout = (seconds < 0) ? -1 : seconds;
}

int World::getUnusedChunkTimeout() const
{
    return m_UnusedChunkTimeout;
}

void World::setModifiedChunkTimeout( int seconds )
{
    m_ModifiedChunkTimeout = (seconds < 0) ? -1 : seconds;
}

int World::getModifiedChunkTimeout() const
{
    return m_ModifiedChunkTimeout;
}

void World::scheduleCheck( CheckCause cause, Chunk* chunk )
{
    int seconds = 0.0;
    switch(cause)
    {
        case CHECK_CAUSE_UNUSED:
            seconds = getUnusedChunkTimeout();
            break;

        case CHECK_CAUSE_MODIFIED:
            seconds = getModifiedChunkTimeout();
            break;

        default:
            assert(false);
    }

    scheduleCheck(chunk, seconds);
}

void World::scheduleCheck( Chunk* chunk, double seconds )
{
	if(m_StopSchedulerThread == true)
		return;

    ScheduledCheck check;
    check.executionTime = AddSeconds(time(NULL), seconds);
    check.chunkId = chunk->getId();

    m_ScheduledChecksMutex.lock();
    m_ScheduledChecks.push_back(check); // TODO: Sort in correctly
	maxStatistic(STATISTIC_MAX_SCHEDULED_CHECKS, m_ScheduledChecks.size());
    m_ScheduledChecksMutex.unlock();

	m_SchedulerReevaluateCondition.notify_one();
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
        ScheduledCheck check;

        {
            lock_guard scheduledChecksGuard(m_ScheduledChecksMutex);

            while(m_ScheduledChecks.empty())
            {
				if(m_StopSchedulerThread.load())
					return;

                m_SchedulerReevaluateCondition.wait(m_ScheduledChecksMutex);

                if(m_ScheduledChecks.empty() && m_StopSchedulerThread.load())
                    return;
            }

            check = m_ScheduledChecks.front();
            m_ScheduledChecks.pop_front();
        }
		
		{
			lock_guard worldGuard(m_Mutex);
			
			const double waitTime = m_StopSchedulerThread.load() ? 0.0 : difftime(check.executionTime, time(NULL));
			if(waitTime > NO_WAIT_EPSILON)
			{
				const tthread::chrono::milliseconds milliseconds(waitTime*1000);
				m_SchedulerReevaluateCondition.wait_for(m_Mutex, milliseconds);
			}

			Chunk* chunk = getLoadedChunkById(check.chunkId);
			if(chunk != NULL)
			{
				checkChunk(chunk);
			}
		}
    }
}




/* --- Load/Save Jobs --- */

std::list<JobEntry>::iterator World::findJobByChunk( Chunk* chunk )
{
    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        if(i->getChunk() == chunk)
            return i;
    }
    return m_JobList.end();
}

void World::addJob( JobType type, int priority, Chunk* chunk )
{
    // Neither the load nor the save jobs can be run if disk access has been disabled.
    assert(m_BaseDir.empty() == false);

    std::list<JobEntry>::iterator jobWithSameChunk = findJobByChunk(chunk);
    if(jobWithSameChunk->getType() != INVALID_JOB)
    {
        if(type == jobWithSameChunk->getType())
        {
            if(priority > jobWithSameChunk->getPriority())
            {
                m_JobList.erase(jobWithSameChunk);
            }
            else
            {
                // If there is already a job of this type enqueued
                // and it has a even higher priority,
                // just abort, since we dont't need to do run enqueued jobs twice.
                return;
            }
        }
        else
        {
            // load <=> save:
            //assert(!"Unimplemented!");
        }
    }

    const JobEntry job(priority, type, chunk);

    // Sort in the job.
    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        // This has the neat side effect that,
        // if there are jobs with equal priority,
        // new jobs are inserted *after* the old ones.
        if(job.getPriority() > i->getPriority())
        {
            m_JobList.insert(i, job);
            break;
        }
    }

    // If there is no job that has a lower priority like us ..
    if(i == m_JobList.end())
        m_JobList.push_back(job);

	maxStatistic(STATISTIC_MAX_ENQUEUED_JOBS, m_JobList.size());

    // Notify one waiting thread, that there is a new job available
    m_NewJobCondition.notify_one();
}

JobEntry World::getJob()
{
    if(m_JobList.empty())
        return JobEntry::InvalidJob;

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
        if(i->getType() == favoredJob)
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
    while(true)
    {
        JobEntry job = JobEntry::InvalidJob;
        bool success = true;

        {
			{
				lock_guard guard(m_JobListMutex);
				job = getJob();

				while(job.getType() == INVALID_JOB)
				{
					if(m_StopJobThreads.load())
						return;

					// Unlocks mutex while waiting for the condition
					m_NewJobCondition.wait(m_JobListMutex);
					job = getJob();
				}
			}

            {
                lock_guard chunkGuard(*job.getChunk()->getMutex());
                switch(job.getType())
                {
                    case LOAD_JOB:
                        if(job.getChunk()->isUnused())
                        {
                            log(LOG_WARNING, "Canceled load job of chunk %s, because it's unused and would be deleted immediately.",
                                job.getChunk()->toString().c_str()
                            );
                        }
                        else
                        {
                            success = job.getChunk()->loadFromFile();
                        }
                        break;

                    case SAVE_JOB:
                        success = job.getChunk()->saveToFile();
                        break;

                    default:
                        assert(false);
                }
            }
        }

        if(success)
        {
			lock_guard worldGuard(m_Mutex);
            checkChunk(job.getChunk());
            // ^- For deleting unused chunks directly after saving them to disk
        }

        tthread::this_thread::yield();
    }
}

tthread::mutex* World::getMutex()
{
    return &m_Mutex;
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
