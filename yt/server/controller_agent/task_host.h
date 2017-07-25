#pragma once

#include "private.h"
#include "serialize.h"
#include "table.h"

#include <yt/ytlib/ypath/rich.h>

#include <yt/ytlib/object_client/public.h>

#include <yt/server/chunk_pools/public.h>

#include <yt/server/scheduler/public.h>

#include <yt/ytlib/node_tracker_client/public.h>

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

//! Interface defining the interaction between task and controller.
struct ITaskHost
    : public virtual TRefCounted
    , public IPersistent
    , public NPhoenix::TFactoryTag<NPhoenix::TNullFactory>
{
    virtual NLogging::TLogger GetLogger() const = 0;

    virtual IInvokerPtr GetCancelableInvoker() const = 0;

    //! Called to extract stderr table path from the spec.
    virtual TNullable<NYPath::TRichYPath> GetStderrTablePath() const = 0;
    //! Called to extract core table path from the spec.
    virtual TNullable<NYPath::TRichYPath> GetCoreTablePath() const = 0;

    virtual void RegisterInputStripe(const NChunkPools::TChunkStripePtr& stripe, const TTaskPtr& task) = 0;
    virtual void AddTaskLocalityHint(const NChunkPools::TChunkStripePtr& stripe, const TTaskPtr& task) = 0;
    virtual void AddTaskLocalityHint(NNodeTrackerClient::TNodeId nodeId, const TTaskPtr& task) = 0;
    virtual void AddTaskPendingHint(const TTaskPtr& task) = 0;

    virtual ui64 NextJobIndex() = 0;
    virtual std::unique_ptr<TJobMetricsUpdater> CreateJobMetricsUpdater() const = 0;

    virtual void CustomizeJobSpec(const TJobletPtr& joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec) = 0;
    virtual void CustomizeJoblet(const TJobletPtr& joblet) = 0;

    virtual void AddValueToEstimatedHistogram(const TJobletPtr& joblet) = 0;
    virtual void RemoveValueFromEstimatedHistogram(const TJobletPtr& joblet) = 0;

    virtual const TSchedulerConfigPtr& SchedulerConfig() const = 0;
    virtual const TOperationSpecBasePtr& Spec() const = 0;

    virtual void OnOperationFailed(const TError& error, bool flush = true) = 0;

    //! If |true| then all jobs started within the operation must
    //! preserve row count. This invariant is checked for each completed job.
    //! Should a violation be discovered, the operation fails.
    virtual bool IsRowCountPreserved() const = 0;
    virtual bool IsJobInterruptible() const = 0;
    virtual bool ShouldSkipSanityCheck() = 0;

    virtual const IDigest* GetJobProxyMemoryDigest(EJobType jobType) const = 0;
    virtual const IDigest* GetUserJobMemoryDigest(EJobType jobType) const = 0;

    virtual NObjectClient::TCellTag GetIntermediateOutputCellTag() const = 0;

    virtual const TChunkListPoolPtr& ChunkListPool() const = 0;
    virtual NChunkClient::TChunkListId ExtractChunkList(NObjectClient::TCellTag cellTag) = 0;
    virtual void ReleaseChunkLists(const std::vector<NChunkClient::TChunkListId>& chunkListIds) = 0;

    virtual TOperationId GetOperationId() const = 0;
    virtual EOperationType GetOperationType() const = 0;

    virtual const std::vector<TOutputTable>& OutputTables() const = 0;
    virtual const TNullable<TOutputTable>& StderrTable() const = 0;
    virtual const TNullable<TOutputTable>& CoreTable() const = 0;

    virtual void RegisterStderr(const TJobletPtr& joblet, const NScheduler::TJobSummary& summary) = 0;
    virtual void RegisterCores(const TJobletPtr& joblet, const NScheduler::TJobSummary& summary) = 0;

    virtual void RegisterJoblet(const TJobletPtr& joblet) = 0;

    virtual IJobSplitter* JobSplitter() = 0;

    virtual const TNullable<TJobResources>& CachedMaxAvailableExecNodeResources() const = 0;

    virtual const NNodeTrackerClient::TNodeDirectoryPtr& InputNodeDirectory() const = 0;

    virtual void RegisterIntermediate(
        const TJobletPtr& joblet,
        const TCompletedJobPtr& completedJob,
        const NChunkPools::TChunkStripePtr& stripe,
        bool attachToLivePreview) = 0;

    virtual void RegisterOutput(
        const TJobletPtr& joblet,
        const TOutputChunkTreeKey& key,
        const NScheduler::TCompletedJobSummary& jobSummary) = 0;

    virtual void Persist(const TPersistenceContext& context) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITaskHost);

////////////////////////////////////////////////////////////////////////////////

}
}