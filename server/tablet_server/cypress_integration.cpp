#include "cypress_integration.h"
#include "tablet.h"
#include "tablet_cell.h"
#include "tablet_manager.h"

#include <yt/server/cell_master/bootstrap.h>

#include <yt/server/cypress_server/node_detail.h>
#include <yt/server/cypress_server/node_proxy_detail.h>
#include <yt/server/cypress_server/virtual.h>

#include <yt/server/tablet_server/tablet_manager.h>

#include <yt/core/misc/common.h>

namespace NYT {
namespace NTabletServer {

using namespace NYPath;
using namespace NRpc;
using namespace NYson;
using namespace NYTree;
using namespace NObjectClient;
using namespace NCypressServer;
using namespace NTransactionServer;
using namespace NObjectServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TTabletCellNodeProxy
    : public TMapNodeProxy
{
public:
    TTabletCellNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        TTransaction* transaction,
        TMapNode* trunkNode)
        : TMapNodeProxy(
            typeHandler,
            bootstrap,
            transaction,
            trunkNode)
    { }

    virtual TResolveResult ResolveSelf(const TYPath& path, IServiceContextPtr context) override
    {
        const auto& method = context->GetMethod();
        if (method == "Remove") {
            return TResolveResult::There(GetTargetProxy(), path);
        } else {
            return TMapNodeProxy::ResolveSelf(path, context);
        }
    }

    virtual IYPathService::TResolveResult ResolveAttributes(
        const TYPath& path,
        IServiceContextPtr /*context*/) override
    {
        return TResolveResult::There(
            GetTargetProxy(),
            "/@" + path);
    }

    virtual void SerializeAttributes(
        IYsonConsumer* consumer,
        const TAttributeFilter& filter,
        bool sortKeys) override
    {
        GetTargetProxy()->SerializeAttributes(consumer, filter, sortKeys);
    }

private:
    IObjectProxyPtr GetTargetProxy() const
    {
        auto key = GetParent()->AsMap()->GetChildKey(this);
        auto id = TTabletCellId::FromString(key);
     
        auto tabletManager = Bootstrap_->GetTabletManager();
        auto* cell = tabletManager->GetTabletCellOrThrow(id);

        auto objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(cell, nullptr);
    }

};

////////////////////////////////////////////////////////////////////////////////

class TTabletCellNodeTypeHandler
    : public TMapNodeTypeHandler
{
public:
    explicit TTabletCellNodeTypeHandler(TBootstrap* Bootstrap_)
        : TMapNodeTypeHandler(Bootstrap_)
    { }

    virtual EObjectType GetObjectType() override
    {
        return EObjectType::TabletCellNode;
    }

private:
    virtual ICypressNodeProxyPtr DoGetProxy(
        TMapNode* trunkNode,
        TTransaction* transaction) override
    {
        return New<TTabletCellNodeProxy>(
            this,
            Bootstrap_,
            transaction,
            trunkNode);
    }

};

INodeTypeHandlerPtr CreateTabletCellNodeTypeHandler(TBootstrap* Bootstrap_)
{
    return New<TTabletCellNodeTypeHandler>(Bootstrap_);
}

////////////////////////////////////////////////////////////////////////////////

INodeTypeHandlerPtr CreateTabletMapTypeHandler(TBootstrap* Bootstrap_)
{
    YCHECK(Bootstrap_);

    auto service = CreateVirtualObjectMap(
        Bootstrap_,
        Bootstrap_->GetTabletManager()->Tablets());
    return CreateVirtualTypeHandler(
        Bootstrap_,
        EObjectType::TabletMap,
        service,
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT
