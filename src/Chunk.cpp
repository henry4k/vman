#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "Util.h"
#include "Volume.h"
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
    // Size has to match, since there must be no undefined space in the id.
    assert(sizeof(int16_t)*4 == sizeof(ChunkId));

    ChunkIdHelper helper;

    helper.pos.x = chunkX;
    helper.pos.y = chunkY;
    helper.pos.z = chunkZ;
    helper.pos.w = 0;

    return helper.id;
}

void Chunk::UnpackChunkId( ChunkId chunkId, int* outX, int* outY, int* outZ )
{
    assert(sizeof(int16_t)*4 == sizeof(ChunkId));
    // Size has to match, since there must be no undefined space in the id.

    ChunkIdHelper helper;

    helper.id = chunkId;

    *outX = helper.pos.x;
    *outY = helper.pos.y;
    *outZ = helper.pos.z;
    assert(helper.pos.w == 0); // See GenerateChunkId
}

std::string Chunk::ChunkIdToString( ChunkId chunkId )
{
    int x, y, z;
    UnpackChunkId(chunkId, &x, &y, &z);
    return CoordsToString(x,y,z);
}

Chunk::Chunk( Volume* volume, int chunkX, int chunkY, int chunkZ ) :
    m_Volume(volume),
    m_ChunkX(chunkX),
    m_ChunkY(chunkY),
    m_ChunkZ(chunkZ),
    m_Layers(volume->getLayerCount()), // n layers initialized with NULL
    m_Modified(false)
{
	memset(m_Layers[0], 0, m_Layers.size()*sizeof(char*));
}

Chunk::~Chunk()
{
    if(m_Volume->getBaseDir() != NULL)
        assert(m_Modified == false);
    assert(m_References == 0);
    clearLayers(true);
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

std::string Chunk::toString() const
{
    return Format("%d|%d|%d", m_ChunkX, m_ChunkY, m_ChunkZ);
}

void Chunk::initializeLayer( int index )
{
    const vmanLayer* layer = m_Volume->getLayer(index);
    assert(layer != NULL);

    const int bytes = m_Volume->getVoxelsPerChunk()*layer->voxelSize;

    assert(m_Layers[index] == NULL);
    m_Layers[index] = new char[bytes];
    memset(m_Layers[index], 0, bytes);

    setModified();
}

void Chunk::clearLayers( bool silent )
{
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        if(m_Layers[i] != NULL)
        {
            delete[] m_Layers[i];
            m_Layers[i] = NULL;
            if(!silent)
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
    setModified();
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
    m_Volume->incStatistic(STATISTIC_CHUNK_LOAD_OPS);

    m_Volume->log(VMAN_LOG_DEBUG, "Loading chunk %s from file ..\n", toString().c_str());

    if(m_Volume->getBaseDir() == NULL)
    {
        assert(!"Probably redundant.");
        return false;
    }

    const int voxelsPerChunk = m_Volume->getVoxelsPerChunk();

    std::string fileName = m_Volume->getChunkFileName(m_ChunkX, m_ChunkY, m_ChunkZ);

    FILE* f = fopen(fileName.c_str(), "rb");
    if(f == NULL)
    {
        m_Volume->log(VMAN_LOG_DEBUG, "%s: File does not exist.\n", fileName.c_str());
        return false;
    }

    try
    {
        // -- Read header --
        ChunkFileHeader header;
        if(fread(&header, sizeof(header), 1, f) != 1)
            throw "Read error in file header.";
        header.version = LittleEndian(header.version);
        header.edgeLength = LittleEndian(header.edgeLength);
        header.layerCount = LittleEndian(header.layerCount);

        m_Volume->log(VMAN_LOG_DEBUG, "version: %d\n", header.version);
        m_Volume->log(VMAN_LOG_DEBUG, "edgeLength: %d\n", header.edgeLength);
        m_Volume->log(VMAN_LOG_DEBUG, "layerCount: %d\n", header.layerCount);

        if(header.version != ChunkFileVersion)
            throw "Incorrect file version.";

        std::vector<ChunkFileLayerInfo> layerInfos(header.layerCount);

        // -- Read layer list --
        for(int i = 0; i < layerInfos.size(); ++i)
        {
            ChunkFileLayerInfo* layerInfo = &layerInfos[i];
            if(fread(layerInfo, sizeof(ChunkFileLayerInfo), 1, f) != 1)
                throw Format("Read error in layer info %d", i);
            layerInfo->voxelSize = LittleEndian(layerInfo->voxelSize);
            layerInfo->revision = LittleEndian(layerInfo->revision);
            layerInfo->fileOffset = LittleEndian(layerInfo->fileOffset);

            m_Volume->log(VMAN_LOG_DEBUG, "[layer %d] name: '%s'\n", i, layerInfo->name);
            m_Volume->log(VMAN_LOG_DEBUG, "[layer %d] voxelSize: %d\n", i, layerInfo->voxelSize);
            m_Volume->log(VMAN_LOG_DEBUG, "[layer %d] revision: %d\n", i, layerInfo->revision);
            m_Volume->log(VMAN_LOG_DEBUG, "[layer %d] fileOffset: %d\n", i, layerInfo->fileOffset);

            if(m_Volume->getLayerIndexByName(layerInfo->name) == -1)
            {
                m_Volume->log(VMAN_LOG_INFO, "%s: Ignoring chunk layer '%s'.\n", fileName.c_str(), layerInfo->name);
            }
        }

        // -- Copy used layers --
        std::vector<char> buffer(voxelsPerChunk * m_Volume->getMaxLayerVoxelSize());
        for(int i = 0; i < m_Layers.size(); ++i)
        {
            const vmanLayer* layer = m_Volume->getLayer(i);
            const ChunkFileLayerInfo* layerInfo = FindChunkLayerByName(layerInfos, layer->name);
            if(layerInfo)
            {
                if(
                    (layer->voxelSize != layerInfo->voxelSize) ||
                    (layer->revision != layerInfo->revision)
                )
                {
                    m_Volume->log(VMAN_LOG_ERROR,"%s: Chunk layer '%s' differs, ignoring it.\n", fileName.c_str(), layer->name);
                    // TODO: Maybe let the application try to import/convert the layer.
                    continue;
                }

                fseek(f, layerInfo->fileOffset, SEEK_SET);
                if(fread(&buffer[0], voxelsPerChunk*layer->voxelSize, 1, f) != 1)
                    throw Format("Read error in layer %d.", i);

                m_Layers[i] = new char[voxelsPerChunk*layer->voxelSize];
                layer->deserializeFn(&buffer[0], m_Layers[i], voxelsPerChunk*layer->voxelSize);
            }
        }
    }
    catch(const std::string e)
    {
        m_Volume->log(VMAN_LOG_ERROR, "%s: %s\n", fileName.c_str(), e.c_str());
        fclose(f);
        clearLayers();
        assert(false);
        return false;
    }

    fclose(f);
    return true;
}

bool Chunk::saveToFile()
{
    m_Volume->incStatistic(STATISTIC_CHUNK_SAVE_OPS);

    m_Volume->log(VMAN_LOG_DEBUG, "Saving chunk %s to file ..\n", toString().c_str());

    if(m_Volume->getBaseDir() == NULL)
    {
        assert(!"Probably redundant.");
        return false;
    }

    assert(m_Layers.size() > 0);

    const int voxelsPerChunk = m_Volume->getVoxelsPerChunk();

    std::string fileName = m_Volume->getChunkFileName(m_ChunkX, m_ChunkY, m_ChunkZ);

    MakePath(fileName.c_str());

    FILE* f = fopen(fileName.c_str(), "wb"); // TODO: Check
    if(f == NULL)
    {
        m_Volume->log(VMAN_LOG_ERROR, "%s: Can't open file for writing.\n", fileName.c_str());
        return false;
    }

    // -- Write header ---
    ChunkFileHeader header;
    header.version = LittleEndian( ChunkFileVersion );
    header.edgeLength = LittleEndian( m_Volume->getChunkEdgeLength() );
    int usedLayers = 0;
    for(int i = 0; i < m_Layers.size(); ++i)
        if(m_Layers[i] != NULL)
            ++usedLayers;
    header.layerCount = LittleEndian( usedLayers );
    fwrite(&header, sizeof(header), 1, f);

    const uint32_t headerSize = sizeof(ChunkFileHeader) + sizeof(ChunkFileLayerInfo)*usedLayers;

    // -- Write layer list --
    uint32_t fileOffset = headerSize;
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        if(m_Layers[i] != NULL)
        {
            ChunkFileLayerInfo layerInfo;
            const vmanLayer* layer = m_Volume->getLayer(i);

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
    std::vector<char> buffer(voxelsPerChunk * m_Volume->getMaxLayerVoxelSize()); // TODO: This buffer could be thread local ...
    for(int i = 0; i < m_Layers.size(); ++i)
    {
        if(m_Layers[i] != NULL)
        {
            const vmanLayer* layer = m_Volume->getLayer(i);
            layer->serializeFn(m_Layers[i], &buffer[0], voxelsPerChunk);
            fwrite(&buffer[0], voxelsPerChunk*layer->voxelSize, 1, f); // TODO: Check
        }
    }

    fclose(f);
    unsetModified();
    return true;
}


void Chunk::addReference()
{
    m_References++;
    //m_Volume->log(VMAN_LOG_DEBUG, "%p references++ = %d\n", this, (int)m_References);
}

void Chunk::releaseReference()
{
    assert(m_References > 0);
    if(--m_References == 0)
    {
        m_Volume->scheduleCheck(Volume::CHECK_CAUSE_UNUSED, this);
    }
    //m_Volume->log(VMAN_LOG_DEBUG, "%p references-- = %d\n", this, (int)m_References);
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
        m_Volume->scheduleCheck(Volume::CHECK_CAUSE_MODIFIED, this);
    }
}

void Chunk::unsetModified()
{
    m_Modified = false;
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
