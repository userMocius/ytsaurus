#include "framework.h"

#include <yt/core/misc/persistent_queue.h>

namespace NYT {
namespace {

////////////////////////////////////////////////////////////////////////////////

using TQueue = TPersistentQueue<int, 10>;
using TSnapshot = TPersistentQueueSnapshot<int, 10>;

TEST(TPersistentQueue, Empty)
{
    TQueue queue;
    EXPECT_EQ(0, queue.Size());
    EXPECT_TRUE(queue.Empty());
    EXPECT_EQ(queue.Begin(), queue.End());

    auto snapshot = queue.MakeSnapshot();
    EXPECT_EQ(0, snapshot.Size());
    EXPECT_TRUE(snapshot.Empty());
    EXPECT_EQ(snapshot.Begin(), snapshot.End());
}

TEST(TPersistentQueue, EnqueueDequeue)
{
    TQueue queue;

    const int N = 100;

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(i, queue.Size());
        queue.Enqueue(i);
    }

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(N - i, queue.Size());
        EXPECT_EQ(i, queue.Dequeue());
    }
}

TEST(TPersistentQueue, Iterate)
{
    TQueue queue;

    const int N = 100;

    for (int i = 0; i < 2 * N; ++i) {
        queue.Enqueue(i);
    }

    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(i, queue.Dequeue());
    }

    int expected = N;
    for (int x : queue) {
        EXPECT_EQ(expected, x);
        ++expected;
    }
}

TEST(TPersistentQueue, Snapshot1)
{
    TQueue queue;

    const int N = 100;
    std::vector<TSnapshot> snapshots;

    for (int i = 0; i < N; ++i) {
        snapshots.push_back(queue.MakeSnapshot());
        queue.Enqueue(i);
    }

    for (int i = 0; i < N; ++i) {
        const auto& snapshot = snapshots[i];
        EXPECT_EQ(i, snapshot.Size());
        int expected = 0;
        for (int x : snapshot) {
            EXPECT_EQ(expected, x);
            ++expected;
        }
    }
}

TEST(TPersistentQueue, Snapshot2)
{
    TQueue queue;

    const int N = 100;
    std::vector<TSnapshot> snapshots;

    for (int i = 0; i < N; ++i) {
        queue.Enqueue(i);
    }

    for (int i = 0; i < N; ++i) {
        snapshots.push_back(queue.MakeSnapshot());
        EXPECT_EQ(i, queue.Dequeue());
    }

    for (int i = 0; i < N; ++i) {
        const auto& snapshot = snapshots[i];
        EXPECT_EQ(i, N - snapshot.Size());
        int expected = i;
        for (int x : snapshot) {
            EXPECT_EQ(expected, x);
            ++expected;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT
