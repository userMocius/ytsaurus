#include "stdafx.h"
#include "transaction_service.h"

namespace NYT {
namespace NTransaction {

using namespace NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = TransactionLogger;

////////////////////////////////////////////////////////////////////////////////

TTransactionService::TTransactionService(
    TTransactionManager::TPtr transactionManager,
    IInvoker::TPtr serviceInvoker,
    NRpc::TServer::TPtr server)
    : TMetaStateServiceBase(
        serviceInvoker,
        TTransactionServiceProxy::GetServiceName(),
        TransactionLogger.GetCategory())
    , TransactionManager(transactionManager)
{
    YASSERT(~transactionManager != NULL);
    YASSERT(~server != NULL);

    RegisterMethods();

    server->RegisterService(this);
}

void TTransactionService::RegisterMethods()
{
    RegisterMethod(RPC_SERVICE_METHOD_DESC(StartTransaction));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(CommitTransaction));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(AbortTransaction));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(RenewTransactionLease));
}

void TTransactionService::ValidateTransactionId(const TTransactionId& id)
{
    if (TransactionManager->FindTransaction(id) == NULL) {
        ythrow TServiceException(EErrorCode::NoSuchTransaction) <<
            Sprintf("Unknown or expired transaction id (TransactionId: %s)",
                ~id.ToString());
    }
}

////////////////////////////////////////////////////////////////////////////////

RPC_SERVICE_METHOD_IMPL(TTransactionService, StartTransaction)
{
    UNUSED(request);

    context->SetRequestInfo("");

    TransactionManager
        ->InitiateStartTransaction()
        ->OnSuccess(FromFunctor([=] (TTransactionId id)
            {
                response->SetTransactionId(id.ToProto());

                context->SetResponseInfo("TransactionId: %s",
                    ~id.ToString());

                context->Reply();
            }))
        ->OnError(CreateErrorHandler(context))
        ->Commit();
}

RPC_SERVICE_METHOD_IMPL(TTransactionService, CommitTransaction)
{
    UNUSED(response);

    auto id = TTransactionId::FromProto(request->GetTransactionId());

    context->SetRequestInfo("TransactionId: %s",
        ~id.ToString());
    
    ValidateTransactionId(id);

    TransactionManager
        ->InitiateCommitTransaction(id)
        ->OnSuccess(CreateSuccessHandler(context))
        ->OnError(CreateErrorHandler(context))
        ->Commit();
}

RPC_SERVICE_METHOD_IMPL(TTransactionService, AbortTransaction)
{
    UNUSED(response);

    auto id = TTransactionId::FromProto(request->GetTransactionId());

    context->SetRequestInfo("TransactionId: %s",
        ~id.ToString());
    
    ValidateTransactionId(id);

    TransactionManager
        ->InitiateAbortTransaction(id)
        ->OnSuccess(CreateSuccessHandler(context))
        ->OnError(CreateErrorHandler(context))
        ->Commit();
}

RPC_SERVICE_METHOD_IMPL(TTransactionService, RenewTransactionLease)
{
    UNUSED(response);

    auto id = TTransactionId::FromProto(request->GetTransactionId());

    context->SetRequestInfo("TransactionId: %s",
        ~id.ToString());
    
    ValidateTransactionId(id);

    TransactionManager->RenewLease(id);

    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransaction
} // namespace NYT
