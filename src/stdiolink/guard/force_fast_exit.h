#pragma once

namespace stdiolink {

/**
 * Cross-platform fast exit without cleanup.
 * Skips atexit callbacks and global destructors to avoid deadlocks.
 * Used by ProcessGuardClient when parent process death is detected.
 */
void forceFastExit(int code);

} // namespace stdiolink
