#include "stdafx.h"
#include "timestamp_manager.h"
#include "config.h"
#include "private.h"

#include <core/misc/serialize.h>

#include <core/concurrency/action_queue.h>
#include <core/concurrency/thread_affinity.h>
#include <core/concurrency/periodic_executor.h>
#include <core/concurrency/delayed_executor.h>

#include <core/rpc/service_detail.h>
#include <core/rpc/server.h>

#include <ytlib/transaction_client/timestamp_service_proxy.h>

#include <server/election/election_manager.h>

#include <server/hydra/composite_automaton.h>
#include <server/hydra/hydra_manager.h>
#include <server/hydra/mutation.h>

#include <server/transaction_server/timestamp_manager.pb.h>

namespace NYT {
namespace NTransactionServer {

using namespace NRpc;
using namespace NHydra;
using namespace NTransactionClient;
using namespace NConcurrency;
using namespace NTransactionServer::NProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = TransactionServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TTimestampManager::TImpl
    : public TServiceBase
    , public TCompositeAutomatonPart
{
public:
    TImpl(
        TTimestampManagerConfigPtr config,
        IInvokerPtr automatonInvoker,
        IHydraManagerPtr hydraManager,
        TCompositeAutomatonPtr automaton)
        : TServiceBase(
            GetSyncInvoker(),
            TTimestampServiceProxy::GetServiceName(),
            TransactionServerLogger.GetCategory())
        , TCompositeAutomatonPart(
            hydraManager,
            automaton)
        , Config_(config)
        , AutomatonInvoker_(automatonInvoker)
        , HydraManager_(hydraManager)
        , Active_(false)
        , CurrentTimestamp_(NullTimestamp)
        , CommittedTimestamp_(NullTimestamp)
    {
        YCHECK(Config_);
        YCHECK(AutomatonInvoker_);
        YCHECK(HydraManager_);

        TimestampQueue_ = New<TActionQueue>("Timestamp");
        TimestampInvoker_ = TimestampQueue_->GetInvoker();
        VERIFY_INVOKER_AFFINITY(TimestampInvoker_, TimestampThread);

        VERIFY_INVOKER_AFFINITY(AutomatonInvoker_, AutomatonThread);

        CalibrationExecutor_ = New<TPeriodicExecutor>(
            TimestampInvoker_,
            BIND(&TImpl::Calibrate, Unretained(this)),
            Config_->CalibrationPeriod);
        CalibrationExecutor_->Start();

        automaton->RegisterPart(this);

        TServiceBase::RegisterMethod(RPC_SERVICE_METHOD_DESC(GenerateTimestamps)
            .SetInvoker(TimestampInvoker_));
        TServiceBase::RegisterMethod(RPC_SERVICE_METHOD_DESC(GetTimestamp)
            .SetInvoker(TimestampInvoker_));

        RegisterLoader(
            "TimestampManager",
            BIND(&TImpl::Load, Unretained(this)));
        RegisterSaver(
            ESerializationPriority::Values,
            "TimestampManager",
            BIND(&TImpl::Save, Unretained(this)));

        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraCommitTimestamp, Unretained(this)));
    }

    IServicePtr GetRpcService()
    {
        return this;
    }

private:
    TTimestampManagerConfigPtr Config_;
    IInvokerPtr AutomatonInvoker_;
    IHydraManagerPtr HydraManager_;

    TActionQueuePtr TimestampQueue_;
    IInvokerPtr TimestampInvoker_;
    
    TPeriodicExecutorPtr CalibrationExecutor_;

    // Timestamp thread affinity:
    
    //! Can we generate timestamps?
    volatile bool Active_;

    //! First unused timestamp.
    TTimestamp CurrentTimestamp_;

    //! Last committed timestamp as viewed by the timestamp thread.
    //! All generated timestamps must be less than this one.
    TTimestamp CommittedTimestamp_;


    // Automaton thread affinity:

    //! Last committed timestamp as viewed by the automaton.
    TTimestamp PersistentTimestamp_;


    DECLARE_THREAD_AFFINITY_SLOT(TimestampThread);
    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);


    // RPC handlers.

    DECLARE_RPC_SERVICE_METHOD(NTransactionClient::NProto, GenerateTimestamps)
    {
        VERIFY_THREAD_AFFINITY(TimestampThread);

        context->SetRequestInfo("Count: %v", request->count());

        DoGenerateTimestamps(context);
    }

    DECLARE_RPC_SERVICE_METHOD(NTransactionClient::NProto, GetTimestamp)
    {
        VERIFY_THREAD_AFFINITY(TimestampThread);

        context->SetRequestInfo();

        if (!CheckActive(context))
            return;

        context->SetRequestInfo("Timestamp: %v", CurrentTimestamp_);

        response->set_timestamp(CurrentTimestamp_);
        context->Reply();
    }


    bool CheckActive(const IServiceContextPtr& context)
    {
        if (!Active_) {
            context->Reply(TError(
                NRpc::EErrorCode::Unavailable,
                "Timestamp provider is not active"));
            return false;
        }

        return true;
    }

    void DoGenerateTimestamps(const TCtxGenerateTimestampsPtr& context)
    {
        VERIFY_THREAD_AFFINITY(TimestampThread);

        if (!CheckActive(context))
            return;

        int count = context->Request().count();
        YCHECK(count >= 0);
        if (count > Config_->MaxTimestampsPerRequest) {
            context->Reply(TError("Too many timestamps requested: %v > %v",
                count,
                Config_->MaxTimestampsPerRequest));
            return;
        }

        if (CurrentTimestamp_ + count >= CommittedTimestamp_) {
            // Backoff and retry.
            LOG_WARNING("Not enough spare timestamps, backing off");
            TDelayedExecutor::Submit(
                BIND(&TImpl::DoGenerateTimestamps, MakeStrong(this), context)
                    .Via(TimestampInvoker_),
                Config_->RequestBackoffTime);
            return;
        }

        // Make sure there's no overflow in the counter part.
        YCHECK(((CurrentTimestamp_ + count) >> TimestampCounterWidth) == (CurrentTimestamp_ >> TimestampCounterWidth));

        auto result = CurrentTimestamp_;
        CurrentTimestamp_ += count;

        context->SetRequestInfo("Timestamp: %v", result);

        context->Response().set_timestamp(result);
        context->Reply();
    }


    void Calibrate()
    {
        VERIFY_THREAD_AFFINITY(TimestampThread);

        if (!Active_)
            return;

        auto now = TInstant::Now();
        ui64 nowSeconds = now.Seconds();
        ui64 prevSeconds = (CurrentTimestamp_ >> TimestampCounterWidth);
        
        if (nowSeconds == prevSeconds)
            return;

        if (nowSeconds < prevSeconds) {
            LOG_WARNING("Clock went back, keeping current timestamp (PrevSeconds: %v, NowSeconds: %v)",
                prevSeconds,
                nowSeconds);
            return;
        }

        CurrentTimestamp_ = (nowSeconds << TimestampCounterWidth);
        LOG_DEBUG("Timestamp advanced (Timestamp: %v)",
            CurrentTimestamp_);

        auto commitTimestamp =
            CurrentTimestamp_ +
            (Config_->CommitAdvance.Seconds() << TimestampCounterWidth);

        TReqCommitTimestamp request;
        request.set_timestamp(commitTimestamp);

        auto mutation = CreateMutation(HydraManager_, request)
            ->OnSuccess(BIND(&TImpl::OnCommitSuccess, MakeStrong(this), commitTimestamp)
                .Via(TimestampInvoker_));

        AutomatonInvoker_->Invoke(BIND(IgnoreResult(&TMutation::Commit), mutation));
    }

    void OnCommitSuccess(TTimestamp timestamp)
    {
        VERIFY_THREAD_AFFINITY(TimestampThread);

        CommittedTimestamp_ = timestamp;

        LOG_DEBUG("Timestamp committed (Timestamp: %v)",
            CommittedTimestamp_);
    }


    virtual void Clear() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        PersistentTimestamp_ = NullTimestamp;
    }

    void Load(TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        PersistentTimestamp_ = NYT::Load<TTimestamp>(context);
    }

    void Save(TSaveContext& context) const
    {
        NYT::Save(context, PersistentTimestamp_);
    }


    virtual void OnLeaderActive() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        LOG_INFO("Persistent timestamp is %v",
            PersistentTimestamp_);

        auto this_ = MakeStrong(this);
        auto persistentTimestamp = PersistentTimestamp_;
        auto invoker = HydraManager_
            ->GetAutomatonEpochContext()
            ->CancelableContext
            ->CreateInvoker(TimestampInvoker_);

        auto callback = BIND([this, this_, persistentTimestamp] () {
            VERIFY_THREAD_AFFINITY(TimestampThread);

            Active_ = true;
            CurrentTimestamp_ = persistentTimestamp;
            CommittedTimestamp_ = persistentTimestamp;

            LOG_INFO("Timestamp generator is now active (Timestamp: %v)",
                persistentTimestamp);
        }).Via(invoker);

        auto deadline = TInstant::Seconds(PersistentTimestamp_ >> TimestampCounterWidth);
        if (TInstant::Now() >= deadline) {
            callback.Run();
        } else {
            LOG_INFO("Timestamp generation postponed until %v to ensure monotonicity",
                deadline);
            TDelayedExecutor::Submit(callback, deadline);
        }
    }

    virtual void OnStopLeading() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto this_ = MakeStrong(this);
        TimestampInvoker_->Invoke(BIND([this, this_] () {
            VERIFY_THREAD_AFFINITY(TimestampThread);

            if (!Active_)
                return;

            Active_ = false;
            CurrentTimestamp_ = NullTimestamp;
            CommittedTimestamp_ = NullTimestamp;

            LOG_INFO("Timestamp generator is no longer active");
        }));
    }


    void HydraCommitTimestamp(const TReqCommitTimestamp& request)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        PersistentTimestamp_ = request.timestamp();
    }


    virtual bool IsUp() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Active_;
    }

};

////////////////////////////////////////////////////////////////////////////////

TTimestampManager::TTimestampManager(
    TTimestampManagerConfigPtr config,
    IInvokerPtr automatonInvoker,
    IHydraManagerPtr hydraManager,
    TCompositeAutomatonPtr automaton)
    : Impl_(New<TImpl>(
        config,
        automatonInvoker,
        hydraManager,
        automaton))
{ }

TTimestampManager::~TTimestampManager()
{ }

IServicePtr TTimestampManager::GetRpcService()
{
    return Impl_->GetRpcService();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionServer
} // namespace NYT
