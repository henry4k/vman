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
#include "Manager.h"
#include "Volume.h"


namespace vman
{

Volume::Volume( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir ) :
    m_Layers(&layers[0], &layers[layerCount]),
    m_MaxLayerVoxelSize(0),
    m_ChunkEdgeLength(chunkEdgeLength),
    m_ChunkMap(),
    m_BaseDir(), // Just to make it clear.
    m_Mutex()
{
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

    if(baseDir != NULL)
        m_BaseDir = baseDir;

    //resetStatistics();
}

Volume::~Volume()
{
    Manager::Singleton()->flushScheduledChunksOfVolume(this);
    saveModifiedChunks();

    Manager::Singleton()->flushEnqueuedJobsOfVolume(this);

    std::map<ChunkId,Chunk*>::const_iterator j = m_ChunkMap.begin();
    for(; j != m_ChunkMap.end(); ++j)
    {
        assert(j->second != NULL);
        delete j->second;
    }
}

int Volume::getLayerCount() const
{
    return m_Layers.size();
}

int Volume::getMaxLayerVoxelSize() const
{
    return m_MaxLayerVoxelSize;
}

const vmanLayer* Volume::getLayer( int index ) const
{
    assert(index >= 0);
    assert(index < getLayerCount());
    return &m_Layers[index];
}

int Volume::getLayerIndexByName( const char* name ) const
{
    for(int i = 0; i < m_Layers.size(); ++i)
        if(strncmp(name, m_Layers[i].name, VMAN_MAX_LAYER_NAME_LENGTH) == 0)
            return i;
    return -1;
}

int Volume::getVoxelsPerChunk() const
{
    return m_ChunkEdgeLength*m_ChunkEdgeLength*m_ChunkEdgeLength;
}

int Volume::getChunkEdgeLength() const
{
    return m_ChunkEdgeLength;
}

const char* Volume::getBaseDir() const
{
    if(m_BaseDir.empty())
        return NULL;
    else
        return m_BaseDir.c_str();
}

std::string Volume::getChunkFileName( int chunkX, int chunkY, int chunkZ ) const
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

/*
void Volume::resetStatistics()
{
    if(m_StatisticsEnabled)
        for(int i = 0; i < STATISTIC_COUNT; ++i)
            m_Statistics[i] = 0;
}

void Volume::incStatistic( Statistic statistic, int amount )
{
    if(m_StatisticsEnabled)
        m_Statistics[statistic].fetch_add(amount);
}

void Volume::decStatistic( Statistic statistic, int amount )
{
    if(m_StatisticsEnabled)
        m_Statistics[statistic].fetch_sub(amount);
}

void Volume::maxStatistic( Statistic statistic, int value )
{
    if(m_StatisticsEnabled)
        if(value > m_Statistics[statistic])
            m_Statistics[statistic] = value;
}

void Volume::minStatistic( Statistic statistic, int value )
{
    if(m_StatisticsEnabled)
        if(m_Statistics[statistic] > value)
            m_Statistics[statistic] = value;
}

bool Volume::getStatistics( vmanStatistics* statisticsDestination ) const
{
    assert(statisticsDestination != NULL);

    if(m_StatisticsEnabled == false)
        return false;

    statisticsDestination->chunkGetHits = m_Statistics[STATISTIC_CHUNK_GET_HITS];
    statisticsDestination->chunkGetMisses = m_Statistics[STATISTIC_CHUNK_GET_MISSES];

    statisticsDestination->chunkLoadOps = m_Statistics[STATISTIC_CHUNK_LOAD_OPS];
    statisticsDestination->chunkSaveOps = m_Statistics[STATISTIC_CHUNK_SAVE_OPS];
    statisticsDestination->chunkUnloadOps = m_Statistics[STATISTIC_CHUNK_UNLOAD_OPS];

    statisticsDestination->readOps = m_Statistics[STATISTIC_READ_OPS];
    statisticsDestination->writeOps = m_Statistics[STATISTIC_WRITE_OPS];

    statisticsDestination->maxLoadedChunks = m_Statistics[STATISTIC_MAX_LOADED_CHUNKS];
    statisticsDestination->maxScheduledChecks = m_Statistics[STATISTIC_MAX_SCHEDULED_CHECKS];
    statisticsDestination->maxEnqueuedJobs = m_Statistics[STATISTIC_MAX_ENQUEUED_JOBS];

    return true;
}
*/

void Volume::voxelToChunkCoordinates( const int voxelX, const int voxelY, const int voxelZ, int* chunkX, int* chunkY, int* chunkZ )
{
    *chunkX = (int)floor( float(voxelX) / float(m_ChunkEdgeLength) );
    *chunkY = (int)floor( float(voxelY) / float(m_ChunkEdgeLength) );
    *chunkZ = (int)floor( float(voxelZ) / float(m_ChunkEdgeLength) );
}

void Volume::voxelToChunkSelection( const vmanSelection* voxelSelection, vmanSelection* chunkSelection )
{
    voxelToChunkCoordinates(
        voxelSelection->x,
        voxelSelection->y,
        voxelSelection->z,

        &chunkSelection->x,
        &chunkSelection->y,
        &chunkSelection->z
    );

    int maxChunkX, maxChunkY, maxChunkZ;
    voxelToChunkCoordinates(
        voxelSelection->x + voxelSelection->w,
        voxelSelection->y + voxelSelection->h,
        voxelSelection->z + voxelSelection->d,

        &maxChunkX,
        &maxChunkY,
        &maxChunkZ
    );

    chunkSelection->w = maxChunkX - chunkSelection->x + 1;
    chunkSelection->h = maxChunkY - chunkSelection->y + 1;
    chunkSelection->d = maxChunkZ - chunkSelection->z + 1;
}

void Volume::getSelection( const vmanSelection* chunkSelection, Chunk** chunksOut, int priority )
{
    assert(chunkSelection != NULL);
    assert(chunksOut != NULL);

    for(int x = 0; x < chunkSelection->w; ++x)
    {
        for(int y = 0; y < chunkSelection->h; ++y)
        {
            for(int z = 0; z < chunkSelection->d; ++z)
            {
                Chunk* chunk = getChunkAt(
                    chunkSelection->x+x,
                    chunkSelection->y+y,
                    chunkSelection->z+z,
                    priority // TODO: Hmm...
                );

                chunksOut[ Index3D(
                    chunkSelection->w, chunkSelection->h, chunkSelection->d,
                    x, y, z
                ) ] = chunk;
            }
        }
    }
}

bool Volume::chunkFileExists( int chunkX, int chunkY, int chunkZ )
{
    if(m_BaseDir.empty())
        return false;
    const std::string fileName = getChunkFileName(chunkX, chunkY, chunkZ);
    return GetFileType(fileName.c_str()) == FILE_TYPE_REGULAR;
}

// TODO: Make sure that chunks returned by this get referenced .. or they may become zombies.
Chunk* Volume::getChunkAt( int chunkX, int chunkY, int chunkZ, int priority )
{
    ChunkId id = Chunk::GenerateChunkId(chunkX, chunkY, chunkZ);

    Chunk* chunk = getLoadedChunkById(id);

    if(chunk == NULL)
    {
        //incStatistic(STATISTIC_CHUNK_GET_MISSES);

        chunk = new Chunk(this, chunkX, chunkY, chunkZ);

        if(chunkFileExists(chunkX, chunkY, chunkZ))
        {
            log(VMAN_LOG_DEBUG, "Try loading chunk %s ..\n",
                CoordsToString(chunkX, chunkY, chunkZ).c_str()
            );
            Manager::Singleton()->addJob(LOAD_JOB, priority, this, chunk);
        }

        m_ChunkMap.insert( std::pair<ChunkId,Chunk*>(id,chunk) );

        //maxStatistic(STATISTIC_MAX_LOADED_CHUNKS, m_ChunkMap.size());
    }
    else
    {
        //incStatistic(STATISTIC_CHUNK_GET_HITS);
    }
    return chunk;
}

Chunk* Volume::getLoadedChunkById( ChunkId id )
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

bool Volume::checkChunk( ChunkId chunkId )
{
    lock_guard guard(m_Mutex);

    Chunk* chunk = getLoadedChunkById(chunkId);
    if(chunk == NULL)
        return false;

    chunk->getMutex()->lock();

    bool unloadChunk = chunk->isUnused();
    bool saveChunk = false;

    int modifiedChunkTimeout = Manager::Singleton()->getModifiedChunkTimeout();

    if(chunk->isModified() && m_BaseDir.empty() == false)
    {
        // Don't save if automatic saving has been disabled
        if(modifiedChunkTimeout < 0)
            saveChunk = false;
        // Save immediately
        else if(modifiedChunkTimeout == 0 || Manager::Singleton()->isStoppingJobThreads())
            saveChunk = true;
        // Timeout triggered
        else if(difftime(time(NULL), chunk->getModificationTime()) >= modifiedChunkTimeout)
            saveChunk = true;
    }

    if(saveChunk)
    {
        Manager::Singleton()->addJob(SAVE_JOB, 0, this, chunk); // TODO: Should have minimum priority
    }
    else if(unloadChunk && chunk->isModified() == false)
    {
        //incStatistic(STATISTIC_CHUNK_UNLOAD_OPS);
        log(VMAN_LOG_DEBUG, "Unloading chunk %s ...\n", chunk->toString().c_str());
        m_ChunkMap.erase(chunk->getId());
        chunk->getMutex()->unlock();
        delete chunk;
        return true;
    }

    chunk->getMutex()->unlock();
    return false;
}

void Volume::saveModifiedChunks()
{
    if(m_BaseDir.empty())
        return;

    std::map<ChunkId,Chunk*>::const_iterator i = m_ChunkMap.begin();
    for(; i != m_ChunkMap.end(); ++i)
    {
        Chunk* chunk = i->second;
        assert(chunk != NULL);

        lock_guard chunkGuard(*chunk->getMutex());

        if(chunk->isModified())
            Manager::Singleton()->addJob(SAVE_JOB, 0, this, chunk); // TODO: Should have minimum priority
    }
}

void Volume::log( vmanLogLevel level, const char* format, ... ) const
{
    char buffer[256];

    va_list vl;
    va_start(vl,format);
    vsprintf(buffer, format, vl);
    va_end(vl);

    Manager::Singleton()->log(level, "%s", buffer);
}

tthread::mutex* Volume::getMutex()
{
    return &m_Mutex;
}


/** Forbidden Stuff **/

Volume::Volume( const Volume& volume )
{
    assert(false);
}

Volume& Volume::operator = ( const Volume& volume )
{
    assert(false);
    return *this;
}


}
