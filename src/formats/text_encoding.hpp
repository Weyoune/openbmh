// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Text encoding of the original game and of the JSON the front ends emit.
//
// The original stores German text in ISO 646-DE: the seven bytes [ \ ] { | } ~ stand for
// Ä Ö Ü ä ö ü ß, everything else is ASCII. The engine and the screens keep text in that original
// encoding (the bitmap fonts are indexed by it); UTF-8 is used only where text leaves the program
// (JSON state, console, window titles) or enters it (typed names).
//
// These functions lived in ui/manager.hpp until 1.0.12, although nearly every screen, the console
// client and the app use them; ui/manager.hpp still makes them visible as bmh::ui::... .

#include <string>
#include <string_view>

namespace bmh {

// A JSON string literal (with the quotes) for UTF-8 text: escapes '"', '\' and control characters.
std::string json_quote(std::string_view utf8);

// Original (ISO 646-DE) bytes -> UTF-8. A byte above 127 is not part of the original encoding
// and becomes U+FFFD, so that it stays visible.
std::string original_to_utf8(std::string_view original);

// UTF-8 -> original bytes; throws FormatError for a character the original cannot store.
std::string utf8_to_original(std::string_view utf8);

}  // namespace bmh
