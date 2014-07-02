#include "stdafx.h"
#include "transaction.h"

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/serialize.h>

#include <server/security_server/account.h>

namespace NYT {
namespace NTransactionServer {

using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(const TTransactionId& id)
    : TNonversionedObjectBase(id)
    , UncommittedAccountingEnabled_(true)
    , StagedAccountingEnabled_(true)
    , Parent_(nullptr)
    , StartTime_(TInstant::Zero())
    , Acd_(this)
{ }

void TTransaction::Save(NCellMaster::TSaveContext& context) const
{
    TNonversionedObjectBase::Save(context);

    using NYT::Save;
    Save(context, GetPersistentState());
    Save(context, Timeout_);
    Save(context, UncommittedAccountingEnabled_);
    Save(context, StagedAccountingEnabled_);
    Save(context, NestedTransactions_);
    Save(context, Parent_);
    Save(context, StartTime_);
    Save(context, StagedObjects_);
    Save(context, LockedNodes_);
    Save(context, Locks_);
    Save(context, BranchedNodes_);
    Save(context, StagedNodes_);
    Save(context, AccountResourceUsage_);
    Save(context, Acd_);
}

void TTransaction::Load(NCellMaster::TLoadContext& context)
{
    TNonversionedObjectBase::Load(context);

    using NYT::Load;
    Load(context, State_);
    Load(context, Timeout_);
    Load(context, UncommittedAccountingEnabled_);
    Load(context, StagedAccountingEnabled_);
    Load(context, NestedTransactions_);
    Load(context, Parent_);
    Load(context, StartTime_);
    Load(context, StagedObjects_);
    Load(context, LockedNodes_);
    // COMPAT(babenko)
    if (context.GetVersion() >= 24) {
        Load(context, Locks_);
    }
    Load(context, BranchedNodes_);
    Load(context, StagedNodes_);
    Load(context, AccountResourceUsage_);
    Load(context, Acd_);
}

ETransactionState TTransaction::GetPersistentState() const
{
    switch (State_) {
        case ETransactionState::TransientlyPrepared:
        case ETransactionState::Aborting:
            return ETransactionState::Active;
        default:
            return State_;
    }
}

void TTransaction::ValidateActive() const
{
    if (State_ != ETransactionState::Active) {
        THROW_ERROR_EXCEPTION("Transaction %s is not active",
            ~ToString(Id));
    }    
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT

