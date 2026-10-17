# Contributing to OpenBMH

Use [issues](https://github.com/Weyoune/openbmh/issues) for reproducible bugs and proposals. For a bug, include the OpenBMH version, platform, game edition, expected behaviour and the smallest sequence of actions that shows the problem. Remove personal paths and account information from diagnostic text.

Keep proprietary game files out of public submissions: no executables, databases, manuals, extracted text, artwork, recordings or saved games. Prefer synthetic inputs and a short description. If an investigation requires privately owned data, coordinate an appropriate process with the maintainers instead of attaching it publicly.

## Changes and review

Keep patches focused on a concrete problem. Describe the resulting behaviour, relevant compatibility decisions and validation. Discuss architecture changes, balance changes and substantial new features before investing in a large patch.

- Use C++20 and the conventions of the surrounding code; write new comments in English.
- Keep gameplay in `src/game`, format handling in `src/formats` and shared session transactions in `src/app`. Frontends translate input and display projections.
- Preserve explicit integer widths, signedness, deterministic random consumption and save compatibility.
- Treat malformed input, stale revisions and unsupported modes explicitly. Do not silently substitute guessed gameplay.
- Mark intentional differences with `OpenBMH deviates from the original:` and explain them in [compatibility documentation](docs/COMPATIBILITY.md).
- Use semantic names for concepts. File offsets and compatibility layout constants belong with format documentation; implementation notes should explain the operation rather than cite disassembly labels.

## Validation

Follow [BUILDING.md](docs/BUILDING.md), build the affected targets and run the appropriate CTest subset. Prefer tests with synthetic data that can run from a clean checkout. Tests needing a player's game installation must be conditional on `BMH_DATA`; unavailable private fixtures must not become public dependencies. State which checks ran and their limits. A build on one operating system does not establish native behaviour on another.

For UI changes, exercise input and state transitions as well as rendering. For session actions, cover rejected/stale input and rollback where those risks are relevant. Avoid making tests restate an implementation without checking behaviour.

## Licensing

Contributions to OpenBMH's own code and documentation are under **GPL-3.0-only**. Submit only work you have the right to contribute. Keep existing third-party copyright and license notices intact; vendored code retains its upstream license. Do not add proprietary resources to tests or examples. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
