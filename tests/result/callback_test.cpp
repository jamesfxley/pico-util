// SPDX-License-Identifier: MIT
// Callback<Ret(Args...)> (FR-5, FR-10). The empty-is-a-no-op contract is the
// one that matters: it is what lets a sink be left unbound without a null check
// at every call site, so a test that only ever invokes bound callbacks proves
// nothing.
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

    // Reads and mutates total_ deliberately: a scale() that ignored the
    // object would pass through a static trampoline just as well and prove
    // nothing about bind() reaching the bound instance.
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

// FR-5: a callback that returns a value has no unbound state. Its only
// candidate return would be Ret{}, which for a Result is success -- an unset
// sink reporting that the data went through. The default constructor is
// withheld, so the state cannot be written; asserted on the trait, which
// evaluates without failing this translation unit's build.
enum class SinkError : unsigned char { NONE = 0, FULL };

TEST(CallbackTest, AValueReturningCallbackHasNoUnboundState) {
    static_assert(std::is_default_constructible_v<Callback<void(int)>>);
    static_assert(!std::is_default_constructible_v<Callback<int(int)>>);
    static_assert(!std::is_default_constructible_v<
                  Callback<pico_util::Result<void, SinkError>(int)>>);
    SUCCEED();
}

// The explicit route to the same state is refused too, in the builds that
// check it: a null function pointer handed to a value-returning Callback.
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

    // The context is borrowed, not copied: a change to it is visible.
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
// the bound object's own state part of the answer.
TEST(CallbackTest, CarriesMoreThanOneArgument) {
    Counter counter;
    counter.bump(1);
    const auto scaler =
        Callback<int(int, int)>::bind<Counter, &Counter::scale>(&counter);
    EXPECT_EQ(scaler(5, 7), 42);
    EXPECT_EQ(counter.calls(), 1);
}

// FR-5: a function pointer and a context pointer, so it copies across a seam
// without allocating and without a vtable.
TEST(CallbackTest, IsTriviallyCopyableAndPointerSized) {
    static_assert(std::is_trivially_copyable_v<Callback<void()>>);
    static_assert(std::is_trivially_copyable_v<Callback<int(int, int)>>);
    static_assert(sizeof(Callback<void()>) == 2 * sizeof(void*));
    SUCCEED();
}

// FR-10: bind() takes a non-const member function. A const one has a different
// pointer type and does not bind -- asserted as a requires-expression, which
// evaluates the refusal without failing this translation unit's build.
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
