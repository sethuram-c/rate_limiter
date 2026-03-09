# Phase 26 - GitHub Actions CI and cross-compiler verification

The source-controlled `.github/workflows/ci.yml` introduces automatic Linux
builds on GitHub-hosted runners. **The existence of this file is not evidence
that GitHub has run it.** Check the Actions tab after pushing the branch.

## Goals and trust boundaries

* Trigger for every `push`, `pull_request`, and manual `workflow_dispatch`.
* Set `permissions: contents: read` to minimize `GITHUB_TOKEN` privileges.
* Run separate `gcc` and `clang` jobs, both with C11 `-Wall -Wextra`
  `-Wpedantic -Werror` configured by the existing Makefile.
* Build `make all`, then run the *full* `make test` suite on each compiler.
* Run ASan+UBSan in an independent job (`make audit-asan`).
* Limit jobs to 25 / 15 minutes. Keep matrix `fail-fast: false` so a compiler
  failure doesn't stop the other compiler's diagnostics.
* Avoid perf claims: CI virtualization makes throughput non-comparable.

## What is and is not covered

The test suite includes deterministic token buckets, atomic and lock-free
buckets, hash tables, service/gateway and policy tests, concurrency audits,
unit and benchmark *smoke* tests, and Phase 26 configuration checks. Heavy
full-length benchmark reports are not executed in CI. A pass is not a proof
of all possible thread interleavings.

The sanitizer job runs a *focused* concurrent audit with both ASan and UBSan.
ThreadSanitizer remains a separate *manual* `make audit-tsan` workflow, because
some Linux environments cannot initialize the TSan runtime. The wrapper
returns 77 for unsupported environments and never claims that is a pass.
If the local runtime works, TSan clean results provide additional evidence.

## Verify on Fedora Linux / Linux workstation

```bash
sudo dnf install -y gcc clang make python3 git
make clean
make CC=gcc -j2 all
make CC=gcc ci-config-test
make CC=gcc test
make clean
make CC=clang -j2 all
make CC=clang test
make clean
make CC=gcc audit-asan
# Optional: TSan may return 77 if runtime cannot start
make CC=gcc audit-tsan
```

`make ci-config-test` checks high-value policy strings using only Python's
standard library. It intentionally does **not** parse YAML or verify GitHub's
workflow schema. Install and run `actionlint .github/workflows/ci.yml` for a
stronger local syntax/schema check when that tool is available.

## Activate remotely

1. Copy the Phase 26 changes into your existing Git repository, *preserving
   your `.git/` folder*.
2. Run the local tests, commit the six changed files, and inspect `git status`.
3. Run `git remote -v`. If there is no suitable `origin`, configure your actual
   GitHub repository URL. A missing upstream configuration alone does not
   indicate that your local commits are damaged.
4. Run `git push -u origin main` only after verifying the destination is yours.
5. Open GitHub repository > Actions > C11 Build and Test. Expect two compiler
   matrix executions and one sanitizer job. Investigate any failed job logs.
6. If Actions are disabled by repository policy, enable them in repository
   Settings > Actions if you have permission. Do not infer a successful run
   from local tests.

## Failure diagnostics

* `-Werror` compiler failures: identify the exact failing source line and
  compare GCC versus Clang warnings; don't remove `-Werror` just to pass.
* `error: ThreadSanitizer: unexpected memory mapping`: TSan unsupported,
  not a passing race-free run; keep separate from required CI checks.
* GitHub Action `checkout` fails: verify repository permissions, remote
  availability, and that Actions are allowed for the repository.
* CI test timeout: inspect whether a real deadlock occurred before increasing
  timeout. Full benchmark-report generation isn't in the required CI suite.
* Invalid workflow syntax: run `actionlint` (optional tool) or inspect
  whitespace and matrix expression syntax; Python contract checks alone
  cannot prove valid YAML.
* An upstream is gone warning is independent of local code verification.

## Security

Do not add repository secrets, elevated token permissions, untrusted PR code
executed via `pull_request_target`, or `curl | bash` installation to CI.
GitHub-hosted `ubuntu-24.04` includes the toolchain needed for this project;
if runner images change, document installed tool versions in the job logs.
Workflow source is committed to Git; personal learning PDF stays outside Git.
