# obdeddi 0.1.0

A small native Windows data editor for the standalone OpenBMH 1.1 COMMUNITY.OBD format. It uses Win32/common controls, not SDL, RmlUI or a browser. The model and archive-writing tests build on all supported platforms; the graphical editor is Windows x64 only.

## Use

Extract the separate editor package and start `obdeddi.exe`. Open a COMMUNITY.OBD with the Open button or Ctrl+O. A file path may also be passed on the command line. Search and sortable virtual lists cover players and clubs; the club filter applies to players. No artwork is decoded or presented for editing.

Player fields include names, position, nationality, age, form/technique/condition, bonus and the seven Classic skills. Displayed strength is derived, not a second independent field. Club fields include names, the stored short-name span and existing kit selectors. League sizes and record counts stay fixed. Country rows and rules are not editable. League players can move between squads or swap places; temporary/reserve-table players cannot be reassigned as if they were ordinary roster members.

Classic new-career generation is deliberately retained: many stored player values are randomized/recalculated when a career starts. Editing the database does not modify an existing saved game. This limitation is visible in the player form.

Apply commits the current form to the in-memory document. Changing selection also validates the draft form. Ctrl+S writes the document to its original OBD after verification and backup. Closing or opening another file asks about unsaved changes.

## Save safety

- ZIP/resource limits and supplied hashes are checked on open. Original database order, IDs, unknown player bits and non-database resource bytes are preserved.
- The M3D writer validates a decode/encode roundtrip. Unsupported characters and full 16-bit string pools are rejected rather than silently replaced.
- Save serializes a temporary sibling archive and reopens it for validation. A cooperating editor lock prevents overlapping saves.
- The complete prior archive is copied to `backups/` and read back for verification. External modifications are checked before the transaction and again before replacement.
- Only then does the OS atomically replace the original. A failed backup, validation or replacement leaves the original intact; a temporary file is cleaned up. Successful saves update resource hashes and append an editor-history record.
- Saving an unchanged document is a no-op. Backups are independent OBD bytes and can be copied back while the application is closed.

## Performance and delivery

Owner-data lists request only visible cell text. Search is debounced, forms update on selection/change, and the native message loop does not render continuously while idle. Assets are retained as bytes for integrity and save roundtrips, without image/font/audio decoding.

The editor has its own ZIP/release. It is not inserted into the small Windows community game package. Corresponding full sources are offered separately. OpenBMH 1.1 Beta keeps a fixed `OpenBMH 1.1` window caption, fits 18/20-team strengths views above the controls and uses a 25% longer default scene-frame delay (at least 16 ms). Explicit frame-delay overrides remain available; simulation clocks and random streams are not changed.
