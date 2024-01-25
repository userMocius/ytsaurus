#pragma once

#include "public.h"

namespace NYT::NLsm {

////////////////////////////////////////////////////////////////////////////////

struct TTabletLsmStatistics
{
    TEnumIndexedArray<EStoreCompactionReason, int> PendingCompactionStoreCount;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLsm

