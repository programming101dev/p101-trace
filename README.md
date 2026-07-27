# p101-trace

`p101-trace` reads `P101CALL` records from `lib_env` and turns them into a
portable, wrapper-level trace. It is the Programming 101 answer to the useful
parts of `strace` and `ltrace`: not every system call in the process, but every
p101-traced call your code elects to record, plus any student function that uses
the same tracing rules.

The default view is an indented call tree. `-s` prints aggregate counts by call
site, including result-bearing exits and failure-looking return values, and `-f`
prints normalized flat records that are easier for scripts to consume.

## Producing a call log

The observer lives in `lib_env`. The easiest path is the environment bridge:

    P101_CALL_LOG=calls.log ./my-program
    ./build-clang/p101-trace calls.log
    ./build-clang/p101-trace -s calls.log
    ./build-clang/p101-trace -f calls.log

Or turn on the sink manually:

    #include <p101_env/env.h>

    env = p101_env_create(err, NULL);
    p101_env_set_call_log(env, stderr);

Use `-` as `P101_CALL_LOG` to write the call stream to stderr.

## The log format

One record per line, tab separated:

    P101CALL <TAB> 2 <TAB> pid <TAB> seq <TAB> mono_ns <TAB> wall_unix_ns <TAB> ENTER|EXIT <TAB> line <TAB> function <TAB> call <TAB> args <TAB> result <TAB> file

The `2` is the supported format version. `seq` is the per-env event sequence.
`mono_ns` and `wall_unix_ns` are timestamps, or `-` when unavailable. `args` and
`result` are `-` when not supplied.
Tabs, newlines, carriage returns, and backslashes inside fields are escaped by
`lib_env`, so a record stays one physical line. Lines that do not begin with
`P101CALL` are skipped, which means a log stream can be shared with ordinary
program output.

`p101-trace` adds a derived event number to rendered output. It is the 1-based
sequence of successfully parsed `P101CALL` records in that log file, useful for
linking a trace line back to `p101-report` context.

## Options

    p101-trace [-h] [-v] [-s|-f] [file]

With no file, or with `-`, it reads standard input.

- `-s` prints summary counts by source call site, with result/suspect columns.
- `-f` prints one normalized tab-separated line per parsed event.
- `-v` enables p101 tracing inside `p101-trace` itself.

Exit status is `0` when the log parsed cleanly, `1` when malformed or
unsupported-version `P101CALL` records were found, and `2` for bad usage or an
I/O/tool failure.

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
