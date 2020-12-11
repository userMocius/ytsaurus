#pragma once

#include "private.h"

namespace NYT::NChunkPools {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EJobState,
    (Pending)
    (Running)
    (Completed)
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkPools
