#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <vman.h>

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
    vmanWorld world = vmanCreateWorld(layers, LAYER_COUNT, CHUNK_EDGE_LENGTH, ".", false);

    vmanAccess access = vmanCreateAccess(world);
    vmanVolume volume =
    {
        0,0,0,
        4,4,4
    }; // 4*4*4 cube
    vmanSetAccessVolume(access, &volume);
    vmanLockAccess(access, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);
    
    int32_t* baseVoxel;
    baseVoxel = (int32_t*)vmanReadWriteVoxelLayer(access, 0,0,0, BASE_LAYER);
    *baseVoxel = 4000;
    baseVoxel = (int32_t*)vmanReadWriteVoxelLayer(access, 0,0,0, BASE_LAYER);
    printf("Voxel at %d/%d/%d is %d\n",
        volume.x,
        volume.y,
        volume.z,
        *baseVoxel
    );

    vmanUnlockAccess(access);
    vmanDeleteAccess(access);
    
    vmanDeleteWorld(world);
    return 0;
}
