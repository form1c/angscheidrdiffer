# Contributing

Thank you for considering a contribution.

## Before you start

For anything beyond a small fix, open an issue first and describe what you intend to change. That avoids work that cannot be merged.

## Setting up

`doc/development.md` describes the source layout, the engines and the design decisions. `doc/installation.md` section 1 lists the requirements. Building and running the tests:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

All tests have to pass before a change is proposed.

## Expectations for a change

**Every change comes with a test.** A fix comes with a case that fails without it. A new behaviour comes with a case that describes it.

**Verify a new test by mutation.** Damage the code the test covers and check that the test fails. A test that stays green over damaged code does not test what it claims to.

**Test the effect, not the setting.** Comparing a configured value says nothing about whether that value has any effect. Where an option is meant to change the outcome, compare the outcome with and without it.

**The source is written in English.** Identifiers, comments, test names and
interface text. A comment in another language is treated as a defect.

**Keep the engine free of interface code.** Everything below `code/diff/` links against `Qt6::Core` only and is tested without a display. Comparison and merge rules are decided there, not in a widget. The reverse dependency does not exist and must not be introduced.

**Blocks refer to raw lines.** Ignore options change what is compared, never what is displayed or written. A normalised line list always has the same length as the original one.

**Note what a test cannot see.** Some conditions live in the interface layer and are not observable from the engine tests. Where that applies, say so in a comment next to the case rather than writing a test that appears to cover it.

## Interface changes

The interface is not covered by automated tests. A change there is checked by running the application, including the offscreen mode described in `doc/development.md` section 2.

## Licence of contributions

Contributions are accepted under the MIT licence of this project. By opening a pull request you agree that your contribution may be published under those terms.
