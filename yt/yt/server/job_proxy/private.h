#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

inline const NLogging::TLogger JobProxyLogger("JobProxy");

constexpr auto RpcServerShutdownTimeout = TDuration::Seconds(15);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy

