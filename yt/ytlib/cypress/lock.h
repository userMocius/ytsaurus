#pragma once

#include "id.h"

#include <yt/ytlib/misc/property.h>
#include <yt/ytlib/object_server/object.h>

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(ELockMode,
    (SharedRead)
    (SharedWrite)
    (ExclusiveWrite)
);

class TLock
    : public NObjectServer::TObjectWithIdBase
{
    DEFINE_BYVAL_RO_PROPERTY(TNodeId, NodeId);
    DEFINE_BYVAL_RO_PROPERTY(TTransactionId, TransactionId);
    DEFINE_BYVAL_RO_PROPERTY(ELockMode, Mode);

public:
    TLock(
        const TLockId& id,
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        ELockMode mode);

    TAutoPtr<TLock> Clone() const;

    void Save(TOutputStream* output) const;
    static TAutoPtr<TLock> Load(const TLockId& id, TInputStream* input);

private:
    TLock(const TLock& other);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

