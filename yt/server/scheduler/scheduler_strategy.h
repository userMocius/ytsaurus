#pragma once

#include "public.h"

#include <ytlib/actions/signal.h>

#include <ytlib/scheduler/job.pb.h>

#include <ytlib/ytree/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

struct ISchedulerStrategyHost
{
    virtual ~ISchedulerStrategyHost()
    { }

    DECLARE_INTERFACE_SIGNAL(void(TOperationPtr), OperationStarted);
    DECLARE_INTERFACE_SIGNAL(void(TOperationPtr), OperationFinished);

    virtual TMasterConnector* GetMasterConnector() = 0;

    virtual NProto::TNodeResources GetTotalResourceLimits() = 0;

};

struct ISchedulerStrategy
{
    virtual ~ISchedulerStrategy()
    { }

    virtual void ScheduleJobs(ISchedulingContext* context) = 0;
    virtual void BuildOperationProgressYson(TOperationPtr operation, NYTree::IYsonConsumer* consumer) = 0;
    virtual void BuildOrchidYson(NYTree::IYsonConsumer* consumer) = 0;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
