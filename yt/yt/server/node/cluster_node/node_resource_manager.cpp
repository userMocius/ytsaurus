#include "node_resource_manager.h"
#include "private.h"
#include "bootstrap.h"
#include "config.h"

#include <yt/yt/server/node/tablet_node/bootstrap.h>
#include <yt/yt/server/node/tablet_node/slot_manager.h>

#include <yt/yt/server/node/cluster_node/dynamic_config_manager.h>

#include <yt/yt/server/lib/containers/instance_limits_tracker.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

#include <yt/yt/ytlib/misc/memory_usage_tracker.h>

#include <yt/yt/core/concurrency/periodic_executor.h>

#include <limits>

namespace NYT::NClusterNode {

using namespace NConcurrency;
using namespace NNodeTrackerClient;
using namespace NNodeTrackerClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = ClusterNodeLogger;

////////////////////////////////////////////////////////////////////////////////

//! These categories have limits that are defined externally in relation to the resource manager and its config.
//! Resource manager simply skips them while updating limits.
static const THashSet<EMemoryCategory> ExternalMemoryCategories = {
    EMemoryCategory::BlockCache,
    EMemoryCategory::ChunkMeta,
    EMemoryCategory::ChunkBlockMeta,
    EMemoryCategory::VersionedChunkMeta
};

////////////////////////////////////////////////////////////////////////////////

TNodeResourceManager::TNodeResourceManager(IBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
    , UpdateExecutor_(New<TPeriodicExecutor>(
        Bootstrap_->GetControlInvoker(),
        BIND(&TNodeResourceManager::UpdateLimits, MakeWeak(this)),
        Bootstrap_->GetConfig()->ResourceLimitsUpdatePeriod))
    , TotalCpu_(Bootstrap_->GetConfig()->ResourceLimits->TotalCpu)
    , TotalMemory_(Bootstrap_->GetConfig()->ResourceLimits->TotalMemory)
{ }

void TNodeResourceManager::Start()
{
    UpdateExecutor_->Start();
}

void TNodeResourceManager::OnInstanceLimitsUpdated(double cpuLimit, i64 memoryLimit)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    YT_LOG_INFO("Instance limits updated (OldCpuLimit: %v, NewCpuLimit: %v, OldMemoryLimit: %v, NewMemoryLimit: %v)",
        TotalCpu_,
        cpuLimit,
        TotalMemory_,
        memoryLimit);

    TotalCpu_ = cpuLimit;
    TotalMemory_ = memoryLimit;
}

double TNodeResourceManager::GetJobsCpuLimit() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return JobsCpuLimit_;
}

void TNodeResourceManager::SetResourceLimitsOverride(const TNodeResourceLimitsOverrides& resourceLimitsOverride)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    ResourceLimitsOverride_ = resourceLimitsOverride;
}

void TNodeResourceManager::UpdateLimits()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    YT_LOG_DEBUG("Updating node resource limits");

    UpdateMemoryFootprint();
    UpdateMemoryLimits();
    UpdateJobsCpuLimit();
}

void TNodeResourceManager::UpdateMemoryLimits()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    const auto& config = Bootstrap_->GetConfig()->ResourceLimits;
    auto dynamicConfig = Bootstrap_->GetDynamicConfigManager()->GetConfig()->ResourceLimits;

    auto getMemoryLimit = [&] (EMemoryCategory category) {
        auto memoryLimit = dynamicConfig->MemoryLimits[category];
        if (!memoryLimit) {
            memoryLimit = config->MemoryLimits[category];
        }

        return memoryLimit;
    };

    i64 freeMemoryWatermark = dynamicConfig->FreeMemoryWatermark.value_or(*config->FreeMemoryWatermark);
    i64 totalDynamicMemory = TotalMemory_ - freeMemoryWatermark;

    const auto& memoryUsageTracker = Bootstrap_->GetMemoryUsageTracker();

    memoryUsageTracker->SetTotalLimit(TotalMemory_);

    int dynamicCategoryCount = 0;
    TEnumIndexedVector<EMemoryCategory, i64> newLimits;
    for (auto category : TEnumTraits<EMemoryCategory>::GetDomainValues()) {
        auto memoryLimit = getMemoryLimit(category);

        if (!memoryLimit || !memoryLimit->Type || memoryLimit->Type == EMemoryLimitType::None) {
            newLimits[category] = std::numeric_limits<i64>::max();
            totalDynamicMemory -= memoryUsageTracker->GetUsed(category);
        } else if (memoryLimit->Type == EMemoryLimitType::Static) {
            newLimits[category] = *memoryLimit->Value;
            totalDynamicMemory -= *memoryLimit->Value;
        } else {
            ++dynamicCategoryCount;
        }
    }

    if (dynamicCategoryCount > 0) {
        auto dynamicMemoryPerCategory = std::max<i64>(totalDynamicMemory / dynamicCategoryCount, 0);
        for (auto category : TEnumTraits<EMemoryCategory>::GetDomainValues()) {
            auto memoryLimit = getMemoryLimit(category);
            if (memoryLimit && memoryLimit->Type == EMemoryLimitType::Dynamic) {
                newLimits[category] = dynamicMemoryPerCategory;
            }
        }
    }

    if (ResourceLimitsOverride_.has_system_memory()) {
        newLimits[EMemoryCategory::SystemJobs] = ResourceLimitsOverride_.system_memory();
    }
    if (ResourceLimitsOverride_.has_user_memory()) {
        newLimits[EMemoryCategory::UserJobs] = ResourceLimitsOverride_.user_memory();
    }

    for (auto category : TEnumTraits<EMemoryCategory>::GetDomainValues()) {
        if (ExternalMemoryCategories.contains(category)) {
            continue;
        }

        auto oldLimit = memoryUsageTracker->GetExplicitLimit(category);
        auto newLimit = newLimits[category];

        if (std::abs(oldLimit - newLimit) > config->MemoryAccountingTolerance) {
            YT_LOG_INFO("Updating memory category limit (Category: %v, OldLimit: %v, NewLimit: %v)",
                category,
                oldLimit,
                newLimit);
            memoryUsageTracker->SetCategoryLimit(category, newLimit);
        }
    }

    auto externalMemory = std::max(
        memoryUsageTracker->GetLimit(EMemoryCategory::UserJobs),
        memoryUsageTracker->GetUsed(EMemoryCategory::UserJobs));
    auto selfMemoryGuarantee = std::max(
        static_cast<i64>(0),
        TotalMemory_ - externalMemory - config->MemoryAccountingGap);
    if (std::abs(selfMemoryGuarantee - SelfMemoryGuarantee_) > config->MemoryAccountingTolerance) {
        SelfMemoryGuarantee_ = selfMemoryGuarantee;
        SelfMemoryGuaranteeUpdated_.Fire(SelfMemoryGuarantee_);
    }
}

void TNodeResourceManager::UpdateMemoryFootprint()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    const auto& memoryUsageTracker = Bootstrap_->GetMemoryUsageTracker();

    auto allocCounters = NYTAlloc::GetTotalAllocationCounters();
    auto bytesUsed = allocCounters[NYTAlloc::ETotalCounter::BytesUsed];
    auto bytesCommitted = allocCounters[NYTAlloc::ETotalCounter::BytesCommitted];

    auto newFragmentation = std::max<i64>(0, bytesCommitted - bytesUsed);

    auto newFootprint = bytesUsed;
    for (auto memoryCategory : TEnumTraits<EMemoryCategory>::GetDomainValues()) {
        if (memoryCategory == EMemoryCategory::UserJobs ||
            memoryCategory == EMemoryCategory::Footprint ||
            memoryCategory == EMemoryCategory::AllocFragmentation ||
            memoryCategory == EMemoryCategory::TmpfsLayers ||
            memoryCategory == EMemoryCategory::SystemJobs) {
            continue;
        }

        newFootprint -= memoryUsageTracker->GetUsed(memoryCategory);
    }
    newFootprint = std::max<i64>(newFootprint, 0);

    auto oldFootprint = memoryUsageTracker->UpdateUsage(EMemoryCategory::Footprint, newFootprint);
    auto oldFragmentation = memoryUsageTracker->UpdateUsage(EMemoryCategory::AllocFragmentation, newFragmentation);

    YT_LOG_INFO("Memory footprint updated (BytesCommitted: %v, BytesUsed: %v, Footprint: %v -> %v, Fragmentation: %v -> %v)",
        bytesCommitted,
        bytesUsed,
        oldFootprint,
        newFootprint,
        oldFragmentation,
        newFragmentation);
}

void TNodeResourceManager::UpdateJobsCpuLimit()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    auto config = Bootstrap_->GetConfig()->ResourceLimits;
    auto dynamicConfig = Bootstrap_->GetDynamicConfigManager()->GetConfig()->ResourceLimits;

    double newJobsCpuLimit = 0;

    // COPMAT(gritukan)
    if (ResourceLimitsOverride_.has_cpu()) {
        newJobsCpuLimit = ResourceLimitsOverride_.cpu();
    } else {
        if (TotalCpu_) {
            double nodeDedicatedCpu = dynamicConfig->NodeDedicatedCpu.value_or(*config->NodeDedicatedCpu);
            newJobsCpuLimit = *TotalCpu_ - nodeDedicatedCpu;
        } else {
            newJobsCpuLimit = Bootstrap_->GetConfig()->ExecNode->JobController->ResourceLimits->Cpu;
        }

        if (Bootstrap_->IsTabletNode()) {
            const auto& tabletSlotManager = Bootstrap_->GetTabletNodeBootstrap()->GetSlotManager();
            if (tabletSlotManager) {
                double cpuPerTabletSlot = dynamicConfig->CpuPerTabletSlot.value_or(*config->CpuPerTabletSlot);
                newJobsCpuLimit -= tabletSlotManager->GetUsedCpu(cpuPerTabletSlot);
            }
        }
    }
    newJobsCpuLimit = std::max<double>(newJobsCpuLimit, 0);

    JobsCpuLimit_.store(newJobsCpuLimit);
    JobsCpuLimitUpdated_.Fire();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClusterNode
