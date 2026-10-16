# Executable images used as data

OpenBMH reads tables and text from the user's original executable. It does not execute that program. The file is not part of the OpenBMH distribution.

## German LE image

`src/formats/le.cpp` loads the DOS/4GW Linear Executable format. The DOS header's u32 field at `0x3c` locates the LE header. Object records define virtual sizes, image bases and page ranges. Page-map entries locate file data; unloaded areas are zero-filled. The image applies supported internal relocation records before consumers follow pointers.

The reader supports the internal offset/selector forms required by the installation. External fixups are rejected. Bounds and object validation belong to the loader; arbitrary executable formats are not accepted merely because they begin with an MZ stub. String access decodes the legacy single-byte encoding.

## English X-32 image

Football Limited uses a different executable layout. `src/launch/fltd.cpp` prepares supported English executable data and maps the required tables into the engine's expected data view. It is not an instruction emulator or a conversion of English game rules into a separate engine.

The [executable layout reference](EXECUTABLE_LAYOUT.md) describes supported table locations and semantic layout identifiers. A byte address is meaningful only with its edition and image layout. Application APIs should expose the table's meaning rather than an address-derived field name.
