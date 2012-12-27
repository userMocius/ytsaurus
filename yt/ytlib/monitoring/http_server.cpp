#include "stdafx.h"
#include "http_server.h"

#include <ytlib/misc/id_generator.h>

#include <ytlib/actions/bind.h>
#include <ytlib/actions/future.h>

#include <ytlib/logging/log.h>

#include <ytlib/profiling/profiler.h>

#include <util/server/http.h>
#include <util/string/http.h>

namespace NYT {
namespace NHttp {

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("HTTP");

////////////////////////////////////////////////////////////////////////////////

class TServer::TImpl
{
private:
    class TClient
        : public ::TClientRequest
    {
    public:
        TClient()
        { }

        virtual bool Reply(void* param)
        {
            auto impl = (TImpl*) param;
            TParsedHttpRequest request(Headers[0]);

            LOG_DEBUG("Started serving HTTP request (Method: %s, Path: %s)",
                ~request.Method.ToString(),
                ~request.Request.ToString());

            // See http://www.w3.org/Protocols/rfc2616/rfc2616.html for HTTP RFC.

            if (request.Method != "GET") {
                Output() << FormatNotImplementedResponse();
                return true;
            }

            if (!request.Request.empty() && request.Request[0] == '/') {
                auto slashPosition = request.Request.find('/', 1);
                if (slashPosition == Stroka::npos) {
                    slashPosition = request.Request.length();
                }

                Stroka prefix = request.Request.substr(0, slashPosition).ToString();
                Stroka suffix = request.Request.substr(slashPosition).ToString();
                
                {
                    auto it = impl->SyncHandlers.find(prefix);
                    if (it != impl->SyncHandlers.end()) {
                        Output() << it->second.Run(suffix);
                        LOG_DEBUG("Request served");
                        return true;
                    }
                }

                {
                    auto it = impl->AsyncHandlers.find(prefix);
                    if (it != impl->AsyncHandlers.end()) {
                        Output() << it->second.Run(suffix).Get();
                        LOG_DEBUG("Request served");
                        return true;
                    }
                }
            }

            LOG_WARNING("Cannot find a handler for HTTP request (Method; %s, Path: %s)",
                ~request.Method.ToString(),
                ~request.Request.ToString());
            Output() << FormatNotFoundResponse();
            return true;
        }
    };

    class TCallback
        : public THttpServer::ICallBack
    {
    public:
        TCallback(const TImpl& impl)
            : Impl(impl)
        { }

        virtual TClientRequest* CreateClient()
        {
            return new TClient();
        }

        virtual void* CreateThreadSpecificResource()
        {
            return (void*) &Impl;
        }
        
    private:
        const TImpl& Impl;
    };

private:
    typedef yhash_map<Stroka, TSyncHandler> TSyncHandlerMap;
    typedef yhash_map<Stroka, TAsyncHandler> TAsyncHandlerMap;

private:
    THolder<TCallback> Callback;
    THolder<THttpServer> Server;
    
    TSyncHandlerMap SyncHandlers;
    TAsyncHandlerMap AsyncHandlers;

public:
    TImpl(int port)
    {
        Callback.Reset(new TCallback(*this));
        Server.Reset(new THttpServer(~Callback,
            THttpServerOptions(static_cast<ui16>(port))
        ));
    }

    void Start()
    {
        if (!Server->Start()) {
            THROW_ERROR_EXCEPTION("Failed to start HTTP server")
                << TError::FromSystem(Server->GetErrorCode());
        }
    }

    void Stop()
    {
        Server->Stop();
    }

    void Register(const Stroka& prefix, TSyncHandler handler)
    {
        YCHECK(SyncHandlers.insert(MakePair(prefix, MoveRV(handler))).second);
    }

    void Register(const Stroka& prefix, TAsyncHandler handler)
    {
        YCHECK(AsyncHandlers.insert(MakePair(prefix, MoveRV(handler))).second);
    }
};

////////////////////////////////////////////////////////////////////////////////

Stroka FormatInternalServerErrorResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatNotImplementedResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 501 Not Implemented\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatBadGatewayResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 502 Bad Gateway\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatServiceUnavailableResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatGatewayTimeoutResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 504 Gateway Timeout\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatBadRequestResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatNotFoundResponse(const Stroka& body)
{
    return Sprintf(
        "HTTP/1.1 404 Not Found\r\n"
        "Connection: close\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

Stroka FormatRedirectResponse(const Stroka& location)
{
    return Sprintf(
        "HTTP/1.1 303 See Other\r\n"
        "Connection: close\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 0\r\n"
        "Location: %s\r\n"
        "\r\n",
        ~location);
}

Stroka FormatOKResponse(const Stroka& body)
{
    // TODO(sandello): Unify headers across all these methods; also implement CRYT-61.
    return Sprintf(
        "HTTP/1.1 200 OK\r\n"
        "Server: YT\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache, max-age=0\r\n"
        "Expires: Thu, 01 Jan 1970 00:00:01 GMT\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %" PRISZT "\r\n"
        "\r\n"
        "%s",
        body.length(),
        ~body);
}

////////////////////////////////////////////////////////////////////////////////

TServer::TServer(int port)
    : Impl(new TImpl(port))
{ }

TServer::~TServer()
{ }

void TServer::Register(const Stroka& prefix, TSyncHandler handler)
{
    Impl->Register(prefix, handler);
}

void TServer::Register(const Stroka& prefix, TAsyncHandler handler)
{
    Impl->Register(prefix, handler);
}

void TServer::Start()
{
    Impl->Start();
}

void TServer::Stop()
{
    Impl->Stop();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NHttp
} // namespace NYT
