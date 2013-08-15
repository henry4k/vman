#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "Util.h"
#include "World.h"
#include "Access.h"


namespace vman
{


Access::Access( World* world ) :
	m_World(world),
    m_IsInvalidVolume(true),
    m_IsLocked(false),
    m_AccessMode(VMAN_READ_ACCESS),
    m_Priority(0)
{
    memset(&m_Volume, 0, sizeof(m_Volume));
}

Access::~Access()
{
	assert(m_IsLocked == false);
    setVolume(NULL); // Unload chunks properly (dereference them)
}

void Access::setPriority( int priority )
{
    m_Priority = priority;
}

void Access::setVolume( const vmanVolume* volume )
{
    m_IsInvalidVolume = true;
    for(int i = 0; i < m_Cache.size(); ++i)
        m_Cache[i]->releaseReference();
    m_Cache.clear();

	if(volume != NULL)
	{
		m_IsInvalidVolume = false;
		m_Volume = *volume;
        m_World->voxelToChunkVolume(&m_Volume, &m_ChunkVolume);

        const int chunkCount =
            m_ChunkVolume.w *
            m_ChunkVolume.h *
            m_ChunkVolume.d;
        m_Cache.resize(chunkCount);

        m_World->getMutex()->lock();
        m_World->getVolume(&m_ChunkVolume, &m_Cache[0], m_Priority);
        for(int i = 0; i < m_Cache.size(); ++i)
            m_Cache[i]->addReference();
        m_World->getMutex()->unlock(); // So no one can remove my cached chunks while i'm putting references on them
	}
}

/*
const vmanVolume* Access::getVolume() const
{
	if(m_IsInvalidVolume)
		return NULL;
	else
		return &m_Volume;
}
*/

void Access::lock( int mode )
{
	assert(m_IsLocked == false);
	m_AccessMode = mode;

    for(int i = 0; i < m_Cache.size(); ++i)
    {
        m_Cache[i]->getMutex()->lock();
    }

	m_IsLocked = true;
}

bool Access::tryLock( int mode )
{
	assert(m_IsLocked == false);
	m_AccessMode = mode;

    for(int i = 0; i < m_Cache.size(); ++i)
    {
        if(m_Cache[i]->getMutex()->try_lock() == false)
        {
            // Unlock all previously locked mutexes.
            for(; i >= 0; --i)
            {
                m_Cache[i]->getMutex()->unlock();
            }
            return false;
        }
    }

	m_IsLocked = true;
	return true;
}

void Access::unlock()
{
	assert(m_IsLocked == true);

    for(int i = 0; i < m_Cache.size(); ++i)
    {
        m_Cache[i]->getMutex()->unlock();
    }

	m_IsLocked = false;
}

bool InsideVolume( const vmanVolume* volume, int x, int y, int z )
{
    return
        (x >= volume->x) &&
        (x <  volume->x + volume->w) &&

        (y >= volume->y) &&
        (y <  volume->y + volume->h) &&
        
        (z >= volume->z) &&
        (z <  volume->z + volume->d);
}

const void* Access::readVoxelLayer( int x, int y, int z, int layer ) const
{
	m_World->incStatistic(STATISTIC_VOLUME_READ_HITS);
    return getVoxelLayer(x,y,z, layer, VMAN_READ_ACCESS);
}

void* Access::readWriteVoxelLayer( int x, int y, int z, int layer ) const
{
	m_World->incStatistic(STATISTIC_VOLUME_READ_HITS);
	m_World->incStatistic(STATISTIC_VOLUME_WRITE_HITS);
    return getVoxelLayer(x,y,z, layer, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);
}

void* Access::getVoxelLayer( int x, int y, int z, int layer, int mode ) const
{
	assert(m_IsLocked == true);

	if((m_AccessMode & mode) != mode)
	{
	    m_World->log(LOG_ERROR, "Access mode not allowed!\n");
	    return NULL;
	}

    m_World->log(LOG_DEBUG, "readWriteVoxelLayer( %s ) in access volume (%s).\n",
        CoordsToString(x,y,z).c_str(),
        VolumeToString(&m_Volume).c_str()
    );

	if(InsideVolume(&m_Volume, x,y,z) == false)
	{
	    m_World->log(LOG_ERROR, "Voxel %s is not in access volume (%s).\n",
	        CoordsToString(x,y,z).c_str(),
	        VolumeToString(&m_Volume).c_str()
	    );
	    return NULL;
	}

    const int edgeLength = m_World->getChunkEdgeLength();
    int chunkX, chunkY, chunkZ;
    m_World->voxelToChunkCoordinates(
        x,
        y,
        z,
        &chunkX,
        &chunkY,
        &chunkZ
    );

    // TODO: Temporary
    if(InsideVolume(&m_ChunkVolume, chunkX, chunkY, chunkZ) == false)
    {
        m_World->log(LOG_ERROR, "Chunk %s is not in access volume (%s).\n",
            CoordsToString(chunkX,chunkY,chunkZ).c_str(),
            VolumeToString(&m_ChunkVolume).c_str()
        );
    }

    assert(InsideVolume(&m_ChunkVolume, chunkX, chunkY, chunkZ));

    Chunk* chunk = m_Cache[ Index3D(
        m_ChunkVolume.w,
        m_ChunkVolume.h,
        m_ChunkVolume.d,

        chunkX-m_ChunkVolume.x,
        chunkY-m_ChunkVolume.y,
        chunkZ-m_ChunkVolume.z
    ) ];
    
    // Check if mode includes write access.
    if(mode & VMAN_WRITE_ACCESS)
        chunk->setModified();

    const int voxelSize = m_World->getLayer(layer)->voxelSize;

    if(mode & VMAN_WRITE_ACCESS)
    {
        return &reinterpret_cast<char*>( chunk->getLayer(layer) )[Index3D(
            voxelSize*edgeLength,
            voxelSize*edgeLength,
            voxelSize*edgeLength,

            x - chunkX*edgeLength,
            y - chunkY*edgeLength,
            z - chunkZ*edgeLength
        )];
    }
    else
    {
        // TODO: Evil evil evil !
        return &const_cast<char*>( reinterpret_cast<const char*>( chunk->getConstLayer(layer) ) )[Index3D(
            voxelSize*edgeLength,
            voxelSize*edgeLength,
            voxelSize*edgeLength,

            x - chunkX*edgeLength,
            y - chunkY*edgeLength,
            z - chunkZ*edgeLength
        )];
    }
}


/** Forbidden Stuff **/

Access::Access( const Access& access )
{
	assert(false);
}

Access& Access::operator = ( const Access& access )
{
	assert(false);
    return *this;
}



}
