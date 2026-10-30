# Community maintenance roadmap

OpenBMH reconstructs the original Bundesliga Manager Hattrick experience using the player's own game data. Development priorities are compatibility, reliability and accessible community maintenance.

## 1.0 acceptance

- Validate German CD-compatible BMH and supported Diamond installations, including career setup, management, matches and season transitions.
- Preserve original data and save layouts; document intentional bug corrections.
- Check Windows x64, macOS ARM64 and Linux x64 builds and publish matching source archives, licenses and checksums.
- Keep release limitations and reproducible defects visible in the issue tracker.

## Continued maintenance

Prioritize reproducible gameplay, save, rendering, input and platform bugs. Keep improvements focused and cover unsafe state transitions with regression tests. Contributions should preserve the familiar interface and the supplied edition's content and competition structure.

Football Limited remains experimental. Improvements require independent checks of its original loading, presentation, competitions, audio and save compatibility; German acceptance does not establish English acceptance.

The SDL2 graphical client and console share the game/session layer. Community clients and automation should use the documented [console interface](CONSOLE_CLIENTS.md) and retain its ownership and revision checks.

See [architecture](architecture.md), [compatibility decisions](COMPATIBILITY.md) and [contributing](../CONTRIBUTING.md) before changing shared behaviour. No proprietary game content belongs in public code, tests or releases.
