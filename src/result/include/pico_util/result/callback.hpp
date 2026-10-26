// SPDX-License-Identifier: MIT
#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

/// \file callback.hpp
/// Non-owning callback: a function pointer plus an opaque context pointer
/// (FR-5).
///
/// This is the whole callback mechanism — no `std::function`, no heap, no
/// vtable, and nothing that allocates when a sink is bound. A default-
/// constructed `Callback<void(...)>` is empty and invoking it is a no-op, so a
/// notification sink can be left unset without a null check at every call site.
/// A callback that returns a value has no empty state (FR-5): there is no value
/// an unbound one could return that would not be a report nobody made.
///
/// It lives beside Result because the two are the same decision seen from the
/// two directions a seam is crossed: a fallible call returns a Result, and a
/// notification the caller cannot return through goes out as a Callback.

namespace pico_util {

/// Primary template, declared but not defined: a Callback is named by a
/// function signature, as in `Callback<void(int)>`.
template <typename Signature>
class Callback;

/// A callback with signature `Ret(Args...)`.
///
/// The stored function takes the context pointer first, then the signature's
/// own arguments. Neither the function nor the context is owned: both must
/// outlive the Callback.
template <typename Ret, typename... Args>
class Callback<Ret(Args...)> {
   public:
    /// The stored function: the context pointer, then the signature's own
    /// arguments.
    using Fn = Ret (*)(void* ctx, Args...);

    /// Construct an empty callback, which is a no-op when invoked.
    ///
    /// Only where `Ret` is `void`. An unbound callback that returns a value
    /// has to return *something*, and `Ret{}` is the only candidate -- which
    /// for a `Result` is success, so an unset sink would tell its caller the
    /// data went through. Withholding the constructor makes that state
    /// impossible to write rather than a precondition to remember.
    constexpr Callback() noexcept
        requires std::is_void_v<Ret>
    = default;

    /// \param fn the function to call. Must not be null where `Ret` is not
    ///        `void`, for the reason the default constructor gives; asserted
    ///        in a build with assertions enabled.
    /// \param ctx passed back to `fn` as its first argument; must outlive this.
    constexpr Callback(Fn fn, void* ctx) : fn_(fn), ctx_(ctx) {
        if constexpr (!std::is_void_v<Ret>) {
            assert(fn != nullptr &&
                   "a Callback returning a value has no unbound state");
        }
    }

    /// \return whether a function is bound.
    constexpr explicit operator bool() const { return fn_ != nullptr; }

    /// Invoke the bound function.
    ///
    /// \param args forwarded to the bound function after the context pointer.
    /// \return the function's result. Only a `void` callback can be empty,
    ///         and invoking an empty one is a no-op rather than a precondition
    ///         violation, which is what removes the null check from every call
    ///         site.
    Ret operator()(Args... args) const {
        if constexpr (std::is_void_v<Ret>) {
            if (fn_ != nullptr) {
                fn_(ctx_, std::forward<Args>(args)...);
            }
        } else {
            return fn_(ctx_, std::forward<Args>(args)...);
        }
    }

    /// Bind a member function through a generated trampoline.
    ///
    /// `Method` is a non-const member function. A const one has a different
    /// pointer type and does not bind here; a caller with one either drops the
    /// `const` or wraps it in a free function taking the context pointer.
    ///
    /// \param obj the object to invoke `Method` on. It must outlive the
    ///        returned Callback, which does not own it.
    /// \return a Callback invoking `obj->*Method`.
    ///
    /// FR-10. Owns neither the object nor the member function.
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
