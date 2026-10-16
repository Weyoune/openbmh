// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace bmh {

// Unsaved EXE-owned context for PendingOfferAnswer's allocator behavior. Direct
// legacy/headless callers have no executable provenance and must stay unsupported.
enum class TransferAllocatorContext { unsupported, zero, blocked };

} // namespace bmh
