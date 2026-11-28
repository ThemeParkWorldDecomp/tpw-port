#pragma once

// The trimmed libarchive headers expect this shim for Android. On 64-bit Android,
// the standard file APIs already use 64-bit offsets.
#if !defined(__LP64__)
#error The Android build requires a 64-bit ABI.
#endif
