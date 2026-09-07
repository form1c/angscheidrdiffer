# AnGscheidrDiffer: Developer Manual

| | |
|---|---|
| **Applies to** | AnGscheidrDiffer, source tree |
| **Audience** | Anyone reading, changing or extending the source |
| **Scope** | Layout of the source, the engines, the interface and the design decisions behind them. Building and installing is covered in [Installation](installation.md), working with the application in the [User manual](usage.md) |

---

## Contents

1. [Outline](#1-outline)
2. [Development environment](#2-development-environment)
3. [Directory layout](#3-directory-layout)
4. [The comparison engine](#4-the-comparison-engine)
5. [The ignore pipeline](#5-the-ignore-pipeline)
6. [Merging](#6-merging)
7. [Three-way merge](#7-three-way-merge)
8. [Folder comparison](#8-folder-comparison)
9. [The interface](#9-the-interface)
10. [Aligned view](#10-aligned-view)
11. [Icons](#11-icons)
12. [Version number](#12-version-number)
13. [Settings](#13-settings)
14. [Tests](#14-tests)
15. [Deliberate design choices](#15-deliberate-design-choices)

---

## 1. Outline

The source is split into two layers with a strict dependency direction.

```
code/diff/   comparison and merge logic, no interface code   →  library diffcore
code/ui/     Qt widgets, one window with tabs                →  application
```

`diffcore` links against `Qt6::Core` only. It contains no widget and opens no dialog, which is what makes it testable without a display. The interface layer depends on the engine layer. The reverse dependency does not exist and must not be introduced.

Everything the comparison produces is expressed as line ranges into the original line lists. Neither engine copies the file contents around, and no engine writes a file.

---

## 2. Development environment

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Requirements are listed in the installation manual. For work on the interface without a display, every entry point can be exercised with the offscreen platform:

```bash
QT_QPA_PLATFORM=offscreen ./build/angscheidrdiffer testdata/left.cpp testdata/right.cpp
```

The message `This plugin does not support propagateSizeHints()` is emitted by the offscreen platform and does not indicate a fault.

### 2.1 Starting clean

Everything the build produces lives in three directories, all of which
`.gitignore` excludes:

| Directory | Contents |
|---|---|
| `build/` | Development build |
| `build-deb/` | Intermediate state of the package build |
| `release/` | Finished packages |

```bash
rm -rf build build-deb          # remove the build, keep finished packages
rm -rf build build-deb release  # remove everything the build produced
```

The two are separate on purpose. `build/` and `build-deb/` are intermediate
state and can go at any time. `release/` holds the packages that are attached
to a release, so deleting it is a decision rather than housekeeping.

> **Attention:** A `build/` directory does not survive being moved. Its
> `CMakeCache.txt` stores absolute paths, so after relocating the source tree
> the build has to be removed and configured again.

---

## 3. Directory layout

| Path | Contents |
|---|---|
| `code/diff/` | Comparison and merge engines, free of interface code |
| `code/ui/` | Window, tabs and widgets |
| `code/main.cpp` | Command line handling and the entry points |
| `data/` | Resource description for the icons, launcher and service menu |
| `graphics/icons/` | Action icons, one directory per pixel size |
| `graphics/logo/` | Application icon |
| `scripts/` | Package build, removal of a direct installation, icon tooling, publication check |
| `tests/` | Engine tests |
| `testdata/` | Sample files for manual checks |

### 3.1 Files of the engine layer

| File | Responsibility |
|---|---|
| `diffengine` | Myers comparison, block list, post-processing |
| `linenormalizer` | Turns raw lines into comparison lines according to the active options |
| `commentstripper` | Removes comments for the languages it knows, across line boundaries |
| `intralinediff` | Determines the changed range within a pair of lines |
| `mergeengine` | Two-way merge operations as line edits |
| `merge3engine` | Three-way merge, chunk list and result |
| `foldercompare` | Recursive comparison of two directory trees |

### 3.2 Files of the interface layer

| File | Responsibility |
|---|---|
| `appwindow` | The single window, the tab bar, the toolbar and the menus |
| `comparetab` | Base class of all tabs, defines what the window expects from a tab |
| `filedifftab` | File comparison as a tab, including loading, saving and watching |
| `folderdifftab` | Folder comparison as a tab |
| `mergetab` | Three-way merge as a tab |
| `diffview` | Arrangement of the two panes, highlighting, navigation and merge operations |
| `difftextedit` | Text pane with line numbers, drag and drop and placeholder rendering |
| `connectorwidget` | The arrows between the panes |
| `overviewbar` | Map of the whole comparison beside the panes |

---

## 4. The comparison engine

`DiffEngine::compare` takes two line lists and returns a list of blocks. A block carries a type and one range per side:

| Type | Meaning |
|---|---|
| `Equal` | Identical on both sides |
| `Changed` | Present on both sides, but different |
| `OnlyLeft` | Present on the left only |
| `OnlyRight` | Present on the right only |

The blocks cover both files completely and without gaps. Every consumer relies on that property, so a change to the engine has to preserve it.

### 4.1 Algorithm

The comparison is a Myers difference of complexity O(ND). Two preparations come before it:

1. **Trimming.** A common prefix and suffix is removed. Files that differ in the middle only reduce to a small problem.
2. **Interning.** Each distinct line becomes an integer. The inner loop then compares integers instead of strings.

The backtrack stores the state before each iteration and appends in reverse, which avoids the quadratic cost of prepending to a list.

> **Note:** The trace grows with the product of the edit distance and the file length. Above a fixed limit the engine stops and reports the remainder as one large change instead of exhausting memory. Inputs that are both large and almost completely different therefore yield a coarse result rather than a failure.

### 4.2 Post-processing

Two passes run after the comparison. Both are separate functions, so their effect can be tested on its own.

**Ambiguous positions.** When an ignore option is active, a pure insertion or deletion inside a run of lines that normalise to the same value has several valid positions, and the algorithm picks one of them. `slideAmbiguousBlocks` moves such a block to the position where the raw lines of the surrounding pairs match best. Without an active ignore option the pass has nothing to do.

**Trivial differences.** `markTrivialBlocks` marks blocks whose lines all normalise to empty. With comments ignored, a block that contains nothing but comments is such a case. Marked blocks are drawn discreetly and are skipped by the navigation, the difference count and the exit code. They can still be merged, because the raw lines are still there.

The second pass is meaningful only when an option is active that can empty a line. The caller decides, the engine does not guess.

### 4.3 Detecting moved blocks

After the comparison, blocks that exist on one side only are compared by content. When the joined content of a left-only block equals that of a right-only block, both are linked through `movedPartner` and drawn in their own colour. Lines shorter than five characters after trimming are not considered, because short lines match by accident too often.

---

## 5. The ignore pipeline

Every option is applied by `LineNormalizer` in one place. The engine never sees the option, it sees the resulting comparison lines. Both the file and the folder comparison call the same normaliser, so a file opened from a folder comparison is judged by the same rules.

| Option | Effect on a comparison line |
|---|---|
| Whitespace | Runs of whitespace are collapsed, leading and trailing whitespace is removed |
| Letter case | The line is lowercased |
| Comments | Comments are removed for the languages the stripper knows |
| First N lines | The first N lines become an empty string |
| Line endings | Handled before the comparison, see below |

**The blocks always refer to the original lines.** The normalised list is the same length as the original one, which is the reason the interface can display raw text while comparing normalised text. Any change to the normaliser has to keep the line count.

The comment stripper is a state machine across line boundaries, because a comment can span lines and a comment marker inside a string is not a comment. It knows the C family and HTML. The language follows from the file name.

> **Note:** Ignoring comments works on content. A line that consists of a comment and exists on one side only still counts as a difference, because the line itself is missing rather than its content differing.

Line endings are normalised to a single form when the files are read. The original ending of each file is remembered and restored on saving, so opening and saving a file does not rewrite every line.

---

## 6. Merging

`MergeEngine` contains the merge logic without any interface code. Every operation returns a list of `LineEdit` values, each one a replacement of a line range in the target:

```cpp
struct LineEdit {
    int dstStart = 0;
    int dstCount = 0;       // 0 means insert before dstStart
    QStringList newLines;
};
```

The view applies these edits from the bottom up, which keeps the earlier positions valid, and wraps them into one undo step. Separating the decision from the application is what makes the merge testable without a window.

### 6.1 Scope of an operation

| Operation | Source | Effect |
|---|---|---|
| Selection | The selected lines | Only the selected lines are transferred |
| Section | The current block | The block is transferred as a whole |
| All | The whole file | The target is replaced |
| Use Both | Section or selection | Both variants are kept, in the chosen order |

### 6.2 Line alignment inside a block

A selection merge must not replace the whole block, otherwise transferring one line of a block would discard the neighbouring lines of the target. The engine therefore aligns the lines of both sides first.

The alignment is a monotone pass over both line lists. Two lines are paired when their similarity reaches a threshold of one half. The similarity is the ratio of the longest common subsequence, computed on trimmed lines and capped in length, so that a long line cannot dominate the running time.

Selected lines then replace their aligned partner. A selected line without a partner is inserted at the aligned position. Unselected source lines leave their partner untouched. "Use Both" transfers the selected lines verbatim, including lines that are equal on both sides, because the closing brace of a block is usually part of the selection and has to come along.

### 6.3 Pending recomputation

The comparison is recomputed after a short delay while typing. Every merge operation forces a pending recomputation to run first. Without that step an operation would act on a block list that no longer matches the document, and would appear to work only on the second attempt.

---

## 7. Three-way merge

`Merge3Engine` follows the principle of `diff3`. It runs two comparisons against the common base, one for each side, and walks both results together. The result is a list of chunks:

| Type | Resolution |
|---|---|
| `Stable` | Nobody changed anything |
| `OnlyMine` | Taken from mine automatically |
| `OnlyTheirs` | Taken from theirs automatically |
| `BothSame` | Both made the same change, taken automatically |
| `Conflict` | Both changed the same region differently, a decision is required |

Changes that overlap or merely touch are combined into one cluster. That is the conservative choice. It produces a slightly larger conflict rather than two adjacent ones whose resolutions could contradict each other.

`buildResult` assembles the result from the raw lines and a map of decisions. Unresolved conflicts are written with `diff3` style markers and counted, which is what the batch mode reports through its exit code.

> **Attention:** The ignore options do not apply to the three-way merge. A merge writes a file, and a line that differs only in whitespace is still a different line. Treating it as equal would silently discard one of the two versions.

---

## 8. Folder comparison

`FolderCompare::compareFolders` merges two directory trees recursively and returns the root entry. Each entry carries its status, the sizes and modification times of both sides and its children.

| Rule | Behaviour |
|---|---|
| Exclude patterns | Wildcards matched against the name, not the path |
| Binary files | Detected by a null byte in the first kilobytes, then compared byte by byte |
| Text files | Compared through the same ignore pipeline as the file comparison |
| No active option | Compared byte by byte, which is the fast path |
| Directory status | Derived from the children. Any child that is not identical makes the directory different |
| One-sided directories | Not descended into. The entry reports the side it exists on |
| Symbolic links | Not followed, which rules out cycles |
| Type conflict | A file and a directory of the same name count as different |

The scan runs outside the interface thread and can be cancelled through an atomic flag. The tab cancels a running scan and waits for it when it closes.

Because directory status is inherited upwards, the tree can start collapsed and still show where the differences are.

---

## 9. The interface

There is exactly one window. Every comparison is a tab inside it.

```
AppWindow            window, tab bar, one toolbar, menus, status bar
 └── CompareTab      base class: title, status text, options changed, reload, close
      ├── FileDiffTab      two panes, loading, saving, file watching
      ├── FolderDiffTab    tree with its own control row
      └── MergeTab         base, mine and theirs above the editable result
```

`CompareTab` defines what the window may expect from a tab. The window routes an action to the current tab and enables or disables its actions when the tab changes. Actions that only apply to a file comparison are disabled while a folder tab is in front.

Options are global. A change is written to the settings and broadcast to all open tabs, so two tabs never disagree about the rules of comparison.

### 9.1 The file comparison

`DiffView` arranges the two panes with the connector between them and the overview bar beside them. It owns the block list and performs the merge operations by applying the edits the merge engine returns.

Both panes stay editable. After a change the comparison is recomputed with a short delay, so that typing does not trigger a comparison per keystroke. A guard suppresses the delay for programmatic edits, which are followed by an explicit recomputation instead.

Scrolling is synchronised in both directions. The vertical mapping goes through the block list, which keeps the sides aligned even where the two files have different lengths. A guard prevents the two scroll bars from driving each other in a loop.

### 9.2 Sections

Clicking into a pane selects the block under the cursor, and the block is framed in both panes. Only blocks that are not equal can become the current section. The frame is drawn in both rendering paths. For an insertion, where one side has no lines, it degenerates into a thick line at the insertion point.

---

## 10. Aligned view

Corresponding lines are kept opposite each other by inserting placeholder rows on the shorter side. **The placeholders exist only while drawing. They are never part of the document.** Undo, redo, typing and input methods therefore behave exactly as in a plain editor, because the document is untouched.

The pane takes over painting when a placeholder table is set and word wrap is off. It builds the list of visible rows, draws the background of each real line, then the text layout of that line including the intra-line highlight and the selection, and finally the placeholder rows as hatched areas.

Two consequences follow from the chosen approach:

1. **Uniform line height is required.** Placeholder arithmetic assumes every row has the same height, which is why the aligned view and word wrap exclude each other. Selecting one switches the other off and reports it in the status bar.
2. **The scroll bar keeps counting real lines.** The value remains the first visible real line, which avoids fighting the internals of the base class and keeps cursor visibility working. Because the base class does not know about the placeholders, the scroll range is extended separately so that the last lines stay reachable when placeholders follow them.

> **Note:** A gap above the anchor line is skipped while scrolling, and a gap before the very first line is visible only at scroll position zero. Both follow from the scroll bar counting real lines.

---

## 11. Icons

The action icons live in `graphics/icons/<size>/<Name>_<size>.png`. Only the sizes from 16 to 64 pixels are embedded, because the interface never asks for more and larger renditions would only grow the binary. The application icon comes from `graphics/logo/`.

`actionIcon("<Name>")` in `code/ui/iconloader.h` assembles one icon from all embedded sizes. After any change to the icon set:

```bash
python3 scripts/generate-icons-qrc.py   # rebuild data/icons.qrc
python3 scripts/check-icon-names.py     # compare names against the source
cmake --build build -j$(nproc)
```

> **Attention:** A misspelled icon name compiles without complaint and shows an empty icon at runtime. The check script exists for that reason. It also lists icons that are present in the resources but referenced nowhere.

The window icon is set from the embedded resources rather than from the icon theme, so that an outdated installed theme icon cannot win over the current one.

---

## 12. Version number

**The version is defined in exactly one place: the `project()` line at the top of `CMakeLists.txt`.**

```cmake
project(AnGscheidrDiffer VERSION 1.0.0 LANGUAGES CXX)
```

To release a new version, change that line. Nothing else carries a number.

Everything else derives from it:

| Consumer | How it obtains the version |
|---|---|
| The application | `CMakeLists.txt` passes `APP_VERSION` as a compile definition. `code/main.cpp` hands it to `QApplication::setApplicationVersion` |
| `--version` on the command line | Qt prints the application version |
| The about dialog | Reads `QApplication::applicationVersion`, so it holds no number of its own |
| The Debian package | `scripts/build-deb.sh` reads the `project()` line from `CMakeLists.txt`. A version given as an argument overrides it |

The format is three numbers separated by dots, as in `1.0.0`.

After changing the line, configure and build again. CMake regenerates the
compile definition, and the new number appears in all four places at once:

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
./build/angscheidrdiffer --version
```

> **Attention:** Do not write the version into the source. A second number
> falls out of step with the first, which is exactly what this arrangement
> prevents.

---

## 13. Settings

Settings are stored through `QSettings` under the application name. The ignore options, the view options and the recently compared pairs are kept there. The window geometry is restored on start.

A guard suppresses the broadcast while the stored options are loaded into the toolbar, otherwise loading a setting would immediately write it back.

---

## 14. Tests

The tests cover the engine layer. They run without a display and are the fast feedback loop while working on a comparison rule.

| Target | Subject |
|---|---|
| `diffengine` | Comparison, normaliser, comment stripper, intra-line difference, post-processing |
| `mergeengine` | Line alignment and the resulting line edits |
| `merge3engine` | Chunk classification and result assembly |

The three-way tests include an invariant that the chunks cover all three inputs completely. An engine change that loses lines fails there rather than in a file someone saved.

The interface is not covered by automated tests. Changes to the interface are checked by running the application, including the offscreen mode described in section 2.

---

## 15. Deliberate design choices

**Qt only, no KDE Frameworks.** The application uses `Qt6::Widgets` and `Qt6::Concurrent`. Syntax highlighting through KF6SyntaxHighlighting is optional and detected at configure time. This keeps the application usable outside KDE Plasma. The Dolphin service menu is an addition, not a requirement.

**Engine without interface.** The comparison and merge logic knows nothing about widgets. Every rule can be tested on line lists, which is why the merge behaviour is decided in the engine and only applied in the view.

**Blocks refer to raw lines.** Ignore options change what is compared, never what is displayed or written. The normalised list always has the same length as the original.

**Placeholders as a rendering layer.** Aligning by inserting real lines into the document would mean owning undo, input handling and text editing. The rendering layer keeps the base class in charge of all three, at the cost of the two restrictions listed in section 10.

**Merge as one undo step.** A merge that spans several blocks is applied bottom up and joined into a single undo entry, so that one action is undone by one undo.

**Conservative conflicts.** In the three-way merge, adjacent changes from both sides are combined instead of being reported separately. A larger conflict that a person resolves is preferable to two neighbouring resolutions that contradict each other.



