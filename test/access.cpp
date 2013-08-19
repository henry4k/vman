#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <Volume.h>
#include <Chunk.h>
#include <Access.h>

using namespace vman;

enum LayerIndex
{
    BASE_LAYER = 0,
    EXTRA_LAYER,
    LAYER_COUNT
};

void CopyBytes( const void* source, void* destination, int count )
{
    memcpy(destination, source, count);
}

static const vmanLayer layers[LAYER_COUNT] =
{
    {"Material", 1, 1, CopyBytes, CopyBytes},
    {"Pressure", 1, 1, CopyBytes, CopyBytes}
};

static const int CHUNK_EDGE_LENGTH = 8;

int main()
{
	vmanVolumeParameters volumeParams;
	vmanInitVolumeParameters(&volumeParams);
	volumeParams.layers = layers;
	volumeParams.layerCount = LAYER_COUNT;
	volumeParams.chunkEdgeLength = CHUNK_EDGE_LENGTH;
	volumeParams.baseDir = ".";
    Volume volume(&volumeParams);
	
    Access access(&volume);

    {
        vmanSelection selection;
        selection.x = -20;
        selection.y = -20;
        selection.z = -20;
        selection.w = 40;
        selection.h = 40;
        selection.d = 40;

        access.select(&selection);
    }

    {
        access.lock(VMAN_WRITE_ACCESS);

        char* voxel = (char*)access.readWriteVoxelLayer(0,0,0, BASE_LAYER);
        *voxel = 'X';

        access.unlock();
    }

    {
        access.lock(VMAN_READ_ACCESS);

        const char* voxel = (const char*)access.readVoxelLayer(0,0,0, BASE_LAYER);
        assert(*voxel == 'X');

        access.unlock();
    }

    puts("No problems detected.");

    return 0;
}
