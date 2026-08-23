#include "warpsim/version.hpp"

#include <algorithm>

#include <gtest/gtest.h>

TEST(Version, IsSemanticVersion) {
    const auto v = warpsim::version();
    EXPECT_EQ(v, "0.1.0");
    EXPECT_EQ(std::count(v.begin(), v.end(), '.'), 2);
}

TEST(Probe, UseAfterFree) {
    auto* p = new int(3);
    delete p;
    EXPECT_EQ(*p, 3); // NOLINT
}

TEST(Probe, SignedOverflow) {
    volatile int big = 2147483647;
    int next = big + 1; // NOLINT
    EXPECT_NE(next, 0);
}
