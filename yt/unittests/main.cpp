#include "framework.h"

#include <yt/ytlib/shutdown.h>

// XXX(sandello): This is a dirty hack. :(
#include <yt/server/hydra/private.h>

class TYTEnvironment
    : public ::testing::Environment
{
public:
    virtual void SetUp() override
    {
        NYT::ConfigureLogging(
            getenv("YT_LOG_LEVEL"),
            getenv("YT_LOG_EXCLUDE_CATEGORIES"),
            getenv("YT_LOG_INCLUDE_CATEGORIES"));
    }

    virtual void TearDown() override
    {
        // TODO(sandello): Replace me with a better shutdown mechanism.
        NYT::NHydra::ShutdownHydraIOInvoker();
        NYT::Shutdown();
    }
};

int main(int argc, char **argv)
{
#ifdef _unix_
    signal(SIGPIPE, SIG_IGN);
#endif

    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TYTEnvironment());

    return RUN_ALL_TESTS();
}

