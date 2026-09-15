# Final Fix Report: gpumemctl multiline response framing

Date: 2026-09-15

## Review finding

`gpumemctl models` treated every command except `status` as a single-line response. When the server fragmented a model listing, the client stopped after the first newline and returned a truncated response. The read loop also accepted EOF without recording whether the expected response frame had completed.

## TDD evidence

A real-CLI regression test was added in `tests/cli_framing_test.cpp`. It starts a Unix-domain socket fixture, executes the built `gpumemctl` process, and sends a fragmented `models` response larger than 4096 bytes. The first fragment contains only the response header.

Before the production change:

```
Test #5: cli_framing ... Subprocess aborted
Assertion failed: (actual == expected)
0% tests passed, 1 tests failed out of 1
```

The test was then expanded to verify that EOF before `status` receives `END\\n` is an error and that a complete single-line `ERR ...\\n` response returns before the peer closes the connection.

## Implementation

The client read loop now:

- treats both `status` and `models` as multiline commands requiring a response ending in `END\\n`;
- accepts a newline-terminated `ERR ` response immediately for every command;
- tracks valid frame completion and returns an error if EOF or a read failure occurs first;
- preserves the existing single-line framing for all other commands.

No registry, protocol, or server behavior was changed.

## Verification

Focused:

```
cmake --build build --target gpumemctl cli_framing_test -j4
ctest --test-dir build -R '^cli_framing$' --output-on-failure
1/1 tests passed
```

Full:

```
cmake --build build -j4
ctest --test-dir build --output-on-failure
9/9 tests passed
```

`git diff --check` completed successfully.

## Concerns

None within the requested scope. The test uses POSIX process and Unix-socket APIs, matching the project's existing Unix-only IPC design.

