#pragma once

#include "public.h"

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

struct TRowGroupHeader
{
    TTransaction* Transaction;
};

////////////////////////////////////////////////////////////////////////////////

class TMemoryTable
    : public TRefCounted
{
public:
    TMemoryTable();

private:

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
