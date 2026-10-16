# Working on OpenBMH

Start with [README.md](README.md), [CONTRIBUTING.md](CONTRIBUTING.md) and [docs/architecture.md](docs/architecture.md). Use [docs/CONSOLE_CLIENTS.md](docs/CONSOLE_CLIENTS.md) when writing clients that operate the game.

## Boundaries

- This repository distributes implementation and documentation, not original game content. Do not add original executables, saves, graphics, audio, manuals, text inventories or decompiled output.
- Use synthetic fixtures for public tests. Read a locally supplied game installation only through documented configuration such as `BMH_DATA`; do not bake a developer's filesystem path into code or docs.
- Work within the requested scope. Check existing changes and coordinate shared-file edits. Do not overwrite another contributor's work.
- Do not publish releases, push branches or contact external services unless the task authorises it.

## Code and tests

Keep rules in `src/game`, file readers in `src/formats` and session ownership/rollback in `src/app`. Reuse shared actions from graphical and console frontends. Preserve deterministic random streams, save layout and revision/manager checks. Errors need explicit outcomes; do not invent a gameplay fallback for an unsupported path.

Use semantic identifiers and English comments. Document deliberate compatibility changes. Run checks appropriate to the actual change and report their outcome and limits; do not claim unexecuted platform or runtime tests. Keep generated builds outside tracked source files.

OpenBMH-owned changes use GPL-3.0-only. Third-party notices and full license texts must remain intact, including attribution within upstream license files. Report licensing questions to the maintainers instead of rewriting vendor notices.
