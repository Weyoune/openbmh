// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The career session the console and its desk/window actions drive.
//
// Normally the Classic CareerSession. With OPENBMH_MODERN_SESSION (the openbmh_modern_cli
// target) the same console and action sources are compiled a second time against the Modern
// session (docs/new_engine): ModernSession is a fork of CareerSession with the same interface,
// so its types are made visible under bmh::app and CareerSession names the Modern session.

#ifdef OPENBMH_MODERN_SESSION
#include "modern/app/modern_session.hpp"
namespace bmh::app {
using namespace ::bmh::modern::app;
using CareerSession = ::bmh::modern::app::ModernSession;
using SessionState = ::bmh::modern::ModernState;  // the state the session's hooks hand out
}  // namespace bmh::app
#else
#include "app/career_session.hpp"
namespace bmh::app {
using SessionState = ::bmh::GameState;
}  // namespace bmh::app
#endif
