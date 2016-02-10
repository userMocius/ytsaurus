#pragma once

#include <yt/core/ytree/public.h>
#include <yt/core/ytree/serialize.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

struct TJobSignalerArg
    : public NYTree::TYsonSerializableLite
{
    std::vector<int> Pids;
    Stroka SignalName;

    TJobSignalerArg();
};

////////////////////////////////////////////////////////////////////////////////

void SendSignal(const std::vector<int>& pids, const Stroka& signalName);

////////////////////////////////////////////////////////////////////////////////

struct TJobSignalerTool
{
    void operator()(const TJobSignalerArg& arg) const;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // NYT
