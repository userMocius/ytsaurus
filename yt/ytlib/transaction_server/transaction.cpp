#include "stdafx.h"
#include "transaction.h"

#include <util/ysaveload.h>

namespace NYT {
namespace NTransactionServer {

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(const TTransactionId& id)
    : TObjectWithIdBase(id)
{ }

TAutoPtr<TTransaction> TTransaction::Clone() const
{
    return new TTransaction(*this);
}

void TTransaction::Save(TOutputStream* output) const
{
    YASSERT(output);

    ::Save(output, State_);
    ::Save(output, UnboundChunkTreeIds_);
    ::Save(output, LockIds_);
    ::Save(output, BranchedNodeIds_);
    ::Save(output, CreatedNodeIds_);
}

TAutoPtr<TTransaction> TTransaction::Load(const TTransactionId& id, TInputStream* input)
{
    YASSERT(input);

    auto* transaction = new TTransaction(id);
    ::Load(input, transaction->State_);
    ::Load(input, transaction->UnboundChunkTreeIds_);
    ::Load(input, transaction->LockIds_);
    ::Load(input, transaction->BranchedNodeIds_);
    ::Load(input, transaction->CreatedNodeIds_);
    return transaction;
}

TTransaction::TTransaction(const TTransaction& other)
    : TObjectWithIdBase(other)
    , State_(other.State_)
    , UnboundChunkTreeIds_(other.UnboundChunkTreeIds_)
    , LockIds_(other.LockIds_)
    , BranchedNodeIds_(other.BranchedNodeIds_)
    , CreatedNodeIds_(other.CreatedNodeIds_)
{ }

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT

