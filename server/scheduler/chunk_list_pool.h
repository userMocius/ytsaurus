#pragma once

#include "public.h"

#include <yt/ytlib/chunk_client/public.h>

#include <yt/ytlib/object_client/master_ypath_proxy.h>
#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/logging/log.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

class TChunkListPool
    : public TRefCounted
{
public:
    TChunkListPool(
        TSchedulerConfigPtr config,
        NRpc::IChannelPtr masterChannel,
        IInvokerPtr controlInvoker,
        const TOperationId& operationId,
        const NTransactionClient::TTransactionId& transactionId);

    bool HasEnough(int requestedCount);
    NChunkClient::TChunkListId Extract();

    void Release(const std::vector<NChunkClient::TChunkListId>& ids);
    void Reinstall(const NChunkClient::TChunkListId& id);

private:
    TSchedulerConfigPtr Config;
    NRpc::IChannelPtr MasterChannel;
    IInvokerPtr ControlInvoker;
    TOperationId OperationId;
    NTransactionClient::TTransactionId TransactionId;

    NLogging::TLogger Logger;
    bool RequestInProgress;
    int LastSuccessCount;
    std::vector<NChunkClient::TChunkListId> Ids;

    void AllocateMore();

    void OnChunkListsCreated(
        const NObjectClient::TMasterYPathProxy::TErrorOrRspCreateObjectsPtr& rspOrError);

    void OnChunkListsReleased(
        const NObjectClient::TObjectServiceProxy::TErrorOrRspExecuteBatchPtr& batchRspOrError);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
