#include <stdlib.h>
#include <assert.h>
#include "vman.h"
#include "World.h"
#include "Access.h"

/*
    Just check the 'this' pointers for NULL here,
    everything else should be done in the apropriate methods.
*/

vmanWorld vmanCreateWorld( const vmanLayer* layers, int layerCount, int chunkEdgeLength, const char* baseDir )
{
    return (vmanWorld)new vman::World(layers, layerCount, chunkEdgeLength, baseDir);
}

void vmanDeleteWorld( const vmanWorld world )
{
    assert(world != NULL);
    delete (const vman::World*)world;
}

vmanAccess vmanCreateAccess( const vmanWorld world )
{
    assert(world != NULL);
    return new vman::Access((vman::World*)world);
}

void vmanDeleteAccess( const vmanAccess access )
{
    assert(access != NULL);
    delete (vman::Access*)access;
}

void vmanSetAccessVolume( vmanAccess access, const vmanVolume* volume )
{
    assert(access != NULL);
    ((vman::Access*)access)->setVolume(volume);
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

