# Commands

Quick reference for `p101-trace`. Every script also supports `--help`.
Run `./change-compiler.sh -c <compiler>` once before building.

## Running the tool

| Command | What it does |
| --- | --- |
| `p101-trace calls.log` | Render `P101CALL` records as an indented call tree |
| `p101-trace < calls.log` | Same, reading standard input (`-` works too) |
| `p101-trace -s calls.log` | Summarize calls by source call site |
| `p101-trace -f calls.log` | Print normalized flat tab-separated events |
| `p101-trace -v calls.log` | Verbose: trace `p101-trace` itself while it runs |

Exit status: `0` clean parse, `1` malformed or unsupported-version call
records, `2` bad usage or unreadable log.

## Building and checking

| Command | What it does |
| --- | --- |
| `./change-compiler.sh -c <cc>` | Configure the build with a compiler (also `./change-compiler.sh <cc>`). `--help` lists detected compilers. |
| `./change-compiler.sh -c <cc> -s address,undefined` | Configure with specific sanitizers |
| `./change-compiler.sh -c <cc> --coverage` | Configure an instrumented build for coverage (gcov) |
| `./build.sh` | Strict analysis build: format-check, clang-tidy, cppcheck, static analyzer, `-Werror`, sanitizers. `-q` = quiet |
| `./build.sh -f` | Auto-fix in place: clang-tidy `--fix` + clang-format |
| `./build.sh -C` | Format check only, no build (hook-friendly); non-zero if unclean |
| `./check.sh` | Format + strict build + tests + fuzz smoke -> one PASS/FAIL. `--cov <pct>` adds a coverage gate |
| `./test.sh` | Build and run the Unity test suite |
| `./test-all.sh` | Run the tests across every supported compiler |
| `./fuzz.sh` | Fuzz the argument parser (coverage-guided + sanitizers); PASS/FAIL. `-t <secs>` sets the time budget |
| `./coverage-report.sh` | HTML coverage report. `--report-only` skips the run; `--min <pct>` fails under a threshold |
| `./report.sh coverage` \| `profile` | One entry point for the coverage / profiling reports |
| `./doctor.sh` | Report what actually works on this machine for this project |
| `./clean.sh` | Remove `build-` / `coverage-` / `profile-` output (`-n` previews) |

Less common: `./build-all.sh` (build with every compiler), `./check-compilers.sh`
(detect installed compilers), `./check-env.sh` (verify required tools).
