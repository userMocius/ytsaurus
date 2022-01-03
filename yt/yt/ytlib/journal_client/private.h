#pragma once

#include "public.h"

#include <yt/yt/core/logging/log.h>

namespace NYT::NJournalClient {

////////////////////////////////////////////////////////////////////////////////

inline const NLogging::TLogger JournalClientLogger("JournalClient");

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TErasurePartsReader)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJournalClient
