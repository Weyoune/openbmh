# Settings

`Settings` operates on the career's normal switch settings. Supported boolean switches, numeric values and ranges are defined by the current projection and command help. The [settings format](../formats/EIN.md) includes reserved and multivalued cells; clients should not replace the raw matrix with their own boolean array.

Frontend capabilities can lock unavailable audio controls. Music preference, effects preference and actual playback-device readiness are different states. Use the projected availability and normal actions. Graphical process options such as `--mute`, `--no-music` or `--volume` are launch options, not new console commands or save fields.

Availability also depends on mode and manager role. A setting rejected by the engine should remain unchanged; do not bypass the rejection by patching a save.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `Settings`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSettings` | [group:enum(Results,Sound,Game,Limits,MenuEdit,Functions,Sportschau,Display,Values,Delays)] | the switch field A1-T9 |
| `ShowSwitch` | key:string | one switch |
| `SetSwitch` | key:string, value:int | set a switch (on/off or a number) |
| `IncreaseSwitch` | key:string | the plus field (columns 8, 9) |
| `DecreaseSwitch` | key:string | the minus field (columns 8, 9) |
| `LoadSettings` | file:string | Load Settings. See current Help for behaviour and bounds. |
| `SaveSettings` | file:string | Save Settings. See current Help for behaviour and bounds. |
| `ShowReadme` | – | Show Readme. See current Help for behaviour and bounds. |
