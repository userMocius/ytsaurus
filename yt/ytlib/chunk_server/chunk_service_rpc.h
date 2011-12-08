#pragma once

#include "common.h"
#include "chunk_service_rpc.pb.h"

#include "../rpc/service.h"
#include "../rpc/client.h"

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

class TChunkServiceProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TChunkServiceProxy> TPtr;

    RPC_DECLARE_PROXY(ChunkManager,
        ((NoSuchTransaction)(1))
        ((NoSuchHolder)(2))
        ((NoSuchChunk)(3))
        ((NotEnoughHolders)(4))
    );

    TChunkServiceProxy(NRpc::IChannel* channel)
        : TProxyBase(channel, GetServiceName())
    { }

    DEFINE_RPC_PROXY_METHOD(NProto, RegisterHolder);
    DEFINE_RPC_PROXY_METHOD(NProto, HolderHeartbeat);
    DEFINE_RPC_PROXY_METHOD(NProto, AllocateChunk);
    DEFINE_RPC_PROXY_METHOD(NProto, ConfirmChunks);
    DEFINE_RPC_PROXY_METHOD(NProto, FindChunk);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
