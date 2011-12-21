#include "stdafx.h"
#include "service.h"

#include "../logging/log.h"
#include "../misc/assert.h"

namespace NYT {
namespace NRpc {

using namespace NBus;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = RpcLogger;

////////////////////////////////////////////////////////////////////////////////

TServiceBase::TServiceBase(
    IInvoker* defaultServiceInvoker,
    const Stroka& serviceName,
    const Stroka& loggingCategory)
    : DefaultServiceInvoker(defaultServiceInvoker)
    , ServiceName(serviceName)
    , ServiceLogger(loggingCategory)
{
    YASSERT(defaultServiceInvoker != NULL);
}

void TServiceBase::RegisterMethod(
    const TMethodDescriptor& descriptor,
    IInvoker* invoker)
{
    YASSERT(invoker != NULL);

    TGuard<TSpinLock> guard(SpinLock);

    if (!RuntimeMethodInfos.insert(MakePair(
        descriptor.Verb,
        TRuntimeMethodInfo(descriptor, invoker))).Second()) {
        ythrow yexception() << Sprintf("Verb is already registered (ServiceName: %s, Verb: %s)",
            ~ServiceName,
            ~descriptor.Verb);
    }
}

void TServiceBase::RegisterMethod(const TMethodDescriptor& descriptor)
{
    RegisterMethod(descriptor, ~DefaultServiceInvoker);
}

void TServiceBase::OnBeginRequest(IServiceContext* context)
{
    YASSERT(context != NULL);

    Stroka verb = context->GetVerb();
    
    TRuntimeMethodInfo* runtimeInfo;
    {
        TGuard<TSpinLock> guard(SpinLock);

        auto methodIt = RuntimeMethodInfos.find(verb);
        runtimeInfo =
            methodIt == RuntimeMethodInfos.end()
            ? NULL
            : &methodIt->Second();

        // TODO (panin): implement and provide here more granulate locking
        // TODO: look carefully here (added not NULL check of runtimeInfo)
        if (runtimeInfo != NULL) {
            if (runtimeInfo->Descriptor.OneWay != context->IsOneWay()) {
                Stroka message = Sprintf("One-way flag mismatch (Expected: %s, Actual: %s, ServiceName: %s, Verb: %s)",
                    ~ToString(runtimeInfo->Descriptor.OneWay),
                    ~ToString(context->IsOneWay()),
                    ~ServiceName,
                    ~verb);
                LOG_WARNING("%s", ~message);
                context->Reply(TError(EErrorCode::NoSuchVerb, message));
            }
        }

        if (!context->IsOneWay()) {
            TActiveRequest activeRequest(runtimeInfo, TInstant::Now());
            YVERIFY(ActiveRequests.insert(MakePair(context, activeRequest)).Second());
        }
    }

    if (runtimeInfo == NULL) {
        Stroka message = Sprintf("Unknown verb (ServiceName: %s, Verb: %s)",
            ~ServiceName,
            ~verb);
        LOG_WARNING("%s", ~message);
        context->Reply(TError(EErrorCode::NoSuchVerb, message));
    } else {
        auto handler = runtimeInfo->Descriptor.Handler;
        auto wrappedHandler = context->Wrap(~handler->Bind(context));
        runtimeInfo->Invoker->Invoke(wrappedHandler);
    }
}

void TServiceBase::OnEndRequest(IServiceContext* context)
{
    YASSERT(context != NULL);
    YASSERT(!context->IsOneWay());

    TGuard<TSpinLock> guard(SpinLock);

    auto it = ActiveRequests.find(context);
    YASSERT(it != ActiveRequests.end());
    
    auto& request = it->Second();
    if (request.RuntimeInfo != NULL) {
        request.RuntimeInfo->ExecutionTime.AddDelta(request.StartTime);       
    }

    ActiveRequests.erase(it);
}

Stroka TServiceBase::GetServiceName() const
{
    return ServiceName;
}

Stroka TServiceBase::GetLoggingCategory() const
{
    return ServiceLogger.GetCategory();
}

Stroka TServiceBase::GetDebugInfo() const
{
    TGuard<TSpinLock> guard(SpinLock);

    Stroka info = "Service " + ServiceName + ":\n";
    FOREACH(const auto& pair, RuntimeMethodInfos) {
        info += Sprintf("Method %s: %s\n",
            ~pair.First(),
            ~pair.Second().ExecutionTime.GetDebugInfo());
    }
    return info;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
