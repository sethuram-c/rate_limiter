# Phase 30: Public version contracts and release-readiness checklist

## Scope

Phase 30 defines a **development milestone**, version `0.1.0`, with an
unreleased changelog. This is NOT a publication, a stable ABI pledge, a
Linux distribution package, or a Git tag. Only the owner can choose to
publish or tag it after verifying CI and the intended release contents.

## Source / binary consistency

1. `VERSION` is the intended source version (`MAJOR.MINOR.PATCH`).
2. `include/rate_version.h` exposes matching compile-time constants.
3. `src/rate_version.c` compiles `rl_version_string()` into the static archive.
4. The external consumer checks header and linked-library version equality.
5. `make release-contract-test` rejects malformed or inconsistent metadata.
6. `make package-test` installs and tests against only installed headers and
   the archive, including the installed `VERSION` and `CHANGELOG.md`.

`0.1.0` means pre-1.0 development. Public `rate_service_t` and related structs
have visible layouts; the library does **not** guarantee ABI compatibility
across independent header/archive builds. Rebuild and relink consumers whenever
upgrading source versions. A successful string equality check is useful but
not a mathematical guarantee of ABI compatibility.

## Fedora / Linux verification

```sh
make clean
make library
make version-test
make release-contract-test
make package-test
make release-check
make test
make static-check
make audit-asan
```

For an independent Clang run:

```sh
make clean
make CC=clang release-check
make CC=clang test
```

`make release-check` performs source-contract, version, staged installation,
CI configuration and basic analyzer verification, but is not a replacement for
full regression and sanitizer testing. Benchmark numbers vary by machine.

## Source control and publication checklist

```sh
git status --short
# after the Phase 30 commit and tests, verify 30 commits:
git rev-list --count HEAD
git show --stat --oneline HEAD
# confirm the configured remote before publishing:
git remote -v
```

- Keep the separate learning PDF OUTSIDE the repository.
- Only stage the files that were intentionally changed.
- Inspect the working tree, changelog, version, and tests before deciding
  whether to publish any tag.
- Confirm GitHub Actions passes on the remote before advertising CI success.
- If you later decide to create a tag, use `git tag -a v0.1.0 -m '...';`
  this phase deliberately does not tag or publish anything for you.
- Author/committer timestamps in a planned learning schedule represent
  chosen Git metadata, not proof of the actual date of development.

## Known limitations

- No concurrent table resizing or deletion, and fixed-capacity exhaustion.
- No network listener, HTTP protocol parser or trusted-client-IP validation.
- User clock callback may block and must be thread safe.
- Do not destroy any active limiter; stop/join worker threads first.
- Static archive is not a shared library; no SONAME/ABI versioning.
- External benchmark results are machine-specific and do not justify
  blanket speedup claims.

## Interview preparation

Explain the difference between compile-time metadata and a runtime version
query; why a static archive with public structs does not imply stable ABI;
what SemVer 0.x promises; how the release checker fails on contradictory
metadata; and which checks must pass before publishing an actual release.
