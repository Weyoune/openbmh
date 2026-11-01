# Optional long-career statistics and title archive

The existing `OBMHEXT1` container can contain block `0x4f424c43` (OpenBMH-owned,
not a guest memory address). Original state blocks and all four name pools retain
their sizes and ordering. No extension is emitted until an archive is needed.
Payload integers use little-endian encoding:

| Field | Encoding |
|---|---|
| Payload version | u32, currently 1 |
| Player/club record count | u32 |
| Known season record count | u32 |
| Each player/club record | u32 key, then six u64 counters |
| Each season record | u32 absolute zero-based season, then nine names |
| Each name | u16 byte length followed by Original-encoding bytes |

The player key is `(runtime_club << 12) | player_id`. The six counters are league,
national-cup and European-cup appearances, total appearances, goals and assists.
Club swaps remap keys; fresh squad entry initialization resets the new player's
club record. The Original byte/word fields continue to update normally.

Season names follow the nine Original title categories. An empty name is a known
season with no holder for that category. Missing season records are unknown,
not an assertion that no competition was held. Names are owned snapshots at the
award/capture time, independent of future club-number and name-pool changes.
A legacy import contributes only its surviving window; no earlier information
is inferred. The title list uses the earliest known absolute season when paging
and compensates for the native rolling-window year offset.

The decoder rejects unknown versions, truncated payloads, invalid/duplicate
identities and seasons, duplicate blocks and trailing bytes before publishing
archive state. Season records remain within the native signed 16-bit calendar
range. The surrounding FBM checksum covers the extension. Older writers may
discard extensions; Original-game roundtrip preservation is not promised.
