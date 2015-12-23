#include "throttling_channel.h"
#include "channel_detail.h"
#include "config.h"

#include <yt/core/concurrency/throughput_throttler.h>

#include <yt/core/misc/common.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

class TThrottlingChannel
    : public TChannelWrapper
{
public:
    TThrottlingChannel(TThrottlingChannelConfigPtr config, IChannelPtr underlyingChannel)
        : TChannelWrapper(std::move(underlyingChannel))
        , Config_(config)
    {
        auto throttlerConfig = New<NConcurrency::TThroughputThrottlerConfig>();
        throttlerConfig->Period = TDuration::Seconds(1);
        throttlerConfig->Limit = Config_->RateLimit;
        Throttler_ = CreateLimitedThrottler(throttlerConfig);
    }

    virtual IClientRequestControlPtr Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        TNullable<TDuration> timeout,
        bool requestAck) override
    {
        auto requestControlThunk = New<TClientRequestControlThunk>();
        Throttler_->Throttle(1).Subscribe(BIND([=, this_ = MakeStrong(this)] (const TError& error) {
            if (!error.IsOK()) {
                responseHandler->HandleError(error);
                return;
            }

            auto requestControl = UnderlyingChannel_->Send(
                std::move(request),
                std::move(responseHandler),
                timeout,
                requestAck);
            requestControlThunk->SetUnderlying(std::move(requestControl));
        }));
        return requestControlThunk;
    }

private:
    const TThrottlingChannelConfigPtr Config_;

    NConcurrency::IThroughputThrottlerPtr Throttler_;

};

IChannelPtr CreateThrottlingChannel(
    TThrottlingChannelConfigPtr config,
    IChannelPtr underlyingChannel)
{
    YCHECK(config);
    YCHECK(underlyingChannel);

    return New<TThrottlingChannel>(config, underlyingChannel);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
