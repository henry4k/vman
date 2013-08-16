#ifndef __VMAN_ACCESS_H__
#define __VMAN_ACCESS_H__

#include <vector>
#include "vman.h"

namespace vman
{

class Volume;
class Chunk;

/**
 * Access objects provide r/w access to the volume.
 * They precache chunks as soon as a valid selection has been set.
 */
class Access
{
public:
	/**
	 * Initially the selection will be invalid and all r/w operations will fail.
	 */
	Access( Volume* volume );

	/**
	 * Deleting a locked access object will cause an error!
	 */
	~Access();

    /**
     * Sets the priority value used for sorting io jobs,
     * caused by this access object.
     */
    void setPriority( int priority );


	/**
	 * Updates the selection.
	 * At this point the affected chunks will be precached and preloaded.
	 * Setting the selection to NULL renders it invalid and will prevent any r/w operations.
	 */
	void select( const vmanSelection* selection );

	/**
	 * Locks access to the specified selection.
	 * May block when intersecting chunks are already locked by other access objects.
	 * May also block while affected chunks are loaded from disk.
	 * Multiple access objects may read simultaneously from the same chunk.
	 * Will generate an error if its already locked!
	 * @param mode: Access mode bitmask.
	 * @see vmanAccessMode
	 */
	void lock( int mode );

    /**
     * Behaves like lock(), except that it returns false if the selection is already locked.
     * Returns true on success.
     * @see lock
     */
    bool tryLock( int mode );

	/**
	 * Unlocks access.
	 * Will generate an error if its not locked!
	 */
	void unlock();

	/**
	 * @return: Returns a read only pointer to the voxel data in the specified layer.
	 * Will return NULL if the voxel lies outside the selection or
	 * an incomplatible access mode has been selected.
	 */
	const void* readVoxelLayer( int x, int y, int z, int layer ) const;

	/**
	 * @return: Returns a pointer to the voxel data in the specified layer.
	 * Will return NULL if the voxel lies outside the selection or
	 * an incomplatible access mode has been selected.
	 * Read operations may yield undefined values if write only is active.
	 */
	void* readWriteVoxelLayer( int x, int y, int z, int layer ) const;

private:
	Access( const Access& access );
	Access& operator = ( const Access& access );

	void* getVoxelLayer( int x, int y, int z, int layer, int mode ) const;

	Volume* m_Volume;
    bool m_SelectionIsInvalid;
	bool m_IsLocked;
    int m_AccessMode;
	vmanSelection m_Selection;
    vmanSelection m_ChunkSelection;
    int m_Priority;

	/**
	 * An 3d array that holds pointers to the selected chunks.
	 * May be NULL, then the cache is just not used.
	 * @see m_Selection
	 */
    std::vector<Chunk*> m_Cache;
};

}

#endif
