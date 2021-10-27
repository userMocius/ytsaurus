#include "functions_cache.h"
#include "functions_cg.h"
#include "private.h"

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/config.h>

#include <yt/yt/client/api/file_reader.h>

#include <yt/yt/client/api/config.h>

#include <yt/yt/ytlib/chunk_client/block.h>
#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/helpers.h>
#include <yt/yt/ytlib/chunk_client/config.h>

#include <yt/yt/client/chunk_client/read_limit.h>

#include <yt/yt/ytlib/file_client/file_ypath_proxy.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/ytlib/object_client/object_service_proxy.h>
#include <yt/yt/ytlib/object_client/object_ypath_proxy.h>
#include <yt/yt/ytlib/object_client/helpers.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/ytlib/file_client/file_chunk_reader.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/misc/guid.h>
#include <yt/yt/core/misc/async_slru_cache.h>
#include <yt/yt/core/misc/async_expiring_cache.h>

#include <yt/yt/core/ytree/yson_serializable.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NQueryClient {

using namespace NApi;
using namespace NChunkClient;
using namespace NObjectClient;
using namespace NConcurrency;
using namespace NFileClient;
using namespace NYPath;
using namespace NYTree;

using NObjectClient::TObjectServiceProxy;
using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = QueryClientLogger;

////////////////////////////////////////////////////////////////////////////////

class TCypressFunctionDescriptor
    : public NYTree::TYsonSerializable
{
public:
    TString Name;
    std::vector<TDescriptorType> ArgumentTypes;
    std::optional<TDescriptorType> RepeatedArgumentType;
    TDescriptorType ResultType;
    ECallingConvention CallingConvention;
    bool UseFunctionContext;

    TCypressFunctionDescriptor()
    {
        RegisterParameter("name", Name)
            .NonEmpty();
        RegisterParameter("argument_types", ArgumentTypes);
        RegisterParameter("result_type", ResultType);
        RegisterParameter("calling_convention", CallingConvention);
        RegisterParameter("use_function_context", UseFunctionContext)
            .Default(false);
        RegisterParameter("repeated_argument_type", RepeatedArgumentType)
            .Default();
    }

    std::vector<TType> GetArgumentsTypes() const
    {
        std::vector<TType> argumentTypes;
        for (const auto& type : ArgumentTypes) {
            argumentTypes.push_back(type.Type);
        }
        return argumentTypes;
    }
};

DECLARE_REFCOUNTED_CLASS(TCypressFunctionDescriptor)
DEFINE_REFCOUNTED_TYPE(TCypressFunctionDescriptor)

class TCypressAggregateDescriptor
    : public NYTree::TYsonSerializable
{
public:
    TString Name;
    TDescriptorType ArgumentType;
    TDescriptorType StateType;
    TDescriptorType ResultType;
    ECallingConvention CallingConvention;

    TCypressAggregateDescriptor()
    {
        RegisterParameter("name", Name)
            .NonEmpty();
        RegisterParameter("argument_type", ArgumentType);
        RegisterParameter("state_type", StateType);
        RegisterParameter("result_type", ResultType);
        RegisterParameter("calling_convention", CallingConvention);
    }
};

DECLARE_REFCOUNTED_CLASS(TCypressAggregateDescriptor)
DEFINE_REFCOUNTED_TYPE(TCypressAggregateDescriptor)

DEFINE_REFCOUNTED_TYPE(TExternalCGInfo)

////////////////////////////////////////////////////////////////////////////////

static const TString FunctionDescriptorAttribute("function_descriptor");
static const TString AggregateDescriptorAttribute("aggregate_descriptor");

////////////////////////////////////////////////////////////////////////////////

TExternalCGInfo::TExternalCGInfo()
    : NodeDirectory(New<NNodeTrackerClient::TNodeDirectory>())
{ }

////////////////////////////////////////////////////////////////////////////////

namespace {

TString GetUdfDescriptorPath(const TYPath& registryPath, const TString& functionName)
{
    return registryPath + "/" + ToYPathLiteral(functionName);
}

} // namespace

std::vector<TExternalFunctionSpec> LookupAllUdfDescriptors(
    const std::vector<std::pair<TString, TString>>& functionNames,
    const NNative::IClientPtr& client)
{
    using NObjectClient::TObjectYPathProxy;
    using NApi::EMasterChannelKind;
    using NObjectClient::FromObjectId;
    using NNodeTrackerClient::TNodeDirectory;
    using NChunkClient::TLegacyReadRange;

    std::vector<TExternalFunctionSpec> result;

    YT_LOG_DEBUG("Looking for UDFs in Cypress");

    TObjectServiceProxy proxy(client->GetMasterChannelOrThrow(EMasterChannelKind::Follower));
    auto batchReq = proxy.ExecuteBatch();

    for (const auto& item : functionNames) {
        auto path = GetUdfDescriptorPath(item.first, item.second);

        auto getReq = TYPathProxy::Get(path);
        ToProto(getReq->mutable_attributes()->mutable_keys(), std::vector<TString>{
            FunctionDescriptorAttribute,
            AggregateDescriptorAttribute
        });
        batchReq->AddRequest(getReq, "get_attributes");

        auto basicAttributesReq = TObjectYPathProxy::GetBasicAttributes(path);
        batchReq->AddRequest(basicAttributesReq, "get_basic_attributes");
    }

    auto batchRsp = WaitFor(batchReq->Invoke())
        .ValueOrThrow();

    auto getRspsOrError = batchRsp->GetResponses<TYPathProxy::TRspGet>("get_attributes");
    auto basicAttributesRspsOrError = batchRsp->GetResponses<TObjectYPathProxy::TRspGetBasicAttributes>("get_basic_attributes");

    THashMap<NObjectClient::TCellTag, std::vector<std::pair<NObjectClient::TObjectId, size_t>>> externalCellTagToInfo;
    for (int index = 0; index < std::ssize(functionNames); ++index) {
        const auto& function = functionNames[index];
        auto path = GetUdfDescriptorPath(function.first, function.second);

        auto getRspOrError = getRspsOrError[index];

        THROW_ERROR_EXCEPTION_IF_FAILED(
            getRspOrError,
            "Failed to find implementation of function %Qv at %v in Cypress",
            function.second,
            function.first);

        auto getRsp = getRspOrError
            .ValueOrThrow();

        auto basicAttrsRsp = basicAttributesRspsOrError[index]
            .ValueOrThrow();

        auto item = ConvertToNode(NYson::TYsonString(getRsp->value()));
        auto objectId = NYT::FromProto<NObjectClient::TObjectId>(basicAttrsRsp->object_id());
        auto cellTag = basicAttrsRsp->external_cell_tag();

        YT_LOG_DEBUG("Found UDF implementation in Cypress (Name: %v, Descriptor: %v)",
            function.second,
            ConvertToYsonString(item, NYson::EYsonFormat::Text).AsStringBuf());

        TExternalFunctionSpec cypressInfo;
        cypressInfo.Descriptor = item;

        result.push_back(cypressInfo);

        externalCellTagToInfo[cellTag].emplace_back(objectId, index);
    }

    for (const auto& [externalCellTag, infos] : externalCellTagToInfo) {
        auto channel = client->GetMasterChannelOrThrow(EMasterChannelKind::Follower, externalCellTag);
        TObjectServiceProxy proxy(channel);

        auto fetchBatchReq = proxy.ExecuteBatchWithRetries(client->GetNativeConnection()->GetConfig()->ChunkFetchRetries);

        for (auto [objectId, index] : infos) {
            auto fetchReq = TFileYPathProxy::Fetch(FromObjectId(objectId));
            AddCellTagToSyncWith(fetchReq, objectId);
            fetchReq->add_extension_tags(TProtoExtensionTag<NChunkClient::NProto::TMiscExt>::Value);
            ToProto(fetchReq->mutable_ranges(), std::vector<TLegacyReadRange>({TLegacyReadRange()}));
            fetchBatchReq->AddRequest(fetchReq);
        }

        auto fetchBatchRsp = WaitFor(fetchBatchReq->Invoke())
            .ValueOrThrow();

        for (size_t rspIndex = 0; rspIndex < infos.size(); ++rspIndex) {
            auto resultIndex = infos[rspIndex].second;

            auto fetchRsp = fetchBatchRsp->GetResponse<TFileYPathProxy::TRspFetch>(rspIndex)
                .ValueOrThrow();

            auto nodeDirectory = New<TNodeDirectory>();

            NChunkClient::ProcessFetchResponse(
                client,
                fetchRsp,
                externalCellTag,
                nodeDirectory,
                10000,
                std::nullopt,
                Logger,
                &result[resultIndex].Chunks);

            YT_VERIFY(!result[resultIndex].Chunks.empty());

            nodeDirectory->DumpTo(&result[resultIndex].NodeDirectory);
        }
    }

    return result;
}

void AppendUdfDescriptors(
    const TTypeInferrerMapPtr& typeInferrers,
    const TExternalCGInfoPtr& cgInfo,
    const std::vector<TString>& functionNames,
    const std::vector<TExternalFunctionSpec>& externalFunctionSpecs)
{
    YT_VERIFY(functionNames.size() == externalFunctionSpecs.size());

    YT_LOG_DEBUG("Appending UDF descriptors (Count: %v)", externalFunctionSpecs.size());

    for (size_t index = 0; index < externalFunctionSpecs.size(); ++index) {
        const auto& item = externalFunctionSpecs[index];
        const auto& descriptor = item.Descriptor;
        const auto& name = functionNames[index];

        YT_LOG_DEBUG("Appending UDF descriptor (Name: %v, Descriptor: %v)",
            name,
            ConvertToYsonString(descriptor, NYson::EYsonFormat::Text).AsStringBuf());

        if (!descriptor) {
            continue;
        }
        cgInfo->NodeDirectory->MergeFrom(item.NodeDirectory);

        const auto& attributes = descriptor->Attributes();

        auto functionDescriptor = attributes.Find<TCypressFunctionDescriptorPtr>(
            FunctionDescriptorAttribute);
        auto aggregateDescriptor = attributes.Find<TCypressAggregateDescriptorPtr>(
            AggregateDescriptorAttribute);

        if (bool(functionDescriptor) == bool(aggregateDescriptor)) {
            THROW_ERROR_EXCEPTION("Item must have either function descriptor or aggregate descriptor");
        }

        const auto& chunks = item.Chunks;

        // NB(lukyan): Aggregate initialization is not supported in GCC in this case
        TExternalFunctionImpl functionBody;
        functionBody.Name = name;
        functionBody.ChunkSpecs = chunks;

        YT_LOG_DEBUG("Appending UDF descriptor {%v}",
            MakeFormattableView(chunks, [] (TStringBuilderBase* builder, const NChunkClient::NProto::TChunkSpec& chunkSpec) {
                builder->AppendFormat("%v", FromProto<TGuid>(chunkSpec.chunk_id()));
            }));

        if (functionDescriptor) {
            YT_LOG_DEBUG("Appending function UDF descriptor %Qv", name);

            functionBody.IsAggregate = false;
            functionBody.SymbolName = functionDescriptor->Name;
            functionBody.CallingConvention = functionDescriptor->CallingConvention;
            functionBody.RepeatedArgType = functionDescriptor->RepeatedArgumentType
                ? functionDescriptor->RepeatedArgumentType->Type
                : EValueType::Null,
            functionBody.RepeatedArgIndex = int(functionDescriptor->GetArgumentsTypes().size());
            functionBody.UseFunctionContext = functionDescriptor->UseFunctionContext;

            auto typer = functionDescriptor->RepeatedArgumentType
                ? New<TFunctionTypeInferrer>(
                    std::unordered_map<TTypeArgument, TUnionType>(),
                    functionDescriptor->GetArgumentsTypes(),
                    functionDescriptor->RepeatedArgumentType->Type,
                    functionDescriptor->ResultType.Type)
                : New<TFunctionTypeInferrer>(
                    std::unordered_map<TTypeArgument, TUnionType>(),
                    functionDescriptor->GetArgumentsTypes(),
                    functionDescriptor->ResultType.Type);

            typeInferrers->emplace(name, typer);
            cgInfo->Functions.push_back(std::move(functionBody));
        }

        if (aggregateDescriptor) {
            YT_LOG_DEBUG("Appending aggregate UDF descriptor %Qv", name);

            functionBody.IsAggregate = true;
            functionBody.SymbolName = aggregateDescriptor->Name;
            functionBody.CallingConvention = aggregateDescriptor->CallingConvention;
            functionBody.RepeatedArgType = EValueType::Null;
            functionBody.RepeatedArgIndex = -1;

            auto typer = New<TAggregateTypeInferrer>(
                std::unordered_map<TTypeArgument, TUnionType>(),
                aggregateDescriptor->ArgumentType.Type,
                aggregateDescriptor->ResultType.Type,
                aggregateDescriptor->StateType.Type);

            typeInferrers->emplace(name, typer);
            cgInfo->Functions.push_back(std::move(functionBody));
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

DEFINE_REFCOUNTED_TYPE(IFunctionRegistry)

namespace {

class TCypressFunctionRegistry
    : public TAsyncExpiringCache<std::pair<TString, TString>, TExternalFunctionSpec>
    , public IFunctionRegistry
{
public:
    TCypressFunctionRegistry(
        TAsyncExpiringCacheConfigPtr config,
        TWeakPtr<NNative::IClient> client,
        IInvokerPtr invoker)
        : TAsyncExpiringCache(
            config,
            QueryClientLogger.WithTag("Cache: CypressFunctionRegistry"))
        , Client_(std::move(client))
        , Invoker_(std::move(invoker))
    { }

    TFuture<std::vector<TExternalFunctionSpec>> FetchFunctions(
        const TString& udfRegistryPath,
        const std::vector<TString>& names) override
    {
        std::vector<std::pair<TString, TString>> keys;
        for (const auto& name : names) {
            keys.emplace_back(udfRegistryPath, name);
        }
        return GetMany(keys)
            .Apply(BIND([] (std::vector<TErrorOr<TExternalFunctionSpec>> specs) {
                for (const auto& spec : specs) {
                    spec.ThrowOnError();
                }
                std::vector<TExternalFunctionSpec> result;
                result.reserve(specs.size());
                for (const auto& spec : specs) {
                    result.emplace_back(std::move(spec.Value()));
                }
                return result;
            }));
    }

private:
    const TWeakPtr<NNative::IClient> Client_;
    const IInvokerPtr Invoker_;

    TFuture<TExternalFunctionSpec> DoGet(
        const std::pair<TString, TString>& key,
        bool isPeriodicUpdate) noexcept override
    {
        return DoGetMany({key}, isPeriodicUpdate)
            .Apply(BIND([] (const std::vector<TErrorOr<TExternalFunctionSpec>>& specs) {
                return specs[0]
                    .ValueOrThrow();
            }));
    }

    TFuture<std::vector<TErrorOr<TExternalFunctionSpec>>> DoGetMany(
        const std::vector<std::pair<TString, TString>>& keys,
        bool /*isPeriodicUpdate*/) noexcept override
    {
        if (auto client = Client_.Lock()) {
            auto future = BIND(LookupAllUdfDescriptors, keys, std::move(client))
                .AsyncVia(Invoker_)
                .Run();
            return future
                .Apply(BIND([] (const std::vector<TExternalFunctionSpec>& specs) {
                    return std::vector<TErrorOr<TExternalFunctionSpec>>(specs.begin(), specs.end());
                }));
        } else {
            return MakeFuture<std::vector<TErrorOr<TExternalFunctionSpec>>>(TError("Client destroyed"));
        }
    }
};

} // namespace

////////////////////////////////////////////////////////////////////////////////

IFunctionRegistryPtr CreateFunctionRegistryCache(
    TAsyncExpiringCacheConfigPtr config,
    TWeakPtr<NNative::IClient> client,
    IInvokerPtr invoker)
{
    return New<TCypressFunctionRegistry>(
        std::move(config),
        std::move(client),
        std::move(invoker));
}

////////////////////////////////////////////////////////////////////////////////

namespace {

struct TFunctionImplKey
{
    std::vector<NChunkClient::NProto::TChunkSpec> ChunkSpecs;

    // Hasher.
    operator size_t() const;

    // Comparer.
    bool operator == (const TFunctionImplKey& other) const;
};

TFunctionImplKey::operator size_t() const
{
    size_t result = 0;

    for (const auto& spec : ChunkSpecs) {
        auto id = FromProto<TGuid>(spec.chunk_id());
        HashCombine(result, id);
    }

    return result;
}

bool TFunctionImplKey::operator == (const TFunctionImplKey& other) const
{
    if (ChunkSpecs.size() != other.ChunkSpecs.size())
        return false;

    for (int index = 0; index < std::ssize(ChunkSpecs); ++index) {
        const auto& lhs = ChunkSpecs[index];
        const auto& rhs = other.ChunkSpecs[index];

        auto leftId = FromProto<TGuid>(lhs.chunk_id());
        auto rightId = FromProto<TGuid>(rhs.chunk_id());

        if (leftId != rightId)
            return false;
    }

    return true;
}

TString ToString(const TFunctionImplKey& key)
{
    return Format("{%v}", JoinToString(key.ChunkSpecs, [] (
        TStringBuilderBase* builder,
        const NChunkClient::NProto::TChunkSpec& chunkSpec)
    {
        builder->AppendFormat("%v", FromProto<TGuid>(chunkSpec.chunk_id()));
    }));
}

class TFunctionImplCacheEntry
    : public TAsyncCacheValueBase<TFunctionImplKey, TFunctionImplCacheEntry>
{
public:
    TFunctionImplCacheEntry(const TFunctionImplKey& key, TSharedRef file)
        : TAsyncCacheValueBase(key)
        , File(std::move(file))
    { }

    TSharedRef File;
};

typedef TIntrusivePtr<TFunctionImplCacheEntry> TFunctionImplCacheEntryPtr;

} // namespace

class TFunctionImplCache
    : public TAsyncSlruCacheBase<TFunctionImplKey, TFunctionImplCacheEntry>
{
public:
    TFunctionImplCache(
        const TSlruCacheConfigPtr& config,
        TWeakPtr<NNative::IClient> client)
        : TAsyncSlruCacheBase(config)
        , Client_(client)
    { }

    TSharedRef DoFetch(
        const TFunctionImplKey& key,
        TNodeDirectoryPtr nodeDirectory,
        const TClientChunkReadOptions& chunkReadOptions)
    {
        auto client = Client_.Lock();
        YT_VERIFY(client);
        auto chunks = key.ChunkSpecs;

        YT_LOG_DEBUG("Downloading implementation for UDF function (Chunks: %v, ReadSessionId: %v)",
            key,
            chunkReadOptions.ReadSessionId);

        auto reader = NFileClient::CreateFileMultiChunkReader(
            New<NApi::TFileReaderConfig>(),
            New<NChunkClient::TMultiChunkReaderOptions>(),
            client,
            /*localDescriptor*/ {},
            /*partitionTag*/ std::nullopt,
            client->GetNativeConnection()->GetBlockCache(),
            client->GetNativeConnection()->GetChunkMetaCache(),
            std::move(nodeDirectory),
            chunkReadOptions,
            std::move(chunks));

        std::vector<TSharedRef> blocks;
        while (true) {
            NChunkClient::TBlock block;
            if (!reader->ReadBlock(&block)) {
                break;
            }

            if (block.Data) {
                blocks.push_back(std::move(block.Data));
            }

            WaitFor(reader->GetReadyEvent())
                .ThrowOnError();
        }

        i64 size = GetByteSize(blocks);
        YT_VERIFY(size);
        auto file = TSharedMutableRef::Allocate(size);
        auto memoryOutput = TMemoryOutput(file.Begin(), size);

        for (const auto& block : blocks) {
            memoryOutput.Write(block.Begin(), block.Size());
        }

        return file;
    }

    TFuture<TFunctionImplCacheEntryPtr> FetchImplementation(
        const TFunctionImplKey& key,
        TNodeDirectoryPtr nodeDirectory,
        const TClientChunkReadOptions& chunkReadOptions)
    {
        auto cookie = BeginInsert(key);
        if (!cookie.IsActive()) {
            return cookie.GetValue();
        }

        return BIND([=, this_ = MakeStrong(this), cookie = std::move(cookie)] (
                const TFunctionImplKey& key,
                TNodeDirectoryPtr nodeDirectory,
                const TClientChunkReadOptions& chunkReadOptions) mutable
            {
                try {
                    auto file = DoFetch(key, std::move(nodeDirectory), chunkReadOptions);
                    cookie.EndInsert(New<TFunctionImplCacheEntry>(key, file));
                } catch (const std::exception& ex) {
                    cookie.Cancel(TError(ex).Wrap("Failed to download function implementation"));
                }

                return cookie.GetValue();
            })
            .AsyncVia(GetCurrentInvoker())
            .Run(key, nodeDirectory, chunkReadOptions);
    }

private:
    const TWeakPtr<NNative::IClient> Client_;

};

DEFINE_REFCOUNTED_TYPE(TFunctionImplCache)

TFunctionImplCachePtr CreateFunctionImplCache(
    const TSlruCacheConfigPtr& config,
    TWeakPtr<NNative::IClient> client)
{
    return New<TFunctionImplCache>(config, client);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

TSharedRef GetImplFingerprint(const std::vector<NChunkClient::NProto::TChunkSpec>& chunks)
{
    auto size = chunks.size();
    auto fingerprint = TSharedMutableRef::Allocate(2 * sizeof(ui64) * size);
    auto memoryOutput = TMemoryOutput(fingerprint.Begin(), fingerprint.Size());

    for (const auto& chunk : chunks) {
        auto id = FromProto<TGuid>(chunk.chunk_id());
        memoryOutput.Write(id.Parts64, 2 * sizeof(ui64));
    }

    return fingerprint;
}

void AppendFunctionImplementation(
    const TFunctionProfilerMapPtr& functionProfilers,
    const TAggregateProfilerMapPtr& aggregateProfilers,
    const TExternalFunctionImpl& function,
    const TSharedRef& impl)
{
    YT_VERIFY(!impl.Empty());

    const auto& name = function.Name;

    if (function.IsAggregate) {
        aggregateProfilers->emplace(name, New<TExternalAggregateCodegen>(
            name,
            impl,
            function.CallingConvention,
            false,
            GetImplFingerprint(function.ChunkSpecs)));
    } else {
        functionProfilers->emplace(name, New<TExternalFunctionCodegen>(
            name,
            function.SymbolName,
            impl,
            function.CallingConvention,
            function.RepeatedArgType,
            function.RepeatedArgIndex,
            function.UseFunctionContext,
            GetImplFingerprint(function.ChunkSpecs)));
    }
}

} // namespace

void FetchFunctionImplementationsFromCypress(
    const TFunctionProfilerMapPtr& functionProfilers,
    const TAggregateProfilerMapPtr& aggregateProfilers,
    const TConstExternalCGInfoPtr& externalCGInfo,
    const TFunctionImplCachePtr& cache,
    const TClientChunkReadOptions& chunkReadOptions)
{
    std::vector<TFuture<TFunctionImplCacheEntryPtr>> asyncResults;

    for (const auto& function : externalCGInfo->Functions) {
        const auto& name = function.Name;

        YT_LOG_DEBUG("Fetching UDF implementation (Name: %v, ReadSessionId: %v)",
            name,
            chunkReadOptions.ReadSessionId);

        TFunctionImplKey key;
        key.ChunkSpecs = function.ChunkSpecs;

        asyncResults.push_back(cache->FetchImplementation(key, externalCGInfo->NodeDirectory, chunkReadOptions));
    }

    auto results = WaitFor(AllSucceeded(asyncResults))
        .ValueOrThrow();

    for (size_t index = 0; index < externalCGInfo->Functions.size(); ++index) {
        const auto& function = externalCGInfo->Functions[index];
        AppendFunctionImplementation(
            functionProfilers,
            aggregateProfilers,
            function,
            results[index]->File);
    }
}

void FetchFunctionImplementationsFromFiles(
    const TFunctionProfilerMapPtr& functionProfilers,
    const TAggregateProfilerMapPtr& aggregateProfilers,
    const TConstExternalCGInfoPtr& externalCGInfo,
    const TString& rootPath)
{
     for (const auto& function : externalCGInfo->Functions) {
        const auto& name = function.Name;

        YT_LOG_DEBUG("Fetching UDF implementation (Name: %v)", name);

        auto path = rootPath + "/" + function.Name;
        auto file = TUnbufferedFileInput(path);
        auto impl = TSharedRef::FromString(file.ReadAll());

        AppendFunctionImplementation(functionProfilers, aggregateProfilers, function, impl);
    }
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TDescriptorType& value, NYson::IYsonConsumer* consumer)
{
    using namespace NYTree;

    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("tag").Value(static_cast<ETypeCategory>(value.Type.index()))
            .Do([&] (TFluentMap fluent) {
                Visit(value.Type,
                    [&] (const auto& v) { fluent.Item("value").Value(v); });
            })
        .EndMap();
}

void Deserialize(TDescriptorType& value, NYTree::INodePtr node)
{
    using namespace NYTree;

    auto mapNode = node->AsMap();

    auto tagNode = mapNode->GetChildOrThrow("tag");
    ETypeCategory tag;
    Deserialize(tag, tagNode);

    auto valueNode = mapNode->GetChildOrThrow("value");
    switch (tag) {
        case ETypeCategory::TypeArgument: {
            TTypeArgument type;
            Deserialize(type, valueNode);
            value.Type = type;
            break;
        }
        case ETypeCategory::UnionType: {
            TUnionType type;
            Deserialize(type, valueNode);
            value.Type = type;
            break;
        }
        case ETypeCategory::ConcreteType: {
            EValueType type;
            Deserialize(type, valueNode);
            value.Type = type;
            break;
        }
        default:
            YT_ABORT();
    }
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TExternalFunctionImpl* proto, const TExternalFunctionImpl& object)
{
    proto->set_is_aggregate(object.IsAggregate);
    proto->set_name(object.Name);
    proto->set_symbol_name(object.SymbolName);
    proto->set_calling_convention(int(object.CallingConvention));
    ToProto(proto->mutable_chunk_specs(), object.ChunkSpecs);

    TDescriptorType descriptorType;
    descriptorType.Type = object.RepeatedArgType;

    proto->set_repeated_arg_type(ConvertToYsonString(descriptorType).ToString());
    proto->set_repeated_arg_index(object.RepeatedArgIndex);
    proto->set_use_function_context(object.UseFunctionContext);
}

void FromProto(TExternalFunctionImpl* original, const NProto::TExternalFunctionImpl& serialized)
{
    original->IsAggregate = serialized.is_aggregate();
    original->Name = serialized.name();
    original->SymbolName = serialized.symbol_name();
    original->CallingConvention = ECallingConvention(serialized.calling_convention());
    original->ChunkSpecs = FromProto<std::vector<NChunkClient::NProto::TChunkSpec>>(serialized.chunk_specs());
    original->RepeatedArgType = ConvertTo<TDescriptorType>(NYson::TYsonString(serialized.repeated_arg_type())).Type;
    original->RepeatedArgIndex = serialized.repeated_arg_index();
    original->UseFunctionContext = serialized.use_function_context();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
