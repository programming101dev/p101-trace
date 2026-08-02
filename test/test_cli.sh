#!/usr/bin/env bash
set -euo pipefail

tool="${1:?usage: test_cli.sh <p101-trace>}"
work="$(mktemp -d "${TMPDIR:-/tmp}/p101-trace-cli.XXXXXX")"
trap 'rm -rf "$work"' EXIT

expect_status() {
  local expected="$1"
  shift
  local actual

  set +e
  "$@" >"$work/stdout" 2>"$work/stderr"
  actual=$?
  set -e
  if [ "$actual" -ne "$expected" ]; then
    printf 'expected exit %s, got %s: ' "$expected" "$actual" >&2
    printf '%q ' "$@" >&2
    printf '\n' >&2
    cat "$work/stderr" >&2
    exit 1
  fi
}

cat >"$work/clean.log" <<'EOF'
P101CALL	5	test-run	42	7	1	100	200	ENTER	17	main	p101_open	path=/tmp/x	-	server.c
P101CALL	5	test-run	42	7	2	160	260	EXIT	17	main	p101_open	-	3	server.c
P101COMPLETE	5	test-run	42	7	3	170	270	2	0	0
EOF

cat >"$work/finding.log" <<'EOF'
P101CALL	5	test-run	42	7	1	100	200	EXIT	17	main	p101_open	-	-1	server.c
P101COMPLETE	5	test-run	42	7	2	170	270	1	0	0
EOF

cat >"$work/malformed.log" <<'EOF'
P101CALL	5	test-run	bad
EOF

cat >"$work/bad-version.log" <<'EOF'
P101CALL	99	42	7	1	100	200	ENTER	17	main	p101_open	-	-	server.c
EOF

cat >"$work/fork.log" <<'EOF'
P101CALL	5	test-run	42	7	1	100	200	ENTER	17	main	p101_fork	-	-	server.c
P101FORK	5	test-run	42	7	2	110	210	43	18	main	server.c
P101CALL	5	test-run	42	7	3	120	220	EXIT	17	main	p101_fork	-	43	server.c
P101COMPLETE	5	test-run	42	7	4	170	270	3	0	0
P101COMPLETE	5	test-run	43	7	1	170	270	0	0	0
EOF

{
  for ((index = 0; index < 5000; index++)); do
    printf x
  done
  printf '\n'
} >"$work/overlong-other.log"

expect_status 0 "$tool" --help
expect_status 0 "$tool" -h
expect_status 0 "$tool" "$work/clean.log"
expect_status 0 "$tool" -v "$work/clean.log"
expect_status 0 "$tool" -f "$work/clean.log"
expect_status 0 "$tool" -s -l 40 "$work/clean.log"
expect_status 0 "$tool" "$work/fork.log"
expect_status 1 "$tool" "$work/finding.log"
expect_status 2 "$tool" "$work/malformed.log"
expect_status 2 "$tool" "$work/bad-version.log"
expect_status 2 "$tool" "$work/overlong-other.log"
expect_status 2 "$tool" "$work/missing.log"
expect_status 2 "$tool" -z
expect_status 2 "$tool" -l
expect_status 2 "$tool" -l nope
expect_status 2 "$tool" -l -1
expect_status 2 "$tool" -s -f
expect_status 2 "$tool" one two
expect_status 2 "$tool" ""
expect_status 2 "$tool" $'-\001'
