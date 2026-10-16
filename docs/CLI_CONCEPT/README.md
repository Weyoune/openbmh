# Console reference

`openbmh_cli --console` provides a window-oriented command language over the same career session used by the graphical game. It supports interactive use, scripts, independent manager clients and alternative frontends. It is separate from the smaller [JSON request protocol](../protocol.md) selected without `--console`.

```sh
openbmh_cli --console --data "path/to/game" --saves "path/to/saves" --seed 1
```

Enter `ListOptions`, then `go.` on its own line to see the current commands. Use `Help` for signatures. Commands execute as a block when `go.` is read; they are not executed merely by entering a line.

- [Syntax, output, errors and questions](01-grundlagen.md)
- [Windows and navigation](02-fenster.md)
- [Starting and loading careers](03-start.md)
- [Desktop, calendar and saved games](04-schreibtisch.md)
- [Tables and information](05-information.md)
- [Club management](06-verein.md)
- [Matches and season boundaries](07-spieltag.md)
- [Settings](08-einstellungen.md)
- [Text and edition handling](09-texte.md)
- [Client development and tests](10-agenten-und-tests.md)
- [Small examples](12-beispiele.md)
- [Alternative frontends](15-frontends.md)

The [client and agent introduction](../CONSOLE_CLIENTS.md) explains mailbox/socket transport and safe ownership rules. [commands.csv](commands.csv) is a machine-readable command/window index. Its nine-column schema is retained for existing tooling; descriptive provenance columns may be empty, and implementation markers are not a runtime capability query. The running build's `ListOptions` and `Help` take precedence over this index. Recognised operations can still return `E_ENGINE_GAP` when a required engine path is unavailable.

Examples use independently written names and placeholder paths. The repository supplies no original game content, text catalogue or recorded game session.
