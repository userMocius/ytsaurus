#include "table_reader.h"
#include "private.h"

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_spec.h>
#include <yt/ytlib/chunk_client/dispatcher.h>
#include <yt/ytlib/chunk_client/multi_chunk_reader_base.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/node_tracker_client/node_directory.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/table_client/name_table.h>
#include <yt/ytlib/table_client/schemaless_chunk_reader.h>
#include <yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/ytlib/transaction_client/helpers.h>
#include <yt/ytlib/transaction_client/transaction_listener.h>
#include <yt/ytlib/transaction_client/transaction_manager.h>

#include <yt/ytlib/ypath/rich.h>

#include <yt/core/concurrency/scheduler.h>
#include <yt/core/concurrency/throughput_throttler.h>

#include <yt/core/misc/common.h>
#include <yt/core/misc/protobuf_helpers.h>

#include <yt/core/rpc/public.h>

#include <yt/core/ytree/ypath_proxy.h>

namespace NYT {
namespace NApi {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;
using namespace NCypressClient;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NTableClient::NProto;
using namespace NTransactionClient;
using namespace NYPath;
using namespace NYTree;
using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

class TSchemalessTableReader
    : public ISchemalessMultiChunkReader
    , public TTransactionListener
{
public:
    TSchemalessTableReader(
        TTableReaderConfigPtr config,
        TRemoteReaderOptionsPtr options,
        IClientPtr client,
        TTransactionPtr transaction,
        const TRichYPath& richPath,
        bool unordered);

    virtual TFuture<void> Open() override;
    virtual bool Read(std::vector<TUnversionedRow>* rows) override;
    virtual TFuture<void> GetReadyEvent() override;

    virtual i64 GetTableRowIndex() const override;
    virtual i32 GetRangeIndex() const override;
    virtual TNameTablePtr GetNameTable() const override;
    virtual i64 GetTotalRowCount() const override;

    virtual TKeyColumns GetKeyColumns() const override;

    // not actually used
    virtual int GetTableIndex() const override;
    virtual i64 GetSessionRowIndex() const override;
    virtual bool IsFetchingCompleted() const override;
    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const override;
    virtual std::vector<TChunkId> GetFailedChunkIds() const override;

private:
    const TTableReaderConfigPtr Config_;
    const TRemoteReaderOptionsPtr Options_;
    const IClientPtr Client_;
    const IChannelPtr MasterChannel_;
    const TTransactionPtr Transaction_;
    const IBlockCachePtr BlockCache_;
    const TRichYPath RichPath_;

    const TTransactionId TransactionId_;
    const bool Unordered_;

    ISchemalessMultiChunkReaderPtr UnderlyingReader_;

    NLogging::TLogger Logger = ApiLogger;

    void DoOpen();

};

////////////////////////////////////////////////////////////////////////////////

TSchemalessTableReader::TSchemalessTableReader(
    TTableReaderConfigPtr config,
    TRemoteReaderOptionsPtr options,
    IClientPtr client,
    TTransactionPtr transaction,
    const TRichYPath& richPath,
    bool unordered)
    : Config_(config)
    , Options_(options)
    , Client_(client)
    , MasterChannel_(client->GetMasterChannel(NApi::EMasterChannelKind::LeaderOrFollower))
    , Transaction_(transaction)
    , BlockCache_(client->GetConnection()->GetBlockCache())
    , RichPath_(richPath)
    , TransactionId_(transaction ? transaction->GetId() : NullTransactionId)
    , Unordered_(unordered)
{
    YCHECK(Config_);
    YCHECK(MasterChannel_);
    YCHECK(BlockCache_);

    Logger.AddTag("Path: %v, TransactionId: %v",
        RichPath_.GetPath(),
        TransactionId_);
}

TFuture<void> TSchemalessTableReader::Open()
{
    return BIND(&TSchemalessTableReader::DoOpen, MakeStrong(this))
        .AsyncVia(TDispatcher::Get()->GetReaderInvoker())
        .Run();
}

void TSchemalessTableReader::DoOpen()
{
    const auto& path = RichPath_.GetPath();

    LOG_INFO("Opening table reader");

    TObjectServiceProxy objectProxy(MasterChannel_);

    auto batchReq = objectProxy.ExecuteBatch();

    {
        auto req = TYPathProxy::Get(path + "/@type");
        SetTransactionId(req, Transaction_);
        SetSuppressAccessTracking(req, Config_->SuppressAccessTracking);
        batchReq->AddRequest(req, "get_type");
    }

    {
        auto req = TTableYPathProxy::Fetch(path);
        InitializeFetchRequest(req.Get(), RichPath_);
        req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
        SetTransactionId(req, Transaction_);
        SetSuppressAccessTracking(req, Config_->SuppressAccessTracking);
        batchReq->AddRequest(req, "fetch");
    }

    LOG_INFO("Fetching table info");
    auto batchRspOrError = WaitFor(batchReq->Invoke());
    THROW_ERROR_EXCEPTION_IF_FAILED(batchRspOrError, "Error fetching table info");

    auto batchRsp = batchRspOrError.Value();
    {
        auto rspOrError = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_type");
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error getting object type");
        auto rsp = rspOrError.Value();

        auto type = ConvertTo<EObjectType>(TYsonString(rsp->value()));
        if (type != EObjectType::Table) {
            THROW_ERROR_EXCEPTION("Invalid type of %v: expected %Qlv, actual %Qlv",
                path,
                EObjectType::Table,
                type);
        }
    }

    {
        auto rspOrError = batchRsp->GetResponse<TTableYPathProxy::TRspFetch>("fetch");
        THROW_ERROR_EXCEPTION_IF_FAILED(rspOrError, "Error fetching table chunks");
        auto rsp = rspOrError.Value();

        auto nodeDirectory = New<TNodeDirectory>();
        nodeDirectory->MergeFrom(rsp->node_directory());

        std::vector<TChunkSpec> chunkSpecs;
        for (const auto& chunkSpec : rsp->chunks()) {
            if (!IsUnavailable(chunkSpec)) {
                chunkSpecs.push_back(chunkSpec);
                continue;
            }

            if (Config_->IgnoreUnavailableChunks) {
                continue;
            }

            THROW_ERROR_EXCEPTION(
                NChunkClient::EErrorCode::ChunkUnavailable,
                "Chunk %v is unavailable",
                NYT::FromProto<TChunkId>(chunkSpec.chunk_id()));
        }

        auto readerFactory = Unordered_
            ? CreateSchemalessParallelMultiChunkReader
            : CreateSchemalessSequentialMultiChunkReader;

        UnderlyingReader_ = readerFactory(
            Config_,
            New<TMultiChunkReaderOptions>(),
            Client_,
            BlockCache_,
            nodeDirectory,
            chunkSpecs,
            New<TNameTable>(),
            TColumnFilter(),
            TKeyColumns(),
            NConcurrency::GetUnlimitedThrottler());

        WaitFor(UnderlyingReader_->Open())
            .ThrowOnError();
    }

    if (Transaction_) {
        ListenTransaction(Transaction_);
    }

    LOG_INFO("Table reader opened");
}

bool TSchemalessTableReader::Read(std::vector<TUnversionedRow> *rows)
{
    if (IsAborted()) {
        return true;
    }

    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->Read(rows);
}

TFuture<void> TSchemalessTableReader::GetReadyEvent()
{
    if (IsAborted()) {
        return MakeFuture(TError("Transaction %v aborted",
            TransactionId_));
    }

    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetReadyEvent();
}

i64 TSchemalessTableReader::GetTableRowIndex() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetTableRowIndex();
}

i32 TSchemalessTableReader::GetRangeIndex() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetRangeIndex();
}

i64 TSchemalessTableReader::GetTotalRowCount() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetTotalRowCount();
}

TNameTablePtr TSchemalessTableReader::GetNameTable() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetNameTable();
}

TKeyColumns TSchemalessTableReader::GetKeyColumns() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetKeyColumns();
}

int TSchemalessTableReader::GetTableIndex() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetTableIndex();
}

i64 TSchemalessTableReader::GetSessionRowIndex() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetSessionRowIndex();
}

bool TSchemalessTableReader::IsFetchingCompleted() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->IsFetchingCompleted();
}

NChunkClient::NProto::TDataStatistics TSchemalessTableReader::GetDataStatistics() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetDataStatistics();
}

std::vector<TChunkId> TSchemalessTableReader::GetFailedChunkIds() const
{
    YCHECK(UnderlyingReader_);
    return UnderlyingReader_->GetFailedChunkIds();
}

////////////////////////////////////////////////////////////////////////////////

ISchemalessMultiChunkReaderPtr CreateTableReader(
    IClientPtr client,
    const NYPath::TRichYPath& path,
    const TTableReaderOptions& options)
{
    NTransactionClient::TTransactionPtr transaction;

    if (options.TransactionId != NTransactionClient::NullTransactionId) {
        NTransactionClient::TTransactionAttachOptions transactionOptions;
        transactionOptions.Ping = options.Ping;
        transactionOptions.PingAncestors = options.PingAncestors;

        transaction = client->GetTransactionManager()->Attach(
            options.TransactionId,
            transactionOptions);
    }

    return New<TSchemalessTableReader>(
        options.Config,
        New<TRemoteReaderOptions>(),
        client,
        transaction,
        path,
        options.Unordered);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi
} // namespace NYT

