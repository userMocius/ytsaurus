#pragma once

#include "operation_controller_detail.h"
#include "private.h"

#include "data_flow_graph.h"
#include "extended_job_resources.h"

#include <yt/yt/server/controller_agent/controller_agent.h>

#include <yt/yt/server/lib/chunk_pools/chunk_pool.h>

#include <yt/yt/server/lib/scheduler/job_metrics.h>
#include <yt/yt/server/lib/scheduler/exec_node_descriptor.h>

#include <yt/yt/server/lib/controller_agent/serialize.h>

#include <yt/yt/ytlib/job_tracker_client/public.h>

#include <yt/yt/core/misc/statistics.h>

namespace NYT::NControllerAgent::NControllers {

////////////////////////////////////////////////////////////////////////////////

//! A reduced version of TExecNodeDescriptor, which is associated with jobs.
struct TJobNodeDescriptor
{
    TJobNodeDescriptor() = default;
    TJobNodeDescriptor(const TJobNodeDescriptor& other) = default;
    TJobNodeDescriptor& operator=(const TJobNodeDescriptor& other) = default;
    TJobNodeDescriptor(const NScheduler::TExecNodeDescriptor& other);

    NNodeTrackerClient::TNodeId Id = NNodeTrackerClient::InvalidNodeId;
    TString Address;
    double IOWeight = 0.0;

    void Persist(const TPersistenceContext& context);
};

////////////////////////////////////////////////////////////////////////////////

struct TJoblet
    : public TRefCounted
{
    NJobTrackerClient::TJobId JobId;
    NJobTrackerClient::EJobType JobType;

    bool JobInterruptible;

    TJobNodeDescriptor NodeDescriptor;

    TInstant StartTime;
    TInstant FinishTime;
    TInstant LastUpdateTime = TInstant();
    TInstant LastStatisticsUpdateTime = TInstant();

    // XXX: refactor possibles job states, that identified by presence of StartTime and IsStarted flag.
    bool IsStarted = false;

    TString DebugArtifactsAccount;
    bool Suspicious = false;
    TInstant LastActivityTime;
    TBriefJobStatisticsPtr BriefStatistics;
    double Progress = 0.0;
    i64 StderrSize = 0;

    // Shared pointers from two following fields must be accessed (read, written) only from
    // controller invoker which is serialized. Pointees (i.e. statistics objects) may be accessed
    // in thread-safe read-only manner.

    //! Statistics obtained from job heartbeat. Always non-nullptr. This field may be heavy, so
    //! we must not copy or traverse them in the controller invoker for running job events.
    //! Traversing this field for finished job events or for job attribute retrieval
    //! events is fine as it happens occasionally.
    std::shared_ptr<const TStatistics> JobStatistics = std::make_shared<const TStatistics>();

    //! Statistics generated by controller. Always non-nullptr. This field is of constant size, so
    //! we are rebuilding them from scratch for running job events and for finished job events.
    std::shared_ptr<const TStatistics> ControllerStatistics = std::make_shared<const TStatistics>();

    EJobPhase Phase = EJobPhase::Missing;
    TEnumIndexedVector<EJobCompetitionType, TJobId> CompetitionIds;
    TEnumIndexedVector<EJobCompetitionType, bool> HasCompetitors;
    TString TaskName;

    // Controller encapsulates lifetime of both, tasks and joblets.
    TTask* Task;
    int JobIndex = -1;
    int TaskJobIndex = 0;
    i64 StartRowIndex = -1;
    bool Restarted = false;
    bool Revived = false;
    std::optional<EJobCompetitionType> CompetitionType;

    // It is necessary to store tree id here since it is required to
    // create job metrics updater after revive.
    TString TreeId;
    // Is the tree marked as tentative in the spec?
    bool TreeIsTentative = false;

    TFuture<TSharedRef> JobSpecProtoFuture;

    TExtendedJobResources EstimatedResourceUsage;
    std::optional<double> JobProxyMemoryReserveFactor;
    std::optional<double> UserJobMemoryReserveFactor;
    // TODO(ignat): use TJobResourcesWithQuota.
    TJobResources ResourceLimits;
    NScheduler::TDiskQuota DiskQuota;

    i64 UserJobMemoryReserve = 0;

    EPredecessorType PredecessorType = EPredecessorType::None;
    TJobId PredecessorJobId;

    std::optional<TString> DiskRequestAccount;

    NChunkPools::TChunkStripeListPtr InputStripeList;
    NChunkPools::IChunkPoolOutput::TCookie OutputCookie = -1;

    //! All chunk lists allocated for this job.
    /*!
     *  For jobs with intermediate output this list typically contains one element.
     *  For jobs with final output this list typically contains one element per each output table.
     */
    std::vector<NChunkClient::TChunkListId> ChunkListIds;

    NChunkClient::TChunkListId StderrTableChunkListId;
    NChunkClient::TChunkListId CoreTableChunkListId;

    NScheduler::TJobMetrics JobMetrics;
    bool HasLoggedJobMetricsMonotonicityViolation = false;

    std::optional<TDuration> JobSpeculationTimeout;

    std::vector<TOutputStreamDescriptorPtr> OutputStreamDescriptors;
    std::vector<TInputStreamDescriptorPtr> InputStreamDescriptors;

    // These fields are used only to build job spec and thus transient.
    std::optional<TString> UserJobMonitoringDescriptor;
    std::optional<TString> PoolPath;

    NScheduler::TJobProfilerSpecPtr EnabledJobProfiler;

    // Used only for persistence.
    TJoblet() = default;

    TJoblet(
        TTask* task,
        int jobIndex,
        int taskJobIndex,
        const TString& treeId,
        bool treeIsTentative);

    void Persist(const TPersistenceContext& context);

    NScheduler::TJobMetrics UpdateJobMetrics(
        const TJobSummary& jobSummary,
        bool isJobFinished);

    //! Put controller statistics over job statistics (preferring controller summaries for
    //! common paths) and return result.
    //! This method traverses both statistics fields, so do not call it often.
    TStatistics BuildCombinedStatistics() const;

    TJobStatisticsTags GetAggregationTags(EJobState state);

    bool ShouldLogFinishedEvent() const;
};

DEFINE_REFCOUNTED_TYPE(TJoblet)

////////////////////////////////////////////////////////////////////////////////

struct TCompletedJob
    : public TRefCounted
{
    bool Suspended = false;

    std::set<NChunkClient::TChunkId> UnavailableChunks;

    TJobId JobId;

    TTaskPtr SourceTask;
    NChunkPools::IChunkPoolOutput::TCookie OutputCookie;
    i64 DataWeight;

    NChunkPools::IPersistentChunkPoolInputPtr DestinationPool;
    NChunkPools::IChunkPoolInput::TCookie InputCookie;
    NChunkPools::TChunkStripePtr InputStripe;
    bool Restartable;

    TJobNodeDescriptor NodeDescriptor;

    void Persist(const TPersistenceContext& context);
};

DEFINE_REFCOUNTED_TYPE(TCompletedJob)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
