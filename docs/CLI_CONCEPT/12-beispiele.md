# Small console examples

These fragments illustrate the console syntax. They are not a recorded game session. Run each only in the indicated context and inspect the actual response before submitting a dependent block.

## Discover commands

```text
ListOptions
Help
Status
go.
```

## Inspect a running career

```text
State(Desk)
State(Lineup)
Advice
go.
```

`State` requires a running career. A manager client can only inspect its permitted owned views. Read returned IDs before constructing actions that refer to a player or club.

## Open a management window

From the active manager's desktop:

```text
Open(Tactics)
ListOptions
go.
```

Choose a supported setting using the returned help, then close the window before ending the turn. A command executed earlier in a failed batch is not undone.

## Resolve a question

If the current response addresses question ID 42 to this manager and lists `No` as an option, this block selects it:

```text
Answer(42, No)
go.
```

The number 42 is illustrative. Always use the actual ID, owner and options. Do not repeat a previous answer against a new question.

## Finish a turn or advance a match

From the desktop, after completing management decisions:

```text
Kickoff
go.
```

From an active match before half-time:

```text
ShowMatchStats
Run(Halftime)
go.
```

Check the returned minute and current phase before advancing again. Use `Run(End)` after half-time. With multiple managers, the shared clock waits for every required seat. Questions can interrupt these requests.

## Ask a mailbox client to wait

After starting a multi-seat server as described in the [client introduction](../CONSOLE_CLIENTS.md), send `WaitTurn` followed by `go.` to that seat using `--send`. Keep that one request pending until its response arrives. Do not start a second client for the same seat merely because the server has not answered yet.
