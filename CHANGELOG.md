# Changelog

All notable changes to this project are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [semantic versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-09-07

First public release.

- **File comparison** in two editable panes. Both sides stay editable while the comparison runs, differences are recomputed while typing, and the changed part within a line is highlighted. An overview bar maps the whole file, connector arrows sit between the panes.
- **Merging** of selected lines, the current section or the whole file, in both directions. "Use Both" keeps both variants in the chosen order. Every operation is a single undo step. A selection merge transfers only the selected lines and aligns them with their counterparts by similarity, so neighbouring lines of the target stay untouched.
- **Aligned view**: placeholder rows keep corresponding lines opposite each other. The placeholders exist only while drawing, so editing, undo and input methods behave as in a plain editor.
- **Folder comparison**, recursive, with status per entry, copy and delete between the sides, opening either side in the file manager, view filters by name and modification date, a quick mode comparing file size only, exclude patterns and a report export.
- **Three-way merge** for conflicts with a common ancestor. Base, mine and theirs are shown above the editable result, with conflict navigation and one button per resolution. Changes made by one side only are taken automatically.
- **Ignore options**: whitespace, line endings, comments, letter case and the first N lines. The last one covers version control keyword headers. File and folder comparison use the same rules.
- **Detection of moved blocks** and of differences that consist only of ignored content, which are drawn discreetly and skipped by the navigation.
- **Command line**: comparison without a window with exit codes following `diff`, a read-only mode, a three-way merge that writes only when free of conflicts, and two entry points for the file manager service menu.
- **Desktop integration**: application launcher, icon in eleven sizes, and a Dolphin service menu for two selected files or folders.
- Encoding is preserved on saving, as is the line ending style of each file.
