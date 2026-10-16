# Developing clients and tests

Start with the [client introduction](../CONSOLE_CLIENTS.md). A client should keep policy separate from transport: the transport submits one block, preserves the whole response and returns ownership, questions, errors and structured records to the policy. The policy then chooses a legal action for its own manager.

For automated checks, use independently created input and synthetic fixtures. Parser tests should exercise quoting, named arguments and block boundaries. Session tests should verify real outcomes such as a rejected stale action leaving state unchanged, a question being resolved by its actual owner, or a cancelled staged offer releasing its suspended operation. A printout matching a proposed answer is not evidence that the game state changed correctly.

Replay checks compare execution with the same binary, data and initial state; they do not prove universal compatibility with every original edition. Keep test outputs private when they contain game text or personal information. Public bug reports should contain minimal independently written steps, relevant version identifiers and the observed failure, without original executables, artwork, recordings or saved games.

Build/test setup is described in [BUILDING](../BUILDING.md). Original-data checks require a user-supplied installation and can be skipped when that input is absent. Do not report a skipped check as passed. New frontend code should reuse session actions rather than implementing another transfer, match or finance engine.
