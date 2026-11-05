// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
// newlib declares the malloc family with _NOTHROW in <stdlib.h> and without it
// in <malloc.h>. Clang models that attribute as an exception specification and
// rejects the later declaration that adds one, so <stdlib.h> must come first.
// The include stays in this block because beside <malloc.h> the include sort
// would put the C header above <cstdlib> and restore the failing order.
#include <cstdlib>

#if defined(__has_include)
// glibc's version macros are read below. <cstdlib> already includes glibc's
// <features.h>. Including it here names the source of the macros, and the guard
// keeps an unrelated features.h of a consumer out on any other libc.
#if defined(__GLIBC__)
#include <features.h>
#endif
#if __has_include(<malloc.h>)
#include <malloc.h>
// glibc 2.33 added mallinfo2 and deprecated mallinfo. newlib provides only
// mallinfo.
// PICO_UTIL_HAVE_MALLINFO and PICO_UTIL_HAVE_MALLINFO2 are feature-detection
// flags and must be testable by #if, which a constexpr constant cannot be.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PICO_UTIL_HAVE_MALLINFO 1
#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
// A feature-detection flag, for the reason given above.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PICO_UTIL_HAVE_MALLINFO2 1
#endif
#endif
#endif

/// \file heap_watermark.hpp
/// Observe net heap growth since a baseline (MR-1), using uordblks (MR-2).
/// An allocation and free that net to zero are invisible to this proxy (MR-1).

namespace pico_util {

namespace detail {
/// Whether this build can read its allocator through mallinfo (MR-2).
inline constexpr bool HEAP_READABLE =
#if !(defined(PICO_UTIL_HAVE_MALLINFO2) || defined(PICO_UTIL_HAVE_MALLINFO))
    false;
#elif defined(__SANITIZE_ADDRESS__)
    false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    false;
#else
    true;
#endif
#else
    true;
#endif
}  // namespace detail

/// Net C-heap growth since an explicitly captured baseline (MR-1).
class HeapWatermark {
   public:
    /// Bytes in use via mallinfo2, mallinfo, or 0 if neither exists (MR-2).
    /// This is a single-threaded diagnostic. mallinfo is not thread safe.
    static std::size_t in_use_bytes() noexcept {
#if defined(PICO_UTIL_HAVE_MALLINFO2)
        // The caller's contract is single-threaded use, stated in the doc
        // comment.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const struct mallinfo2 info = mallinfo2();
        return static_cast<std::size_t>(info.uordblks);
#elif defined(PICO_UTIL_HAVE_MALLINFO)
        // The single-threaded contract is the same as for mallinfo2 above.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const struct mallinfo info = mallinfo();
        return static_cast<std::size_t>(info.uordblks);
#else
        return 0;
#endif
    }

    /// Capture the current heap usage as the baseline. Call it once
    /// initialisation is done.
    void arm() noexcept {
        baseline_ = in_use_bytes();
        armed_ = true;
    }

    /// Whether a baseline exists, so that "no baseline" is distinguishable
    /// from "nothing grew" (MR-8).
    [[nodiscard]] bool armed() const noexcept { return armed_; }

    /// Whether this build can measure net heap growth (MR-8, MR-2, MR-1).
    [[nodiscard]] static constexpr bool measuring() noexcept {
        return detail::HEAP_READABLE;
    }

    /// Heap growth since arm(), clamped at 0, so a net shrink reads as 0. The
    /// value is 0 before arm().
    [[nodiscard]] std::size_t growth_bytes() const noexcept {
        if (!armed_) {
            return 0;
        }
        const std::size_t used = in_use_bytes();
        return used > baseline_ ? used - baseline_ : 0;
    }

   private:
    std::size_t baseline_ = 0;
    bool armed_ = false;
};

}  // namespace pico_util
