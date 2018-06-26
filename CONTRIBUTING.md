Contributor guidelines
======================

This document contains a set of rules and guidelines for everyone who wishes
to contribute to the contents of this repository, hereafter referred to as
"the software".


General
-------
All contributors implicitly agree to license their contribution under the same
terms as the rest of the software, and accept that those terms may change in the
future.  See the `LICENCE.txt` file for details.

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

  * **API symbols use the C++ standard library naming conventions.**
    This means `snake_case` for namespaces, functions, classes, class members,
    and constants, and `SCREAMING_SNAKE_CASE` for macros.
    (Rationale: This style is familiar to 100% of C++ programmers, and it
                looks and feels consistent when used together with standard
                C++ and Boost symbols.)
  * **Use spaces for indentation and alignment, never tabs.**
    Use 4 spaces for indentation.
    (Rationale: Spaces are usable for both indentation and alignment while
                tabs are only usable for indentation, and mixing them is a
                bad idea.)
  * **Use a single line feed character to terminate lines, never carriage
    return.**
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
  * Use "[the one true bracing style]".
  * Lines should be broken at 80 columns, except in special cases when this
    makes code particularly ugly/unreadable.
  * Do not indent from the top-level namespace declaration (i.e., the one that
    surrounds all symbols in the file).

However, when it comes to purely aesthetic issues, the most important thing
is to stay consistent with surrounding code and not mix coding styles.

With regards to indentation and end-of-line markers, it is recommended that
contributors install the [EditorConfig] plugin for their editor/IDE.  The root
source directory contains a `.editorconfig` file with the appropriate settings.

[the one true bracing style]: https://en.wikipedia.org/wiki/Indent_style#Variant:_1TBS
[EditorConfig]: http://editorconfig.org


File names
----------
Use the following file extensions:

  * `.hpp` for C++ headers
  * `.cpp` for C++ sources
  * `.h` for pure C headers
  * `.c` for pure C sources

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
