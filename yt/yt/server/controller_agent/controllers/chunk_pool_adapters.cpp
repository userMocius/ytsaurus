#include "chunk_pool_adapters.h"

#include "task.h"

#include <yt/ytlib/chunk_client/legacy_data_slice.h>
#include <yt/ytlib/chunk_client/input_chunk.h>

namespace NYT::NControllerAgent::NControllers {

using namespace NChunkPools;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

TChunkPoolInputAdapterBase::TChunkPoolInputAdapterBase(IChunkPoolInputPtr underlyingInput)
    : UnderlyingInput_(std::move(underlyingInput))
{ }

IChunkPoolInput::TCookie TChunkPoolInputAdapterBase::AddWithKey(TChunkStripePtr stripe, TChunkStripeKey key)
{
    return UnderlyingInput_->AddWithKey(std::move(stripe), key);
}

IChunkPoolInput::TCookie TChunkPoolInputAdapterBase::Add(TChunkStripePtr stripe)
{
    return UnderlyingInput_->Add(std::move(stripe));
}

void TChunkPoolInputAdapterBase::Suspend(IChunkPoolInput::TCookie cookie)
{
    return UnderlyingInput_->Suspend(cookie);
}

void TChunkPoolInputAdapterBase::Resume(IChunkPoolInput::TCookie cookie)
{
    return UnderlyingInput_->Resume(cookie);
}

void TChunkPoolInputAdapterBase::Reset(IChunkPoolInput::TCookie cookie, TChunkStripePtr stripe, TInputChunkMappingPtr mapping)
{
    return UnderlyingInput_->Reset(cookie, std::move(stripe), std::move(mapping));
}

void TChunkPoolInputAdapterBase::Finish()
{
    return UnderlyingInput_->Finish();
}

bool TChunkPoolInputAdapterBase::IsFinished() const
{
    return UnderlyingInput_->IsFinished();
}

void TChunkPoolInputAdapterBase::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, UnderlyingInput_);
}

////////////////////////////////////////////////////////////////////////////////

TChunkPoolOutputAdapterBase::TChunkPoolOutputAdapterBase(IChunkPoolOutputPtr underlyingOutput)
    : UnderlyingOutput_(std::move(underlyingOutput))
{ }

const TProgressCounterPtr& TChunkPoolOutputAdapterBase::GetJobCounter() const
{
    return UnderlyingOutput_->GetJobCounter();
}

const TProgressCounterPtr& TChunkPoolOutputAdapterBase::GetDataWeightCounter() const
{
    return UnderlyingOutput_->GetDataWeightCounter();
}

const TProgressCounterPtr& TChunkPoolOutputAdapterBase::GetRowCounter() const
{
    return UnderlyingOutput_->GetRowCounter();
}

const TProgressCounterPtr& TChunkPoolOutputAdapterBase::GetDataSliceCounter() const
{
    return UnderlyingOutput_->GetDataSliceCounter();
}

TOutputOrderPtr TChunkPoolOutputAdapterBase::GetOutputOrder() const
{
    return UnderlyingOutput_->GetOutputOrder();
}

i64 TChunkPoolOutputAdapterBase::GetLocality(TNodeId nodeId) const
{
    return UnderlyingOutput_->GetLocality(nodeId);
}

TChunkStripeStatisticsVector TChunkPoolOutputAdapterBase::GetApproximateStripeStatistics() const
{
    return UnderlyingOutput_->GetApproximateStripeStatistics();
}

IChunkPoolOutput::TCookie TChunkPoolOutputAdapterBase::Extract(TNodeId nodeId)
{
    return UnderlyingOutput_->Extract(nodeId);
}

TChunkStripeListPtr TChunkPoolOutputAdapterBase::GetStripeList(TCookie cookie)
{
    return UnderlyingOutput_->GetStripeList(cookie);
}

bool TChunkPoolOutputAdapterBase::IsCompleted() const
{
    return UnderlyingOutput_->IsCompleted();
}

int TChunkPoolOutputAdapterBase::GetStripeListSliceCount(TCookie cookie) const
{
    return UnderlyingOutput_->GetStripeListSliceCount(cookie);
}

void TChunkPoolOutputAdapterBase::Completed(TCookie cookie, const TCompletedJobSummary& jobSummary)
{
    UnderlyingOutput_->Completed(cookie, jobSummary);
}

void TChunkPoolOutputAdapterBase::Failed(TCookie cookie)
{
    UnderlyingOutput_->Failed(cookie);
}

void TChunkPoolOutputAdapterBase::Aborted(TCookie cookie, EAbortReason reason)
{
    UnderlyingOutput_->Aborted(cookie, reason);
}

void TChunkPoolOutputAdapterBase::Lost(TCookie cookie)
{
    UnderlyingOutput_->Lost(cookie);
}

void TChunkPoolOutputAdapterBase::Persist(const TPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, UnderlyingOutput_);
}

DELEGATE_SIGNAL(TChunkPoolOutputAdapterBase, void(NChunkClient::TInputChunkPtr, std::any tag), ChunkTeleported, *UnderlyingOutput_);
DELEGATE_SIGNAL(TChunkPoolOutputAdapterBase, void(), Completed, *UnderlyingOutput_);
DELEGATE_SIGNAL(TChunkPoolOutputAdapterBase, void(), Uncompleted, *UnderlyingOutput_);

////////////////////////////////////////////////////////////////////////////////

class TIntermediateLivePreviewAdapter
    : public TChunkPoolInputAdapterBase
{
public:
    TIntermediateLivePreviewAdapter() = default;

    TIntermediateLivePreviewAdapter(IChunkPoolInputPtr chunkPoolInput, ITaskHost* taskHost)
        : TChunkPoolInputAdapterBase(std::move(chunkPoolInput))
        , TaskHost_(taskHost)
    { }

    virtual TCookie AddWithKey(TChunkStripePtr stripe, TChunkStripeKey key) override
    {
        YT_VERIFY(!stripe->DataSlices.empty());
        for (const auto& dataSlice : stripe->DataSlices) {
            auto chunk = dataSlice->GetSingleUnversionedChunkOrThrow();
            TaskHost_->AttachToIntermediateLivePreview(chunk->GetChunkId());
        }
        return TChunkPoolInputAdapterBase::AddWithKey(std::move(stripe), key);
    }

    virtual TCookie Add(TChunkStripePtr stripe) override
    {
        return AddWithKey(stripe, TChunkStripeKey());
    }

    void Persist(const TPersistenceContext& context)
    {
        TChunkPoolInputAdapterBase::Persist(context);

        using NYT::Persist;

        Persist(context, TaskHost_);
    }

private:
    DECLARE_DYNAMIC_PHOENIX_TYPE(TIntermediateLivePreviewAdapter, 0x1241741a);

    ITaskHost* TaskHost_ = nullptr;
};

DEFINE_DYNAMIC_PHOENIX_TYPE(TIntermediateLivePreviewAdapter);

IChunkPoolInputPtr CreateIntermediateLivePreviewAdapter(
    IChunkPoolInputPtr chunkPoolInput,
    ITaskHost* taskHost)
{
    return New<TIntermediateLivePreviewAdapter>(std::move(chunkPoolInput), taskHost);
}

////////////////////////////////////////////////////////////////////////////////

class TTaskUpdatingAdapter
    : public TChunkPoolInputAdapterBase
{
public:
    TTaskUpdatingAdapter() = default;

    TTaskUpdatingAdapter(IChunkPoolInputPtr chunkPoolInput, TTask* task)
        : TChunkPoolInputAdapterBase(std::move(chunkPoolInput))
        , Task_(task)
    { }

    virtual TCookie AddWithKey(TChunkStripePtr stripe, TChunkStripeKey key) override
    {
        Task_->GetTaskHost()->UpdateTask(Task_);

        return TChunkPoolInputAdapterBase::AddWithKey(std::move(stripe), key);
    }

    virtual TCookie Add(TChunkStripePtr stripe) override
    {
        return AddWithKey(stripe, TChunkStripeKey());
    }

    void Persist(const TPersistenceContext& context)
    {
        TChunkPoolInputAdapterBase::Persist(context);

        using NYT::Persist;

        Persist(context, Task_);
    }

private:
    DECLARE_DYNAMIC_PHOENIX_TYPE(TTaskUpdatingAdapter, 0x1fe32cba);

    TTask* Task_ = nullptr;
};

DEFINE_DYNAMIC_PHOENIX_TYPE(TTaskUpdatingAdapter);

IChunkPoolInputPtr CreateTaskUpdatingAdapter(
    IChunkPoolInputPtr chunkPoolInput,
    TTask* task)
{
    return New<TTaskUpdatingAdapter>(std::move(chunkPoolInput), task);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
