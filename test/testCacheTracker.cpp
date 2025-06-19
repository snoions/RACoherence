#include "cacheTracker.hpp" // Updated header file name
#include <gtest/gtest.h>

TEST(CacheLineTrackerTest, MarkAndCheckDirty) {
    CacheLineTracker tracker;

    virt_addr_t va = 0x00007fff12345000;
    tracker.mark_dirty(va);
    EXPECT_TRUE(tracker.is_dirty(va));

    tracker.clear_dirty(va);
    EXPECT_FALSE(tracker.is_dirty(va));
}

TEST(CacheLineTrackerTest, MultipleLines) {
    CacheLineTracker tracker;

    virt_addr_t va1 = 0x00007fff12345000;
    virt_addr_t va2 = va1 + 64;  // Next cache line

    tracker.mark_dirty(va1);
    EXPECT_TRUE(tracker.is_dirty(va1));
    EXPECT_FALSE(tracker.is_dirty(va2));
}

TEST(CacheLineTrackerTest, DifferentPages) {
    CacheLineTracker tracker;

    virt_addr_t va1 = 0x00007fff12345000;
    virt_addr_t va2 = 0x00007fff12346000; // Different page

    tracker.mark_dirty(va1);
    tracker.mark_dirty(va2);

    EXPECT_TRUE(tracker.is_dirty(va1));
    EXPECT_TRUE(tracker.is_dirty(va2));

    tracker.clear_dirty(va1);
    EXPECT_FALSE(tracker.is_dirty(va1));
    EXPECT_TRUE(tracker.is_dirty(va2));
}
