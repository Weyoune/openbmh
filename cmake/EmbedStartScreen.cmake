# SPDX-License-Identifier: GPL-3.0-only
file(READ "${INPUT}" artwork HEX)
string(REGEX REPLACE "(..)" "0x\\1," artwork "${artwork}")
file(WRITE "${OUTPUT}" "// Generated from the repository-owned compressed start screen.\n#include \"app/start_screen_data.hpp\"\nnamespace bmh::app {\nconst unsigned char start_screen_jpeg[] = {${artwork}};\nconst std::size_t start_screen_jpeg_size=sizeof(start_screen_jpeg);\n}\n")
