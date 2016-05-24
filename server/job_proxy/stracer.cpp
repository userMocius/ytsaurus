#include "stracer.h"
#include "private.h"

#include <yt/core/concurrency/action_queue.h>

#include <yt/core/misc/proc.h>
#include <yt/core/misc/subprocess.h>

#include <yt/core/tools/registry.h>
#include <yt/core/tools/tools.h>

#include <yt/core/ytree/fluent.h>
#include <yt/core/ytree/serialize.h>

namespace NYT {
namespace NJobProxy {

using namespace NYTree;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = JobProxyLogger;
static const TDuration TraceInterval = TDuration::Seconds(5);
static const TDuration TraceTimeout = TDuration::Seconds(10);
static const int StraceConcurrencyFactor = 4;

////////////////////////////////////////////////////////////////////////////////

TStracerResult TStraceTool::operator()(const std::vector<int>& pids) const
{
    SafeSetUid(0);
    return Strace(pids);;
}

REGISTER_TOOL(TStraceTool);

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TStrace& trace, NYson::IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("trace").Value(trace.Trace)
            .Item("process_name").Value(trace.ProcessName)
            .Item("process_command_line").List(trace.ProcessCommandLine)
        .EndMap();
}

void Deserialize(TStrace& value, INodePtr node)
{
    auto mapNode = node->AsMap();

    value.Trace = ConvertTo<Stroka>(mapNode->GetChild("trace"));
    value.ProcessName = ConvertTo<Stroka>(mapNode->GetChild("process_name"));
    value.ProcessCommandLine = ConvertTo<std::vector<Stroka>>(mapNode->GetChild("process_command_line"));
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TStracerResult& stracerResult, NYson::IYsonConsumer* consumer)
{
    NYTree::Serialize(stracerResult.Traces, consumer);
}

void Deserialize(TStracerResult& value, INodePtr node)
{
    Deserialize(value.Traces, node);
}

////////////////////////////////////////////////////////////////////////////////

TStrace DoStrace(int pid)
{
    TStrace trace;

    try {
        trace.ProcessName = GetProcessName(pid);
        trace.ProcessCommandLine = GetProcessCommandLine(pid);
    } catch (const std::exception& ex) {
        LOG_ERROR(ex, "Failed to get process information for %v", pid);
    }

    TSubprocess tracer("/usr/bin/strace");
    tracer.AddArguments({
        "-tt",
        "-p",
        ToString(pid)
    });

    auto cookie = TDelayedExecutor::Submit(BIND([&] () {
            try {
                tracer.Kill(SIGINT);
            } catch (const std::exception& ex) {
                LOG_ERROR(ex, "Failed to interrupt stracer %v", pid);
            }
        }),
        TraceInterval);

    auto killCookie = TDelayedExecutor::Submit(BIND([&] () {
            try {
                tracer.Kill(SIGKILL);
            } catch (const std::exception& ex) {
                LOG_ERROR(ex, "Failed to kill stracer %v", pid);
            }
        }),
        TraceTimeout);

    TSubprocessResult tracerResult;
    try {
        tracerResult = tracer.Execute();

        TDelayedExecutor::Cancel(cookie);
        TDelayedExecutor::Cancel(killCookie);
    } catch (const std::exception&) {
        TDelayedExecutor::Cancel(cookie);
        TDelayedExecutor::Cancel(killCookie);
        throw;
    }

    if (!tracerResult.Status.IsOK()) {
        THROW_ERROR_EXCEPTION("Failed to strace %v", pid)
            << TErrorAttribute("stderr", ToString(tracerResult.Error))
            << tracerResult.Status;
    }

    trace.Trace = Stroka(tracerResult.Error.Begin(), tracerResult.Error.End());
    return trace;
}

TStracerResult Strace(const std::vector<int>& pids)
{
    TStracerResult result;

    auto pool = New<TThreadPool>(StraceConcurrencyFactor, "StracePool");

    std::map<int, TFuture<TStrace>> traceFutures;

    for (const auto& pid : pids) {
        traceFutures[pid] =
            BIND([=] () {
                return DoStrace(pid);
            })
            .AsyncVia(pool->GetInvoker())
            .Run();
    }

    for (const auto& pid : pids) {
        result.Traces[pid] = traceFutures[pid].Get().ValueOrThrow();;
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////


} // namespace NJobProxy
} // namespace NYT
