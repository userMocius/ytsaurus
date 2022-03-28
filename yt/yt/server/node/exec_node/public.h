#pragma once

#include <yt/yt/server/lib/exec_node/public.h>

#include <yt/yt/server/lib/job_agent/public.h>

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/misc/optional.h>

namespace NYT::NExecNode {

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

class TJobProxyResources;

} // namespace NProto

struct TControllerAgentDescriptor;

////////////////////////////////////////////////////////////////////////////////

struct IBootstrap;

struct TTmpfsVolume
{
    TString Path;
    i64 Size;
};

struct TUserSandboxOptions
{
    std::vector<TTmpfsVolume> TmpfsVolumes;
    std::optional<i64> InodeLimit;
    std::optional<i64> DiskSpaceLimit;
    TCallback<void(const TError&)> DiskOverdraftCallback;
};

extern const TString ProxyConfigFileName;

DECLARE_REFCOUNTED_CLASS(TChunkCache)
DECLARE_REFCOUNTED_STRUCT(IVolumeArtifact)
DECLARE_REFCOUNTED_CLASS(IVolumeChunkCache)

DECLARE_REFCOUNTED_CLASS(TGpuManager)

DECLARE_REFCOUNTED_STRUCT(IVolume)
DECLARE_REFCOUNTED_STRUCT(IVolumeManager)
DECLARE_REFCOUNTED_STRUCT(IPlainVolumeManager)

DECLARE_REFCOUNTED_STRUCT(IMasterConnector)

DECLARE_REFCOUNTED_CLASS(TSlotManager)
DECLARE_REFCOUNTED_CLASS(TSlotLocation)
DECLARE_REFCOUNTED_STRUCT(IJobDirectoryManager)

DECLARE_REFCOUNTED_STRUCT(ISlot)

DECLARE_REFCOUNTED_CLASS(TControllerAgentConnectorPool)
DECLARE_REFCOUNTED_CLASS(TSchedulerConnector)

DECLARE_REFCOUNTED_STRUCT(IJobEnvironment)

DEFINE_ENUM(EExecNodeThrottlerKind,
    //! Controls incoming bandwidth used by Artifact Cache downloads.
    (ArtifactCacheIn)
    //! Controls incoming bandwidth consumed by local jobs.
    (JobIn)
    //! Controls outcoming bandwidth consumed by local jobs.
    (JobOut)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NExecNode
