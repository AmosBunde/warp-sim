#include "warpsim/asm/assembler.hpp"
#include "warpsim/core/device.hpp"
#include "warpsim/core/types.hpp"

#include <bit>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using warpsim::assembler::assemble;
using warpsim::assembler::Program;
using warpsim::core::Device;
using warpsim::core::Dim2;

Program must_assemble(const std::string& source) {
    const auto p = assemble(source);
    if (!p.has_value()) {
        ADD_FAILURE() << p.error().line << ": " << p.error().message;
        return {};
    }
    return *p;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << path;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_words(Device& device, std::uint32_t offset, const std::vector<std::uint32_t>& words) {
    std::memcpy(&device.global().bytes()[offset], words.data(), words.size() * 4);
}

std::vector<std::uint32_t> read_words(const Device& device, std::uint32_t offset, std::size_t n) {
    std::vector<std::uint32_t> out(n);
    std::memcpy(out.data(), &device.global().bytes()[offset], n * 4);
    return out;
}

TEST(Device, VecaddAcrossBlocksWithRaggedTail) {
    const Program program = must_assemble(read_file(WARPSIM_KERNELS_DIR "/examples/vecadd.wisa"));
    constexpr std::uint32_t n = 203; // 4 blocks of 64 minus a ragged tail
    Device device(4096);
    std::vector<std::uint32_t> a(n);
    std::vector<std::uint32_t> b(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        a[i] = std::bit_cast<std::uint32_t>(static_cast<float>(i) * 0.5F);
        b[i] = std::bit_cast<std::uint32_t>(static_cast<float>(i) * 0.25F);
    }
    write_words(device, 0, a);
    write_words(device, 1024, b);
    const std::vector<std::uint32_t> params = {0, 1024, 2048, n};
    const auto stats = device.launch(program, Dim2{.x = 4, .y = 1}, Dim2{.x = 64, .y = 1}, params);
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    const auto c = read_words(device, 2048, n + 1);
    for (std::uint32_t i = 0; i < n; ++i) {
        EXPECT_EQ(std::bit_cast<float>(c[i]), static_cast<float>(i) * 0.75F) << i;
    }
    EXPECT_EQ(c[n], 0U) << "lane past n must not store";
    EXPECT_EQ(stats->blocks_executed, 4U);
    EXPECT_EQ(stats->warps_launched, 8U);
    EXPECT_GT(stats->instructions_issued, 0U);
}

TEST(Device, TwoDimensionalGeometry) {
    // out[tid.y * ntid.x + tid.x + block offset] = ctaid.x * 1000 + ctaid.y * 100 + tid.y * 10 +
    // tid.x
    const Program program = must_assemble(R"(.entry geometry
.param out
        mov.sreg r0, %tid.x
        mov.sreg r1, %tid.y
        mov.sreg r2, %ntid.x
        mov.sreg r3, %ntid.y
        mov.sreg r4, %ctaid.x
        mov.sreg r5, %ctaid.y
        mov.sreg r6, %nctaid.x
        mul r7, r1, r2
        add r7, r7, r0          // linear lane index
        mul r8, r5, r6
        add r8, r8, r4          // linear block index
        mul r9, r2, r3          // lanes per block
        mul r8, r8, r9
        add r7, r7, r8          // global element
        shl r7, r7, 2
        ld.param r10, [out]
        add r7, r7, r10
        mul r11, r4, 1000
        mul r12, r5, 100
        add r11, r11, r12
        mul r12, r1, 10
        add r11, r11, r12
        add r11, r11, r0
        st.global [r7], r11
        exit
)");
    Device device(4096);
    const std::vector<std::uint32_t> params = {0};
    const auto stats = device.launch(program, Dim2{.x = 2, .y = 3}, Dim2{.x = 5, .y = 7}, params);
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    constexpr std::size_t element_count = 210; // 2 x 3 blocks of 5 x 7 lanes
    const auto out = read_words(device, 0, element_count);
    std::size_t idx = 0;
    for (std::uint32_t by = 0; by < 3; ++by) {
        for (std::uint32_t bx = 0; bx < 2; ++bx) {
            for (std::uint32_t ty = 0; ty < 7; ++ty) {
                for (std::uint32_t tx = 0; tx < 5; ++tx) {
                    EXPECT_EQ(out[idx], (bx * 1000) + (by * 100) + (ty * 10) + tx) << idx;
                    ++idx;
                }
            }
        }
    }
    EXPECT_EQ(stats->blocks_executed, 6U);
    EXPECT_EQ(stats->warps_launched, 12U); // 35 lanes per block: 2 warps
}

TEST(Device, BarrierOrdersProducerAndConsumerWarps) {
    // Warp 0 writes shared[lane] = lane * 3; after bar.sync warp 1 reads it back.
    const Program program = must_assemble(R"(.entry barrier
.param out
.shared 128
        mov.sreg r0, %laneid
        mov.sreg r1, %warpid
        shl r2, r0, 2
        setp.eq.s32 p0, r1, 0
    @!p0 bra consume
        mul r3, r0, 3
        st.shared [r2], r3
consume:
        bar.sync
        setp.eq.s32 p1, r1, 1
    @!p1 exit
        ld.shared r4, [r2]
        ld.param r5, [out]
        add r5, r5, r2
        st.global [r5], r4
        exit
)");
    Device device(1024);
    const std::vector<std::uint32_t> params = {0};
    const auto stats = device.launch(program, Dim2{.x = 1, .y = 1}, Dim2{.x = 64, .y = 1}, params);
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    const auto out = read_words(device, 0, 32);
    for (unsigned lane = 0; lane < 32; ++lane) {
        EXPECT_EQ(out[lane], lane * 3) << lane;
    }
    EXPECT_EQ(stats->barriers_completed, 1U);
}

TEST(Device, BarrierCompletesWhenAWarpHasRetired) {
    const Program program = must_assemble(R"(.entry early
        mov.sreg r1, %warpid
        setp.eq.s32 p0, r1, 0
    @p0 exit
        bar.sync
        bar.sync
        exit
)");
    Device device(64);
    const auto stats = device.launch(program, Dim2{}, Dim2{.x = 96, .y = 1}, {});
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    EXPECT_EQ(stats->barriers_completed, 2U);
}

TEST(Device, SharedMemoryIsPerBlockAndZeroed) {
    // Each block reads shared[0] before writing it; the read must be 0 in every block.
    const Program program = must_assemble(R"(.entry fresh
.param out
.shared 16
        mov.sreg r0, %ctaid.x
        mov r2, 0
        ld.shared r1, [r2]
        shl r3, r0, 2
        ld.param r4, [out]
        add r4, r4, r3
        st.global [r4], r1
        mov r5, 77
        st.shared [r2], r5
        exit
)");
    Device device(64);
    const std::vector<std::uint32_t> params = {0};
    const auto stats = device.launch(program, Dim2{.x = 3, .y = 1}, Dim2{.x = 1, .y = 1}, params);
    ASSERT_TRUE(stats.has_value()) << stats.error().describe();
    EXPECT_EQ(read_words(device, 0, 3), (std::vector<std::uint32_t>{0, 0, 0}));
}

TEST(Device, Faults) {
    Device device(64);
    const auto oob = must_assemble(".entry k\n mov r1, 64\n ld.global r2, [r1]\n");
    auto r = device.launch(oob, Dim2{}, Dim2{.x = 1, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "access out of bounds");
    EXPECT_EQ(r.error().address, 64U);
    EXPECT_EQ(r.error().pc, 1U);

    const auto misaligned = must_assemble(".entry k\n mov r1, 2\n st.global [r1], r1\n");
    r = device.launch(misaligned, Dim2{}, Dim2{.x = 1, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "misaligned 32-bit access");

    const auto shared_oob = must_assemble(".entry k\n.shared 8\n mov r1, 8\n ld.shared r2, [r1]\n");
    r = device.launch(shared_oob, Dim2{}, Dim2{.x = 1, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "access out of bounds");

    const auto bad_param = must_assemble(".entry k\n.param a\n ld.param r1, [a]\n");
    r = device.launch(bad_param, Dim2{}, Dim2{.x = 1, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "parameter ordinal out of range");

    const auto divergent_barrier = must_assemble(R"(.entry k
        mov.sreg r0, %laneid
        setp.lt.s32 p0, r0, 4
    @p0 bra a
        exit
a:      bar.sync
        exit
)");
    r = device.launch(divergent_barrier, Dim2{}, Dim2{.x = 32, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "bar.sync executed in divergent code");

    r = device.launch(oob, Dim2{.x = 0, .y = 1}, Dim2{.x = 1, .y = 1}, {});
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "empty grid or block");
}

TEST(Device, LaunchIsDeterministic) {
    const Program program = must_assemble(read_file(WARPSIM_KERNELS_DIR "/examples/vecadd.wisa"));
    std::vector<std::uint32_t> a(100);
    for (std::uint32_t i = 0; i < 100; ++i) {
        a[i] = std::bit_cast<std::uint32_t>(static_cast<float>(i));
    }
    const auto run = [&] {
        Device device(2048);
        write_words(device, 0, a);
        write_words(device, 512, a);
        const std::vector<std::uint32_t> params = {0, 512, 1024, 100};
        const auto stats =
            device.launch(program, Dim2{.x = 4, .y = 1}, Dim2{.x = 32, .y = 1}, params);
        EXPECT_TRUE(stats.has_value());
        return std::make_pair(*stats, read_words(device, 1024, 100));
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(first.first, second.first);
    EXPECT_EQ(first.second, second.second);
}

} // namespace
