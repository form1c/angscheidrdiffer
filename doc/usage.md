# AnGscheidrDiffer: User Manual

| | |
|---|---|
| **Applies to** | AnGscheidrDiffer 1.0.0 |
| **Audience** | Anyone comparing or merging files with the application |
| **Scope** | Working with the interface: comparing, merging, folder comparison and three-way merge. Building and installing is described in [Installation](installation.md), the inner workings in the [Developer manual](development.md) |

---

## Contents

1. [The window](#1-the-window)
2. [Comparing two files](#2-comparing-two-files)
3. [Merging](#3-merging)
4. [Ignore options](#4-ignore-options)
5. [View options](#5-view-options)
6. [Comparing folders](#6-comparing-folders)
7. [Three-way merge](#7-three-way-merge)
8. [Searching](#8-searching)
9. [Saving and exporting](#9-saving-and-exporting)
10. [Keyboard reference](#10-keyboard-reference)
11. [Using it from a script](#11-using-it-from-a-script)

---

## 1. The window

There is one window. Every comparison is a tab inside it, so several files and folders can stay open side by side.

| Tab | Opened by |
|---|---|
| File comparison | Two files on the command line, **File** then **Open Left** and **Open Right**, or a double click in a folder comparison |
| Folder comparison | Two directories on the command line, or **File** then **Compare Folders...** |
| Three-way merge | `--merge` on the command line, or **File** then **3-Way Merge (Base/Mine/Theirs)...** |

The toolbar always acts on the tab in front. Actions that only apply to a file comparison are disabled while a folder tab is shown.

**The options are global.** Changing an ignore option applies it to every open tab at once, so two tabs never judge a difference by different rules.

Files can also be dropped onto a pane. Dropping a directory on one side remembers it, and as soon as both sides hold a directory the folder comparison opens.

---

## 2. Comparing two files

```bash
angscheidrdiffer left.cpp right.cpp
```

### 2.1 What the display shows

Both files are shown side by side, each with its own line numbers. Around the text sit three aids:

| Element | Meaning |
|---|---|
| Coloured line background | The line belongs to a difference. Each kind of difference has its own colour |
| Stronger colour inside a line | The part of the line that actually changed |
| Bar beside the panes | A map of the whole file. Every difference appears as a stripe, and a click jumps to it |
| Arrows between the panes | One pair per visible difference. A click copies that block to the other side |
| Small wedge in the line number column | The other side has additional lines at this point |

The status bar shows how many differences exist and which one is current, for example `Difference 3 of 12`. When both files agree it reads `Files are identical`.

### 2.2 The current section

A **section** is one connected block of changed lines. It is the unit that the section merges work on.

Clicking into a coloured block makes it the current section, and a frame marks it in **both** panes. Navigating with `Alt+Up` and `Alt+Down` does the same. Only blocks that differ can become the current section, so a click into unchanged text leaves the selection alone.

Where one side has no lines at all, meaning something was purely inserted, the frame becomes a thick line at the insertion point.

### 2.3 Editing

**Both sides stay editable during the comparison.** Typing works as in any editor, and the comparison is recomputed shortly after the last keystroke rather than on every character.

Undo and redo of your own typing work per pane with the usual `Ctrl+Z`. The separate buttons in the toolbar undo the last **merge**, see section 3.5.

If a file changes on disk while it is open, the application reloads it when you have no unsaved changes. Otherwise a banner offers **Reload** or **Ignore**.

---

## 3. Merging

Every merge moves text from one side to the other. Four operations differ in **how much** they move.

| Operation | What it transfers |
|---|---|
| **Selection → / ←** | Only the lines you selected |
| **Section → / ←** | The current section as a whole |
| **All → / ←** | Every difference, making the target equal to the source |
| **Use Both** | Both versions, in the order you choose |

The arrow always points at the **target**. `Selection →` takes the selection from the left pane and writes it to the right one.

> **Note:** Before every merge, a comparison that is still pending after your last keystroke is completed first. A merge therefore never works on an outdated picture of the file.

### 3.1 Section merge

Replaces the target range of the current section completely with the source. This is the usual behaviour of a comparison tool and the fastest way to take a change over.

Available as `Alt+Right` and `Alt+Left`, through the toolbar, the **Merge** menu and the arrows between the panes.

### 3.2 Selection merge

Transfers **only the selected lines** instead of the whole block. This matters whenever a block contains more than one change and you want just one of them.

Inside a block, the lines of both sides are paired by similarity first. A selected line then replaces the line it was paired with. A selected line without a partner is inserted at the matching position, and **lines you did not select leave their counterpart untouched**.

**Example.** The left file holds a comment that the right one does not have, and one line differs:

```
left                          right

int add(int a, int b)         int add(int a, int b)
{                             {
    // new line                   test(a);
    test();                       return a + b;
    return a + b;             }
}
```

Selecting `test(a);` on the right and choosing **← Selection** gives:

```
int add(int a, int b)
{
    // new line          kept, it was not the counterpart of the selection
    test(a);             replaced test(); as the most similar line
    return a + b;
}
```

**← Section** would have replaced the whole block instead, and the comment would be gone. That is the expected behaviour of a section merge, and the reason both operations exist.

Without a selection in the source pane, the status bar says so rather than guessing which lines you meant.

### 3.3 Use Both

Keeps both versions instead of replacing one. Four variants decide the order, for example **Use Both (Left First)** inserts the left section before the right one.

With a selection in the source pane, **exactly the selected lines** are inserted, including lines that are equal on both sides. That last part is deliberate: a selection usually ends on a closing brace, and that brace has to come along for the result to compile.

### 3.4 All

Replaces every difference at once, which makes the target side equal to the source. Useful when a file is to be reset to the other version entirely.

### 3.5 Undoing a merge

**Every merge is a single undo step**, even when it spans several blocks. The two undo and redo buttons in the toolbar act on the side that was changed last and keep the scroll position where it is.

`Ctrl+Alt+Z` undoes, `Ctrl+Alt+Shift+Z` redoes.

---

## 4. Ignore options

These decide **when two lines count as equal**. They are in the **Options** menu and in the drop-down of the same name in the toolbar.

| Option | Effect |
|---|---|
| **Ignore Whitespace** | Leading, trailing and repeated spaces no longer make a difference |
| **Ignore Line Endings** | Files using CRLF and LF are not reported as different for that reason alone |
| **Ignore Comments** | Comments are disregarded. The application knows C, C++, Java, JavaScript, TypeScript and HTML, chosen by the file name |
| **Ignore Case** | Upper and lower case no longer make a difference |
| **Ignore first N lines** | The first N lines of both files always count as equal |

**The display always shows the original text.** An ignore option changes what is compared, never what you see or what gets written when you save.

The last option is meant for headers that a version control system rewrites, such as an `$Id$` keyword line, which would otherwise be reported on every single file.

> **Note:** Ignoring comments works on content. A line that consists only of a comment and is **missing** on one side still counts as a difference, because the line itself is absent rather than its content differing.

A difference whose lines all become empty through an ignore option, a comment-only block for instance, is drawn discreetly and skipped by the navigation and the counter. It can still be merged.

---

## 5. View options

| Option | Effect |
|---|---|
| **Aligned View** | Inserts placeholder rows so that corresponding lines stay opposite each other |
| **Word Wrap** | Wraps long lines instead of scrolling sideways |
| **Show Whitespace** | Makes spaces and tabs visible |

**Aligned View** is worth knowing in detail. Where one side has more lines than the other, the shorter side is padded with hatched rows so that matching lines stay level. Those rows exist only on screen. They are never part of the file, so typing, undo and saving behave exactly as without them.

> **Attention:** Aligned View and Word Wrap exclude each other, because the placeholder rows require every line to have the same height. Switching one on switches the other off and says so in the status bar.

---

## 6. Comparing folders

```bash
angscheidrdiffer dir1 dir2
```

The two trees are merged into one list. Every entry shows its status and the size and modification time of both sides.

| Status | Meaning |
|---|---|
| Identical | Same on both sides |
| Different | Present on both sides with differing content |
| Only Left, Only Right | Present on one side only |

A directory is marked as different as soon as any entry inside it is. The tree therefore starts collapsed and still shows where the differences are. **Expand All** and **Collapse All** are in the control bar.

A double click on a file opens it as a file comparison in a neighbouring tab, using the same ignore options.

### 6.1 Narrowing down what is shown

| Control | Effect |
|---|---|
| **Show** toggles | Hide entries by status, for example everything identical |
| **Filter** drop-down | Restrict by name pattern or to files changed since a given date |
| **Exclude** field | Skip entries entirely, as wildcards separated by semicolons. The default covers version control and build directories |

The **Show** toggles and the **Filter** only change what is displayed and need no new scan. The **Exclude** field changes what is compared and starts one.

### 6.2 Quick comparison

**Quick (size only)** compares the file size and nothing else. It is much faster on large trees, and it is less exact: two files of the same length with different content count as identical. Use it to get an overview, then switch it off to see the real result.

### 6.3 Acting on entries

The context menu of an entry offers:

| Entry | Effect |
|---|---|
| **Open Diff** | Opens the file comparison, as a double click does |
| **Copy to Left, Copy to Right** | Copies the entry to the other side, directories including their content |
| **Delete Left, Delete Right** | Deletes the entry on that side, after a confirmation |
| **Open Left in File Manager, Open Right in File Manager** | Opens the directory in the file manager of the desktop. For a file, the directory containing it is opened |

Copying and deleting are followed by a new scan. The nodes you had opened stay open.

**Export Report...** writes the status and the relative path of every entry that is not identical to a text file.

---

## 7. Three-way merge

Used when two people changed the same file and a common ancestor exists.

```bash
angscheidrdiffer --merge base.c mine.c theirs.c -o result.c
```

The three inputs are shown read-only at the top, the editable result below.

| Input | Meaning |
|---|---|
| **Base** | The common starting point both sides began from |
| **Mine** | Your version |
| **Theirs** | The other version |

### 7.1 What is decided automatically

Anything that is not genuinely contested is resolved without asking:

| Situation | Result |
|---|---|
| Only one side changed a passage | That change is taken |
| Both sides made the same change | The change is taken once |
| Nobody changed it | The passage is kept |

**Only a real conflict remains**, meaning both sides changed the same lines differently. Conflicts are framed and counted, and the counter names how many are still open.

### 7.2 Resolving a conflict

Step through the conflicts with **Previous Conflict** and **Next Conflict**, then choose per conflict:

| Choice | Result |
|---|---|
| **Base** | The original passage |
| **Mine**, **Theirs** | One of the two versions |
| **Mine+Theirs**, **Theirs+Mine** | Both, in that order |

After a choice the view jumps to the next unresolved conflict. The result can also be edited directly, which counts as resolving it by hand.

> **Attention:** Saving warns as long as conflict markers are still present in the result text. The line endings of the result follow those of **Mine**.

**The ignore options do not apply here.** A merge writes a file, and a line that differs only in whitespace is still a different line. Treating it as equal would silently discard one of the two versions.

---

## 8. Searching

`Ctrl+F` opens the search bar. It searches the pane that has the focus and wraps around at the end of the file.

---

## 9. Saving and exporting

| Action | Effect |
|---|---|
| **Save Left**, **Save Right** | Writes that side back |
| **Save Left As...**, **Save Right As...** | Writes that side to a new path |
| **Export Unified Diff...** | Writes the comparison as a patch file |

Two properties of a file are preserved when saving: its **character encoding** and its **line ending style**. Opening a file and saving it unchanged therefore does not rewrite every line.

Closing a tab with unsaved changes asks first.

---

## 10. Keyboard reference

| Shortcut | Action |
|---|---|
| `Alt+Up`, `Alt+Down` | Previous and next difference |
| `Alt+Left`, `Alt+Right` | Copy the current section to the left or right |
| `Ctrl+Alt+Z`, `Ctrl+Alt+Shift+Z` | Undo and redo the last merge |
| `Ctrl+Z` | Undo your own typing in the focused pane |
| `Ctrl+O`, `Ctrl+Shift+O` | Open the left and the right file |
| `Ctrl+S` | Save |
| `Ctrl+F` | Search |
| `Ctrl+T` | New comparison tab |
| `F5` | Reload both sides from disk |
| `Ctrl+Q` | Quit |

The merge operations that need a selection are in the **Merge** menu and in the context menu of a pane, which is where a selection usually already exists after a right click.

---

## 11. Using it from a script

The application reports its result as an exit code, following the convention of `diff`.

```bash
angscheidrdiffer --batch a.txt b.txt; echo $?   # 0 identical, 1 different, 2 error
angscheidrdiffer --batch dir1 dir2; echo $?     # same for two directories
```

| Option | Effect |
|---|---|
| `--batch` | Compares without opening a window |
| `--readonly` | Opens without editing, merging or saving |
| `--merge <base> <mine> <theirs>` | Three-way merge. With `--batch` the result is written only when no conflict remains |
| `-o`, `--output <file>` | Target file for `--merge`. Without it the result goes to standard output |
| `--select-for-compare`, `--compare-with` | Remember a path, then compare it with a second one. Used by the file manager service menu |

The graphical mode reports the same codes when the window closes, which makes it usable as the comparison tool of another application. For a three-way merge it reports 0 only when the result was saved and no conflict remained.
