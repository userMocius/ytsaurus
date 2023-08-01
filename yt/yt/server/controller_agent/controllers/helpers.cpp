#include "helpers.h"
#include "aggregated_job_statistics.h"
#include "config.h"
#include "table.h"
#include "job_info.h"

#include <yt/yt/ytlib/chunk_client/data_source.h>
#include <yt/yt/ytlib/chunk_client/data_sink.h>

#include <yt/yt/ytlib/object_client/object_service_proxy.h>

#include <yt/yt/ytlib/controller_agent/proto/output_result.pb.h>

#include <yt/yt/client/table_client/row_buffer.h>

#include <util/string/split.h>

namespace NYT::NControllerAgent::NControllers {

using namespace NApi;
using namespace NChunkClient;
using namespace NChunkPools;
using namespace NConcurrency;
using namespace NTableClient;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;

using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

const static auto& Logger = ControllerLogger;

////////////////////////////////////////////////////////////////////////////////

TBoundaryKeys BuildBoundaryKeysFromOutputResult(
    const NProto::TOutputResult& boundaryKeys,
    const TOutputStreamDescriptorPtr& streamDescriptor,
    const TRowBufferPtr& rowBuffer)
{
    YT_VERIFY(!boundaryKeys.empty());
    YT_VERIFY(boundaryKeys.sorted());
    YT_VERIFY(!streamDescriptor->TableWriterOptions->ValidateUniqueKeys || boundaryKeys.unique_keys());

    auto trimAndCaptureKey = [&] (const TLegacyOwningKey& key) {
        int limit = streamDescriptor->TableUploadOptions.TableSchema->GetKeyColumnCount();
        if (key.GetCount() > limit) {
            // NB: This can happen for a teleported chunk from a table with a wider key in sorted (but not unique_keys) mode.
            YT_VERIFY(!streamDescriptor->TableWriterOptions->ValidateUniqueKeys);
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), limit)), limit);
        } else {
            return TKey::FromRowUnchecked(rowBuffer->CaptureRow(MakeRange(key.Begin(), key.GetCount())), key.GetCount());
        }
    };

    return TBoundaryKeys {
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.min())),
        trimAndCaptureKey(FromProto<TLegacyOwningKey>(boundaryKeys.max())),
    };
}

////////////////////////////////////////////////////////////////////////////////

TDataSourceDirectoryPtr BuildDataSourceDirectoryFromInputTables(const std::vector<TInputTablePtr>& inputTables)
{
    auto dataSourceDirectory = New<TDataSourceDirectory>();
    for (const auto& inputTable : inputTables) {
        auto dataSource = (inputTable->Dynamic && inputTable->Schema->IsSorted())
            ? MakeVersionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->Path.GetTimestamp().value_or(AsyncLastCommittedTimestamp),
                inputTable->Path.GetRetentionTimestamp().value_or(NullTimestamp),
                inputTable->ColumnRenameDescriptors)
            : MakeUnversionedDataSource(
                inputTable->GetPath(),
                inputTable->Schema,
                inputTable->Path.GetColumns(),
                inputTable->OmittedInaccessibleColumns,
                inputTable->ColumnRenameDescriptors);

        dataSource.SetObjectId(inputTable->ObjectId);
        dataSource.SetAccount(inputTable->Account);
        dataSource.SetForeign(inputTable->IsForeign());
        dataSourceDirectory->DataSources().push_back(dataSource);
    }

    return dataSourceDirectory;
}

NChunkClient::TDataSink BuildDataSinkFromOutputTable(const TOutputTablePtr& outputTable)
{
    TDataSink dataSink;
    dataSink.SetPath(outputTable->GetPath());
    dataSink.SetObjectId(outputTable->ObjectId);
    dataSink.SetAccount(outputTable->Account);
    return dataSink;
}

TDataSinkDirectoryPtr BuildDataSinkDirectoryFromOutputTables(const std::vector<TOutputTablePtr>& outputTables)
{
    auto dataSinkDirectory = New<TDataSinkDirectory>();
    dataSinkDirectory->DataSinks().reserve(outputTables.size());
    for (const auto& outputTable : outputTables) {
        dataSinkDirectory->DataSinks().push_back(BuildDataSinkFromOutputTable(outputTable));
    }
    return dataSinkDirectory;
}

NChunkClient::TDataSinkDirectoryPtr BuildDataSinkDirectoryWithAutoMerge(
    const std::vector<TOutputTablePtr>& outputTables,
    const std::vector<bool>& autoMergeEnabled,
    const std::optional<TString>& intermediateAccountName)
{
    auto dataSinkDirectory = New<TDataSinkDirectory>();
    dataSinkDirectory->DataSinks().reserve(outputTables.size());
    YT_VERIFY(ssize(outputTables) == ssize(autoMergeEnabled));
    for (int index = 0; index < ssize(outputTables); ++index) {
        const auto& outputTable = outputTables[index];
        if (autoMergeEnabled[index]) {
            auto& dataSink = dataSinkDirectory->DataSinks().emplace_back();
            dataSink.SetPath(GetIntermediatePath(index));
            dataSink.SetAccount(intermediateAccountName ? intermediateAccountName : outputTable->Account);
        } else {
            dataSinkDirectory->DataSinks().push_back(BuildDataSinkFromOutputTable(outputTable));
        }
    }
    return dataSinkDirectory;
}

std::vector<TInputStreamDescriptorPtr> BuildInputStreamDescriptorsFromOutputStreamDescriptors(
    const std::vector<TOutputStreamDescriptorPtr>& outputStreamDescriptors)
{
    std::vector<TInputStreamDescriptorPtr> inputStreamDescriptors;
    inputStreamDescriptors.reserve(outputStreamDescriptors.size());

    for (const auto& descriptor : outputStreamDescriptors) {
        inputStreamDescriptors.push_back(TInputStreamDescriptor::FromOutputStreamDescriptor(descriptor));
    }

    return inputStreamDescriptors;
}


////////////////////////////////////////////////////////////////////////////////

void TControllerFeatures::AddSingular(TStringBuf name, double value)
{
    Features_[name] += value;
}

void TControllerFeatures::AddSingular(const TString& name, const INodePtr& node)
{
    switch (node->GetType()) {
        case ENodeType::Map:
            for (const auto& [key, child] : node->AsMap()->GetChildren()) {
                AddSingular(name + "." + key, child);
            }
            break;
        case ENodeType::Int64:
            AddSingular(name, node->AsInt64()->GetValue());
            break;
        case ENodeType::Uint64:
            AddSingular(name, node->AsUint64()->GetValue());
            break;
        case ENodeType::Double:
            AddSingular(name, node->AsDouble()->GetValue());
            break;
        case ENodeType::Boolean:
            AddSingular(name, node->AsBoolean()->GetValue());
            break;
        default:
            YT_LOG_FATAL("Unexpected type as controller feature (Type: %v)",
                node->GetType());
            break;
    }
}

void TControllerFeatures::AddCounted(TStringBuf name, double value)
{
    TString sumFeature{name};
    sumFeature += ".sum";
    Features_[sumFeature] += value;
    TString countFeature{name};
    countFeature += ".count";
    Features_[countFeature] += 1;
}

void TControllerFeatures::CalculateJobSatisticsAverage()
{
    static const TString SumSuffix = ".sum";
    static const TString CountSuffix = ".count";
    static const TString AvgSuffix = ".avg";
    static const TString JobStatisticsPrefix = "job_statistics.";
    for (const auto& [sumFeature, sum] : Features_) {
        if (sumFeature.StartsWith(JobStatisticsPrefix) && sumFeature.EndsWith(SumSuffix)) {
            auto feature = sumFeature;
            feature.resize(std::ssize(feature) - std::ssize(SumSuffix));
            auto countFeature = feature + CountSuffix;
            auto avgFeature = feature + AvgSuffix;
            auto it = Features_.find(countFeature);
            if (it != Features_.end() && it->second != 0) {
                Features_[avgFeature] = sum / it->second;
            }
        }
    }
}

void Serialize(const TControllerFeatures& features, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer).BeginMap()
        .Item("tags").Value(features.Tags_)
        .Item("features").Value(features.Features_)
    .EndMap();
}

NTableClient::TTableReaderOptionsPtr CreateTableReaderOptions(const NScheduler::TJobIOConfigPtr& ioConfig)
{
    auto options = New<NTableClient::TTableReaderOptions>();
    options->EnableRowIndex = ioConfig->ControlAttributes->EnableRowIndex;
    options->EnableTableIndex = ioConfig->ControlAttributes->EnableTableIndex;
    options->EnableRangeIndex = ioConfig->ControlAttributes->EnableRangeIndex;
    options->EnableTabletIndex = ioConfig->ControlAttributes->EnableTabletIndex;
    return options;
}

////////////////////////////////////////////////////////////////////////////////

void UpdateAggregatedJobStatistics(
    TAggregatedJobStatistics& targetStatistics,
    const TJobStatisticsTags& tags,
    const TStatistics& jobStatistics,
    const TStatistics& controllerStatistics,
    int customStatisticsLimit,
    bool* isLimitExceeded)
{
    targetStatistics.AppendStatistics(controllerStatistics, tags);

    if (targetStatistics.CalculateCustomStatisticsCount() > customStatisticsLimit) {
        // Limit is already exceeded, so truncate the statistics.
        auto jobStatisticsCopy = jobStatistics;
        jobStatisticsCopy.RemoveRangeByPrefix("/custom");
        targetStatistics.AppendStatistics(jobStatisticsCopy, tags);
    } else {
        targetStatistics.AppendStatistics(jobStatistics, tags);
    }

    // NB. We need the second check of custom statistics count to ensure that the limit was not
    // violated after the update.
    *isLimitExceeded = targetStatistics.CalculateCustomStatisticsCount() > customStatisticsLimit;
}

////////////////////////////////////////////////////////////////////////////////

std::vector<TRichYPath> GetLayerPathsFromDockerImage(
    NNative::IClientPtr client,
    const TString& dockerImage)
{
    try {
        TStringBuf image;
        TStringBuf tag;

        if (!StringSplitter(dockerImage).Split(':').Limit(2).TryCollectInto(&image, &tag)) {
            image = dockerImage;
            tag = "latest";
        }

        auto tagsPath = TYPath::Join("//", image, "/_tags");

        auto proxy = NObjectClient::CreateObjectServiceReadProxy(client, EMasterChannelKind::Follower);
        auto req = TYPathProxy::Get(tagsPath);
        auto rspOrError = WaitFor(proxy.Execute(req));

        if (!rspOrError.IsOK()) {
            THROW_ERROR_EXCEPTION("Failed to read tags from %Qv", tagsPath)
                << rspOrError;
        }

        auto rspTags = ConvertToNode(TYsonString(rspOrError.ValueOrThrow()->value()));
        if (!rspTags || rspTags->GetType() != ENodeType::Map) {
            THROW_ERROR_EXCEPTION("Tags document %Qv is not a map", tagsPath);
        }

        auto rspTag = rspTags->AsMap()->FindChild(TString(tag));
        if (!rspTag) {
            THROW_ERROR_EXCEPTION(
                "No tag %Qv in %Qv, available tags are %v",
                tag,
                tagsPath,
                rspTags->AsMap()->GetKeys());
        }

        return ConvertTo<std::vector<TRichYPath>>(rspTag);
    } catch (const std::exception& exc) {
        THROW_ERROR_EXCEPTION("Failed to load docker image %Qv", dockerImage)
            << exc;
    }
}

////////////////////////////////////////////////////////////////////////////////

IAttributeDictionaryPtr GetNetworkProject(
    NApi::NNative::IClientPtr client,
    TString authenticatedUser,
    TString networkProject)
{
    const auto networkProjectPath = "//sys/network_projects/" + ToYPathLiteral(networkProject);
    auto checkPermissionRsp = WaitFor(client->CheckPermission(authenticatedUser, networkProjectPath, EPermission::Use))
        .ValueOrThrow();
    if (checkPermissionRsp.Action == NSecurityClient::ESecurityAction::Deny) {
        THROW_ERROR_EXCEPTION("User %Qv is not allowed to use network project %Qv",
            authenticatedUser,
            networkProject);
    }

    TGetNodeOptions options{
        .Attributes = TAttributeFilter({"project_id", "enable_nat64", "disable_network"})
    };
    auto networkProjectNode = ConvertToNode(WaitFor(client->GetNode(networkProjectPath, options))
        .ValueOrThrow());
    return networkProjectNode->Attributes().Clone();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent::NControllers
