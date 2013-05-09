#ifndef __VMAN_CHUNK_H__
#define __VMAN_CHUNK_H__

#include <stdint.h>
#include <time.h>
#include <vector>
#include <tinythread.h>


namespace vman
{

class World;

typedef uint64_t ChunkId;

class Chunk
{
public:
	static ChunkId GenerateChunkId( int chunkX, int chunkY, int chunkZ );


	Chunk( World* world, int chunkX, int chunkY, int chunkZ );
    ~Chunk();

    int getChunkX() const;
    int getChunkY() const;
    int getChunkZ() const;
    ChunkId getId() const;

	/**
	 * Will create a layer if it doesn't exists already.
	 * Data is initialized to `0`.
	 * Use the chunk edge length to compute the array size.
	 * @return Data of the given layer.
	 * @see World#getChunkEdgeLength
	 */
	void* getLayer( int index );
	
	/**
	 * Const pointer version of getLayer.
	 * @return `NULL` if a layer doesn't exists.
	 * @see getLayer
	 */
	const void* getConstLayer( int index ) const;

    /**
     * Clears chunk on failure!
     * @return `false` if the file is not readable.
     */
    bool loadFromFile();

    /**
     * Will unset `m_Modified` on success.
     * @return `true` on success.
     */
    bool saveToFile();


    /**
     * Increments the internal reference counter.
     * Chunks with reference won't be unloaded.
     * @see releaseReference
     */
    void addReference();

    /**
     * Decrements the internal reference counter.
     * @see addReference
     */
    void releaseReference();

    /**
     * Whether the chunk may be unloaded.
     */
    bool isUnused() const;

    /**
     * Timestamp since the reference count changed from or to 0.
     * @see isUnused
     */
    time_t getReferenceChangeTime() const;

    /**
     *
     */
    bool isModified() const;

    /**
     * Timestamp of the first modification since last save event.
     */
    time_t getModificationTime() const;

    /**
     * If it wasn't modified before:
     * Sets the modification flag, updates the modification time
     * and adds the chunk to the modified list.
     */
    void setModified();

    /**
     *
     */
    tthread::mutex* getMutex();


private:
	Chunk( const Chunk& chunk );
	Chunk& operator = ( const Chunk& chunk );

	void initializeLayer( int index );


    /**
     * Deletes all layers and resets them to `NULL`.
     * If at least one layer was deleted `m_Modified` will be set.
     */
    void clearLayers();

	World* m_World;

    const int m_ChunkX;
    const int m_ChunkY;
    const int m_ChunkZ;

    /**
     * Holds voxel arrays for all layers registered in the world.
     * A pointer is `NULL` if the layer is not used in this chunk,
     * in that case the layers default voxel value shuld be used.
     * Voxels have to be initialized with `0`!
     */
	std::vector<char*> m_Layers;
	
	/**
	 * Which layers are compressed.
	 */
	// std::vector<bool> m_LayerCompressed;

	/**
	 * True when the chunk has been modified
	 * and needs to be written to disk.
	 */
	bool m_Modified;


    /**
     * Timestamp of the first modification since last save event.
     * Is updated each time, when m_Modified changes from false to true.
     */
    time_t m_ModificationTime;


    /**
     * Resets the modified flag.
     */
    void unsetModified();


    /**
     * Reference count on this chunk.
     */
    int m_References;

    /**
     * Timestamp since the reference count changed from or to 0.
     */
    time_t m_ReferenceChangeTime;


    tthread::mutex m_Mutex;
};

}

#endif
