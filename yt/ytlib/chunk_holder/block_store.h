#pragma once

#include "common.h"

#include "../misc/cache.h"

namespace NYT {
namespace NChunkHolder {

////////////////////////////////////////////////////////////////////////////////

//! Represents a cached block of chunk.
class TCachedBlock
    : public TCacheValueBase<TBlockId, TCachedBlock, TBlockIdHash>
{
public:
    typedef TIntrusivePtr<TCachedBlock> TPtr;
    typedef TAsyncResult<TPtr> TAsync;

    //! Constructs a new block from id and data.
    TCachedBlock(const TBlockId& blockId, const TSharedRef& data);

    //! Returns block data.
    TSharedRef GetData() const;

private:
    TSharedRef Data;

};

////////////////////////////////////////////////////////////////////////////////

class TChunkStore;

//! Manages cached blocks.
class TBlockStore
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TBlockStore> TPtr;

    //! Constructs a store.
    TBlockStore(
        const TChunkHolderConfig& config,
        TIntrusivePtr<TChunkStore> chunkStore);

    //! Gets (asynchronously) a block from the store.
    /*!
     * This call returns an async result that becomes set when the 
     * block is fetched. Fetching an already-cached block is cheap
     * (i.e. requires no context switch). Fetching an uncached block
     * enqueues a disk-read action to the appropriate IO queue.
     */
    TCachedBlock::TAsync::TPtr FindBlock(const TBlockId& blockId, i32 blockSize);

    TCachedBlock::TPtr PutBlock(const TBlockId& blockId, const TSharedRef& data);

private:
    class TBlockCache;

    friend class TBlockCache;

    //! Caches blocks in memory.
    TIntrusivePtr<TBlockCache> Cache;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkHolder
} // namespace NYT

