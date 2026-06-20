#include <DeepSeer/Core/Buffer.hpp>
#include <gtest/gtest.h>

#include <string>

using namespace DeepSeer;

// ===========================================================================
// Slice tests
// ===========================================================================

TEST(SliceTest, DefaultConstruction)
{
    Slice s;
    EXPECT_EQ(s.length(), 0);
    EXPECT_EQ(s.capacity(), Slice::kDefaultCapacity);
    EXPECT_EQ(s.available(), Slice::kDefaultCapacity);
}

TEST(SliceTest, ConstructFromData)
{
    std::byte const data[] = {std::byte{0x52}, std::byte{0x42}, std::byte{0x53}};
    Slice s(data, 3);
    EXPECT_EQ(s.length(), 3);
    EXPECT_EQ(s.available(), 0);
    auto view = s.data();
    EXPECT_EQ(view[0], std::byte{0x52});
    EXPECT_EQ(view[2], std::byte{0x53});
}

TEST(SliceTest, ReserveAndCommit)
{
    Slice s(64);
    auto res = s.reservation();
    EXPECT_EQ(res.size(), 64);
    res[0] = std::byte{0xFF};
    s.commit(1);
    EXPECT_EQ(s.length(), 1);
    EXPECT_EQ(s.data()[0], std::byte{0xFF});
}

TEST(SliceTest, MultipleCommits)
{
    Slice s(64);
    auto res = s.reservation();
    res[0] = std::byte{0xAA};
    s.commit(1);

    res = s.reservation();
    EXPECT_EQ(res.size(), 63);
    res[0] = std::byte{0xBB};
    s.commit(1);

    EXPECT_EQ(s.length(), 2);
    EXPECT_EQ(s.available(), 62);
    EXPECT_EQ(s.data()[0], std::byte{0xAA});
    EXPECT_EQ(s.data()[1], std::byte{0xBB});
}

TEST(SliceTest, Drain)
{
    std::byte const data[] = {std::byte{1}, std::byte{2}, std::byte{3}};
    Slice s(data, 3);
    s.drain(2);
    EXPECT_EQ(s.length(), 1);
    EXPECT_EQ(s.data()[0], std::byte{3});
}

TEST(SliceTest, DrainAll)
{
    std::byte const data[] = {std::byte{1}, std::byte{2}};
    Slice s(data, 2);
    s.drain(2);
    EXPECT_EQ(s.length(), 0);
    EXPECT_TRUE(s.data().empty());
}

TEST(SliceTest, DrainZeroIsNoop)
{
    std::byte const data[] = {std::byte{1}};
    Slice s(data, 1);
    s.drain(0);
    EXPECT_EQ(s.length(), 1);
    EXPECT_EQ(s.data()[0], std::byte{1});
}

TEST(SliceTest, FillToCapacity)
{
    Slice s(4);
    auto res = s.reservation();
    res[0] = std::byte{0x10};
    res[1] = std::byte{0x20};
    res[2] = std::byte{0x30};
    res[3] = std::byte{0x40};
    s.commit(4);
    EXPECT_EQ(s.length(), 4);
    EXPECT_EQ(s.available(), 0);
    EXPECT_TRUE(s.reservation().empty());
}

// ===========================================================================
// Buffer — basic operations
// ===========================================================================

TEST(BufferTest, EmptyBuffer)
{
    Buffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.length(), 0);
    EXPECT_EQ(buf.toString(), "");
    EXPECT_TRUE(buf.linearize().empty());
    EXPECT_TRUE(buf.slices().empty());
}

TEST(BufferTest, AddAndLinearize)
{
    Buffer buf;
    buf.add("Deep");
    buf.add("Seer");
    EXPECT_EQ(buf.length(), 8);
    EXPECT_EQ(buf.toString(), "DeepSeer");
}

TEST(BufferTest, AddEmptyString)
{
    Buffer buf;
    buf.add("");
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.length(), 0);
}

TEST(BufferTest, ReserveCommit)
{
    Buffer buf;
    auto res = buf.reserve(4);
    ASSERT_GE(res.size(), 4);
    res[0] = std::byte{'R'};
    res[1] = std::byte{'B'};
    buf.commit(2);
    EXPECT_EQ(buf.length(), 2);
    EXPECT_EQ(buf.toString(), "RB");
}

TEST(BufferTest, MultipleReserveCommitCycles)
{
    Buffer buf;
    for (int i = 0; i < 5; ++i) {
        auto res = buf.reserve(4);
        ASSERT_GE(res.size(), 1);
        res[0] = std::byte('A' + i);
        buf.commit(1);
    }
    EXPECT_EQ(buf.length(), 5);
    EXPECT_EQ(buf.toString(), "ABCDE");
}

// ===========================================================================
// Buffer — drain (the main optimization target)
// ===========================================================================

TEST(BufferTest, DrainPartialWithinOneSlice)
{
    Buffer buf;
    buf.add("abcdef");
    buf.drain(3);
    EXPECT_EQ(buf.length(), 3);
    EXPECT_EQ(buf.toString(), "def");
}

TEST(BufferTest, DrainEntireBuffer)
{
    Buffer buf;
    buf.add("hello");
    buf.drain(5);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.length(), 0);
    EXPECT_EQ(buf.toString(), "");
    EXPECT_TRUE(buf.slices().empty());
}

TEST(BufferTest, DrainZeroIsNoop)
{
    Buffer buf;
    buf.add("data");
    buf.drain(0);
    EXPECT_EQ(buf.length(), 4);
    EXPECT_EQ(buf.toString(), "data");
}

TEST(BufferTest, DrainOnEmptyBuffer)
{
    Buffer buf;
    buf.drain(0);
    EXPECT_TRUE(buf.empty());
}

// Force multiple slices by adding data larger than kDefaultCapacity.
// Each add of >16KiB forces a new slice allocation.
TEST(BufferTest, DrainAcrossManySlices)
{
    Buffer buf;
    size_t const chunkSize = Slice::kDefaultCapacity; // 16 KiB
    int const numChunks = 8;

    // Build a buffer with multiple slices, each filled with a distinct byte.
    for (int i = 0; i < numChunks; ++i) {
        std::string chunk(chunkSize, static_cast<char>('A' + i));
        buf.add(chunk);
        // Fill the current slice so the next add creates a new one.
        // Each chunk exactly fills a slice since kDefaultCapacity == chunkSize.
    }

    EXPECT_EQ(buf.length(), chunkSize * numChunks);

    // Drain the first 3.5 slices worth of data.
    size_t drainAmount = chunkSize * 3 + chunkSize / 2;
    buf.drain(drainAmount);

    EXPECT_EQ(buf.length(), chunkSize * numChunks - drainAmount);

    // The first readable byte should be from the 4th chunk (index 3), halfway in.
    auto lin = buf.linearize();
    EXPECT_EQ(static_cast<char>(lin[0]), 'D');
}

TEST(BufferTest, DrainExactlyOneSlice)
{
    Buffer buf;
    std::string chunk(Slice::kDefaultCapacity, 'X');
    buf.add(chunk);
    buf.add("tail");

    size_t slicesBefore = buf.slices().size();
    buf.drain(Slice::kDefaultCapacity);

    EXPECT_LT(buf.slices().size(), slicesBefore);
    EXPECT_EQ(buf.length(), 4);
    EXPECT_EQ(buf.toString(), "tail");
}

TEST(BufferTest, DrainExactlyMultipleSlices)
{
    Buffer buf;
    size_t const chunkSize = Slice::kDefaultCapacity;

    for (int i = 0; i < 5; ++i) {
        std::string chunk(chunkSize, static_cast<char>('0' + i));
        buf.add(chunk);
    }

    // Drain exactly 3 slices.
    buf.drain(chunkSize * 3);
    EXPECT_EQ(buf.length(), chunkSize * 2);

    auto lin = buf.linearize();
    // Remaining data starts with chunk '3'.
    EXPECT_EQ(static_cast<char>(lin[0]), '3');
    EXPECT_EQ(static_cast<char>(lin[chunkSize]), '4');
}

TEST(BufferTest, RepeatedDrainOneByteAtATime)
{
    Buffer buf;
    buf.add("abcdefghij"); // 10 bytes
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(buf.length(), 10u - i);
        buf.drain(1);
    }
    EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, DrainThenAddAgain)
{
    Buffer buf;
    buf.add("hello");
    buf.drain(5);
    EXPECT_TRUE(buf.empty());

    buf.add("world");
    EXPECT_EQ(buf.length(), 5);
    EXPECT_EQ(buf.toString(), "world");
}

// ===========================================================================
// Buffer — length caching correctness
// ===========================================================================

TEST(BufferTest, LengthTrackedThroughAddDrainCycles)
{
    Buffer buf;

    buf.add("12345");
    EXPECT_EQ(buf.length(), 5);

    buf.add("67890");
    EXPECT_EQ(buf.length(), 10);

    buf.drain(3);
    EXPECT_EQ(buf.length(), 7);

    buf.drain(7);
    EXPECT_EQ(buf.length(), 0);
    EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, LengthTrackedThroughReserveCommit)
{
    Buffer buf;
    buf.add("abc");
    EXPECT_EQ(buf.length(), 3);

    auto res = buf.reserve(8);
    // reserve itself doesn't change length.
    EXPECT_EQ(buf.length(), 3);

    res[0] = std::byte{'d'};
    res[1] = std::byte{'e'};
    buf.commit(2);
    EXPECT_EQ(buf.length(), 5);
    EXPECT_EQ(buf.toString(), "abcde");
}

TEST(BufferTest, LengthTrackedThroughMove)
{
    Buffer a, b;
    a.add("aaa");
    b.add("bbbbb");

    a.move(b);
    EXPECT_EQ(a.length(), 8);
    EXPECT_EQ(b.length(), 0);
    EXPECT_TRUE(b.empty());
}

TEST(BufferTest, LengthAfterMoveAndDrain)
{
    Buffer a, b;
    a.add("hello");
    b.add("world");

    a.move(b);
    EXPECT_EQ(a.length(), 10);

    a.drain(5);
    EXPECT_EQ(a.length(), 5);
    EXPECT_EQ(a.toString(), "world");
}

// ===========================================================================
// Buffer — move
// ===========================================================================

TEST(BufferTest, MoveBasic)
{
    Buffer a, b;
    a.add("hello");
    b.add(" DeepSeer");
    a.move(b);
    EXPECT_EQ(a.toString(), "hello DeepSeer");
    EXPECT_TRUE(b.empty());
}

TEST(BufferTest, MoveFromEmptyBuffer)
{
    Buffer a, b;
    a.add("data");
    a.move(b); // move empty into non-empty
    EXPECT_EQ(a.length(), 4);
    EXPECT_EQ(a.toString(), "data");
    EXPECT_TRUE(b.empty());
}

TEST(BufferTest, MoveIntoEmptyBuffer)
{
    Buffer a, b;
    b.add("data");
    a.move(b);
    EXPECT_EQ(a.length(), 4);
    EXPECT_EQ(a.toString(), "data");
    EXPECT_TRUE(b.empty());
}

TEST(BufferTest, MoveBothEmpty)
{
    Buffer a, b;
    a.move(b);
    EXPECT_TRUE(a.empty());
    EXPECT_TRUE(b.empty());
}

TEST(BufferTest, MovePreservesMultipleSlices)
{
    Buffer a, b;
    size_t const chunkSize = Slice::kDefaultCapacity;
    a.add(std::string(chunkSize, 'A'));
    b.add(std::string(chunkSize, 'B'));
    b.add(std::string(chunkSize, 'C'));

    size_t aSlicesBefore = a.slices().size();
    size_t bSlicesCount = b.slices().size();
    a.move(b);

    EXPECT_EQ(a.slices().size(), aSlicesBefore + bSlicesCount);
    EXPECT_EQ(a.length(), chunkSize * 3);
}

// ===========================================================================
// Buffer — linearize and toString
// ===========================================================================

TEST(BufferTest, LinearizeAfterPartialDrain)
{
    Buffer buf;
    buf.add("abcdefghij");
    buf.drain(4);
    auto lin = buf.linearize();
    EXPECT_EQ(lin.size(), 6);
    std::string result(reinterpret_cast<char const*>(lin.data()), lin.size());
    EXPECT_EQ(result, "efghij");
}

TEST(BufferTest, LinearizeMultipleSlices)
{
    Buffer buf;
    size_t const chunkSize = Slice::kDefaultCapacity;
    std::string full;
    for (int i = 0; i < 4; ++i) {
        std::string chunk(chunkSize, static_cast<char>('W' + i));
        buf.add(chunk);
        full += chunk;
    }
    EXPECT_EQ(buf.toString(), full);
}

// ===========================================================================
// Buffer — large data / stress
// ===========================================================================

TEST(BufferTest, LargeDataSpansMultipleSlices)
{
    // A single add larger than kDefaultCapacity should still work.
    size_t const totalSize = Slice::kDefaultCapacity * 4 + 123;
    std::string data(totalSize, 'Z');
    // Make data non-uniform so we can verify content.
    for (size_t i = 0; i < totalSize; ++i) {
        data[i] = static_cast<char>('A' + (i % 26));
    }

    Buffer buf;
    buf.add(data);
    EXPECT_EQ(buf.length(), totalSize);
    EXPECT_EQ(buf.toString(), data);
}

TEST(BufferTest, ManySmallAddsAndFullDrain)
{
    Buffer buf;
    int const n = 1000;
    for (int i = 0; i < n; ++i) {
        buf.add("x");
    }
    EXPECT_EQ(buf.length(), static_cast<size_t>(n));

    buf.drain(n);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.length(), 0);
}

TEST(BufferTest, InterleavedAddAndDrain)
{
    Buffer buf;
    size_t totalAdded = 0;
    size_t totalDrained = 0;

    for (int round = 0; round < 50; ++round) {
        std::string chunk(100, static_cast<char>('A' + (round % 26)));
        buf.add(chunk);
        totalAdded += 100;

        size_t drainNow = 70;
        buf.drain(drainNow);
        totalDrained += drainNow;

        EXPECT_EQ(buf.length(), totalAdded - totalDrained);
    }

    // Drain everything remaining.
    buf.drain(buf.length());
    EXPECT_TRUE(buf.empty());
}

TEST(BufferTest, DrainLargeAcrossManySlicesAtOnce)
{
    Buffer buf;
    size_t const chunkSize = Slice::kDefaultCapacity;
    int const numChunks = 20;

    for (int i = 0; i < numChunks; ++i) {
        buf.add(std::string(chunkSize, static_cast<char>('a' + (i % 26))));
    }

    EXPECT_EQ(buf.length(), chunkSize * numChunks);

    // Drain 15 slices at once — this is where the batch erase optimization matters.
    buf.drain(chunkSize * 15);
    EXPECT_EQ(buf.length(), chunkSize * 5);
    EXPECT_EQ(buf.slices().size(), 5u);

    auto lin = buf.linearize();
    // First byte of remaining data should be from chunk index 15.
    EXPECT_EQ(static_cast<char>(lin[0]), static_cast<char>('a' + (15 % 26)));
}
