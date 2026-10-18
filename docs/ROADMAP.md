# Roadmap

OpenBMH combines a playable reconstruction with a foundation for extension. These milestones describe direction and dependencies, not promised dates.

## 0.1: a usable engine

Career management, matches, finances, transfers, original-style SDL2 screens, audio and a console share the same session logic. German game data is the primary target; Football Limited support is experimental. Compatibility and remaining limitations are documented in the [release notes](release/0.1/RELEASE_NOTES.md).

## 0.15: broader English compatibility

Complete edition-specific loading, presentation, competitions and sound support. Imported content determines names, text, clubs and league organisation; a shared engine rule base defines the simulation.

## 0.2: portable Classic content and native saves

Import a supplied BMH or Football Limited installation through the launcher into a
sealed [OBW](formats/OBW.md) Classic mod. Run the original-style interface and
console exclusively from converted content, with native careers and preferences
stored separately. Legacy save migration is an import operation. See the
[0.2 release notes](release/0.2/RELEASE_NOTES.md) and
[runtime contract](formats/obw/runtime-0.2.md).

Modern game screens are deferred. The Modern UI POC remains separate from the
0.2 distribution. Replacing the remaining compatibility blocks with typed clubs,
players, contracts and competitions is subsequent work, not a completed 0.2 claim.

## 0.3: configurable rules

Expose rule values and balance choices as configuration, with reproducible simulations to assess changes. Improve tactical automation, transfer decisions and scoring distributions through documented changes rather than hidden edition-specific branches.

## 0.4: adaptable interfaces and competitions

Add a scalable interface (RmlUi) through the shared application/protocol boundary. Make league sizes and competition structures data-driven. Define content-pack formats for independently licensed artwork, names and other resources; do not distribute third-party material without permission.

## 0.5: independently distributable content

Provide original or freely licensed graphics, sound and generated football worlds so that OpenBMH can be played without proprietary game data. Support editors, new competitions and multiplayer clients built on the same engine interfaces.

The typed model and open format are prerequisites for flexible competitions, configurable rules and new content. See [architecture](architecture.md) for the current boundaries and [compatibility decisions](COMPATIBILITY.md) for intentional departures.
