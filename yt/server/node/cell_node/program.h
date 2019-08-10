#include <yt/server/node/cell_node/bootstrap.h>
#include <yt/server/node/cell_node/config.h>

#include <yt/ytlib/program/program.h>
#include <yt/ytlib/program/program_config_mixin.h>
#include <yt/ytlib/program/program_tool_mixin.h>
#include <yt/ytlib/program/program_pdeathsig_mixin.h>
#include <yt/ytlib/program/configure_singletons.h>

#include <yt/core/phdr_cache/phdr_cache.h>

#include <library/ytalloc/api/ytalloc.h>

#include <yt/core/ytalloc/bindings.h>

#include <yt/core/misc/ref_counted_tracker_profiler.h>

namespace NYT::NCellNode {

////////////////////////////////////////////////////////////////////////////////

class TCellNodeProgram
    : public TProgram
    , public TProgramPdeathsigMixin
    , public TProgramToolMixin
    , public TProgramConfigMixin<NCellNode::TCellNodeConfig>
{
public:
    TCellNodeProgram()
        : TProgramPdeathsigMixin(Opts_)
        , TProgramToolMixin(Opts_)
        , TProgramConfigMixin(Opts_, false)
    { }

protected:
    virtual void DoRun(const NLastGetopt::TOptsParseResult& parseResult) override
    {
        TThread::CurrentThreadSetName("NodeMain");

        ConfigureUids();
        ConfigureSignals();
        ConfigureCrashHandler();
        ConfigureExitZeroOnSigterm();
        EnablePhdrCache();
        EnableRefCountedTrackerProfiling();
        NYTAlloc::EnableYTLogging();
        NYTAlloc::EnableYTProfiling();
        NYTAlloc::SetLibunwindBacktraceProvider();
        NYTAlloc::ConfigureFromEnv();
        NYTAlloc::EnableStockpile();

        if (HandlePdeathsigOptions()) {
            return;
        }

        if (HandleToolOptions()) {
            return;
        }

        if (HandleConfigOptions()) {
            return;
        }

        auto config = GetConfig();
        auto configNode = GetConfigNode();

        ConfigureSingletons(config);

        // TODO(babenko): This memory leak is intentional.
        // We should avoid destroying bootstrap since some of the subsystems
        // may be holding a reference to it and continue running some actions in background threads.
        auto* bootstrap = new NCellNode::TBootstrap(std::move(config), std::move(configNode));
        bootstrap->Run();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellNode
