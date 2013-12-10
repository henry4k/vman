#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "Util.h"
#include "Volume.h"
#include "Access.h"


namespace vman
{


Access::Access( Volume* volume ) :
    m_Volume(volume),
    m_SelectionIsInvalid(true),
    m_IsLocked(false),
    m_AccessMode(VMAN_READ_ACCESS),
    m_Priority(0)
{
    memset(&m_Selection, 0, sizeof(m_Selection));
}

Access::~Access()
{
    assert(m_IsLocked == false);
    select(NULL); // Unload chunks properly (dereference them)
}

void Access::setPriority( int priority )
{
    m_Priority = priority;
}

void Access::select( const vmanSelection* selection )
{
    m_SelectionIsInvalid = true;
    for(int i = 0; i < m_Cache.size(); ++i)
        m_Cache[i]->releaseReference();
    m_Cache.clear();

    if(selection != NULL)
    {
        m_SelectionIsInvalid = false;
        m_Selection = *selection;
        m_Volume->voxelToChunkSelection(&m_Selection, &m_ChunkSelection);

        const int chunkCount =
            m_ChunkSelection.w *
            m_ChunkSelection.h *
            m_ChunkSelection.d;
        m_Cache.resize(chunkCount);

        m_Volume->getMutex()->lock();
        m_Volume->getSelection(&m_ChunkSelection, &m_Cache[0], m_Priority);
        for(int i = 0; i < m_Cache.size(); ++i)
            m_Cache[i]->addReference();
        m_Volume->getMutex()->unlock(); // So no one can remove my cached chunks while i'm putting references on them
    }
}

/*
const vmanSelection* Access::getSelection() const
{
    if(m_SelectionIsInvalid)
        return NULL;
    else
        return &m_Selection;
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

bool InsideSelection( const vmanSelection* selection, int x, int y, int z )
{
    return
        (x >= selection->x) &&
        (x <  selection->x + selection->w) &&

        (y >= selection->y) &&
        (y <  selection->y + selection->h) &&

        (z >= selection->z) &&
        (z <  selection->z + selection->d);
}

const void* Access::readVoxelLayer( int x, int y, int z, int layer ) const
{
    //m_Volume->incStatistic(STATISTIC_READ_OPS);
    return getVoxelLayer(x,y,z, layer, VMAN_READ_ACCESS);
}

void* Access::readWriteVoxelLayer( int x, int y, int z, int layer ) const
{
    //m_Volume->incStatistic(STATISTIC_READ_OPS);
    //m_Volume->incStatistic(STATISTIC_WRITE_OPS);
    return getVoxelLayer(x,y,z, layer, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);
}

void* Access::getVoxelLayer( int x, int y, int z, int layer, int mode ) const
{
    assert(m_IsLocked == true);

    if((m_AccessMode & mode) != mode)
    {
        m_Volume->log(VMAN_LOG_ERROR, "Access mode not allowed!\n");
        return NULL;
    }

    m_Volume->log(VMAN_LOG_DEBUG, "readWriteVoxelLayer( %s ) in access selection (%s).\n",
        CoordsToString(x,y,z).c_str(),
        SelectionToString(&m_Selection).c_str()
    );

    if(InsideSelection(&m_Selection, x,y,z) == false)
    {
        m_Volume->log(VMAN_LOG_ERROR, "Voxel %s is not in access selection (%s).\n",
            CoordsToString(x,y,z).c_str(),
            SelectionToString(&m_Selection).c_str()
        );
        return NULL;
    }

    const int edgeLength = m_Volume->getChunkEdgeLength();
    int chunkX, chunkY, chunkZ;
    m_Volume->voxelToChunkCoordinates(
        x,
        y,
        z,
        &chunkX,
        &chunkY,
        &chunkZ
    );

    // TODO: Temporary
    if(InsideSelection(&m_ChunkSelection, chunkX, chunkY, chunkZ) == false)
    {
        m_Volume->log(VMAN_LOG_ERROR, "Chunk %s is not in access selection (%s).\n",
            CoordsToString(chunkX,chunkY,chunkZ).c_str(),
            SelectionToString(&m_ChunkSelection).c_str()
        );
    }

    assert(InsideSelection(&m_ChunkSelection, chunkX, chunkY, chunkZ));

    Chunk* chunk = m_Cache[ Index3D(
        m_ChunkSelection.w,
        m_ChunkSelection.h,
        m_ChunkSelection.d,

        chunkX-m_ChunkSelection.x,
        chunkY-m_ChunkSelection.y,
        chunkZ-m_ChunkSelection.z
    ) ];

    // Check if mode includes write access.
    if(mode & VMAN_WRITE_ACCESS)
        chunk->setModified();

    const int voxelSize = m_Volume->getLayer(layer)->voxelSize;

    if(mode & VMAN_WRITE_ACCESS)
    {
        return &reinterpret_cast<char*>( chunk->getLayer(layer) )[Index3D(
            voxelSize*edgeLength,
            voxelSize*edgeLength,
            voxelSize*edgeLength,

            x % edgeLength,
            y % edgeLength,
            z % edgeLength
        )];
    }
    else
    {
        // TODO: Evil evil evil !
        return &const_cast<char*>( reinterpret_cast<const char*>( chunk->getConstLayer(layer) ) )[Index3D(
            voxelSize*edgeLength,
            voxelSize*edgeLength,
            voxelSize*edgeLength,

            x % edgeLength,
            y % edgeLength,
            z % edgeLength
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
