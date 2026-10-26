// SPDX-License-Identifier: MIT
// The static reading, which needs no armed instance.
#include <cstddef>
#include <pico_util/pico_util.hpp>

namespace doc_examples {
void heap_in_use() {
#include "heap_in_use.inc"
    static_cast<void>(now);
}
}  // namespace doc_examples
