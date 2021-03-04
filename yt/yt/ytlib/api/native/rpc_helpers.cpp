#include "rpc_helpers.h"
#include "config.h"

namespace NYT::NApi::NNative {

using namespace NRpc;
using namespace NObjectClient;

////////////////////////////////////////////////////////////////////////////////

namespace {

bool IsCachingEnabled(
    const TConnectionConfigPtr& config,
    const TMasterReadOptions& options)
{
    if (options.ReadFrom == EMasterChannelKind::LocalCache) {
        return true;
    }

    const auto& cache = config->MasterCache;
    if (!cache) {
        return false;
    }

    if (!cache->EnableMasterCacheDiscovery && (!cache->Addresses || cache->Addresses->empty())) {
        return false;
    }

    return
        options.ReadFrom == EMasterChannelKind::Cache ||
        options.ReadFrom == EMasterChannelKind::MasterCache;
}

} // namespace

void SetCachingHeader(
    const IClientRequestPtr& request,
    const TConnectionConfigPtr& config,
    const TMasterReadOptions& options,
    NHydra::TRevision refreshRevision)
{
    if (!IsCachingEnabled(config, options)) {
        return;
    }

    auto* cachingHeaderExt = request->Header().MutableExtension(NYTree::NProto::TCachingHeaderExt::caching_header_ext);
    cachingHeaderExt->set_expire_after_successful_update_time(ToProto<i64>(options.ExpireAfterSuccessfulUpdateTime));
    cachingHeaderExt->set_expire_after_failed_update_time(ToProto<i64>(options.ExpireAfterFailedUpdateTime));
    if (refreshRevision != NHydra::NullRevision) {
        cachingHeaderExt->set_refresh_revision(refreshRevision);
    }
}

void SetBalancingHeader(
    const TObjectServiceProxy::TReqExecuteBatchPtr& request,
    const TConnectionConfigPtr& config,
    const TMasterReadOptions& options)
{
    if (!IsCachingEnabled(config, options)) {
        return;
    }

    request->SetStickyGroupSize(options.CacheStickyGroupSize.value_or(
        config->DefaultCacheStickyGroupSize));
    request->SetEnableClientStickiness(options.EnableClientCacheStickiness);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
