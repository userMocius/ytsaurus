#pragma once

#include <yt/core/misc/public.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class ECategory>
class TMemoryUsageTracker;

template <class ECategory>
class TMemoryUsageTrackerGuard;

class TServerConfig;
typedef TIntrusivePtr<TServerConfig> TServerConfigPtr;

DECLARE_REFCOUNTED_CLASS(TDiskHealthChecker)
DECLARE_REFCOUNTED_CLASS(TDiskHealthCheckerConfig)

DECLARE_REFCOUNTED_CLASS(TDiskLocationConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
