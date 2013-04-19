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
}

void Access::setPriority( int priority )
{
    m_Priority = priority;
}

void Access::setVolume( const vmanVolume* volume )
{
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
        m_World->getVolume(&m_ChunkVolume, &m_Cache[0], m_Priority);
	}
	else
	{
		m_IsInvalidVolume = true;
        m_Cache.clear();
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
        x >= volume->x &&
        x <  volume->x + volume->w &&
        y >= volume->y &&
        y <  volume->y + volume->h &&
        z >= volume->z &&
        z <  volume->z + volume->d;
}

const void* Access::readVoxelLayer( int x, int y, int z, int layer ) const
{
    return readWriteVoxelLayer(x,y,z, layer);
}

void* Access::readWriteVoxelLayer( int x, int y, int z, int layer ) const
{
	assert(m_IsLocked == true);
    assert(InsideVolume(&m_Volume, x,y,z));
    
    const int edgeLength = m_World->getChunkEdgeLength();
    const int chunkX = x/edgeLength;
    const int chunkY = y/edgeLength;
    const int chunkZ = z/edgeLength;

    assert(InsideVolume(&m_ChunkVolume, chunkX, chunkY, chunkZ));

    Chunk* chunk = m_Cache[ Index3D(
        m_ChunkVolume.w,
        m_ChunkVolume.h,
        m_ChunkVolume.d,    
        
        chunkX-m_ChunkVolume.x,
        chunkY-m_ChunkVolume.y,
        chunkZ-m_ChunkVolume.z
    ) ];

    const int voxelSize = m_World->getLayer(layer)->voxelSize; 

    return &reinterpret_cast<char*>( chunk->getLayer(layer) )[Index3D(
        voxelSize*edgeLength,
        voxelSize*edgeLength,
        voxelSize*edgeLength,

        chunkX*edgeLength - x,
        chunkY*edgeLength - y,
        chunkZ*edgeLength - z
    )];
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
