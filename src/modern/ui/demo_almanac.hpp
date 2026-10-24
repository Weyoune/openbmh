// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "modern/ui/catalog.hpp"

namespace bmh::modern_ui {
// Immutable presentation archive. It is deliberately separate from Snapshot and
// UI checkpoints; neither generation nor queries touch an engine, OBS or RNG.
// Two completed seasons precede the season of manifest.start_date (2026-07-01
// when absent). Missing catalogues yield a valid empty archive.
Json make_demo_almanac(const Catalog &);
bool valid_demo_almanac(const Json &);
// Requests: {kind:"index"}, {kind:"calendar",year:2025},
// {kind:"day",date:"2025-08-03"}, {kind:"match",id:"..."}.
// Responses always contain ok. Errors contain error; successful index responses
// have years (ascending), match_count, first_date/last_date (nullable). Calendar
// has year and days [{date,count,highlights:[match IDs]}], day has date/matches,
// and match has match. Only records dated <= as_of are ever returned.
// regulation contains 90-minute goals; extra contains only additional goals
// during extra time. Their sum is final; final excludes shootout goals.
// An engine adapter may derive regulation from complete goal events <= minute
// 90. AlmanacMatch itself only stores the end score and separate shootout; this
// demo does not claim that its explicit phase fields came from the backend.
// In particular, backend decided==2 alone does not establish extra-time
// participation, and F10 player identities do not carry archived positions.
Json query_demo_almanac(const Json &archive, const Json &request,
                       const std::string &as_of);
} // namespace bmh::modern_ui
