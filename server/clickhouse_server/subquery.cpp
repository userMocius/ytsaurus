#include "subquery.h"

#include "query_context.h"
#include "config.h"

#include "subquery_spec.h"
#include "table_schema.h"
#include "table.h"
#include "helpers.h"

#include "private.h"

#include <yt/server/lib/chunk_pools/chunk_stripe.h>
#include <yt/server/lib/chunk_pools/helpers.h>
#include <yt/server/lib/chunk_pools/unordered_chunk_pool.h>
#include <yt/server/lib/chunk_pools/sorted_chunk_pool.h>
#include <yt/server/lib/controller_agent/job_size_constraints.h>

#include <yt/ytlib/api/native/client.h>

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/chunk_spec.h>
#include <yt/ytlib/chunk_client/input_chunk_slice.h>
#include <yt/ytlib/chunk_client/data_slice_descriptor.h>
#include <yt/ytlib/chunk_client/input_data_slice.h>
#include <yt/ytlib/chunk_client/data_source.h>
#include <yt/ytlib/chunk_client/helpers.h>
#include <yt/ytlib/chunk_client/chunk_spec_fetcher.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/cypress_client/rpc_helpers.h>

#include <yt/ytlib/table_client/chunk_meta_extensions.h>
#include <yt/ytlib/table_client/chunk_slice_fetcher.h>
#include <yt/ytlib/table_client/schema.h>
#include <yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/client/node_tracker_client/node_directory.h>

#include <yt/client/object_client/helpers.h>

#include <yt/client/ypath/rich.h>

#include <yt/client/table_client/row_buffer.h>

#include <yt/client/chunk_client/proto/chunk_meta.pb.h>

#include <yt/core/logging/log.h>
#include <yt/core/misc/protobuf_helpers.h>
#include <yt/core/ytree/convert.h>

#include <Storages/MergeTree/KeyCondition.h>
#include <DataTypes/IDataType.h>

namespace NYT::NClickHouseServer {

using namespace NApi;
using namespace NChunkClient;
using namespace NChunkPools;
using namespace NConcurrency;
using namespace NControllerAgent;
using namespace NCypressClient;
using namespace NNodeTrackerClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NTransactionClient;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace DB;

using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

struct TInputTable
    : public TUserObject
{
    int ChunkCount = 0;
    bool IsDynamic = false;
    TTableSchema Schema;
    int TableIndex = 0;
};

////////////////////////////////////////////////////////////////////////////////

// TODO(max42): rename
class TDataSliceFetcher
    : public TRefCounted
{
public:
    // TODO(max42): use from bootstrap?
    DEFINE_BYREF_RW_PROPERTY(TDataSourceDirectoryPtr, DataSourceDirectory, New<TDataSourceDirectory>());
    DEFINE_BYREF_RW_PROPERTY(TNodeDirectoryPtr, NodeDirectory, New<TNodeDirectory>());
    DEFINE_BYREF_RW_PROPERTY(TChunkStripeListPtr, ResultStripeList, New<TChunkStripeList>());

public:
    TDataSliceFetcher(
        NNative::IClientPtr client,
        IInvokerPtr invoker,
        std::vector<TTableSchema> tableSchemas,
        std::vector<std::vector<TRichYPath>> inputTablePaths,
        std::vector<std::optional<KeyCondition>> keyConditions,
        TRowBufferPtr rowBuffer,
        TSubqueryConfigPtr config)
        : Client_(std::move(client))
        , Invoker_(std::move(invoker))
        , TableSchemas_(std::move(tableSchemas))
        , InputTablePaths_(std::move(inputTablePaths))
        , KeyConditions_(std::move(keyConditions))
        , RowBuffer_(std::move(rowBuffer))
        , Config_(std::move(config))
    {}

    TFuture<void> Fetch()
    {
        return BIND(&TDataSliceFetcher::DoFetch, MakeWeak(this))
            .AsyncVia(Invoker_)
            .Run();
    }

private:
    const NLogging::TLogger& Logger = ServerLogger;

    NApi::NNative::IClientPtr Client_;

    IInvokerPtr Invoker_;

    std::vector<TTableSchema> TableSchemas_;
    std::vector<std::vector<TRichYPath>> InputTablePaths_;
    std::vector<std::optional<KeyCondition>> KeyConditions_;

    int KeyColumnCount_ = 0;
    TKeyColumns KeyColumns_;

    DataTypes KeyColumnDataTypes_;

    std::vector<TInputTable> InputTables_;

    TRowBufferPtr RowBuffer_;

    TSubqueryConfigPtr Config_;

    std::vector<TInputChunkPtr> InputChunks_;

    void DoFetch()
    {
        CollectTablesAttributes();
        ValidateSchema();
        FetchChunks();

        std::vector<TChunkStripePtr> stripes;
        for (int index = 0; index < static_cast<int>(InputTablePaths_.size()); ++index) {
            stripes.emplace_back(New<TChunkStripe>());
        }
        for (const auto& chunk : InputChunks_) {
            if (!chunk->BoundaryKeys() ||
                GetRangeMask(chunk->BoundaryKeys()->MinKey, chunk->BoundaryKeys()->MaxKey, chunk->GetTableIndex()).can_be_true)
            {
                stripes[chunk->GetTableIndex()]->DataSlices.emplace_back(CreateUnversionedInputDataSlice(CreateInputChunkSlice(chunk)));
            }
        }
        for (auto& stripe : stripes) {
            ResultStripeList_->AddStripe(std::move(stripe));
        }
        YT_LOG_INFO(
            "Input chunks fetched (TotalChunkCount: %v, TotalDataWeight: %v, TotalRowCount: %v)",
            ResultStripeList_->TotalChunkCount,
            ResultStripeList_->TotalDataWeight,
            ResultStripeList_->TotalRowCount);
    }

    // TODO(max42): get rid of duplicating code.
    void CollectBasicAttributes()
    {
        YT_LOG_DEBUG("Requesting basic object attributes");

        for (
            int logicalTableIndex = 0;
            logicalTableIndex < static_cast<int>(InputTablePaths_.size());
            ++logicalTableIndex)
        {
            auto tablePaths = InputTablePaths_[logicalTableIndex];
            for (const auto& tablePath : tablePaths) {
                auto& table = InputTables_.emplace_back();
                table.Path = tablePath;
                table.TableIndex = logicalTableIndex;
            }
        }

        GetUserObjectBasicAttributes(
            Client_,
            MakeUserObjectList(InputTables_),
            NullTransactionId,
            Logger,
            EPermission::Read);

        for (const auto& table : InputTables_) {
            if (table.Type != NObjectClient::EObjectType::Table) {
                THROW_ERROR_EXCEPTION("Object %v has invalid type: expected %Qlv, actual %Qlv",
                    table.Path.GetPath(),
                    NObjectClient::EObjectType::Table,
                    table.Type);
            }
        }
    }

    void CollectTableSpecificAttributes()
    {
        // XXX(babenko): fetch from external cells
        YT_LOG_DEBUG("Requesting extended table attributes");

        auto channel = Client_->GetMasterChannelOrThrow(EMasterChannelKind::Follower);

        TObjectServiceProxy proxy(channel);
        auto batchReq = proxy.ExecuteBatch();

        for (const auto& table : InputTables_) {
            auto req = TTableYPathProxy::Get(table.GetObjectIdPath() + "/@");
            ToProto(req->mutable_attributes()->mutable_keys(), std::vector<TString>{
                "dynamic",
                "chunk_count",
                "schema",
            });
            SetTransactionId(req, NullTransactionId);
            batchReq->AddRequest(req, "get_attributes");
        }

        auto batchRspOrError = WaitFor(batchReq->Invoke());
        THROW_ERROR_EXCEPTION_IF_FAILED(GetCumulativeError(batchRspOrError), "Error requesting extended attributes of tables");
        const auto& batchRsp = batchRspOrError.Value();

        auto getInAttributesRspsOrError = batchRsp->GetResponses<TTableYPathProxy::TRspGet>("get_attributes");
        for (size_t index = 0; index < InputTables_.size(); ++index) {
            auto& table = InputTables_[index];

            {
                const auto& rsp = getInAttributesRspsOrError[index].Value();
                auto attributes = ConvertToAttributes(TYsonString(rsp->value()));

                table.ChunkCount = attributes->Get<int>("chunk_count");
                table.IsDynamic = attributes->Get<bool>("dynamic");
                table.Schema = attributes->Get<TTableSchema>("schema");
            }
        }
    }

    void CollectTablesAttributes()
    {
        YT_LOG_INFO("Collecting input tables attributes");

        CollectBasicAttributes();
        CollectTableSpecificAttributes();
    }

    void ValidateSchema()
    {
        if (InputTables_.empty()) {
            return;
        }

        const auto& representativeTable = InputTables_.front();
        for (size_t i = 1; i < InputTables_.size(); ++i) {
            const auto& table = InputTables_[i];
            if (table.IsDynamic != representativeTable.IsDynamic) {
                THROW_ERROR_EXCEPTION(
                    "Table dynamic flag mismatch: %v and %v",
                    representativeTable.Path.GetPath(),
                    table.Path.GetPath());
            }
        }

        KeyColumnCount_ = representativeTable.Schema.GetKeyColumnCount();
        KeyColumns_ = representativeTable.Schema.GetKeyColumns();
        KeyColumnDataTypes_ = TClickHouseTableSchema::From(TClickHouseTable("", representativeTable.Schema)).GetKeyDataTypes();
    }

    void FetchChunks()
    {
        i64 totalChunkCount = 0;
        for (const auto& inputTable : InputTables_) {
            totalChunkCount += inputTable.ChunkCount;
        }
        YT_LOG_INFO("Fetching input chunks (InputTableCount: %v, TotalChunkCount: %v)",
            InputTables_.size(),
            totalChunkCount);

        auto chunkSpecFetcher = New<TChunkSpecFetcher>(
            Client_,
            NodeDirectory_,
            Invoker_,
            Config_->MaxChunksPerFetch,
            Config_->MaxChunksPerLocateRequest,
            [=] (const TChunkOwnerYPathProxy::TReqFetchPtr& req) {
                req->set_fetch_all_meta_extensions(false);
                req->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
                req->add_extension_tags(TProtoExtensionTag<NTableClient::NProto::TBoundaryKeysExt>::Value);
                SetTransactionId(req, NullTransactionId);
                SetSuppressAccessTracking(req, true);
            },
            Logger);

        for (const auto& table : InputTables_) {
            auto logicalTableIndex = table.TableIndex;

            if (table.IsDynamic) {
                THROW_ERROR_EXCEPTION("Dynamic tables are not supported yet (YT-9404)")
                    << TErrorAttribute("table", table.Path.GetPath());
            }

            auto dataSource = MakeUnversionedDataSource(
                std::nullopt /* path */,
                table.Schema,
                std::nullopt /* columns */,
                // TODO(max42): YT-10402, omitted inaccessible columns
                {});

            DataSourceDirectory_->DataSources().push_back(std::move(dataSource));

            chunkSpecFetcher->Add(
                table.ObjectId,
                table.ExternalCellTag,
                table.ChunkCount,
                logicalTableIndex,
                table.Path.GetRanges());
        }

        WaitFor(chunkSpecFetcher->Fetch())
            .ThrowOnError();

        YT_LOG_INFO("Chunks fetched (ChunkCount: %v)", chunkSpecFetcher->ChunkSpecs().size());

        for (const auto& chunkSpec : chunkSpecFetcher->ChunkSpecs()) {
            auto inputChunk = New<TInputChunk>(chunkSpec);
            InputChunks_.emplace_back(std::move(inputChunk));
        }
    }

    BoolMask GetRangeMask(TKey lowerKey, TKey upperKey, int tableIndex)
    {
        if (!KeyConditions_[tableIndex]) {
            return BoolMask(true, true);
        }

        YT_VERIFY(static_cast<int>(lowerKey.GetCount()) >= KeyColumnCount_);
        YT_VERIFY(static_cast<int>(upperKey.GetCount()) >= KeyColumnCount_);

        Field minKey[KeyColumnCount_];
        Field maxKey[KeyColumnCount_];
        ConvertToFieldRow(lowerKey, KeyColumnCount_, minKey);
        ConvertToFieldRow(upperKey, KeyColumnCount_, maxKey);

        return KeyConditions_[tableIndex]->checkInRange(KeyColumnCount_, minKey, maxKey, KeyColumnDataTypes_);
    }
};

DEFINE_REFCOUNTED_TYPE(TDataSliceFetcher);

////////////////////////////////////////////////////////////////////////////////

TChunkStripeListPtr FetchInput(
    NNative::IClientPtr client,
    const IInvokerPtr& invoker,
    std::vector<TTableSchema> tableSchemas,
    std::vector<std::vector<TRichYPath>> inputTablePaths,
    std::vector<std::optional<KeyCondition>> keyConditions,
    TRowBufferPtr rowBuffer,
    TSubqueryConfigPtr config,
    TSubquerySpec& specTemplate)
{
    auto dataSliceFetcher = New<TDataSliceFetcher>(
        std::move(client),
        invoker,
        std::move(tableSchemas),
        std::move(inputTablePaths),
        std::move(keyConditions),
        std::move(rowBuffer),
        std::move(config));
    WaitFor(dataSliceFetcher->Fetch())
        .ThrowOnError();

    if (!specTemplate.NodeDirectory) {
        specTemplate.NodeDirectory = New<TNodeDirectory>();
    }
    specTemplate.NodeDirectory->MergeFrom(dataSliceFetcher->NodeDirectory());
    YT_VERIFY(!specTemplate.DataSourceDirectory);
    specTemplate.DataSourceDirectory = std::move(dataSliceFetcher->DataSourceDirectory());

    return std::move(dataSliceFetcher->ResultStripeList());
}

std::vector<NChunkPools::TChunkStripeListPtr> BuildSubqueries(
    const TChunkStripeListPtr& inputStripeList,
    std::optional<int> keyColumnCount,
    EPoolKind poolKind,
    int jobCount,
    std::optional<double> samplingRate,
    const DB::Context& context)
{
    if (jobCount == 0) {
        jobCount = 1;
    }

    auto* queryContext = GetQueryContext(context);

    std::vector<TChunkStripeListPtr> result;

    auto dataWeightPerJob = inputStripeList->TotalDataWeight / jobCount;

    if (inputStripeList->TotalRowCount * samplingRate.value_or(1.0) < 1.0) {
        return result;
    }

    i64 originalJobCount = jobCount;

    if (samplingRate) {
        constexpr int MaxJobCount = 1'000'000;
        double rate = samplingRate.value();
        rate = std::max(rate, static_cast<double>(jobCount) / MaxJobCount);
        jobCount = std::floor(jobCount / rate);
    }

    std::unique_ptr<IChunkPool> chunkPool;

    auto jobSizeConstraints = CreateExplicitJobSizeConstraints(
        false /* canAdjustDataWeightPerJob */,
        true /* isExplicitJobCount */,
        jobCount,
        dataWeightPerJob,
        1_TB /* primaryDataWeightPerJob */,
        1_TB,
        10_GB /* maxDataWeightPerJob */,
        10_GB /* primaryMaxDataWeightPerJob */,
        std::max<i64>(1, dataWeightPerJob * 0.26) /* inputSliceDataWeight */,
        std::max<i64>(1, inputStripeList->TotalRowCount / jobCount) /* inputSliceRowCount */,
        std::nullopt /* samplingRate */);

    if (poolKind == EPoolKind::Unordered) {
        chunkPool = CreateUnorderedChunkPool(
            TUnorderedChunkPoolOptions{
                .JobSizeConstraints = std::move(jobSizeConstraints),
                .OperationId = queryContext->QueryId,
            },
            TInputStreamDirectory({TInputStreamDescriptor(false /* isTeleportable */, true /* isPrimary */, false /* isVersioned */)}));
    } else if (poolKind == EPoolKind::Sorted) {
        YT_VERIFY(keyColumnCount);
        chunkPool = CreateSortedChunkPool(
            TSortedChunkPoolOptions{
                .SortedJobOptions = TSortedJobOptions{
                    .EnableKeyGuarantee = true,
                    .PrimaryPrefixLength = *keyColumnCount,
                    .MaxTotalSliceCount = std::numeric_limits<int>::max() / 2,
                },
                .MinTeleportChunkSize = std::numeric_limits<i64>::max() / 2,
                .JobSizeConstraints = jobSizeConstraints,
                .OperationId = queryContext->QueryId,
                .RowBuffer = queryContext->RowBuffer,
            },
            CreateCallbackChunkSliceFetcherFactory(BIND([] { return nullptr; })),
            TInputStreamDirectory({
                TInputStreamDescriptor(false /* isTeleportable */, true /* isPrimary */, false /* isVersioned */),
                TInputStreamDescriptor(false /* isTeleportable */, true /* isPrimary */, false /* isVersioned */)
            }));
    } else {
        Y_UNREACHABLE();
    }

    for (const auto& chunkStripe : inputStripeList->Stripes) {
        for (const auto& dataSlice : chunkStripe->DataSlices) {
            InferLimitsFromBoundaryKeys(dataSlice, queryContext->RowBuffer);
        }
        chunkPool->Add(chunkStripe);
    }
    chunkPool->Finish();

    while (true) {
        auto cookie = chunkPool->Extract();
        if (cookie == IChunkPoolOutput::NullCookie) {
            break;
        }
        auto stripeList = chunkPool->GetStripeList(cookie);
        if (poolKind == EPoolKind::Unordered) {
            // Stripelists from unordered pool consist of lot of stripes; we expect a single
            // stripe with lots of data slices inside, so we flatten them.
            auto flattenedStripe = New<TChunkStripe>();
            for (const auto& stripe : stripeList->Stripes) {
                for (const auto& dataSlice : stripe->DataSlices) {
                    flattenedStripe->DataSlices.emplace_back(dataSlice);
                }
            }
            auto flattenedStripeList = New<TChunkStripeList>();
            flattenedStripeList->Stripes.emplace_back(std::move(flattenedStripe));
            stripeList.Swap(flattenedStripeList);
        }
        result.emplace_back(stripeList);
    }

    if (originalJobCount != jobCount) {
        std::mt19937 gen;
        std::shuffle(result.begin(), result.end(), gen);
        result.resize(std::min<int>(result.size(), originalJobCount));
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
