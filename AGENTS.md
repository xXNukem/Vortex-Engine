# Vortex Engine — Mandatory agent instructions

## Architectural source of truth

For any task involving Vortex architecture, modules, public API, project structure or cross-module changes, read **`docs/architecture-contract.md` first**, then `docs/architecture.md` and the relevant source files. The contract is normative; the latter describes the current implementation. Do not invent a replacement architecture in each conversation.

The fixed objective is a **complete, independent, modular, extensible and very loosely coupled first-person engine, eventually including Vortex's own development tools**. Today's renderer/audio/input libraries are its foundation, **not** its final scope. Ease of external adoption is a primary design requirement.

## Rules for implementation

- Keep the established module responsibilities and directed, acyclic dependencies from the contract.
- Do not pull In Extremis or any other game's rules, formats, physics or HUD into Vortex.
- Preserve existing low-level APIs while developing optional high-level convenience APIs using the same underlying implementations.
- Keep portable CPU/Core/Audio independent of SDL3; use optional platform backends.
- Make **one justified change at a time**. No large speculative refactors, duplicate implementations or trivial forwarding layers.
- Before editing, identify the *specific owning module and files*, the entry point, the consumers, and the tests that protect the behaviour.
- New public APIs must have concise documentation of behaviour, parameters, ownership/lifetime and threading constraints when relevant.
- Verify with independent Vortex tests and relevant Extremis RE compatibility tests; never silently change original game behaviour.
- A **structural architecture change** needs a written problem/impact proposal and the project owner's **explicit approval before implementation**, then an amendment to `docs/architecture-contract.md`. Do not interpret prior assistant suggestions as approval.

For detailed review of architecture/API changes use the repository skill **`vortex-architecture-review`** when available; the skill follows the same single contract and does not define another architecture.
