#pragma once

#include "lsm_backend.h"

namespace NYT::NLsm {

////////////////////////////////////////////////////////////////////////////////

ILsmBackendPtr CreateStoreRotator();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLsm
