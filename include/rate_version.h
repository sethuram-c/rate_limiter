#ifndef RATE_VERSION_H
#define RATE_VERSION_H

/*
 * Phase 30: version identity for the installed C11 API and static archive.
 * VERSION is the release checklist's authoritative version file; the release
 * contract test rejects mismatches between it and these compile-time macros.
 *
 * Version 0.x is an experimental, pre-1.0 source release. Public structure
 * layouts remain visible; this is NOT a promise of stable binary ABI.
 */
#define RL_VERSION_MAJOR 0
#define RL_VERSION_MINOR 1
#define RL_VERSION_PATCH 0
#define RL_VERSION_STRING "0.1.0"

/* All arguments must be nonnegative integer constant expressions. */
#define RL_VERSION_AT_LEAST(major, minor, patch)                              \
    ((RL_VERSION_MAJOR > (major)) ||                                         \
     (RL_VERSION_MAJOR == (major) && RL_VERSION_MINOR > (minor)) ||          \
     (RL_VERSION_MAJOR == (major) && RL_VERSION_MINOR == (minor) &&           \
      RL_VERSION_PATCH >= (patch)))

/* Library-side version string, useful for detecting mismatched header/archive.
 * Storage has static lifetime; caller must not free or modify the pointer. */
const char *rl_version_string(void);

#endif /* RATE_VERSION_H */
