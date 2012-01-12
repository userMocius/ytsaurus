#include "stdafx.h"
#include "holder.h"

#include <yt/ytlib/misc/assert.h>
#include <yt/ytlib/misc/serialize.h>

namespace NYT {
namespace NChunkServer {

using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

void SaveStatistics(TOutputStream* out, const THolderStatistics& statistics)
{
    ::Save(out, statistics.available_space());
    ::Save(out, statistics.used_space());
    ::Save(out, statistics.chunk_count());
    ::Save(out, statistics.session_count());
}

void LoadStatistics(TInputStream* in, THolderStatistics& statistics)
{
    i64 availableSpace;
    i64 usedSpace;
    i32 chunkCount;
    i32 sessionCount;
    ::Load(in, availableSpace);
    ::Load(in, usedSpace);
    ::Load(in, chunkCount);
    ::Load(in, sessionCount);

    statistics.set_available_space(availableSpace);
    statistics.set_used_space(usedSpace);
    statistics.set_chunk_count(chunkCount);
    statistics.set_session_count(sessionCount);
}

////////////////////////////////////////////////////////////////////////////////

THolder::THolder(
    THolderId id,
    const Stroka& address,
    EHolderState state,
    const THolderStatistics& statistics)
    : Id_(id)
    , Address_(address)
    , State_(state)
    , Statistics_(statistics)
{ }

THolder::THolder(const THolder& other)
    : Id_(other.Id_)
    , Address_(other.Address_)
    , State_(other.State_)
    , Statistics_(other.Statistics_)
    , StoredChunkIds_(other.StoredChunkIds_)
    , CachedChunkIds_(other.CachedChunkIds_)
    , JobIds_(other.JobIds_)
{ }

TAutoPtr<THolder> THolder::Clone() const
{
    return new THolder(*this);
}

void THolder::Save(TOutputStream* output) const
{
    ::Save(output, Address_);
    ::Save(output, State_);
    SaveStatistics(output, Statistics_);
    SaveSet(output, StoredChunkIds_);
    SaveSet(output, CachedChunkIds_);
    ::Save(output, JobIds_);
}

TAutoPtr<THolder> THolder::Load(THolderId id, TInputStream* input)
{
    Stroka address;
    EHolderState state;
    THolderStatistics statistics;
    ::Load(input, address);
    ::Load(input, state);
    LoadStatistics(input, statistics);

    TAutoPtr<THolder> holder = new THolder(id, address, state, statistics);
    LoadSet(input, holder->StoredChunkIds_);
    LoadSet(input, holder->CachedChunkIds_);
    ::Load(input, holder->JobIds_);
    return holder;
}

void THolder::AddJob(const TJobId& id)
{
    JobIds_.push_back(id);
}

void THolder::RemoveJob(const TJobId& id)
{
    auto it = std::find(JobIds_.begin(), JobIds_.end(), id);
    if (it != JobIds_.end()) {
        JobIds_.erase(it);
    }
}

void THolder::AddChunk(const TChunkId& chunkId, bool cached)
{
    if (cached) {
        YVERIFY(CachedChunkIds().insert(chunkId).second);
    } else {
        YVERIFY(StoredChunkIds().insert(chunkId).second);
    }
}

void THolder::RemoveChunk(const TChunkId& chunkId, bool cached)
{
    if (cached) {
        YVERIFY(CachedChunkIds().erase(chunkId) == 1);
    } else {
        YVERIFY(StoredChunkIds().erase(chunkId) == 1);
    }
}

bool THolder::HasChunk(const TChunkId& chunkId, bool cached) const
{
    if (cached) {
        return CachedChunkIds_.find(chunkId) != CachedChunkIds_.end();
    } else {
        return StoredChunkIds_.find(chunkId) != StoredChunkIds_.end();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT

