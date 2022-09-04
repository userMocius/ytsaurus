#include "disk_location.h"

#include "config.h"

#include <yt/yt/server/node/cluster_node/bootstrap.h>

#include <yt/yt/server/lib/misc/private.h>

#include <yt/yt/core/yson/string.h>

#include <yt/yt/core/misc/fs.h>

namespace NYT::NDataNode {

using namespace NClusterNode;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

TDiskLocation::TDiskLocation(
    TDiskLocationConfigPtr config,
    TString id,
    const NLogging::TLogger& logger)
    : Id_(std::move(id))
    , Logger(logger.WithTag("LocationId: %v", id))
    , StaticConfig_(std::move(config))
    , RuntimeConfig_(StaticConfig_)
{ }

const TString& TDiskLocation::GetId() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return Id_;
}

TDiskLocationConfigPtr TDiskLocation::GetRuntimeConfig() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return RuntimeConfig_.Acquire();
}

void TDiskLocation::Reconfigure(TDiskLocationConfigPtr config)
{
    VERIFY_THREAD_AFFINITY_ANY();

    RuntimeConfig_.Store(std::move(config));
}

bool TDiskLocation::IsEnabled() const
{
    VERIFY_THREAD_AFFINITY_ANY();

    return Enabled_.load();
}

void TDiskLocation::ValidateLockFile() const
{
    YT_LOG_INFO("Checking lock file");

    auto lockFilePath = NFS::CombinePaths(StaticConfig_->Path, DisabledLockFileName);
    if (!NFS::Exists(lockFilePath)) {
        return;
    }

    TFile file(lockFilePath, OpenExisting | RdOnly | Seq | CloseOnExec);
    TFileInput fileInput(file);

    auto errorData = fileInput.ReadAll();
    if (errorData.empty()) {
        THROW_ERROR_EXCEPTION("Empty lock file found");
    }

    TError error;
    try {
        error = ConvertTo<TError>(TYsonString(errorData));
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error parsing lock file contents")
            << ex;
    }
    THROW_ERROR error;
}

void TDiskLocation::ValidateMinimumSpace() const
{
    YT_LOG_INFO("Checking minimum space");

    auto config = GetRuntimeConfig();
    if (config->MinDiskSpace) {
        auto minSpace = *config->MinDiskSpace;
        auto totalSpace = GetTotalSpace();
        if (totalSpace < minSpace) {
            THROW_ERROR_EXCEPTION("Minimum disk space requirement is not met for location %Qv",
                Id_)
                << TErrorAttribute("actual_space", totalSpace)
                << TErrorAttribute("required_space", minSpace);
        }
    }
}

i64 TDiskLocation::GetTotalSpace() const
{
    auto statistics = NFS::GetDiskSpaceStatistics(StaticConfig_->Path);
    return statistics.TotalSpace;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
