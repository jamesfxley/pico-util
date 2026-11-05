// SPDX-License-Identifier: MIT
// Empty-callback and member-binding tests (FR-5, FR-10).
#include "pico_util/result/callback.hpp"

#include <gtest/gtest.h>

#include <type_traits>

#include "pico_util/result/result.hpp"

using pico_util::Callback;

namespace {

int add_context(void* ctx, int addend) {
    return *static_cast<int*>(ctx) + addend;
}

class Counter {
   public:
    void bump(int by) { total_ += by; }

    // Instance state distinguishes binding from a static trampoline.
    int scale(int by, int times) {
        ++calls_;
        return (by + total_) * times;
    }

    [[nodiscard]] int calls() const { return calls_; }

    [[nodiscard]] int total() const { return total_; }

   private:
    int total_ = 0;
    int calls_ = 0;
};

TEST(CallbackTest, EmptyCallbackIsANoOp) {
    const Callback<void(int)> unbound;
    EXPECT_FALSE(static_cast<bool>(unbound));
    unbound(5);  // must be safe to call
    SUCCEED();
}

// Non-void callbacks cannot default-construct an empty state (FR-5).
enum class SinkError : unsigned char { NONE = 0, FULL };

TEST(CallbackTest, AValueReturningCallbackHasNoUnboundState) {
    static_assert(std::is_default_constructible_v<Callback<void(int)>>);
    static_assert(!std::is_default_constructible_v<Callback<int(int)>>);
    static_assert(!std::is_default_constructible_v<
                  Callback<pico_util::Result<void, SinkError>(int)>>);
    SUCCEED();
}

// An explicit route to the same state fails too, in the builds that check it: a
// null function pointer passed to a value-returning Callback.
TEST(CallbackTest, ANullTargetForAValueReturningCallbackIsRefused) {
#ifdef NDEBUG
    GTEST_SKIP() << "the refusal is an assert, which NDEBUG removes";
#else
    using IntCallback = Callback<int(int)>;
    EXPECT_DEATH(
        {
            const IntCallback empty(nullptr, nullptr);
            static_cast<void>(empty);
        },
        "a Callback returning a value has no unbound state");
#endif
}

TEST(CallbackTest, InvokesAFreeFunctionWithItsContext) {
    int base = 10;
    const Callback<int(int)> bound(add_context, &base);
    EXPECT_TRUE(static_cast<bool>(bound));
    EXPECT_EQ(bound(5), 15);

    // The context is borrowed and not copied, so a change to it is visible.
    base = 100;
    EXPECT_EQ(bound(5), 105);
}

TEST(CallbackTest, BindReachesTheObject) {
    Counter counter;
    const auto bumper =
        Callback<void(int)>::bind<Counter, &Counter::bump>(&counter);
    bumper(3);
    bumper(4);
    EXPECT_EQ(counter.total(), 7);
}

// More than one argument, and a non-void return through the trampoline, with
// the bound object's own state part of the result.
TEST(CallbackTest, CarriesMoreThanOneArgument) {
    Counter counter;
    counter.bump(1);
    const auto scaler =
        Callback<int(int, int)>::bind<Counter, &Counter::scale>(&counter);
    EXPECT_EQ(scaler(5, 7), 42);
    EXPECT_EQ(counter.calls(), 1);
}

// A function pointer and a context pointer, so a callback copies across a seam
// without allocating and without a vtable (FR-5).
TEST(CallbackTest, IsTriviallyCopyableAndPointerSized) {
    static_assert(std::is_trivially_copyable_v<Callback<void()>>);
    static_assert(std::is_trivially_copyable_v<Callback<int(int, int)>>);
    static_assert(sizeof(Callback<void()>) == 2 * sizeof(void*));
    SUCCEED();
}

// A requires-expression checks that a const member cannot bind (FR-10).
class Reader {
   public:
    int read(int value) {
        offset_ += value;
        return offset_;
    }

    [[nodiscard]] int peek(int value) const { return value + offset_; }

   private:
    int offset_ = 1;
};

template <auto Method>
concept BindsAsReader = requires(Reader* reader) {
    Callback<int(int)>::bind<Reader, Method>(reader);
};

TEST(CallbackTest, AConstMemberFunctionDoesNotBind) {
    static_assert(BindsAsReader<&Reader::read>);
    static_assert(!BindsAsReader<&Reader::peek>);
    SUCCEED();
}

}  // namespace
