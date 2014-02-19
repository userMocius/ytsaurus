#pragma once

#include "public.h"

#include <server/cell_node/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TStoreFlusher
    : public TRefCounted
{
public:
    TStoreFlusher(
        TStoreFlusherConfigPtr config,
        NCellNode::TBootstrap* bootstrap);
    ~TStoreFlusher();

    void Start();

private:
    class TImpl;
    TIntrusivePtr<TImpl> Impl_;

};

DEFINE_REFCOUNTED_TYPE(TStoreFlusher)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
