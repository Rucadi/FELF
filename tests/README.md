# Tests

The suite is driven by CTest.  Each test is registered with `add_test`;
there is no separate runner script.

Inside `nix-shell` or `nix develop`, two helpers do this:

```bash
felf_build                 # configure build/host and build it
felf_test                  # ctest --output-on-failure
felf_test -L run -j 8      # extra arguments go to ctest
FELF_BUILD_DIR=build/dev felf_build
```

Or run CMake directly.  Inside the shell it uses Nix's FELF glibc (the shell
exports `FELF_GLIBC_DIR`, `FELF_GLIBC_VERSION` and
`FELF_LOCALE_ARCHIVE_FILE`); outside it, CMake builds the patched glibc
itself (`../src/glibc/README.md`):

```bash
cmake -S . -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

Useful selections:

```bash
ctest --test-dir build/host -N                 # list every test
ctest --test-dir build/host -L run             # run the execution tests
ctest --test-dir build/host -L direct          # only direct mapping
ctest --test-dir build/host -R 'tree|host' -V  # by name, verbose
ctest --test-dir build/host -j 8               # parallel
```

## Layout

| Path | What it is |
|---|---|
| `hello/`, `basic/`, `cpp/`, `dlopen/`, `direct/`, `tree/`, `host/`, `exe/` | C and C++ fixture programs |
| `multicall/`, `ownenv/`, `version/`, `include/`, `many/` | Fixtures for aliases, shims, symbol versions, include warnings and many-object FELFs |
| `tools/mangle.c` | Damages a FELF deterministically for the corruption tests |
| `cmake/TestUtils.cmake` | Assertions shared by the script tests |
| `cmake/*.cmake` | One CTest test per multi-step scenario |
| `docker/` | Manual cross-distribution check: `run.sh` packs FELFs and runs them in stock Debian and Ubuntu containers |

## Fixtures

Packed files are CTest fixtures, not files built at configure time:

- `setup/pack-*` packs one artifact (for example `setup/pack-hello-lazy`).
- A test that needs it says `FIXTURES_REQUIRED hello-lazy`.
- `setup/multi-links`, `setup/multicall-links`, ... create the directory
  trees the execution tests use.
- If a setup test fails, CTest skips its dependents (`Not Run`) instead of
  reporting a misleading pass.  The artifacts are under
  `build/<dir>/tests/work/`, so a failed test can be inspected directly.

## Options

Set these when configuring:

| Variable | Meaning |
|---|---|
| `FELF_TEST_CLI` | CLI under test when `FELF_BUILD_CLI` is off (Nix passes the installed `felf`) |
| `FELF_TEST_STATIC_BOOTSTRAP` | The embedded bootstrap is static; enables the no-`PT_INTERP` and host-`LD_PRELOAD` tests |
| `FELF_TEST_STATIC_CLI` | The CLI itself is static; enables the `cli/static` check |
| `FELF_TEST_BUSYBOX` | Static busybox used by the `/nix`-masked namespace test (default: the environment variable of the same name) |
| `FELF_TEST_READELF` | `readelf` used for static/store-reference checks |
| `FELF_TEST_BASH` | Bash used for the few process-level tests |

`FELF_TEST_STATIC_BOOTSTRAP` defaults to whether this build uses a static
bootstrap or a prebuilt one.  The `direct/*` tests always run: every felf
carries FELF's glibc (handed in or built by `src/glibc`), and configure
checks that a `FELF_TEST_CLI` does too.

## Adding a test

For a single command, register it directly:

```cmake
felf_add_output_test(run/hello-lazy hello-lazy "env=lazy" ${felf_env}
    FELF_TEST_VALUE=lazy "${FELF_TEST_WORK}/hello-lazy.felf" one)
set_tests_properties(run/hello-lazy PROPERTIES LABELS "run")
```

For several steps, add a `cmake/*.cmake` script (include `TestUtils.cmake`)
and register it with `felf_add_script_test`.  Keep assertions specific: a
script is one CTest test, and `felf_assert_contains` names the failed check.
