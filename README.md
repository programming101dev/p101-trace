# p101-trace

> **Workflow role:** this binary is the standalone trace-policy reference
> implementation. The ordinary `p101 analyze` workflow builds one shared run
> model and `p101 trace ANALYSIS_DIR` renders its trace view without reparsing
> the event stream.

`p101-trace` reads `P101CALL` records from `lib_env` and turns them into a
portable, wrapper-level trace. It is the Programming 101 answer to the useful
parts of `strace` and `ltrace`: not every system call in the process, but every
p101-traced call your code elects to record, plus any student function that uses
the same tracing rules.

The default view is an indented call tree. `-s` prints aggregate counts by call
site, including result-bearing exits, failure-looking return values, measured
call durations, and an optional slow-call section. `-f` prints normalized flat
records that are easier for scripts to consume.

## Producing a call log

The observer lives in `lib_env`. The easiest path is the environment bridge:

    P101_CALL_LOG=calls.log ./my-program
    ./build-clang/p101-trace calls.log
    ./build-clang/p101-trace -s calls.log
    ./build-clang/p101-trace -f calls.log

Or turn on the sink manually:

    #include <p101_env/env.h>

    env = p101_env_create(err, NULL);
    p101_env_set_call_log(env, stderr, P101_ENV_CALL_LOG_DEFAULT);

Use `-` as `P101_CALL_LOG` to write the call stream to stderr.

## The log format

One record per line, tab separated:

    P101CALL <TAB> 4 <TAB> pid <TAB> context <TAB> seq <TAB> mono_ns <TAB> wall_unix_ns <TAB> ENTER|EXIT <TAB> line <TAB> function <TAB> call <TAB> args <TAB> result <TAB> file
    P101COMPLETE <TAB> 4 <TAB> pid <TAB> context <TAB> seq <TAB> mono_ns <TAB> wall_unix_ns <TAB> events-attempted <TAB> write-failed <TAB> write-errno

Version 4 is the only supported format. Other versions are rejected. `seq` is
the per-environment event sequence.
`mono_ns` and `wall_unix_ns` are timestamps, or `-` when unavailable. `args` and
`result` are `-` when not supplied.
Tabs, newlines, carriage returns, and backslashes inside fields are escaped by
`lib_env`, so a record stays one physical line. Lines that do not begin with
`P101CALL` are skipped, which means a log stream can be shared with ordinary
program output.

An orderly producer writes `P101COMPLETE` when its environment is destroyed.
A version 4 stream without that receipt, or whose receipt reports a write
failure, is incomplete evidence and makes `p101-trace` return tool trouble. This
distinguishes a truly clean trace from a program that crashed or a log that was
truncated.

Completion is also the terminal boundary for that process/context. Frames still
open there are reported as `abandoned_at_completion` and then closed without an
integrity finding; this is expected for `_Exit` and similar non-returning
boundaries. An EXIT that mismatches the live stack before completion remains a
finding. Use `P101_TRACE_SCOPE` for ordinary application functions so early
returns still emit a matching EXIT.

ENTER/EXIT pairs in the same process and context use their monotonic timestamps
to compute call durations. Missing timestamps and unmatched records remain
visible but are not assigned invented durations.

`p101-trace` adds a derived event number to rendered output. It is the 1-based
sequence of successfully parsed `P101CALL` records in that log file, useful for
linking a trace line back to `p101-report` context.

## Options

    p101-trace [-h] [-v] [-s|-f] [-l <nanoseconds>] [file]

With no file, or with `-`, it reads standard input.

- `-s` prints summary counts by source call site, with result/suspect columns.
- `-f` prints one normalized tab-separated line per parsed event.
- `-l` adds a slow-call section in summary mode for calls whose maximum
  measured duration is at least the supplied threshold.
- `-v` enables p101 tracing inside `p101-trace` itself.

Exit status is `0` for a clean complete trace, `1` when ENTER/EXIT stack
integrity findings were found, and `2` for malformed/unsupported input,
incomplete evidence, bad usage, or an I/O/tool failure.

## Why this is useful

`p101-resource-tracker` tells you what leaked. `p101-error-path-walk` makes error paths
execute. `p101-trace` shows the dynamic story that led there:

    pid 123 main()  [main.c:10]
    pid 123   p101_open(path=/tmp/x)  [main.c:14]
    pid 123   -> p101_open = 3  [main.c:14]
    pid 123 -> main = 0  [main.c:20]

That gives students a readable bridge from source code to runtime behavior
without requiring platform-specific tracing permissions or OS-specific tools.

## Boundaries

`p101-trace` is wrapper-level tracing, not OS tracing. It reads `P101CALL`
records, so direct calls, uninstrumented functions, third-party library internals,
and kernel-level activity are invisible unless code emits compatible p101 call
events. The rendered tree is a readable reconstruction of one log, not proof of
every path the program can take.

## The workflow

1. **Configure** — `./change-compiler.sh -c clang` picks the compiler and
   configures the build. Run it again any time to switch compilers.
2. **Build** — `./build.sh` compiles through the strict analysis pipeline:
   clang-format check, clang-tidy, cppcheck, the Clang static analyzer, hundreds
   of warnings under `-Werror`, and the sanitizers baked in. Add `-q` to hide the
   per-file command dump.
3. **Test** — `./test.sh` runs the Unity tests.
4. **Gate** — `./check.sh` runs the whole local quality gate.
