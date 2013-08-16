#include <string.h>
#include <assert.h>
#include <Volume.h>

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
    Volume volume(layers, LAYER_COUNT, CHUNK_EDGE_LENGTH, ".", false);

    assert(volume.getLayerCount() == LAYER_COUNT);
    assert(volume.getMaxLayerVoxelSize() == 1);
    assert(volume.getChunkEdgeLength() == CHUNK_EDGE_LENGTH);
    assert(volume.getVoxelsPerChunk() == CHUNK_EDGE_LENGTH*CHUNK_EDGE_LENGTH*CHUNK_EDGE_LENGTH);
    for(int i = 0; i < LAYER_COUNT; ++i)
    {
        TestLayersForEquallity(volume.getLayer(i), &layers[i]);
        assert(volume.getLayerIndexByName(layers[i].name) == i);
    }
    assert(volume.getLayerIndexByName("nonexistent") == -1);
    assert(strcmp(volume.getBaseDir(), ".") == 0);

    return 0;
}
