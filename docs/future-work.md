# Future work

Each deferral has one entry in the form `EN-03.7` defines.

## FW-1 — Per-allocation tracing

### What is absent

`HeapWatermark` reports net growth since a baseline. It cannot see an
allocation and free that net to zero inside one observation window. A test that
asserts "this path allocates nothing" therefore asserts only that the path ends
no larger than it started. A trace of every allocation would distinguish the
two.

### Why it is not built

The Pico SDK wraps `malloc` and `free` and overrides `operator new` and
`delete`. A second per-allocation hook would be a link conflict and not an
observation. The specification records this under "Constraints and assumptions",
because it drives the design and does more than explain it.

A consumer that is willing to give up `pico_malloc` could hook the allocator
itself. That decision belongs to an application and not to a utility library
that every image in the family links.

### What would have to be true to build it

Either the SDK would have to gain a documented interposition point that does not
conflict with its own wrappers, or the library would have to gain a mode that is
explicitly incompatible with `pico_malloc` and reports that at link time and not
at runtime.

### Version cost

The change would not be breaking under `EN-06.1`. The observation surface would
be additive, and `HeapWatermark` and its guarantee are unaffected (MR-1).

### Affected repositories

pico-util. pico-http and pico-bno085 consume the zero-heap guarantee and would
not change (MR-1).
