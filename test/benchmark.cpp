#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <vector>
#include <tinythread.h>
#include <vman.h>


int Random( int min, int max )
{
    return rand() % (max-min+1) + min;
}

struct Configuration
{
    vmanWorld world;
    vmanLayer* layers;
    int layerCount;
    int iterations;
    int maxVolumeDistance;
    int maxVolumeSize;
};


// -------



std::vector<tthread::thread*> threads;

void ThreadFn( void* context )
{
    const Configuration* config = (Configuration*)context;

    vmanAccess access = vmanCreateAccess(config->world);

    for(int i = 0; i < config->iterations; ++i)
    {
        vmanVolume volume =
        {
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),
            Random(-config->maxVolumeDistance, config->maxVolumeDistance),

            Random(1, config->maxVolumeSize),
            Random(1, config->maxVolumeSize),
            Random(1, config->maxVolumeSize)
        };
        vmanSetAccessVolume(access, &volume);
        vmanLockAccess(access, VMAN_READ_ACCESS|VMAN_WRITE_ACCESS);

        char* baseVoxel;
        baseVoxel = (char*)vmanReadWriteVoxelLayer(access,
            Random(volume.x, volume.x + volume.w-1),
            Random(volume.y, volume.y + volume.h-1),
            Random(volume.z, volume.z + volume.d-1),
            Random(0, config->layerCount-1)
        );

        if(baseVoxel != NULL)
            *baseVoxel = 42;

        vmanUnlockAccess(access);
    }

    vmanDeleteAccess(access);
}


// -----------


void CopyBytes( const void* source, void* destination, int count )
{
    memcpy(destination, source, count);
}

char* CreateString( const char* format, ... )
{
    char buf[256];

    va_list args;
    va_start(args, format);
    int len = vsprintf(buf, format, args);
    va_end(args);

    char* out = new char[len+1];
    memcpy(out, buf, len+1);
    return out;
}

vmanLayer* CreateLayers( int count, int size )
{
    vmanLayer* layers = new vmanLayer[count];
    for(int i = 0; i < count; ++i)
    {
        vmanLayer& layer = layers[i];
        layer.name = CreateString("Layer %d", i);
        layer.voxelSize = size;
        layer.revision = 1;
        layer.serializeFn = CopyBytes;
        layer.deserializeFn = CopyBytes;
    }
    return layers;
}

void DestroyLayers( vmanLayer* layers, int count )
{
    for(int i = 0; i < count; ++i)
    {
        vmanLayer& layer = layers[i];
        delete[] layer.name;
    }
    delete[] layers;
}

int main()
{
    srand(time(NULL));

    int layerSize = 1;
    int layerCount = 4;
    vmanLayer* layers = CreateLayers(layerCount, layerSize);
    int chunkEdgeLength = 8;
    const char* worldDir = "TestWorld";

    Configuration config;
    config.world = vmanCreateWorld(layers, layerCount, chunkEdgeLength, worldDir);
    config.layers = layers;
    config.layerCount = layerCount;
    config.iterations = 1;
    config.maxVolumeDistance = 10;
    config.maxVolumeSize = 10;

    for(int i = 0; i < 1; ++i)
    {
        threads.push_back( new tthread::thread(ThreadFn, &config) );
    }

    for(int i = 0; i < threads.size(); ++i)
    {
        if(threads[i]->joinable())
            threads[i]->join();
        delete threads[i];
    }

    vmanDeleteWorld(config.world);

    DestroyLayers(layers, layerCount);
    return 0;
}
