#ifndef __VMAN_JOB_ENTRY_H__
#define __VMAN_JOB_ENTRY_H__


namespace vman
{

class Chunk;

enum JobType
{
    /**
     * Marks an job as invalid.
     * Invalid jobs are not processed and can be used to indicate an error.
     */
    INVALID_JOB,

    /**
     * A load job loads or reloads a chunk from the filesystem.
     */
    LOAD_JOB,

    /**
     * A save job saves a chunks to the filesystem.
     */
    SAVE_JOB
};


/**
 * Structure that contains information about a chunk job.
 * It will hold a reference of the chunk.
 */
class JobEntry
{
public:
    /**
     * For better code documentation,
     * this should be used instead of
     * the empty constructor whenever possible.
     * @see JobEntry()
     */
    static const JobEntry InvalidJob;

    /**
     * Constructs an invalid job.
     * Use InvalidJob whenever possible.
     * @see InvalidJob
     */
    JobEntry();

    /**
     * Constructs a new job.
     *
     * @param priority
     * The higher the job priority the earlier it will be processed by the job workers.
     */
    JobEntry( int priority, JobType type, Chunk* chunk );

    JobEntry( const JobEntry& e );
	JobEntry& operator = ( const JobEntry& e );

    ~JobEntry();


    int     getPriority() const;
    JobType getType() const;
    Chunk*  getChunk() const;

private:
    int     m_Priority;
    JobType m_Type;
    Chunk*  m_Chunk;
};


}


#endif
