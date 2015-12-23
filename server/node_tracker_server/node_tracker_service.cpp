#include "node_tracker_service.h"
#include "private.h"
#include "config.h"
#include "node.h"
#include "node_tracker.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/hydra_facade.h>
#include <yt/server/cell_master/master_hydra_service.h>
#include <yt/server/cell_master/world_initializer.h>

#include <yt/server/chunk_server/chunk_manager.h>

#include <yt/server/hydra/rpc_helpers.h>

#include <yt/server/object_server/object_manager.h>

#include <yt/ytlib/hive/cell_directory.h>

#include <yt/ytlib/node_tracker_client/node_tracker_service_proxy.h>

#include <yt/core/misc/common.h>

namespace NYT {
namespace NNodeTrackerServer {

using namespace NHydra;
using namespace NCellMaster;
using namespace NNodeTrackerClient;
using namespace NChunkServer;

using NNodeTrackerClient::NProto::TChunkAddInfo;
using NNodeTrackerClient::NProto::TChunkRemoveInfo;

////////////////////////////////////////////////////////////////////////////////

class TNodeTrackerService
    : public NCellMaster::TMasterHydraServiceBase
{
public:
    explicit TNodeTrackerService(
        TNodeTrackerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap)
        : TMasterHydraServiceBase(
            bootstrap,
            TNodeTrackerServiceProxy::GetServiceName(),
            NodeTrackerServerLogger,
            TNodeTrackerServiceProxy::GetProtocolVersion())
        , Config_(config)
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(RegisterNode));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(FullHeartbeat)
            .SetRequestHeavy(true)
            .SetInvoker(GetGuardedAutomatonInvoker(EAutomatonThreadQueue::FullHeartbeat)));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(IncrementalHeartbeat)
            .SetRequestHeavy(true)
            .SetInvoker(GetGuardedAutomatonInvoker(EAutomatonThreadQueue::IncrementalHeartbeat)));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetRegisteredCells));
    }

private:
    const TNodeTrackerConfigPtr Config_;


    DECLARE_RPC_SERVICE_METHOD(NNodeTrackerClient::NProto, RegisterNode)
    {
        ValidatePeer(EPeerKind::Leader);

        auto worldInitializer = Bootstrap_->GetWorldInitializer();
        if (worldInitializer->CheckProvisionLock()) {
            THROW_ERROR_EXCEPTION(
                "Provision lock is found, which indicates a fresh instance of masters being run. "
                "If this is not intended then please check snapshot/changelog directories location. "
                "Ignoring this warning and removing the lock may cause UNRECOVERABLE DATA LOSS! "
                "If you are sure and wish to continue then run 'yt remove //sys/@provision_lock'");
        }

        auto addresses = FromProto<TAddressMap>(request->addresses());
        const auto& address = GetDefaultAddress(addresses);
        const auto& statistics = request->statistics();

        context->SetRequestInfo("Address: %v, %v",
            address,
            statistics);

        auto nodeTracker = Bootstrap_->GetNodeTracker();
        auto config = nodeTracker->FindNodeConfigByAddress(address);
        if (config && config->Banned) {
            THROW_ERROR_EXCEPTION("Node %v is banned", address);
        }

        if (!nodeTracker->TryAcquireNodeRegistrationSemaphore()) {
            context->Reply(TError(
                NRpc::EErrorCode::Unavailable,
                "Node registration throttling is active"));
            return;
        }

        nodeTracker
            ->CreateRegisterNodeMutation(*request)
            ->Commit()
            .Subscribe(CreateRpcResponseHandler(context));
    }

    DECLARE_RPC_SERVICE_METHOD(NNodeTrackerClient::NProto, FullHeartbeat)
    {
        ValidatePeer(EPeerKind::Leader);

        auto nodeId = request->node_id();
        const auto& statistics = request->statistics();

        auto nodeTracker = Bootstrap_->GetNodeTracker();
        auto* node = nodeTracker->GetNodeOrThrow(nodeId);

        context->SetRequestInfo("NodeId: %v, Address: %v, %v",
            nodeId,
            node->GetDefaultAddress(),
            statistics);

        if (node->GetState() != ENodeState::Registered) {
            context->Reply(TError(
                NNodeTrackerClient::EErrorCode::InvalidState,
                "Cannot process a full heartbeat in %Qlv state",
                node->GetState()));
            return;
        }

        nodeTracker
            ->CreateFullHeartbeatMutation(context)
            ->Commit()
            .Subscribe(CreateRpcResponseHandler(context));
    }

    DECLARE_RPC_SERVICE_METHOD(NNodeTrackerClient::NProto, IncrementalHeartbeat)
    {
        ValidatePeer(EPeerKind::Leader);

        auto nodeId = request->node_id();
        const auto& statistics = request->statistics();

        auto nodeTracker = Bootstrap_->GetNodeTracker();
        auto* node = nodeTracker->GetNodeOrThrow(nodeId);

        context->SetRequestInfo("NodeId: %v, Address: %v, %v",
            nodeId,
            node->GetDefaultAddress(),
            statistics);

        if (node->GetState() != ENodeState::Online) {
            context->Reply(TError(
                NNodeTrackerClient::EErrorCode::InvalidState,
                "Cannot process an incremental heartbeat in %Qlv state",
                node->GetState()));
            return;
        }

        nodeTracker
            ->CreateIncrementalHeartbeatMutation(context)
            ->Commit()
            .Subscribe(CreateRpcResponseHandler(context));
    }

    DECLARE_RPC_SERVICE_METHOD(NNodeTrackerClient::NProto, GetRegisteredCells)
    {
        ValidatePeer(EPeerKind::Leader);

        context->SetRequestInfo();

        auto nodeTracker = Bootstrap_->GetNodeTracker();
        ToProto(response->mutable_cell_descriptors(), nodeTracker->GetCellDescriptors());

        context->SetResponseInfo("DescriptorCount: %v",
            response->cell_descriptors_size());

        context->Reply();
    }

};

NRpc::IServicePtr CreateNodeTrackerService(
    TNodeTrackerConfigPtr config,
    NCellMaster::TBootstrap* bootstrap)
{
    return New<TNodeTrackerService>(config, bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerServer
} // namespace NYT
