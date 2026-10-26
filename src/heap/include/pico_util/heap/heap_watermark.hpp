// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
// newlib declares the malloc family with _NOTHROW in <stdlib.h> and without it
// in <malloc.h>. Clang models that attribute as an exception specification and
// rejects the declaration that adds one, so <stdlib.h> must be seen first. It
// belongs in this block rather than beside <malloc.h> below, where the include
// sort would rank the C header above it and reinstate the bad order.
#include <cstdlib>

#if defined(__has_include)
// glibc's version macros, read below. <cstdlib> has already brought glibc's
// own <features.h> in; naming it keeps the macros' source explicit, and the
// guard keeps a consumer's unrelated features.h out on any other libc.
#if defined(__GLIBC__)
#include <features.h>
#endif
#if __has_include(<malloc.h>)
#include <malloc.h>
// Feature-detection flags, not values: they must be testable by #if, which a
// constexpr constant cannot be. glibc 2.33 added mallinfo2 and deprecated
// mallinfo; newlib provides only the latter.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PICO_UTIL_HAVE_MALLINFO 1
#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
// A feature-detection flag, as above.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PICO_UTIL_HAVE_MALLINFO2 1
#endif
#endif
#endif

/// \file heap_watermark.hpp
/// Post-init heap-growth observability for MR-1 ("zero post-init heap"). The
/// Pico SDK already wraps malloc/free and overrides operator new/delete
/// (pico_malloc, pico_cxx_options), so a second per-allocation hook is not
/// available without a link conflict. Net heap growth since a baseline —
/// captured from uordblks (MR-2) — is a weaker but still falsifiable proxy:
/// it cannot see an alloc/free pair that nets to zero bytes, but it catches
/// (and reports the size of) any steady-state growth. A consumer arms it once
/// its own initialisation is complete and exposes growth_bytes() in its stats,
/// instead of re-deriving the baseline/arming dance. Both newlib (firmware) and
/// glibc (host tests) provide mallinfo via <malloc.h>; on a platform without
/// it, in_use_bytes() reports 0 and growth is always 0, and measuring() says
/// so.
///
/// glibc 2.33 deprecated mallinfo() for mallinfo2(), whose fields are size_t
/// rather than int. newlib — the firmware libc, and the one that matters for
/// MR-1 — has only mallinfo(), so both spellings are kept and the deprecated
/// one is used solely where mallinfo2 is absent.

namespace pico_util {

namespace detail {
/// Whether mallinfo reads the heap this program allocates from: false where
/// the platform has none (MR-2), and under AddressSanitizer, whose allocator
/// mallinfo does not see, so every reading is 0. A property of the build, not
/// of the heap's contents: a working heap with nothing on it also reads 0.
/// Read it through HeapWatermark::measuring().
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
///
/// Arm it once initialisation is complete, then read growth_bytes() to gate a
/// "zero post-init heap" claim on a measurement rather than an assertion. It
/// cannot see an allocate/free pair that nets to zero within one observation
/// window; it does catch, and report the size of, steady-state growth.
class HeapWatermark {
   public:
    /// Bytes currently allocated on the C heap (mallinfo uordblks), or 0 when
    /// the platform has no mallinfo.
    // mallinfo is not thread safe. The library's scope makes the
    // single-threaded caller the
    // consumer's contract to keep, and the value is a diagnostic proxy rather
    // than an allocator decision.
    ///
    /// MR-2: mallinfo2 where the platform has it, mallinfo where it has only
    /// that, and 0 where it has neither — reporting rather than failing to
    /// compile, so a target with no introspection still builds and a caller
    /// can tell from the reading that there is none.
    static std::size_t in_use_bytes() noexcept {
#if defined(PICO_UTIL_HAVE_MALLINFO2)
        // Single-threaded by the caller's contract, stated above.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const struct mallinfo2 info = mallinfo2();
        return static_cast<std::size_t>(info.uordblks);
#elif defined(PICO_UTIL_HAVE_MALLINFO)
        // Same single-threaded contract as the mallinfo2 arm above.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const struct mallinfo info = mallinfo();
        return static_cast<std::size_t>(info.uordblks);
#else
        return 0;
#endif
    }

    /// Capture the current heap usage as the baseline (call once init is done).
    void arm() noexcept {
        baseline_ = in_use_bytes();
        armed_ = true;
    }

    /// Whether arm() has been called. False means growth_bytes() reports 0
    /// because there is no baseline, not because nothing was allocated.
    /// MR-8: whether a baseline has been captured, so "nothing grew" and
    /// "there is no baseline" are distinguishable.
    [[nodiscard]] bool armed() const noexcept { return armed_; }

    /// MR-8: whether the heap can be read here, so a growth of 0 is a
    /// measurement rather than the absence of one. False where the platform
    /// has no mallinfo (MR-2), and under AddressSanitizer, whose allocator
    /// mallinfo does not see: either way every reading is 0. Decided by the
    /// build rather than by a reading, because a working heap with nothing on
    /// it -- the zero-heap program MR-1 is about -- also reads 0.
    [[nodiscard]] static constexpr bool measuring() noexcept {
        return detail::HEAP_READABLE;
    }

    /// Heap growth since arm(), clamped at 0 (net shrink reads as 0). Always 0
    /// before arm().
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
