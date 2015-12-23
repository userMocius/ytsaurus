#pragma once

#include "executor.h"

#include <yt/server/job_proxy/public.h>

#include <yt/ytlib/driver/driver.h>

#include <yt/core/misc/nullable.h>

#include <yt/core/ytree/yson_string.h>

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

class TOperationTracker
{
public:
    TOperationTracker(
        TExecutorConfigPtr config,
        NDriver::IDriverPtr driver,
        const NScheduler::TOperationId& operationId);

    void Run();

private:
    TExecutorConfigPtr Config;
    NDriver::IDriverPtr Driver;
    NScheduler::TOperationId OperationId;
    NScheduler::EOperationType OperationType;
    TNullable<NYTree::TYsonString> PrevProgress;

    static void AppendPhaseProgress(Stroka* out, const Stroka& phase, const NYTree::TYsonString& progress);

    Stroka FormatProgress(const NYTree::TYsonString& progress);
    void DumpProgress();
    void DumpResult();

    NScheduler::EOperationType GetOperationType(const NScheduler::TOperationId& operationId);

    bool CheckFinished();

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

