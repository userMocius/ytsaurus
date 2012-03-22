#pragma once

#include <ytlib/misc/common.h>
#include <ytlib/misc/guid.h>
#include <ytlib/misc/enum.h>
#include <ytlib/transaction_server/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

using NTransactionServer::TTransactionId;

typedef TGuid TJobId;
typedef TGuid TOperationId;

DECLARE_ENUM(EOperationType,
    ((Map)(0))
);

DECLARE_ENUM(EJobType,
    ((Map)(0))
);

DECLARE_ENUM(EOperationState,
    ((Initializing)(0))
    ((Preparing)(1))
    ((Running)(2))
    ((Completed)(3))
    ((Aborting)(4))
    ((Aborted)(5))
    ((Failing)(6))
    ((Failed)(7))
);

DECLARE_ENUM(EJobState,
    ((Running)(0))
    ((Aborting)(1))
    ((Completed)(2))
    ((Failed)(3))
    ((Aborted)(4))
);

DECLARE_ENUM(EJobProgress,
    ((Created)(0))
    ((PreparingProxy)(1))

    ((PreparingSandbox)(10))
    
    ((StartedProxy)(50))
    ((StartedJob)(51))
    ((FinishedJob)(52))

    ((Cleanup)(80))

    ((Completed)(101))
    ((Failed)(102))
    ((Aborted)(103))
);

DECLARE_ENUM(ESchedulerStrategy,
    (Null)
    (Fifo)
);

class TSchedulerService;
typedef TIntrusivePtr<TSchedulerService> TSchedulerServicePtr;

class TSchedulerServiceProxy;

class TOperation;
typedef TIntrusivePtr<TOperation> TOperationPtr;

class TJob;
typedef TIntrusivePtr<TJob> TJobPtr;

class TExecNode;
typedef TIntrusivePtr<TExecNode> TExecNodePtr;

class TSchedulerConfig;
typedef TIntrusivePtr<TSchedulerConfig> TSchedulerConfigPtr;

class TScheduler;
typedef TIntrusivePtr<TScheduler> TSchedulerPtr;

struct ISchedulerStrategy;

struct IOperationHost;

struct IOperationController;

class TMapOperationSpec;
typedef TIntrusivePtr<TMapOperationSpec> TMapOperationSpecPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
