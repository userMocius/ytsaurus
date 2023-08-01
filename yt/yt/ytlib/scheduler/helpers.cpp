#include "helpers.h"

#include <yt/yt/ytlib/api/native/config.h>
#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/connection.h>

#include <yt/yt/ytlib/object_client/object_service_proxy.h>

#include <yt/yt/ytlib/cypress_client/cypress_ypath_proxy.h>

#include <yt/yt/client/security_client/acl.h>

#include <yt/yt/client/api/operation_archive_schema.h>
#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/client/table_client/row_buffer.h>

#include <yt/yt/library/re2/re2.h>

namespace NYT::NScheduler {

using namespace NApi;
using namespace NConcurrency;
using namespace NYTree;
using namespace NYPath;
using namespace NYson;
using namespace NChunkClient;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NTransactionClient;
using namespace NSecurityClient;
using namespace NLogging;
using namespace NRpc;
using namespace NJobTrackerClient::NProto;
using namespace NTracing;

////////////////////////////////////////////////////////////////////////////////

TYPath GetPoolTreesLockPath()
{
    return "//sys/scheduler/pool_trees_lock";
}

TYPath GetOperationsPath()
{
    return "//sys/operations";
}

TYPath GetOperationPath(TOperationId operationId)
{
    int hashByte = operationId.Parts32[0] & 0xff;
    return
        "//sys/operations/" +
        Format("%02x", hashByte) +
        "/" +
        ToYPathLiteral(ToString(operationId));
}

TYPath GetJobsPath(TOperationId operationId)
{
    return
        GetOperationPath(operationId) +
        "/jobs";
}

TYPath GetJobPath(TOperationId operationId, TJobId jobId)
{
    return
        GetJobsPath(operationId) + "/" +
        ToYPathLiteral(ToString(jobId));
}

TYPath GetStderrPath(TOperationId operationId, TJobId jobId)
{
    return
        GetJobPath(operationId, jobId)
        + "/stderr";
}

TYPath GetFailContextPath(TOperationId operationId, TJobId jobId)
{
    return
        GetJobPath(operationId, jobId)
        + "/fail_context";
}

TYPath GetSchedulerOrchidOperationPath(TOperationId operationId)
{
    return
        "//sys/scheduler/orchid/scheduler/operations/" +
        ToYPathLiteral(ToString(operationId));
}

TYPath GetSchedulerOrchidAliasPath(const TString& alias)
{
    return
        "//sys/scheduler/orchid/scheduler/operations/" +
        ToYPathLiteral(alias);
}

TYPath GetControllerAgentOrchidOperationPath(
    TStringBuf controllerAgentAddress,
    TOperationId operationId)
{
    return
        "//sys/controller_agents/instances/" +
        ToYPathLiteral(controllerAgentAddress) +
        "/orchid/controller_agent/operations/" +
        ToYPathLiteral(ToString(operationId));
}

const TYPath& GetUserToDefaultPoolMapPath()
{
    static const TYPath path = "//sys/scheduler/user_to_default_pool";
    return path;
}

std::optional<TString> FindControllerAgentAddressFromCypress(
    TOperationId operationId,
    const NApi::NNative::IClientPtr& client)
{
    using NYT::ToProto;

    static const std::vector<TString> attributes = {"controller_agent_address"};

    auto proxy = CreateObjectServiceReadProxy(client, EMasterChannelKind::Follower);
    auto batchReq = proxy.ExecuteBatch();

    {
        auto req = TYPathProxy::Get(GetOperationPath(operationId) + "/@controller_agent_address");
        ToProto(req->mutable_attributes()->mutable_keys(), attributes);
        batchReq->AddRequest(req, "get_controller_agent_address");
    }

    auto batchRsp = WaitFor(batchReq->Invoke())
        .ValueOrThrow();

    auto responseOrError = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_controller_agent_address");
    if (responseOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
        return std::nullopt;
    }

    const auto& response = responseOrError.ValueOrThrow();
    return ConvertTo<TString>(TYsonString(response->value()));
}

TYPath GetSnapshotPath(TOperationId operationId)
{
    return
        GetOperationPath(operationId)
        + "/snapshot";
}

TYPath GetSecureVaultPath(TOperationId operationId)
{
    return
        GetOperationPath(operationId)
        + "/secure_vault";
}

NYPath::TYPath GetJobPath(
    TOperationId operationId,
    TJobId jobId,
    const TString& resourceName)
{
    TString suffix;
    if (!resourceName.empty()) {
        suffix = "/" + resourceName;
    }

    return GetJobPath(operationId, jobId) + suffix;
}

const TYPath& GetClusterNamePath()
{
    static const TYPath path = "//sys/@cluster_name";
    return path;
}

const TYPath& GetOperationsArchiveOrderedByIdPath()
{
    static const TYPath path = "//sys/operations_archive/ordered_by_id";
    return path;
}

const TYPath& GetOperationsArchiveOrderedByStartTimePath()
{
    static const TYPath path = "//sys/operations_archive/ordered_by_start_time";
    return path;
}

const TYPath& GetOperationsArchiveOperationAliasesPath()
{
    static const TYPath path = "//sys/operations_archive/operation_aliases";
    return path;
}

const TYPath& GetOperationsArchiveVersionPath()
{
    static const TYPath path = "//sys/operations_archive/@version";
    return path;
}

const TYPath& GetOperationsArchiveJobsPath()
{
    static const TYPath path = "//sys/operations_archive/jobs";
    return path;
}

const TYPath& GetOperationsArchiveJobSpecsPath()
{
    static const TYPath path = "//sys/operations_archive/job_specs";
    return path;
}

const TYPath& GetOperationsArchiveJobStderrsPath()
{
    static const TYPath path = "//sys/operations_archive/stderrs";
    return path;
}

const TYPath& GetOperationsArchiveJobProfilesPath()
{
    static const TYPath path = "//sys/operations_archive/job_profiles";
    return path;
}

const TYPath& GetOperationsArchiveJobFailContextsPath()
{
    static const TYPath path = "//sys/operations_archive/fail_contexts";
    return path;
}

const NYPath::TYPath& GetOperationsArchiveOperationIdsPath()
{
    static const TYPath path = "//sys/operations_archive/operation_ids";
    return path;
}

bool IsOperationFinished(EOperationState state)
{
    return
        state == EOperationState::Completed ||
        state == EOperationState::Aborted ||
        state == EOperationState::Failed;
}

bool IsOperationFinishing(EOperationState state)
{
    return
        state == EOperationState::Completing ||
        state == EOperationState::Aborting ||
        state == EOperationState::Failing;
}

bool IsOperationInProgress(EOperationState state)
{
    return
        state == EOperationState::Starting ||
        state == EOperationState::WaitingForAgent ||
        state == EOperationState::Orphaned ||
        state == EOperationState::Initializing ||
        state == EOperationState::Preparing ||
        state == EOperationState::Materializing ||
        state == EOperationState::Pending ||
        state == EOperationState::ReviveInitializing ||
        state == EOperationState::Reviving ||
        state == EOperationState::RevivingJobs ||
        state == EOperationState::Running ||
        state == EOperationState::Completing ||
        state == EOperationState::Failing ||
        state == EOperationState::Aborting;
}

bool IsSchedulingReason(EAbortReason reason)
{
    return reason > EAbortReason::SchedulingFirst && reason < EAbortReason::SchedulingLast;
}

bool IsNonSchedulingReason(EAbortReason reason)
{
    return reason < EAbortReason::SchedulingFirst;
}

bool IsSentinelReason(EAbortReason reason)
{
    return
        reason == EAbortReason::SchedulingFirst ||
        reason == EAbortReason::SchedulingLast;
}

TError GetSchedulerTransactionsAbortedError(const std::vector<TTransactionId>& transactionIds)
{
    return TError(
        NTransactionClient::EErrorCode::NoSuchTransaction,
        "Scheduler transactions %v have expired or were aborted",
        transactionIds);
}

TError GetUserTransactionAbortedError(TTransactionId transactionId)
{
    return TError(
        NTransactionClient::EErrorCode::NoSuchTransaction,
        "User transaction %v has expired or was aborted",
        transactionId);
}

////////////////////////////////////////////////////////////////////////////////

void ValidateOperationAccess(
    const std::optional<TString>& user,
    TOperationId operationId,
    TJobId jobId,
    EPermissionSet permissionSet,
    const NSecurityClient::TSerializableAccessControlList& acl,
    const NNative::IClientPtr& client,
    const TLogger& logger)
{
    const auto& Logger = logger;

    TCheckPermissionByAclOptions options;
    options.IgnoreMissingSubjects = true;
    auto aclNode = ConvertToNode(acl);

    std::vector<TFuture<TCheckPermissionByAclResult>> futures;
    for (auto permission : TEnumTraits<EPermission>::GetDomainValues()) {
        if (Any(permission & permissionSet)) {
            futures.push_back(client->CheckPermissionByAcl(user, permission, aclNode, options));
        }
    }

    auto results = WaitFor(AllSucceeded(futures))
        .ValueOrThrow();

    if (!results.empty() && !results.front().MissingSubjects.empty()) {
        YT_LOG_DEBUG("Operation has missing subjects in ACL (OperationId: %v, JobId: %v, MissingSubjects: %v)",
            operationId ? ToString(operationId) : "<unknown>",
            jobId ? ToString(jobId) : "<unknown>",
            results.front().MissingSubjects);
    }

    for (const auto& result : results) {
        if (result.Action != ESecurityAction::Allow) {
            auto error = TError(
                NSecurityClient::EErrorCode::AuthorizationError,
                "Operation access denied")
                << TErrorAttribute("user", user.value_or(GetCurrentAuthenticationIdentity().User))
                << TErrorAttribute("required_permissions", permissionSet)
                << TErrorAttribute("acl", acl);
            if (operationId) {
                error = error << TErrorAttribute("operation_id", operationId);
            }
            if (jobId) {
                error = error << TErrorAttribute("job_id", jobId);
            }
            THROW_ERROR error;
        }
    }

    YT_LOG_DEBUG("Operation access successfully validated (OperationId: %v, JobId: %v, User: %v, Permissions: %v, Acl: %v)",
        operationId ? ToString(operationId) : "<unknown>",
        jobId ? ToString(jobId) : "<unknown>",
        user,
        permissionSet,
        ConvertToYsonString(acl, EYsonFormat::Text));
}

////////////////////////////////////////////////////////////////////////////////

static TUnversionedRow CreateOperationKey(
    const TOperationId& operationId,
    const TOrderedByIdTableDescriptor::TIndex& index,
    const TRowBufferPtr& rowBuffer)
{
    auto key = rowBuffer->AllocateUnversioned(2);
    key[0] = MakeUnversionedUint64Value(operationId.Parts64[0], index.IdHi);
    key[1] = MakeUnversionedUint64Value(operationId.Parts64[1], index.IdLo);
    return key;
}

TErrorOr<IUnversionedRowsetPtr> LookupOperationsInArchive(
    const NNative::IClientPtr& client,
    const std::vector<TOperationId>& ids,
    const NTableClient::TColumnFilter& columnFilter,
    std::optional<TDuration> timeout)
{
    static const TOrderedByIdTableDescriptor tableDescriptor;
    auto rowBuffer = New<TRowBuffer>();
    std::vector<TUnversionedRow> keys;
    keys.reserve(ids.size());
    for (const auto& id : ids) {
        keys.push_back(CreateOperationKey(id, tableDescriptor.Index, rowBuffer));
    }

    TLookupRowsOptions lookupOptions;
    lookupOptions.ColumnFilter = columnFilter;
    lookupOptions.Timeout = timeout;
    lookupOptions.KeepMissingRows = true;
    return WaitFor(client->LookupRows(
        GetOperationsArchiveOrderedByIdPath(),
        tableDescriptor.NameTable,
        MakeSharedRange(std::move(keys), std::move(rowBuffer)),
        lookupOptions));
}

////////////////////////////////////////////////////////////////////////////////

const int PoolNameMaxLength = 100;

TString StrictPoolNameRegexSymbols = "-_a-z0-9";
TString NonStrictPoolNameRegexSymbols = StrictPoolNameRegexSymbols + ":A-Z";

TEnumIndexedVector<EPoolNameValidationLevel, TString> RegexStrings = {
    "[" + NonStrictPoolNameRegexSymbols + "]+",
    "[" + StrictPoolNameRegexSymbols + "]+",
};

TEnumIndexedVector<EPoolNameValidationLevel, TIntrusivePtr<NRe2::TRe2>> Regexes = {
    New<NRe2::TRe2>(RegexStrings[EPoolNameValidationLevel::NonStrict]),
    New<NRe2::TRe2>(RegexStrings[EPoolNameValidationLevel::Strict]),
};

TError CheckPoolName(const TString& poolName, EPoolNameValidationLevel validationLevel)
{
    if (poolName == RootPoolName) {
        return TError("Pool name cannot be equal to root pool name")
            << TErrorAttribute("root_pool_name", RootPoolName);
    }

    if (poolName.length() > PoolNameMaxLength) {
        return TError("Pool name %Qv is too long", poolName)
            << TErrorAttribute("length", poolName.length())
            << TErrorAttribute("max_length", PoolNameMaxLength);
    }

    const auto& regex = Regexes[validationLevel];

    if (!NRe2::TRe2::FullMatch(NRe2::StringPiece(poolName), *regex)) {
        const auto& regexString = RegexStrings[validationLevel];
        return TError("Pool name %Qv must match regular expression %Qv", poolName, regexString);
    }

    return TError();
}

void ValidatePoolName(const TString& poolName, EPoolNameValidationLevel validationLevel)
{
    CheckPoolName(poolName, validationLevel).ThrowOnError();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler

