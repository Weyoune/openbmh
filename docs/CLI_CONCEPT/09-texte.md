# Text, encoding and editions

The installation supplies game text and names. The command vocabulary stays stable across editions; it is not translated into the selected game's display language. German and English installation data can have different text, league contents, executable layouts and currency presentation. English support remains experimental as described in the [release notes](../release/1.0/RELEASE_NOTES.md).

Legacy bytes are decoded at the display boundary. Client-facing text is intended to be UTF-8, while save and database offsets refer to legacy byte pools. Avoid using a displayed name as a persistent identifier: names may be edited, ambiguous or rendered differently. Prefer the ID domain required by the current command.

Structured records separate data from prose. Clients should decide from IDs, enums, numeric fields, question options and error codes rather than searching for a translated sentence. Preserve raw response bytes when investigating an encoding problem, and avoid publishing proprietary text collections or full game logs containing original content.

[TEXTD](../formats/TEXTD.md) describes the text database's structure. Replacement text requires an independent redistribution license. No extracted text inventory is included in this repository.
