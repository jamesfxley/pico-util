# pico-util documentation

| Document | What it is | Read it when |
|---|---|---|
| [traceability-exceptions.txt](traceability-exceptions.txt) | Requirements no source file can cite (EN-02.7). | Reviewing uncitable requirements. |
| [orientation.md](orientation.md) | How this repository is built and gated, how it is laid out, and the repository's invariants and traps | Working in it for the first time, or before an edit |
| [technical-specification.md](technical-specification.md) | **Normative.** Scope, constraints, resource budgets, and every requirement (`AR`/`BR`/`CR`/`DR`/`FR`/`MR`/`OR`) with its pass criterion | Deciding what the library must do, or citing a requirement from code |
| [architecture.md](architecture.md) | Descriptive. How the implementation is put together — the header-only shape and each module's design: the byte-order families, `Result` and `Callback`, the ring and map containers, the two publication strategies, the mallinfo proxy and the allocation harness, and what belongs in the library at all | Working out where a change belongs |
| [usage-guide.md](usage-guide.md) | Practical use beyond the README: linking it, choosing a byte-order family and a publication strategy, errors and capacity refusals, the allocation harness, measuring post-init heap growth, and why include order is load-bearing | Building an application on the library |
| [future-work.md](future-work.md) | Work deliberately not built, one entry per deferral, each with its reason and what would have to be true to build it | Asking whether something is absent on purpose, or before proposing it |
| [deviations.md](deviations.md) | Standing (scope-wide) deviations from the coding standard, each with a rationale, a risk assessment and a review date | Adding or reviewing a suppression |
| [deviation-budget.txt](deviation-budget.txt) | Per-language suppression ceilings. These only ever ratchet down | Same |
| [requirement-prefixes.txt](requirement-prefixes.txt) | The requirement prefix convention checked by review | Adding a requirement under a new prefix |
| [GitHub Releases](https://github.com/jamesfxley/pico-util/releases) | What changed in each release, and what to change in your code when an API broke | Upgrading across a release |

The specification is the only normative document here. `architecture.md`
describes what is, and work deliberately not done is an entry in
[future-work.md](future-work.md) (`EN-03.7`); the issue tracker holds defects.

A behaviour claimed in `README.md` or `docs/orientation.md` but absent from the
specification is not a commitment, and a requirement is not satisfied until the
matrix names what establishes it.

The repository `README.md` is the surface. `usage-guide.md` is the other half:
linking the library and using each of its modules.

The API reference is generated from the headers rather than written by hand, so
it cannot drift from the declarations it documents:

```sh
doxygen docs/Doxyfile      # -> docs/api/html/index.html (gitignored)
```

The `api-docs` gate builds it on every run and fails on an undocumented public
entity.

## Conventions

- Requirement IDs are stable and never renumbered. Code and tests cite the ID,
  never a section number: IDs survive a reorganisation of this document and
  section numbers do not. Review checks that specification IDs are cited by code or tests;
  `requirement-prefixes.txt` records their prefix convention.
- The specification states what is true now — no dates, no version history, no
  measured values. It may state a normative gate ("within a CI-enforced
  budget") but not a measurement; a measured figure is a run record and lives
  beside the rig that produced it (`EN-03.2`).
- A behaviour change lands as one change: the specification row, a host test,
  and the affected documents together.
