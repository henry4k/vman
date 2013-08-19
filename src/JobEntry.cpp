#include <assert.h>
#include "Chunk.h"
#include "JobEntry.h"

namespace vman
{

const JobEntry JobEntry::InvalidJob;

JobEntry::JobEntry() :
    m_Priority(0),
    m_Type(INVALID_JOB),
    m_Chunk(NULL)
{
}

JobEntry::JobEntry( int priority, JobType type, Chunk* chunk ) :
    m_Priority(priority),
    m_Type(type),
    m_Chunk(chunk)
{
    assert(m_Chunk != NULL);
    m_Chunk->addReference();
}

JobEntry::JobEntry( const JobEntry& e ) :
    m_Priority(e.m_Priority),
    m_Type(e.m_Type),
    m_Chunk(e.m_Chunk)
{
    if(m_Chunk)
        m_Chunk->addReference();
}

JobEntry& JobEntry::operator = ( const JobEntry& e )
{
    if(m_Chunk)
        m_Chunk->releaseReference();

    m_Priority = e.m_Priority;
    m_Type     = e.m_Type;
    m_Chunk    = e.m_Chunk;

    if(m_Chunk)
        m_Chunk->addReference();

    return *this;
}

JobEntry::~JobEntry()
{
    if(m_Chunk)
    {
        m_Chunk->releaseReference();
    }
}

int JobEntry::getPriority() const
{
    return m_Priority;
}

JobType JobEntry::getType() const
{
    return m_Type;
}

Chunk* JobEntry::getChunk() const
{
    return m_Chunk;
}


}
