#include <stdlib.h>
#include <assert.h>
#include "vman.h"
#include "Manager.h"
#include "Volume.h"
#include "Access.h"

/*
    Just check the 'this' pointers for NULL here,
    everything else should be done in the apropriate methods.
*/


bool vmanInit( vmanLogFn logFn, bool enableStatistics )
{
    return vman::Manager::Init(logFn, enableStatistics);
}

void vmanDeinit()
{
    vman::Manager::Deinit();
}

void vmanSetUnusedChunkTimeout( int seconds )
{
    vman::Manager::Singleton()->setUnusedChunkTimeout(seconds);
}

void vmanSetModifiedChunkTimeout( int seconds )
{
    vman::Manager::Singleton()->setModifiedChunkTimeout(seconds);
}

void vmanPanicExit()
{
    vman::Manager::PanicExit();
}

vmanVolume vmanCreateVolume( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir )
{
    return (vmanVolume)new vman::Volume(layers, layerCount, chunkEdgeLength, baseDir);
}

void vmanDeleteVolume( const vmanVolume volume )
{
    assert(volume != NULL);
    delete (const vman::Volume*)volume;
}

void vmanResetStatistics( const vmanVolume volume )
{
    assert(volume != NULL);
    //((vman::Volume*)volume)->resetStatistics();
}

bool vmanGetStatistics( const vmanVolume volume, vmanStatistics* statisticsDestination )
{
    assert(volume != NULL);
    //return ((vman::Volume*)volume)->getStatistics(statisticsDestination);
    return false;
}

vmanAccess vmanCreateAccess( const vmanVolume volume )
{
    assert(volume != NULL);
    return new vman::Access((vman::Volume*)volume);
}

void vmanDeleteAccess( const vmanAccess access )
{
    assert(access != NULL);
    delete (vman::Access*)access;
}

void vmanSelect( vmanAccess access, const vmanSelection* selection )
{
    assert(access != NULL);
    ((vman::Access*)access)->select(selection);
}

void vmanLockAccess( vmanAccess access, int mode )
{
    assert(access != NULL);
    ((vman::Access*)access)->lock(mode);
}

int vmanTryLockAccess( vmanAccess access, int mode )
{
    assert(access != NULL);
    if( ((vman::Access*)access)->tryLock(mode) )
        return 1;
    else
        return 0;
}

void vmanUnlockAccess( vmanAccess access )
{
    assert(access != NULL);
    ((vman::Access*)access)->unlock();
}

const void* vmanReadVoxelLayer( const vmanAccess access, int x, int y, int z, int layer )
{
    assert(access != NULL);
    return ((vman::Access*)access)->readVoxelLayer(x,y,z, layer);
}

void* vmanReadWriteVoxelLayer( const vmanAccess access, int x, int y, int z, int layer )
{
    assert(access != NULL);
    return ((vman::Access*)access)->readWriteVoxelLayer(x,y,z, layer);
}

