#include "stdafx.h"
#include "chunk_pool.h"
#include "private.h"

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////

class TUnorderedChunkPool
    : public IChunkPool
{
public:
    TUnorderedChunkPool()
        : TotalWeight(0)
    { }

    virtual void Add(TPooledChunkPtr chunk)
    {
        YASSERT(chunk->Weight > 0);

        FOREACH (const auto& address, chunk->InputChunk.holder_addresses()) {
            YVERIFY(AddressToChunks[address].insert(chunk).second);
        }
        YVERIFY(Chunks.insert(chunk).second);
    }

    virtual TExtractResultPtr Extract(
        const Stroka& address,
        i64 weightThreshold,
        int maxCount,
        bool needLocal)
    {
        auto result = New<TExtractResult>();
        int extractedCount = 0;

        // Take local chunks first.
        auto addressIt = AddressToChunks.find(address);
        if (addressIt != AddressToChunks.end()) {
            const auto& localChunks = addressIt->second;
            FOREACH (const auto& chunk, localChunks) {
                if (extractedCount >= maxCount || result->Weight >= weightThreshold) {
                    break;
                }
                result->AddLocal(chunk);
                ++extractedCount;
            }
        }

        if (result->LocalCount == 0 && needLocal) {
            // Could not find any local chunks but requested so.
            // Don't look at remote ones.
            return NULL;
        }

        // Unregister taken local chunks.
        // We have to do this right away, otherwise we risk getting same chunks
        // in the next phase.
        for (int i = 0; i < result->LocalCount; ++i) {
            Extract(result->Chunks[i]);
        }

        // Take remote chunks.
        FOREACH (const auto& chunk, Chunks) {
            if (extractedCount >= maxCount || result->Weight >= weightThreshold) {
                break;
            }
            result->AddRemote(chunk);
            ++extractedCount;
        }

        // Unregister taken remote chunks.
        for (int i = result->LocalCount; i < result->LocalCount + result->RemoteCount; ++i) {
            Extract(result->Chunks[i]);
        }

        return result;
    }

    virtual void PutBack(TExtractResultPtr result)
    {
        FOREACH (const auto& chunk, result->Chunks) {
            Add(chunk);
        }
    }

    virtual i64 GetTotalWeight() const
    {
        return TotalWeight;
    }

    virtual bool HasChunks() const
    {
        return !Chunks.empty();
    }

    virtual bool HasLocalChunksFor(const Stroka& address) const
    {
        auto it = AddressToChunks.find(address);
        return
            it == AddressToChunks.end()
            ? false
            : !it->second.empty();
    }

private:
    i64 TotalWeight;
    yhash_map<Stroka, yhash_set<TPooledChunkPtr> > AddressToChunks;
    yhash_set<TPooledChunkPtr> Chunks;
    
    void Extract(TPooledChunkPtr chunk)
    {
        FOREACH (const auto& address, chunk->InputChunk.holder_addresses()) {
            YVERIFY(AddressToChunks[address].erase(chunk) == 1);
        }
        YVERIFY(Chunks.erase(chunk) == 1);
    }
};

TAutoPtr<IChunkPool> CreateUnorderedChunkPool()
{
    return new TUnorderedChunkPool();
}

////////////////////////////////////////////////////////////////////

class TAtomicChunkPool
    : public IChunkPool
{
public:
    TAtomicChunkPool()
        : TotalWeight(0)
        , Extracted(false)
        , Initialized(false)
        , ExtractResult(New<TExtractResult>())
    { }

    virtual void Add(TPooledChunkPtr chunk)
    {
        YASSERT(!Initialized);
        ExtractResult->AddRemote(chunk);
        FOREACH (const auto& address, chunk->InputChunk.holder_addresses()) {
            Addresses.insert(address);
        }
    }

    virtual TExtractResultPtr Extract(
        const Stroka& address,
        i64 weightThreshold,
        int maxCount,
        bool needLocal)
    {
        UNUSED(address);
        UNUSED(weightThreshold);
        UNUSED(maxCount);
        UNUSED(needLocal);
        
        Initialized = true;
        YASSERT(!Extracted);

        return ExtractResult;
    }

    virtual void PutBack(TExtractResultPtr result)
    {
        YASSERT(result == ExtractResult);
        YASSERT(Initialized);
        YASSERT(Extracted);
        Extracted = false;
    }

    virtual i64 GetTotalWeight() const
    {
        return TotalWeight;
    }

    virtual bool HasPendingChunks() const
    {
        return !Extracted && !ExtractResult->Chunks.empty();
    }

    virtual bool HasPendingLocalChunksFor(const Stroka& address) const
    {
        return
            Extracted
            ? false
            : Addresses.find(address) != Addresses.end();
    }

private:
    //! The sum of weights of all chunks.
    i64 TotalWeight;
    //! Cached extraction result.
    /*!
     *  Pooled chunks are appended here.
     *  In #Extract this result is returned as-is.
     */
    TExtractResultPtr ExtractResult;
    //! Addresses of added chunks.
    yhash_set<Stroka> Addresses;
    //! Have the chunks been #Extract'ed?
    bool Extracted;
    //! Has any #Extract call been made already?
    bool Initialized;

};

TAutoPtr<IChunkPool> CreateAtomicChunkPool()
{
    return new TAtomicChunkPool();
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

