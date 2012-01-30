#pragma once

#include "common.h"
#include "service.h"

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

class TRedirecitingServiceBase
    : public IService
{
public:
    TRedirecitingServiceBase(
        const Stroka& serviceName,
        const Stroka& loggingCategory);

    struct TRedirectParams
    {
        TDuration Timeout;
        Stroka Address;
    };


protected:
    virtual TRedirectParams GetRedirectParams(IServiceContext* context) const = 0;

private:
    class TRequest;
    class TResponseHandler;

    Stroka ServiceName;
    Stroka LoggingCategory;

    virtual void OnBeginRequest(IServiceContext* context);
    virtual void OnEndRequest(IServiceContext* context);

    virtual Stroka GetLoggingCategory() const;
    virtual Stroka GetServiceName() const;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
