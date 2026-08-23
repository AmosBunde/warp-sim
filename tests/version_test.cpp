#include "warpsim/version.hpp"

#include <algorithm>

#include <gtest/gtest.h>

TEST(Version, IsSemanticVersion) {
    const auto v = warpsim::version();
    EXPECT_EQ(v, "0.1.0");
    EXPECT_EQ(std::count(v.begin(), v.end(), '.'), 2);
}
