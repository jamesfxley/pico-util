// SPDX-License-Identifier: MIT
// A consumer's major/minor compatibility check for a pre-1.0 release.
// A boundary change fails this build until the guide states the new pair.
#include <pico_util/pico_util.hpp>

// Inside a namespace, so the include sort cannot move it above the header.
namespace doc_examples {
#include "version_check.inc"
}  // namespace doc_examples
