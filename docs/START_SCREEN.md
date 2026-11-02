# OpenBMH Classic start artwork

The project maintainer supplied this new start-screen artwork for use and
repository distribution with this application. It is not extracted game content.
The repository license applies to project artwork; original game assets remain
external. The unmodified upstream decoder has its separate documented license.

`start-screen.jpg` retains the supplied image's native **1448 × 1086** pixels.
It is an optimized progressive JPEG at quality 92 with 4:4:4 chroma sampling,
without EXIF or other input metadata. It occupies 587,410 bytes.
The application embeds it at build time and decodes it once at startup, so
installed applications need no image sidecar or original title-screen VGA.

The SDL renderer uses the actual image dimensions while showing the title,
including high-DPI output and aspect-preserving side bars, then restores the
320 × 240 game/input canvas. A complete left or right mouse click continues;
there is no loading-ball animation. Changing the artwork also requires an
explicit review/update of its SHA256 in the public-source guard.
