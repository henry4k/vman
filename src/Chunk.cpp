#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "Util.h"
#include "World.h"
#include "Chunk.h"


namespace vman
{

union ChunkIdHelper
{
    struct
    {
        int16_t x, y, z, w;
    } pos;
    ChunkId id;
};

ChunkId Chunk::GenerateChunkId( int chunkX, int chunkY, int chunkZ )
{
	assert(sizeof(int16_t)*4 == sizeof(ChunkId));
	// Size has to match, since there must be no undefined space in the id.

    ChunkIdHelper helper;

	helper.pos.x = chunkX;
	helper.pos.y = chunkY;
	helper.pos.z = chunkZ;
	helper.pos.w = 0;

	return helper.id;
}

/*
void Chunk::UnpackChunkId( ChunkId chunkId, int* outX, int* outY, int* outZ )
{
	assert(sizeof(int16_t)*4 == sizeof(ChunkId));
	// Size has to match, since there must be no undefined space in the id.

    ChunkIdHelper helper;

    helper.id = chunkId;

    *outX = helper.pos.x;
    *outY = helper.pos.y;
    *outZ = helper.pos.z;
    assert(helper.pos.z == 0); // See GenerateChunkId
}

std::string Chunk::ChunkIdToString( ChunkId chunkId )
{
    int x, y, z;
    UnpackChunkId(chunkId, &x, &y, &z);

    char buffer[128];
    sprintf(buffer, "%d/%d/%d", x, y, z);
    return buffer;
}
*/

Chunk::Chunk( World* world, int chunkX, int chunkY, int chunkZ ) :
	m_World(world),
    m_ChunkX(chunkX),
    m_ChunkY(chunkY),
    m_ChunkZ(chunkZ),
	m_Layers(world->getLayerCount(), NULL), // n layers initialized with NULL
    m_Modified(false)
{
}

Chunk::~Chunk()
{
    assert(m_References == 0);
}

int Chunk::getChunkX() const
{
    return m_ChunkX;
}

int Chunk::getChunkY() const
{
    return m_ChunkY;
}

int Chunk::getChunkZ() const
{
    return m_ChunkZ;
}

ChunkId Chunk::getId() const
{
    return GenerateChunkId(m_ChunkX, m_ChunkY, m_ChunkZ);
}

void Chunk::initializeLayer( int index )
{
	assert(m_Layers[index] == NULL);
	const vmanLayer* layer = m_World->getLayer(index);
	assert(layer != NULL);

    const int bytes = m_World->getVoxelsPerChunk()*layer->voxelSize;

	m_Layers[index] = new char[bytes];
	memset(m_Layers[index], 0, bytes);

    m_World->log(LOG_DEBUG, "%d/%d/%d: Initialized layer %d\n",
        m_ChunkX, m_ChunkY, m_ChunkZ, index
    );
}

void Chunk::clearLayers()
{
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        if(m_Layers[i] != NULL)
        {
            delete[] m_Layers[i];
            m_Layers[i] = NULL;
            setModified();
        }
    }
}

void* Chunk::getLayer( int index )
{
	if((index < 0) || (index >= m_Layers.size()))
		return NULL;
	if(m_Layers[index] == NULL)
		initializeLayer(index);
	return m_Layers[index];
}

const void* Chunk::getConstLayer( int index ) const
{
	if((index < 0) || (index >= m_Layers.size()))
		return NULL;
	return m_Layers[index];
}

/*
    Chunk file format:
    
    Header:
        uint32 version
        uint32 edgeLength
        uint32 layerCount
        [
            char[32] name
            uint32 revision
            uint32 offsetPointer
        ]
*/

struct ChunkFileHeader
{
    uint32_t version;
    uint32_t edgeLength;
    uint32_t layerCount;
};

struct ChunkFileLayerInfo
{
    char name[VMAN_MAX_LAYER_NAME_LENGTH+1];
    uint32_t voxelSize;
    uint32_t revision;
    uint32_t fileOffset;
};

ChunkFileLayerInfo* FindChunkLayerByName( std::vector<ChunkFileLayerInfo>& layerInfos, const char* name )
{
    for(int i = 0; i < layerInfos.size(); ++i)
    {
        if(strncmp(name, layerInfos[i].name, VMAN_MAX_LAYER_NAME_LENGTH) == 0)
            return &layerInfos[i];
    }
    return NULL;
}

static const int ChunkFileVersion = 1;

bool Chunk::loadFromFile()
{
    if(m_World->getBaseDir() == NULL)
    {
        assert(!"Probably redundant.");
        return false;
    }

    const int voxelsPerChunk = m_World->getVoxelsPerChunk();

    std::string fileName = m_World->getChunkFileName(m_ChunkX, m_ChunkY, m_ChunkZ);
    
    FILE* f = fopen(fileName.c_str(), "rb");
    if(f == NULL)
    {
        m_World->log(LOG_DEBUG, "%s: File does not exist.\n", fileName.c_str());
        return false;
    }

    try
    {
        // -- Read header --
        ChunkFileHeader header;
        if(fread(&header, sizeof(header), 1, f) != 1)
            throw "Read error in header.";
        header.version = LittleEndian(header.version);
        header.edgeLength = LittleEndian(header.edgeLength);
        header.layerCount = LittleEndian(header.layerCount);
        m_World->log(LOG_DEBUG, "%s: version=%d edgeLength=%d layerCount=%d\n",
            fileName.c_str(),
            header.version,
            header.edgeLength,
            header.layerCount
        );

        if(header.version != ChunkFileVersion)
            throw "Incorrect file version.";

        std::vector<ChunkFileLayerInfo> layerInfos(header.layerCount);

        // -- Read layer list --
        for(int i = 0; i < layerInfos.size(); ++i)
        {
            ChunkFileLayerInfo* layerInfo = &layerInfos[i];
            if(fread(layerInfo, sizeof(ChunkFileLayerInfo), 1, f) != 1)
                throw "Read error in layer list.";
            layerInfo->voxelSize = LittleEndian(layerInfo->voxelSize);
            layerInfo->revision = LittleEndian(layerInfo->revision);
            layerInfo->fileOffset = LittleEndian(layerInfo->fileOffset);
            m_World->log(LOG_DEBUG, "%s layer %d: name=%s voxelSize=%d revision=%d fileOffset=%d\n",
                fileName.c_str(), i,
                layerInfo->name,
                layerInfo->voxelSize,
                layerInfo->revision,
                layerInfo->fileOffset
            );

            if(m_World->getLayerIndexByName(layerInfo->name) == -1)
            {
                m_World->log(LOG_INFO,"%s: Ignoring chunk layer '%s'.\n", fileName.c_str(), layerInfo->name);
            }
        }

        // -- Copy used layers --
        std::vector<char> buffer(voxelsPerChunk * m_World->getMaxLayerVoxelSize());
        for(int i = 0; i < m_Layers.size(); ++i)
        {
            const vmanLayer* layer = m_World->getLayer(i);
            const ChunkFileLayerInfo* layerInfo = FindChunkLayerByName(layerInfos, layer->name);
            if(layerInfo)
            {
                if(
                    (layer->voxelSize != layerInfo->voxelSize) ||
                    (layer->revision != layerInfo->revision)
                )
                {
                    m_World->log(LOG_ERROR,"%s: Chunk layer '%s' differs, ignoring it.\n", fileName.c_str(), layer->name);
                    // TODO: Maybe let the application try to import/convert the layer.
                    continue;
                }

                fseek(f, layerInfo->fileOffset, SEEK_SET);
                if(fread(&buffer[0], voxelsPerChunk*layer->voxelSize, 1, f) != 1)
                    throw "Read error in layer.";
                
                m_Layers[i] = new char[voxelsPerChunk*layer->voxelSize];
                layer->deserializeFn(&buffer[0], m_Layers[i], voxelsPerChunk*layer->voxelSize);
            }
        }
    }
    catch(const char* e)
    {
        m_World->log(LOG_ERROR, "%s: %s\n", fileName.c_str(), e);
        fclose(f);
        clearLayers();
        return false;
    }

    fclose(f);
    return true;
}

bool Chunk::saveToFile()
{
    if(m_World->getBaseDir() == NULL)
    {
        assert(!"Probably redundant.");
        return false;
    }

    const int voxelsPerChunk = m_World->getVoxelsPerChunk();

    std::string fileName = m_World->getChunkFileName(m_ChunkX, m_ChunkY, m_ChunkZ);
    
    FILE* f = fopen(fileName.c_str(), "wb"); // TODO: Check
    if(f == NULL)
    {
        m_World->log(LOG_ERROR, "%s: Can't open file for writing.\n", fileName.c_str());
        return false;
    }

    // -- Write header ---
    uint32_t headerSize = sizeof(ChunkFileHeader) + sizeof(ChunkFileLayerInfo)*m_Layers.size();

    ChunkFileHeader header;
    header.version = LittleEndian( ChunkFileVersion );
    header.edgeLength = LittleEndian( m_World->getChunkEdgeLength() );
    int usedLayers = 0;
    for(int i = 0; i < m_Layers.size(); ++i)
        if(m_Layers[i] != NULL)
            ++usedLayers;
    header.layerCount = LittleEndian( usedLayers );
    fwrite(&header, sizeof(header), 1, f);

    // -- Write layer list --
    uint32_t fileOffset = headerSize;
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        if(m_Layers[i] != NULL)
        {
            ChunkFileLayerInfo layerInfo;
            const vmanLayer* layer = m_World->getLayer(i);

            memset(layerInfo.name, 0, sizeof(layerInfo.name));
            strncpy(layerInfo.name, layer->name, sizeof(layerInfo.name)-1);

            layerInfo.voxelSize = LittleEndian(layer->voxelSize);
            layerInfo.revision = LittleEndian(layer->revision);
            layerInfo.fileOffset = LittleEndian(fileOffset);

            fwrite(&layerInfo, sizeof(layerInfo), 1, f);

            // Calculate layer size
            fileOffset += voxelsPerChunk * layerInfo.voxelSize;
        }
    }

    // -- Write actual layers --
    std::vector<char> buffer(voxelsPerChunk * m_World->getMaxLayerVoxelSize());
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        const vmanLayer* layer = m_World->getLayer(i);
        layer->serializeFn(m_Layers[i], &buffer[0], voxelsPerChunk);
        fwrite(&buffer[0], voxelsPerChunk*layer->voxelSize, 1, f); // TODO: Check
    }

    fclose(f);
    unsetModified();
    return true;
}


void Chunk::addReference()
{
    m_References++;
    if(m_References == 1)
        m_ReferenceChangeTime = time(NULL);
}

void Chunk::releaseReference()
{
    assert(m_References > 0);
    m_References--;
    if(m_References == 0)
    {
        m_ReferenceChangeTime = time(NULL);
        m_World->scheduleTask(World::UNLOAD_UNUSED_TASK, this);
    }
}

bool Chunk::isUnused() const
{
    return m_References == 0;
}

bool Chunk::isModified() const
{
    return m_Modified;
}

time_t Chunk::getModificationTime() const
{
    return m_ModificationTime;
}

void Chunk::setModified()
{
    if(m_Modified == false)
    {
        m_Modified = true;
        m_ModificationTime = time(NULL);
        m_World->scheduleTask(World::SAVE_MODIFIED_TASK, this);
    }
}

void Chunk::unsetModified()
{
    m_Modified = false;
}

time_t Chunk::getReferenceChangeTime() const
{
    return m_ReferenceChangeTime;
}


tthread::mutex* Chunk::getMutex()
{
    return &m_Mutex;
}



/** Forbidden Stuff **/

Chunk::Chunk( const Chunk& chunk ) :
    m_ChunkX(0),
    m_ChunkY(0),
    m_ChunkZ(0)
{
	assert(false);
}

Chunk& Chunk::operator = ( const Chunk& chunk )
{
	assert(false);
    return *this;
}



}
