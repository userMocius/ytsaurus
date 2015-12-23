#pragma once

#include "public.h"

#include <yt/server/scheduler/public.h>

#include <yt/ytlib/api/public.h>

#include <yt/ytlib/hive/public.h>

#include <yt/ytlib/monitoring/http_server.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/bus/public.h>

#include <yt/core/concurrency/action_queue.h>

#include <yt/core/rpc/public.h>


namespace NYT {
namespace NCellScheduler {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EControlQueue,
    (Default)
    (Heartbeat)
);

class TBootstrap
{
public:
    explicit TBootstrap(const NYTree::INodePtr configNode);
    ~TBootstrap();

    TCellSchedulerConfigPtr GetConfig() const;
    NApi::IClientPtr GetMasterClient() const;
    const Stroka& GetLocalAddress() const;
    IInvokerPtr GetControlInvoker(EControlQueue queue = EControlQueue::Default) const;
    NScheduler::TSchedulerPtr GetScheduler() const;
    NHive::TClusterDirectoryPtr GetClusterDirectory() const;
    NRpc::TResponseKeeperPtr GetResponseKeeper() const;
    NConcurrency::IThroughputThrottlerPtr GetChunkLocationThrottler() const;

    void Run();

private:
    const NYTree::INodePtr ConfigNode_;

    TCellSchedulerConfigPtr Config_;
    NConcurrency::TFairShareActionQueuePtr ControlQueue_;
    NBus::IBusServerPtr BusServer_;
    NRpc::IServerPtr RpcServer_;
    std::unique_ptr<NHttp::TServer> HttpServer_;
    NApi::IClientPtr MasterClient_;
    Stroka LocalAddress_;
    NScheduler::TSchedulerPtr Scheduler_;
    NHive::TClusterDirectoryPtr ClusterDirectory_;
    NRpc::TResponseKeeperPtr ResponseKeeper_;
    NConcurrency::IThroughputThrottlerPtr ChunkLocationThrottler_;

    void DoRun();
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellScheduler
} // namespace NYT
