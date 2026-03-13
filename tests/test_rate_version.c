#include "ratelimiter.h"

#include <stdio.h>
#include <string.h>

#if !RL_VERSION_AT_LEAST(RL_VERSION_MAJOR, RL_VERSION_MINOR, RL_VERSION_PATCH)
#error "current version must satisfy its own minimum"
#endif

#if RL_VERSION_AT_LEAST(RL_VERSION_MAJOR, RL_VERSION_MINOR, RL_VERSION_PATCH + 1)
#error "a later patch level must not be considered compatible"
#endif

int main(void)
{
    const char *from_library = rl_version_string();
    if (from_library == NULL || strcmp(from_library, RL_VERSION_STRING) != 0 ||
        from_library[0] == '\0') {
        fputs("rate-version mismatch between public header and library\n", stderr);
        return 1;
    }
    printf("version test passed: installed API/runtime version %s\n", from_library);
    return 0;
}
