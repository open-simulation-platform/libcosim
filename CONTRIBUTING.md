Contributor guidelines
======================

This document contains a set of rules and guidelines for everyone who wishes
to contribute to the contents of this repository, hereafter referred to as
"the software".


General
-------
All contributors implicitly agree to license their contribution under the same
terms as the rest of the software.  See the `LICENSE` file for details.

All contributions to the software, in the form of changes, removals or
additions to source code and other files under source control, shall be made
via pull requests.  A pull request must always be reviewed and merged by someone
other than its author.

Programming language and style
------------------------------
The primary programming language is C++, specifically C++17.
Code should be written in "[modern C++]" style, using high-level types and
constructs when possible.  Use the [C++ core guidelines] actively.

This is especially important when it comes to resources, which should, with
extremely few exceptions, be managed automatically using standard types or
user-defined RAII types.  Do not use explicit `new`/`delete` or `malloc`/`free`
other than in low-level code where it is inavoidable. Use [smart pointers] and
[standard containers] rather than raw (owning) pointers.

Errors are signaled by means of exceptions. The exception to this rule is
functions with a pure C interface, which must necessarily use error codes.

[modern C++]: https://docs.microsoft.com/en-gb/cpp/cpp/welcome-back-to-cpp-modern-cpp
[C++ core guidelines]: https://github.com/isocpp/CppCoreGuidelines
[smart pointers]: https://en.cppreference.com/w/cpp/header/memory
[standard containers]: https://en.cppreference.com/w/cpp/container


Naming and formatting
---------------------
The following are *strict rules*:

  * **Use [clang-format] to format code**.
    Use the `-style=file` option to read the formatting rules from the
    `.clang-format` file.
    (Rationale: This is a low-effort, low-friction way of enforcing a uniform
                code style.)
  * **API symbols use the C++ standard library naming conventions.**
    This means `snake_case` for namespaces, functions, classes, class members,
    and constants, and `SCREAMING_SNAKE_CASE` for macros.
    (Rationale: This style is familiar to 100% of C++ programmers, and it
                looks and feels consistent when used together with standard
                C++ and Boost symbols.)
  * **Use a single line feed character to terminate lines, never carriage
    return.**
    This is configurable in most editors, as well as in Git.
    (Rationale: We need to standardise on one thing, and CRLF is just a
                pointless Microsoft-specific historic artifact.)
  * **Never use the `using` directive (e.g. `using namespace foo;`) in
    headers.**
    (Rationale: It leads to severe namespace pollution with a high probability
                of name clashes.)
  * **Only use `using` declarations (e.g. `using foo::bar;`) in headers when the
    purpose is to declare symbols which are part of the API.**
    (Rationale: Using it simply for convenience leads to pointless namespace
                pollution.)

The following are *recommendations*:

  * Local variables and parameters are in `lowerCamelCase`.
  * Template type parameters are in `UpperCamelCase`.
  * Private member variables are named differently from local variables,
    typically using a trailing underscore (`likeThis_`) or an `m_` prefix
    (`m_likeThis`).
  * Avoid the `using` directive in source files too.  Prefer to use namespace
    aliases (e.g. `namespace sn = some_verbosely_named_namespace;`) instead.

[clang-format]: https://clang.llvm.org


Directory structure
-------------------
`libcosim` follows the [pitchfork layout](https://api.csswg.org/bikeshed/?force=1&url=https://raw.githubusercontent.com/vector-of-bool/pitchfork/develop/data/spec.bs)
and the source directories are organised as follows:

  * `cmake/`:       CMake scripts and other build system files.
  * `data/`:        Additional files included in revision control, but not explicitly code.
  * `include/`:     Header files that define the public API.
  * `src/cosim/`:   Source code with merged header placement for internal headers.
  * `tests/`:       Test suite for the library.
  * `test/data`:    Resources used by the test suite.

Quality checks
--------------
On Linux, `-DLIBCOSIM_ENABLE_SANITIZERS=ON` enables ASan and UBSan for the
local build. The [quality workflow](.github/workflows/ci-quality.yml) enables
ProxyFMU and runs every C++ test suite under ASan and UBSan with leak detection
enabled. CI uses [cpp-linter-action](https://github.com/cpp-linter/cpp-linter-action)
to run clang-tidy 23 and clang-format 23.1.6 on C++ sources and headers in
`src/` and `include/`, not just changed files. CI installs clang-format 23.1.6
from the LLVM package repository and verifies its version after the action.
Both tools use the repository's
`.clang-tidy` and `.clang-format` configurations. CI reads additional per-tool
exclusions from `.clang-tidy-ignore` and `.clang-format-ignore`, one path or
glob per line; clang-format also reads its ignore file when invoked locally.
Findings are advisory and are posted in a single updatable comment on
same-repository pull requests. The workflow
also runs on pushes and manual dispatch, but not on fork pull requests, which
lack the Conan remote credentials.

For a local check, install the Clang tools you intend to run. For clang-tidy,
install Python 3, `run-clang-tidy`, and `clang-tidy` on `PATH`, and configure a
build with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. Fix mode also needs
`clang-apply-replacements` on `PATH`. Run the same script with PowerShell 7
on Windows or Bash on Linux and macOS:

```powershell
pwsh .\clang-check.ps1
```

```sh
bash ./clang-check.ps1
```

Use `-h` or `--help` to display the available options and path arguments.
Pass `--format` to run only clang-format or `--tidy` to run only clang-tidy.
Pass both, or neither, to run both tools. These options also limit which tools
are run in fix mode. A compilation database is not needed for `--format` alone.

The script looks for `compile_commands.json` in `build/Release` by default.
If the database is elsewhere, specify its containing directory with
`-BuildDirectory` in PowerShell or `--build-dir` in Bash. For example:

```powershell
pwsh .\clang-check.ps1 -BuildDirectory build\Release\build\Release
```

```sh
bash ./clang-check.ps1 --build-dir build/Release/build/Release
```

The compilation database contains commands for configured source files, not
separate commands for headers. Tests and optional integrations appear only
when their build options and dependencies are enabled. `run-clang-tidy` checks
only selected source files present in the database; it does not infer commands
for other sources. It checks selected headers only when they are included by
those source files. A header-only selection is not supported: include at least
one corresponding source file or directory in the selection. Check the
`run-clang-tidy` output to confirm which translation units were analyzed;
the runner can succeed even when no database entries match.

By default, the script checks without modifying files and returns a nonzero
exit code for tool failures and errors. Clang-tidy warnings do not cause failure
unless configured as errors. The final summary counts emitted error, warning,
and note diagnostics separately for each tool pass and marks failed passes.
Suppressed diagnostics are not counted, and counts in a fix pass may include
findings subsequently fixed; check the final tidy pass for remaining findings.
Clang-format fix mode does not report the number of files it changed.
To apply available clang-tidy fixes and then
format the selected files in place, pass `--fix`:

```powershell
pwsh .\clang-check.ps1 --fix
```

```sh
bash ./clang-check.ps1 --fix
```

When clang-tidy is selected, fix mode uses `run-clang-tidy -fix` to collect
and apply available fixes; this is not the same as clang-tidy's `--fix-errors`.
When both tools are selected, it runs clang-format even if clang-tidy fails.
It then rechecks with clang-tidy without applying further fixes. Unresolved
errors still cause a nonzero exit, and not every diagnostic can be fixed
automatically. Fixes can affect files referenced by diagnostics, so review
the changes, resolve remaining compiler errors, and rerun the check.

Read-only clang-tidy checks use up to four workers by default, limited by the
available CPU count. Specify a positive worker count with `-Jobs` in
PowerShell or `--jobs` in Bash; use `1` for serial checks. More workers can
consume substantial memory. The fix pass itself remains serial to avoid
concurrent edits to shared headers.

```powershell
pwsh .\clang-check.ps1 -Jobs 2
```

```sh
bash ./clang-check.ps1 --jobs 2
```

The compilation database must describe this checkout; its build generator must
support exporting compile commands, such as Ninja. Clang-format reads its
ignore file, while the local tidy check does not apply `.clang-tidy-ignore`;
only CI applies it. Pass file or directory paths after the build
option to check them recursively; without paths, it checks `src/` and `include/`.
The script selects C/C++ sources and headers with extensions `.c`, `.cc`,
`.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`, and `.hxx`.
For example, `bash ./clang-check.ps1 --build-dir build/Release/build/Release
src/cosim/fmi/v3 include/cosim/fmi` checks just those directories.

FMI Library 3.0.4 releases its FMI 3 instance-name allocation through
`fmi3_import_free_instance`. After `fmi3Fatal`, libcosim first destroys the
library's C API wrapper so that this cleanup cannot call the FMU, as required
by FMI 3. The workflow runs the FMI 3 suites without leak suppressions.


File names
----------
Use the following file extensions:

  * `.hpp` for C++ headers
  * `.cpp` for C++ sources

Use underscores to separate words in file names (`like_this.hpp`).


API documentation
-----------------
The entire API should be documented, including the non-public API.
Documentation comments in [Doxygen] format should
immediately precede the function/type declarations in the header files.

For unstable, experimental and/or in-development code, minimal documentation
(i.e., a brief description of what a function does or what a class represents)
is OK.  As the API matures and stabilises, higher-quality documentation is
expected.

For functions, high-quality documentation should include:

  * What the function does.
  * Parameter descriptions.
  * Return value description.
  * Which exceptions may be thrown, and under which circumstances.
  * Side effects.
  * If the function allocates/acquires resources, where ownership is not made
    explicit through the type system (e.g. raw owning pointers):

      - Transfer/acquisition of ownership.
      - Requirements or guarantees with regard to lifetime.

  * Preconditions, i.e., any assumptions made by the function about its input,
    and which are not guaranteed to be checked.  (Typically, these are checked
    in debug mode with `assert`.)

For classes, high-quality documentation should include:

  * For base classes: Requirements and guidelines for subclassing.  (Which
    methods should/must be implemented, how to use protected members, etc.)
  * If the class has reference semantics—i.e., if a copy of an object will
    refer to the same data as the original (e.g. `std::shared_ptr`)—this
    should be stated.
  * Lifetime/ownership issues not expressible through the type system.

[Doxygen]: http://www.doxygen.org

Documentation build
-------------------
The user guide and FMI 3 feature reference are reStructuredText in
`docs/`. API reference directives use Doxygen XML through Breathe. Install
[uv](https://docs.astral.sh/uv/), create a virtual environment with
`uv venv .venv` if needed, and install `docs/requirements.txt` with
`uv pip install --python .venv -r docs/requirements.txt`. Enable
`LIBCOSIM_BUILD_DOCS` to build the complete public and internal documentation
with the normal CMake build, or build the `doc` target separately. The Sphinx
build treats warnings as errors.
Conan supplies Doxygen for documentation builds; direct CMake builds need it
on `PATH`. See the README for Conan options and output paths.

Keep the Design stack diagram, submodule pages, FMI 3 overview and feature
reference aligned with the implementation and tests. Add a concrete public API
example and its prerequisites to the relevant feature page when behavior
changes; place important limitations beside the feature. Document public and
internal declarations at their declarations and update cross-links as needed.
