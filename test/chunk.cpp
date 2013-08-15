#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <World.h>
#include <Chunk.h>

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
    World world(layers, LAYER_COUNT, CHUNK_EDGE_LENGTH, ".", false);
    
    {
        Chunk chunk(&world, 1,2,3);
        char* material = (char*)chunk.getLayer(0);
        char* pressure = (char*)chunk.getLayer(1);
        material[0] = 42;
        pressure[0] = 100;
        bool success = chunk.saveToFile();
        assert(success);
    }
    
    {
        Chunk chunk(&world, 1,2,3);
        bool success = chunk.loadFromFile();
        assert(success);
        const char* material = (const char*)chunk.getConstLayer(0);
        const char* pressure = (const char*)chunk.getConstLayer(1);
        assert(material[0] == 42);
        assert(pressure[0] == 100);
    }

    {
        Chunk chunk(&world, 9,9,9);
        bool success = chunk.loadFromFile();
        assert(success == false);
    }

    puts("No problems detected.");

    return 0;
}
