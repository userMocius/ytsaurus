#include "stdafx.h"
#include "file_node_proxy.h"

namespace NYT {
namespace NFileServer {

using namespace NChunkServer;
using namespace NCypress;
using namespace NYTree;
using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

TFileNodeProxy::TFileNodeProxy(
    INodeTypeHandler* typeHandler,
    TCypressManager* cypressManager,
    TChunkManager* chunkManager,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCypressNodeProxyBase<IEntityNode, TFileNode>(
        typeHandler,
        cypressManager,
        transactionId,
        nodeId)
    , ChunkManager(chunkManager)
{ }

bool TFileNodeProxy::IsOperationLogged(TYPath path, const Stroka& verb) const
{
    if (verb == "SetFileChunk") {
        return true;
    } else {
        return TBase::IsOperationLogged(path, verb);
    }
}

void TFileNodeProxy::DoInvoke(IServiceContext* context)
{
    Stroka verb = context->GetVerb();
    if (verb == "GetFileChunk") {
        GetFileChunkThunk(context);
    } else if (verb == "SetFileChunk") {
        SetFileChunkThunk(context);
    } else {
        TBase::DoInvoke(context);
    }
}

////////////////////////////////////////////////////////////////////////////////

RPC_SERVICE_METHOD_IMPL(TFileNodeProxy, GetFileChunk)
{
    UNUSED(request);

    const auto& impl = GetTypedImpl();

    TChunkId chunkId;
    auto chunkListId = impl.GetChunkListId();
    if (chunkListId == NullChunkId) {
        chunkId = NullChunkId;
    } else {
        const auto& chunkList = ChunkManager->GetChunkList(chunkListId);
        YASSERT(chunkList.ChunkIds().ysize() == 1);
        chunkId = chunkList.ChunkIds()[0];
    }

    context->SetResponseInfo("ChunkId: %s", ~chunkId.ToString());

    response->SetChunkId(NullChunkId.ToProto());
    context->Reply();
}

RPC_SERVICE_METHOD_IMPL(TFileNodeProxy, SetFileChunk)
{
    UNUSED(response);

    auto chunkId = TChunkId::FromProto(request->GetChunkId());

    context->SetRequestInfo("ChunkId: %s", ~chunkId.ToString());

    auto& chunk = ChunkManager->GetChunkForUpdate(chunkId);
    if (chunk.GetChunkListId() != NullChunkListId) {
        ythrow yexception() << "Chunk is already assigned to another chunk list";
    }

    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    if (impl.GetChunkListId() != NullChunkListId) {
        ythrow yexception() << "File already has a chunk";
    }

    // Create a chunklist and couple it with the chunk.
    auto& chunkList = ChunkManager->CreateChunkList();
    ChunkManager->AddChunkToChunkList(chunk, chunkList);

    // Reference the chunklist from the file.
    impl.SetChunkListId(chunkList.GetId());
    ChunkManager->RefChunkList(chunkList);

    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileServer
} // namespace NYT

