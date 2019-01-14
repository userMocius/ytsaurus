#include "clickhouse.h"

#include "bootstrap.h"
#include "config.h"
#include "coordinator.h"

#include <yt/ytlib/auth/token_authenticator.h>

#include <yt/client/api/client.h>

#include <yt/core/http/client.h>

#include <yt/core/logging/log.h>

#include <library/string_utils/base64/base64.h>

#include <util/string/cgiparam.h>
#include <util/string/vector.h>

#include <util/random/random.h>

namespace NYT::NHttpProxy {

using namespace NConcurrency;
using namespace NHttp;
using namespace NYTree;
using namespace NLogging;
using namespace NYPath;

////////////////////////////////////////////////////////////////////////////////

TLogger ClickHouseLogger("ClickHouseProxy");

/////////////////////////////////////////////////////////////////////////////

class TClickHouseContext
    : public TIntrinsicRefCounted
{
public:
    TClickHouseContext(
        const IRequestPtr& req,
        const IResponseWriterPtr& rsp,
        const TClickHouseConfigPtr& config,
        const NAuth::ITokenAuthenticatorPtr& tokenAuthenticator,
        const NApi::IClientPtr& client,
        const NHttp::IClientPtr& httpClient)
        : Logger(TLogger(ClickHouseLogger).AddTag("RequestId: %v", req->GetRequestId()))
        , Request_(req)
        , Response_(rsp)
        , Config_(config)
        , TokenAuthenticator_(tokenAuthenticator)
        , Client_(client)
        , HttpClient_(httpClient)
    { }

    bool TryPrepare()
    {
        try {
            CgiParameters_ = TCgiParameters(Request_->GetUrl().RawQuery);

            if (!TryProcessHeaders()) {
                return false;
            }

            if (!TryAuthenticate()) {
                return false;
            }

            CliqueId_ = CgiParameters_.Get("database");

            if (CliqueId_.empty()) {
                ReplyWithError(
                    EStatusCode::NotFound,
                    TError("Clique id or alias should be specified using the `database` CGI parameter"));
            }

            if (CliqueId_.StartsWith("*")) {
                if (!TryResolveAlias()) {
                    return false;
                }
            }

            YT_LOG_DEBUG("Clique id parsed (CliqueId: %v)", CliqueId_);

            if (!TryPickRandomInstance()) {
                return false;
            }

            YT_LOG_DEBUG("Forwarding query to a randomly chosen instance (InstanceId: %v, Host: %v, HttpPort: %v)",
                InstanceId_,
                InstanceHost_,
                InstanceHttpPort_);

            ProxiedRequestBody_ = Request_->ReadAll();
            if (!ProxiedRequestBody_) {
                ReplyWithError(EStatusCode::BadRequest, TError("Body should not be empty"));
                return false;
            }

            ProxiedRequestHeaders_ = Request_->GetHeaders()->Duplicate();
            ProxiedRequestHeaders_->Remove("Authorization");
            ProxiedRequestHeaders_->Add("X-Yt-User", User_);
            ProxiedRequestHeaders_->Add("X-Clickhouse-User", User_);

            ProxiedRequestUrl_ = Format("http://%v:%v%v?%v",
                InstanceHost_,
                InstanceHttpPort_,
                Request_->GetUrl().Path,
                CgiParameters_.Print());
        } catch (const std::exception& ex) {
            ReplyWithError(EStatusCode::InternalServerError, TError("Preparation failed")
                << ex);
            return false;
        }
        return true;
    }

    bool TryIssueProxiedRequest()
    {
        try {
            YT_LOG_DEBUG("Querying instance (Url: %v)", ProxiedRequestUrl_);
            ProxiedRespose_ = WaitFor(HttpClient_->Post(ProxiedRequestUrl_, ProxiedRequestBody_, ProxiedRequestHeaders_))
                .ValueOrThrow();
            YT_LOG_DEBUG("Got response from instance (StatusCode: %v)", ProxiedRespose_->GetStatusCode());
        } catch (const std::exception& ex) {
            ReplyWithError(EStatusCode::InternalServerError, TError("ProxiedRequest failed")
                << ex);
            return false;
        }
        return true;
    }

    void ForwardProxiedRespose()
    {
        YT_LOG_DEBUG("Forwarding back subresponse");
        Response_->SetStatus(ProxiedRespose_->GetStatusCode());
        Response_->GetHeaders()->MergeFrom(ProxiedRespose_->GetHeaders());
        PipeInputToOutput(ProxiedRespose_, Response_);
        YT_LOG_DEBUG("ProxiedRespose forwarded");
    }

private:
    TLogger Logger;
    const IRequestPtr& Request_;
    const IResponseWriterPtr& Response_;
    const TClickHouseConfigPtr& Config_;
    const NAuth::ITokenAuthenticatorPtr& TokenAuthenticator_;
    const NApi::IClientPtr& Client_;
    const NHttp::IClientPtr& HttpClient_;

    // These fields contain the request details after parsing CGI params and headers.
    TCgiParameters CgiParameters_;
    TString CliqueId_;
    TString Token_;
    TString User_;
    TString InstanceId_;
    TString InstanceHost_;
    TString InstanceHttpPort_;

    // These fields define the proxied request issued to a randomly chosen instance.
    TString ProxiedRequestUrl_;
    TSharedRef ProxiedRequestBody_;
    THeadersPtr ProxiedRequestHeaders_;

    //! Response from a chosen instance.
    IResponsePtr ProxiedRespose_;

    void ReplyWithError(EStatusCode statusCode, const TError& error) const
    {
        YT_LOG_DEBUG(error, "Request failed (StatusCode: %v)", statusCode);
        ReplyError(Response_, error);
    }

    bool TryResolveAlias()
    {
        auto alias = CliqueId_;
        YT_LOG_DEBUG("Resolving alias (Alias: %v)", alias);
        try {
            auto operationId = ConvertTo<TGuid>(WaitFor(
                Client_->GetNode(
                    Format("//sys/scheduler/orchid/scheduler/operations/%v/operation_id",
                    ToYPathLiteral(alias))))
                    .ValueOrThrow());
            CliqueId_ = ToString(operationId);
        } catch (const std::exception& ex) {
            ReplyWithError(EStatusCode::NotFound, TError("Error while resolving alias %Qv", alias)
                << ex);
            return false;
        }

        YT_LOG_DEBUG("Alias resolved (Alias: %v, CliqueId: %v)", alias, CliqueId_);
        CgiParameters_.ReplaceUnescaped("database", CliqueId_);
        
        return true;
    }

    void ParseTokenFromAuthorizationHeader(const TString& authorization) {
        YT_LOG_DEBUG("Parsing token from Authorization header");
        // Two supported Authorization kinds are "Basic <base64(clique-id:oauth-token)>" and "OAuth <oauth-token>".
        auto authorizationTypeAndCredentials = SplitString(authorization, " ", 2);
        const auto& authorizationType = authorizationTypeAndCredentials[0];
        if (authorizationType == "OAuth" && authorizationTypeAndCredentials.size() == 2) {
            Token_ = authorizationTypeAndCredentials[1];
        } else if (authorizationType == "Basic" && authorizationTypeAndCredentials.size() == 2) {
            const auto& credentials = authorizationTypeAndCredentials[1];
            auto fooAndToken = SplitString(Base64Decode(credentials), ":", 2);
            if (fooAndToken.size() == 2) {
                // First component (that should be username) is ignored.
                Token_ = fooAndToken[1];
            } else {
                ReplyWithError(
                    EStatusCode::Unauthorized,
                    TError("Wrong 'Basic' authorization header format; 'default:<oauth-token>' encoded with base64 expected"));
                return;
            }
        } else {
            ReplyWithError(
                EStatusCode::Unauthorized,
                TError("Unsupported type of authorization header (AuthorizationType: %v, TokenCount: %v)",
                       authorizationType,
                       authorizationTypeAndCredentials.size()));
            return;
        }
        YT_LOG_DEBUG("Token parsed (AuthorizationType: %v)", authorizationType);

    }

    bool TryProcessHeaders()
    {
        const auto* authorization = Request_->GetHeaders()->Find("Authorization");
        if (authorization && !authorization->empty()) {
            ParseTokenFromAuthorizationHeader(*authorization);
        } else if (CgiParameters_.Has("password")) {
            Token_ = CgiParameters_.Get("password");
            CgiParameters_.Erase("password");
            CgiParameters_.Erase("user");
        } else {
            ReplyWithError(EStatusCode::Unauthorized,
                TError("Authorization should be perfomed either by setting `Authorization` header (`Basic` or `OAuth` schemes) "
                       "or `password` CGI parameter"));
            return false;
        }

        return true;
    }

    bool TryAuthenticate()
    {
        try {
            NAuth::TTokenCredentials credentials;
            credentials.Token = Token_;
            User_ = WaitFor(TokenAuthenticator_->Authenticate(credentials))
                .ValueOrThrow()
                .Login;
        } catch (const std::exception& ex) {
            ReplyWithError(
                EStatusCode::Unauthorized,
                TError("Authorization failed")
                    << ex);
            return false;
        }

        YT_LOG_DEBUG("User authenticated (User: %v)", User_);
        return true;
    }

    bool TryPickRandomInstance()
    {
        NApi::TListNodeOptions listOptions;
        listOptions.Attributes = {"http_port", "host"};
        auto listingYson = WaitFor(Client_->ListNode(Config_->DiscoveryPath + "/" + CliqueId_, listOptions))
            .ValueOrThrow();
        auto listingVector = ConvertTo<std::vector<IStringNodePtr>>(listingYson);
        if (listingVector.empty()) {
            ReplyWithError(EStatusCode::NotFound, TError("Clique %v has no running instances", CliqueId_));
            return false;
        }
        const auto& randomEntry = listingVector[RandomNumber(listingVector.size())];
        const auto& attributes = randomEntry->Attributes();
        InstanceId_ = randomEntry->GetValue();
        InstanceHost_ = attributes.Get<TString>("host");
        InstanceHttpPort_ = attributes.Get<TString>("http_port");
        return true;
    }
};

DEFINE_REFCOUNTED_TYPE(TClickHouseContext);
DECLARE_REFCOUNTED_CLASS(TClickHouseContext);

////////////////////////////////////////////////////////////////////////////////

TClickHouseHandler::TClickHouseHandler(TBootstrap* bootstrap)
    : Bootstrap_(bootstrap)
    , Coordinator_(bootstrap->GetCoordinator())
    , Config_(Bootstrap_->GetConfig()->ClickHouse)
    , HttpClient_(CreateClient(Config_->HttpClient, Bootstrap_->GetPoller()))
{ }

void TClickHouseHandler::HandleRequest(
    const IRequestPtr& request,
    const IResponseWriterPtr& response)
{
    if (!Coordinator_->CanHandleHeavyRequests()) {
        // We intentionally read the body of the request and drop it to make sure
        // that client does not block on writing the body.
        request->ReadAll();
        RedirectToDataProxy(request, response, Coordinator_);
    } else {
        ProcessDebugHeaders(request, response, Coordinator_);
        auto context = New<TClickHouseContext>(
            request,
            response,
            Config_,
            Bootstrap_->GetTokenAuthenticator(),
            Bootstrap_->GetClickHouseClient(),
            HttpClient_);
        if (!context->TryPrepare()) {
            // TODO(max42): profile something here.
            return;
        }
        if (!context->TryIssueProxiedRequest()) {
            // TODO(max42): profile something here.
            return;
        }
        context->ForwardProxiedRespose();
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttpProxy
