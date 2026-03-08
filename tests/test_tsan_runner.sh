#!/usr/bin/env bash
# Verify the TSAN wrapper does not silently turn a runtime error into a pass.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

cat > "$workdir/pass" <<'MOCK'
#!/usr/bin/env bash
echo 'audit finished'
exit 0
MOCK
cat > "$workdir/unsupported" <<'MOCK'
#!/usr/bin/env bash
echo 'FATAL: ThreadSanitizer: unexpected memory mapping 0xdead-0xbeef' >&2
exit 66
MOCK
cat > "$workdir/race" <<'MOCK'
#!/usr/bin/env bash
echo 'WARNING: ThreadSanitizer: data race' >&2
exit 66
MOCK
cat > "$workdir/unexpected" <<'MOCK'
#!/usr/bin/env bash
echo 'test assertion failed' >&2
exit 2
MOCK
chmod +x "$workdir"/*
assert_status() {
  local expected=$1 executable=$2 actual
  set +e
  "$root/tests/run_tsan.sh" "$workdir/$executable" >"$workdir/$executable.log" 2>&1
  actual=$?
  set -e
  if [[ $actual -ne $expected ]]; then
    cat "$workdir/$executable.log" >&2
    echo "TSAN wrapper $executable: expected $expected, got $actual" >&2
    exit 1
  fi
}
assert_status 0 pass
assert_status 77 unsupported
assert_status 1 race
assert_status 1 unexpected
echo 'TSAN wrapper tests passed: PASS / SKIP / RACE / ERROR distinct'
