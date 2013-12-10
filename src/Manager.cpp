#include <algorithm>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <set>

#include "Util.h"
#include "Volume.h"
#include "Manager.h"


namespace vman
{

Manager* Manager::s_Singleton = NULL;

bool Manager::Init( vmanLogFn logFn, bool enableStatistics )
{
    assert(s_Singleton == NULL);
    s_Singleton = new Manager(logFn, enableStatistics);
    return true;
}

void Manager::Deinit()
{
    assert(s_Singleton != NULL);
    delete s_Singleton;
    s_Singleton = NULL;
}

Manager* Manager::Singleton()
{
    return s_Singleton;
}

Manager::Manager( vmanLogFn logFn, bool enableStatistics ) :
    m_Mutex(),
    m_LogFn(logFn),
    m_LogMutex(),
    //m_StatisticsEnabled(enableStatistics),

    m_UnusedChunkTimeout(4),
    m_ModifiedChunkTimeout(3),
    m_ScheduledChecks(),
    m_ScheduledChecksMutex(),
    m_SchedulerReevaluateCondition(),
    m_SchedulerThreads(),
    m_StopSchedulerThread(0),

    m_NewJobCondition(),
    m_JobListMutex(),
    m_JobList(),
    m_ActiveLoadJobs(0),
    m_ActiveSaveJobs(0),
    m_JobThreads(),
    m_StopJobThreads(0)
{
    int workerCount = 4; // tthread::thread::hardware_concurrency() * 2; // This should do the trick at first.

    for(int i = 0; i < workerCount; ++i)
    {
        m_JobThreads.push_back(
            new tthread::thread(JobThreadWrapper, this, Format("JobWorker %d", i).c_str())
        );
    }

    m_SchedulerThreads.push_back(
        new tthread::thread(SchedulerThreadWrapper, this, "Scheduler")
    );

    s_PanicMutex.lock();
    s_PanicSet.insert(this);
    s_PanicMutex.unlock();
}

Manager::~Manager()
{
    // DEBUG START
    m_ScheduledChecksMutex.lock();
    log(VMAN_LOG_DEBUG, "%d scheduled checks.\n",
        m_ScheduledChecks.size()
    );
    m_ScheduledChecksMutex.unlock();
    // DEBUG END

    m_StopSchedulerThread = 1;
    m_SchedulerReevaluateCondition.notify_all();
    while(m_SchedulerThreads.empty() == false)
    {
        tthread::thread* thread = m_SchedulerThreads.front();
        m_SchedulerThreads.pop_front();
        if(thread->joinable())
            thread->join();
        delete thread;
    }
    log(VMAN_LOG_DEBUG, "Joined scheduler threads!\n");



    // DEBUG START
    m_JobListMutex.lock();
    log(VMAN_LOG_DEBUG, "%d enqueued jobs.\n",
        m_JobList.size()
    );
    m_JobListMutex.unlock();
    // DEBUG END

    m_StopJobThreads = 1;
    m_NewJobCondition.notify_all();
    while(m_JobThreads.empty() == false)
    {
        tthread::thread* thread = m_JobThreads.front();
        m_JobThreads.pop_front();
        if(thread->joinable())
            thread->join();
        delete thread;
    }



    s_PanicMutex.lock();
    s_PanicSet.erase(this);
    s_PanicMutex.unlock();
}


tthread::mutex   Manager::s_PanicMutex;
std::set<Manager*> Manager::s_PanicSet;

void Manager::PanicExit()
{
    lock_guard guard(s_PanicMutex);
    std::set<Manager*>::const_iterator i = s_PanicSet.begin();
    for(; i != s_PanicSet.end(); ++i)
    {
        (*i)->panicExit();
    }
    s_PanicSet.clear();
}

void Manager::panicExit()
{
    //saveModifiedChunks();

    m_StopJobThreads = 1;
    m_NewJobCondition.notify_all();
    while(m_JobThreads.empty() == false)
    {
        tthread::thread* thread = m_JobThreads.front();
        m_JobThreads.pop_front();
        if(thread->joinable())
            thread->join();
    }
}

void Manager::log( vmanLogLevel level, const char* format, ... ) const
{
    lock_guard guard(m_LogMutex);

    if(m_LogFn)
    {
        char buffer[256];

        va_list vl;
        va_start(vl,format);
        vsprintf(buffer, format, vl);
        va_end(vl);

        m_LogFn(level, buffer);
    }
    else
    {
        FILE* logfile = NULL;
        switch(level)
        {
            case VMAN_LOG_WARNING:
            case VMAN_LOG_ERROR:
                logfile = stderr;

            default:
                logfile = stdout;
        }

        const time_t now = time(NULL);
        const struct tm* timeinfo = localtime(&now);
        char timeBuffer[48];
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", timeinfo);

        const char* levelName = NULL;
        switch(level)
        {
            case VMAN_LOG_DEBUG:   levelName = "DEBUG";   break;
            case VMAN_LOG_INFO:    levelName = "INFO";    break;
            case VMAN_LOG_WARNING: levelName = "WARNING"; break;
            case VMAN_LOG_ERROR:   levelName = "ERROR";   break;
        }

        fprintf(logfile,"[%s %s %s] ", timeBuffer, levelName, tthread::this_thread::get_name().c_str());

        va_list vl;
        va_start(vl,format);
        vfprintf(logfile, format, vl);
        va_end(vl);
    }
}



/* --- Scheduled Tasks --- */

void Manager::setUnusedChunkTimeout( int seconds )
{
    m_UnusedChunkTimeout = (seconds < 0) ? -1 : seconds;
}

int Manager::getUnusedChunkTimeout() const
{
    return m_UnusedChunkTimeout;
}

void Manager::setModifiedChunkTimeout( int seconds )
{
    m_ModifiedChunkTimeout = (seconds < 0) ? -1 : seconds;
}

int Manager::getModifiedChunkTimeout() const
{
    return m_ModifiedChunkTimeout;
}

void Manager::scheduleCheck( CheckCause cause, Volume* volume, Chunk* chunk )
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

    scheduleCheck(volume, chunk, seconds);
}

void Manager::scheduleCheck( Volume* volume, Chunk* chunk, double seconds )
{
    if(m_StopSchedulerThread == true)
        return;

    ScheduledCheck check;
    check.executionTime = AddSeconds(time(NULL), seconds);
    check.chunkId = chunk->getId();

    m_ScheduledChecksMutex.lock();
    m_ScheduledChecks.push_back(check); // TODO: Sort in correctly
    //maxStatistic(STATISTIC_MAX_SCHEDULED_CHECKS, m_ScheduledChecks.size());
    m_ScheduledChecksMutex.unlock();

    m_SchedulerReevaluateCondition.notify_one();
}

void Manager::flushScheduledChunksOfVolume( Volume* volume )
{
    lock_guard scheduledChecksGuard(m_ScheduledChecksMutex);

    const time_t now = time(NULL);

    std::list<ScheduledCheck>::iterator i = m_ScheduledChecks.begin();
    for(; i != m_ScheduledChecks.end(); ++i)
    {
        if(i->volume == volume)
        {
            ScheduledCheck check = *i;

            // Check should run immediately.
            check.executionTime = now;

            // Move it to the beginning.
            m_ScheduledChecks.push_front(check);
            i = m_ScheduledChecks.erase(i);
        }
    }

    // TODO
}

void Manager::SchedulerThreadWrapper( void* managerInstance )
{
    reinterpret_cast<Manager*>(managerInstance)->schedulerThreadFn();
}

void Manager::schedulerThreadFn()
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
            lock_guard managerGuard(m_Mutex);

            const double waitTime = m_StopSchedulerThread.load() ? 0.0 : difftime(check.executionTime, time(NULL));
            if(waitTime > NO_WAIT_EPSILON)
            {
                const tthread::chrono::milliseconds milliseconds(waitTime*1000);
                m_SchedulerReevaluateCondition.wait_for(m_Mutex, milliseconds);
            }
        }

        check.volume->checkChunk(check.chunkId);
    }
}




/* --- Load/Save Jobs --- */

std::list<JobEntry>::iterator Manager::findJobByChunk( Chunk* chunk )
{
    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        if(i->getChunk() == chunk)
            return i;
    }
    return m_JobList.end();
}

void Manager::addJob( JobType type, int priority, Volume* volume, Chunk* chunk )
{
    lock_guard jobListGuard(m_JobListMutex);

    // Neither the load nor the save jobs can be run if disk access has been disabled.
    assert(volume->getBaseDir() != NULL);

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

    const JobEntry job(priority, type, volume, chunk);

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

    //maxStatistic(STATISTIC_MAX_ENQUEUED_JOBS, m_JobList.size());

    // Notify one waiting thread, that there is a new job available
    m_NewJobCondition.notify_one();
}

void Manager::flushEnqueuedJobsOfVolume( Volume* volume )
{
    lock_guard jobListGuard(m_JobListMutex);

    std::list<JobEntry>::iterator i = m_JobList.begin();
    for(; i != m_JobList.end(); ++i)
    {
        if(i->getVolume() == volume)
        {
            JobEntry entry(
                //i->getPriority(),
                9999, // FIXME: MAX_INT or so?
                i->getType(),
                i->getVolume(),
                i->getChunk()
            );

            // Move it to the front.
            i = m_JobList.erase(i);
            m_JobList.push_front(entry);
        }
    }

    // TODO
}

bool Manager::isStoppingJobThreads() const
{
    return m_StopJobThreads.load() != 0;
}

JobEntry Manager::getJob()
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

void Manager::JobThreadWrapper(void* managerInstance)
{
    reinterpret_cast<Manager*>(managerInstance)->jobThreadFn();
}

void Manager::jobThreadFn()
{
    while(true)
    {
        JobEntry job = JobEntry::InvalidJob;

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

        processJob(job);

        tthread::this_thread::yield();
    }
}

void Manager::processJob( JobEntry job )
{
    bool success = true;

    {
        lock_guard chunkGuard(*job.getChunk()->getMutex());
        switch(job.getType())
        {
            case LOAD_JOB:
                if(job.getChunk()->isUnused())
                {
                    log(VMAN_LOG_WARNING, "Canceled load job of chunk %s, because it's unused and would be deleted immediately.",
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

    if(success)
    {
        job.getVolume()->checkChunk(job.getChunk()->getId()); // getChunk()->getId() ... hmm
        // ^- For deleting unused chunks directly after saving them to disk
    }
}

tthread::mutex* Manager::getMutex()
{
    return &m_Mutex;
}


/** Forbidden Stuff **/

Manager::Manager( const Manager& manager )
{
    assert(false);
}

Manager& Manager::operator = ( const Manager& manager )
{
    assert(false);
    return *this;
}


}
