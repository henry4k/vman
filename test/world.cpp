#include <string.h>
#include <assert.h>
#include <World.h>

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

void TestLayersForEquallity( const vmanLayer* a, const vmanLayer* b )
{
    assert(strcmp(a->name, b->name) == 0);
    assert(a->voxelSize == b->voxelSize);
    assert(a->revision == b->revision);
    // ...
}

int main()
{
    World world(layers, LAYER_COUNT, CHUNK_EDGE_LENGTH, ".", false);

    assert(world.getLayerCount() == LAYER_COUNT);
    assert(world.getMaxLayerVoxelSize() == 1);
    assert(world.getChunkEdgeLength() == CHUNK_EDGE_LENGTH);
    assert(world.getVoxelsPerChunk() == CHUNK_EDGE_LENGTH*CHUNK_EDGE_LENGTH*CHUNK_EDGE_LENGTH);
    for(int i = 0; i < LAYER_COUNT; ++i)
    {
        TestLayersForEquallity(world.getLayer(i), &layers[i]);
        assert(world.getLayerIndexByName(layers[i].name) == i);
    }
    assert(world.getLayerIndexByName("nonexistent") == -1);
    assert(strcmp(world.getBaseDir(), ".") == 0);

    return 0;
}
