# pico-util documentation

| Document | What it is | Read it when |
|---|---|---|
| [traceability-exceptions.txt](traceability-exceptions.txt) | Requirements that no source file can cite (EN-02.7) | Reviewing uncitable requirements |
| [orientation.md](orientation.md) | How this repository is built and gated, how it is laid out, and its invariants and traps | Working in it for the first time, or before an edit |
| [technical-specification.md](technical-specification.md) | The normative document: scope, constraints, resource budgets, and every requirement (`AR`/`BR`/`CR`/`DR`/`FR`/`MR`/`OR`) with its pass criterion | Deciding what the library must do, or citing a requirement from code |
| [architecture.md](architecture.md) | A description of how the implementation is put together: the header-only structure and the design of each module, which covers the byte-order families, `Result` and `Callback`, the ring and map containers, the two publication strategies, the mallinfo proxy and the allocation harness, and what belongs in the library | Working out where a change belongs |
| [usage-guide.md](usage-guide.md) | Practical use beyond the README: linking the library, choosing a byte-order family and a publication strategy, error types and container capacity, the allocation harness, measuring heap growth after initialisation, and include order | Building an application on the library |
| [future-work.md](future-work.md) | Work deliberately not built, with one entry per deferral, its reason and what would have to be true to build it | Asking whether something is absent on purpose, or before proposing it |
| [deviations.md](deviations.md) | Standing (scope-wide) deviations from the coding standard, each with a rationale, a risk assessment and a review date | Adding or reviewing a suppression |
| [deviation-budget.txt](deviation-budget.txt) | Per-language suppression ceilings, which only ratchet down | Adding or reviewing a suppression |
| [requirement-prefixes.txt](requirement-prefixes.txt) | The requirement prefix convention that review checks | Adding a requirement under a new prefix |
| [GitHub Releases](https://github.com/jamesfxley/pico-util/releases) | What changed in each release, and what to change in your code when an API broke | Upgrading across a release |

The specification is the only normative document here. `architecture.md`
describes the implementation as it is. Work that is deliberately not done is an
entry in [future-work.md](future-work.md) (`EN-03.7`), and the issue tracker
holds defects.

A behaviour that `README.md` or `docs/orientation.md` claims and the
specification does not contain is not a commitment. A requirement is not
satisfied until the matrix names what establishes it.

The repository `README.md` is the entry point. `usage-guide.md` covers the rest
of the use of the library: linking it and using each of its modules.

The API reference is generated from the headers and not written by hand, so it
cannot drift from the declarations it documents:

```sh
doxygen docs/Doxyfile      # -> docs/api/html/index.html (gitignored)
```

The `api-docs` gate builds the reference on every run and fails on an
undocumented public entity.

## Conventions

- Requirement IDs are stable and never renumbered (EN-02.3). Code and tests cite
  the ID and not a section number (EN-02.2). Review checks that code or tests
  cite each specification ID, and `requirement-prefixes.txt` records the prefix
  convention.
- The specification states what is true now. It has no dates, no version
  history and no measured values. It may state a normative gate, such as "within
  a CI-enforced budget", but not a measurement. A measured figure is a run
  record and lives beside the rig that produced it (`EN-03.2`).
- A behaviour change lands as one change that contains the specification row, a
  host test and the affected documents.
