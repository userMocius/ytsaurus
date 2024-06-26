#pragma once

#include "private.h"

#include <yt/yt/server/lib/controller_agent/structs.h>

namespace NYT::NChunkPools {

////////////////////////////////////////////////////////////////////////////////

// TODO(max42): make customizable?
constexpr double JobSizeBoostFactor = 2.0;

////////////////////////////////////////////////////////////////////////////////

struct IJobSizeAdjuster
    : public virtual IPersistent
{
    virtual void UpdateStatistics(const NControllerAgent::TCompletedJobSummary& jobSummary) = 0;
    virtual void UpdateStatistics(i64 jobDataWeight, TDuration prepareDuration, TDuration execDuration) = 0;
    virtual i64 GetDataWeightPerJob() const = 0;
};

////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<IJobSizeAdjuster> CreateJobSizeAdjuster(
    i64 dataWeightPerJob,
    const TJobSizeAdjusterConfigPtr& config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
