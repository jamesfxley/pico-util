# Future work

Work deliberately not done, one entry per deferral, in the form `EN-03.7`
defines.

## FW-1 — Per-allocation tracing

### What is absent

`HeapWatermark` reports net growth since a baseline. It cannot see an
allocate/free pair that nets to zero inside one observation window, so a test
asserting "this path allocates nothing" is really asserting "this path ends no
larger than it started". A trace of every allocation would distinguish the two.

### Why it is not built

The Pico SDK wraps `malloc`/`free` and overrides `operator new`/`delete`. A
second per-allocation hook is a link conflict rather than an observation. The
specification's "Constraints and assumptions" records this, because it drives
the design rather than merely explaining it.

A consumer willing to give up `pico_malloc` could hook the allocator itself, but
that is a decision for an application, not for a utility library that every
image in the family links.

### What would have to be true to build it

Either the SDK grows a documented interposition point that does not conflict
with its own wrappers, or the library gains a mode that is explicitly
incompatible with `pico_malloc` and says so at link time rather than at runtime.

### Version cost

Not breaking under `EN-06.1`: the observation surface would be additive, and
`HeapWatermark` and MR-1 are unaffected.

### Affected repositories

pico-util. pico-http and pico-bno085 consume MR-1's guarantee but would not
change.
