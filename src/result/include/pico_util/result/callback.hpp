// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

/// \file callback.hpp
/// A non-owning function and context pointer, without allocation (FR-5).
/// An empty void callback is a no-op. Non-void callbacks have no empty state
/// (FR-5).

namespace pico_util {

/// A callback named by its function signature, such as Callback<void(int)>.
template <typename Signature>
class Callback;

/// A non-owning callback whose function and context must outlive it.
template <typename Ret, typename... Args>
class Callback<Ret(Args...)> {
   public:
    /// The function type, taking the context before the signature's arguments.
    using Fn = Ret (*)(void* ctx, Args...);

    /// Construct an empty void callback, safe to invoke.
    constexpr Callback() noexcept
        requires std::is_void_v<Ret>
    = default;

    /// \param fn must not be null for non-void Ret. Debug builds assert this.
    /// \param ctx is passed first to fn and must outlive this object.
    constexpr Callback(Fn fn, void* ctx) : fn_(fn), ctx_(ctx) {
        if constexpr (!std::is_void_v<Ret>) {
            assert(fn != nullptr &&
                   "a Callback returning a value has no unbound state");
        }
    }

    /// \return whether a function is bound.
    constexpr explicit operator bool() const { return fn_ != nullptr; }

    /// Invoke the bound function with forwarded arguments.
    /// An empty void callback does nothing.
    Ret operator()(Args... args) const {
        if constexpr (std::is_void_v<Ret>) {
            if (fn_ != nullptr) {
                fn_(ctx_, std::forward<Args>(args)...);
            }
        } else {
            return fn_(ctx_, std::forward<Args>(args)...);
        }
    }

    /// Bind a non-const member function without taking ownership (FR-10).
    /// The object must outlive the callback. A const member does not bind.
    template <typename T, Ret (T::*Method)(Args...)>
    static Callback bind(T* obj) {
        return Callback(
            [](void* ctx, Args... args) -> Ret {
                return (static_cast<T*>(ctx)->*Method)(
                    std::forward<Args>(args)...);
            },
            obj);
    }

   private:
    Fn fn_ = nullptr;
    void* ctx_ = nullptr;
};

}  // namespace pico_util
