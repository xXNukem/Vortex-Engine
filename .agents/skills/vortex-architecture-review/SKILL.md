---
name: vortex-architecture-review
description: Check architecture, public APIs, modules, integration and third-party usability in Vortex Engine against the project-approved architecture contract. Use for new subsystems, API redesigns, ownership questions, port integration and major engine proposals.
---

# Vortex architecture review

1. Read the repository-root `AGENTS.md`, `docs/architecture-contract.md` (the **only normative architecture**), `docs/architecture.md` (current implementation) and relevant headers, sources and CMake targets.
2. Determine whether the requested work fits an **existing module**. If so, recommend the smallest change and name the exact source/header/test locations. Do **not** invent a new architecture.
3. For a genuine structural proposal, explain (a) observed problem, (b) current owner, (c) exact change to boundaries/dependencies, (d) effect on independent users and the existing low-level API, (e) effect on Extremis RE, (f) verification/migration. Stop and request explicit owner approval **before** changing architecture.
4. Review for acyclic dependencies; no game-specific logic in Vortex; optional SDL3/platform backends; no mandatory physics/ECS/game loop; no duplicated renderer; high-level usability and low-level fidelity both preserved.
5. On approved changes, update the contract's decision history, descriptive documentation and focused tests together. Keep engine code unchanged when the task is only to document or review architecture.
6. End with a brief verdict: compliant / needs owner decision, touched modules and tests. Never claim tests passed if not run.
