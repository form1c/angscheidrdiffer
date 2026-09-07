# AnGscheidrDiffer: Installation

| | |
|---|---|
| **Applies to** | AnGscheidrDiffer, built from source |
| **Audience** | Anyone building, installing or packaging the application |
| **Scope** | Requirements, building, installation, desktop integration and use as an external diff tool. Working with the application is described in the [User manual](usage.md) |

---

## Contents

1. [Requirements](#1-requirements)
2. [Building](#2-building)
3. [Running without installing](#3-running-without-installing)
4. [Installation](#4-installation)
5. [Desktop integration](#5-desktop-integration)
6. [Use as an external diff tool](#6-use-as-an-external-diff-tool)
7. [Removing the application](#7-removing-the-application)
8. [Cleaning the working tree](#8-cleaning-the-working-tree)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Requirements

| Item | Requirement |
|---|---|
| Operating system | Linux with a graphical session |
| Build tools | CMake 3.16 or newer, a C++17 compiler |
| Libraries | Qt 6 with the Widgets, Concurrent and Test components |
| Optional | KF6SyntaxHighlighting for syntax colours |

Built and tested with Qt 6.8.2 and GCC on Debian 13. No minimum Qt 6 version is enforced by the build files, and older Qt 6 releases have not been tried.

On Debian and Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev
sudo apt install libkf6syntaxhighlighting-dev   # optional
```

The application requires Qt alone. KDE Frameworks are optional, so it runs on desktops other than KDE Plasma. The Dolphin service menu described in section 5.2 is the one part that requires the KDE file manager.

---

## 2. Building

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build
```

While configuring, CMake reports whether syntax highlighting is available:

```
-- Syntax highlighting: enabled (KF6SyntaxHighlighting)
-- Syntax highlighting: disabled (KF6SyntaxHighlighting not found)
```

The second line is not an error. The application builds and runs without the library, only the syntax colours are missing. To build without it although it is installed, configure with `-DWITH_KSYNTAXHIGHLIGHTING=OFF`.

`ctest` runs the tests of the comparison and merge engines. They cover the engines only and open no window.

---

## 3. Running without installing

The binary is complete after the build and needs no installation.

```bash
./build/angscheidrdiffer left.cpp right.cpp     # two files
./build/angscheidrdiffer dir1 dir2              # two folders
./build/angscheidrdiffer --batch a b; echo $?   # 0 identical, 1 different, 2 error
```

The exit codes follow the convention of `diff`, which makes the application usable in scripts. The graphical mode reports the same codes when the window is closed.

> **Note:** Comparing a file with a folder is rejected and reports exit code 2.

---

## 4. Installation

Two ways are supported. The Debian package is the recommended one, because it can be removed again completely.

### 4.1 Debian package

Build the package without root rights, then install it:

```bash
./scripts/build-deb.sh              # version taken from CMakeLists.txt
./scripts/build-deb.sh 1.2.0        # own version number
sudo apt install ./release/angscheidrdiffer_1.0.0_amd64.deb
```

Without an argument the script reads the version from the `project()` line in
`CMakeLists.txt`, which is the single place the version is defined.

The result is `release/angscheidrdiffer_<version>_<architecture>.deb`.

The package dependencies are determined during the build with `dpkg-shlibdeps`, which records both the packages and the minimum versions the binary actually needs, for example `libqt6core6t64 (>= 6.8.2)`.

The package therefore matches the system it was built on and is not meant to be passed on to a different distribution release. Two things follow from that:

- On a system with an older Qt 6, the package manager refuses to install it rather than letting it fail at startup.
- The package name `libqt6core6t64` exists only on distributions that carried out the transition to 64 bit time stamps. On older releases the same library is packaged under a different name, and the dependency cannot be resolved there.

Building from source has none of these constraints and is the supported way.

The package contains:

| Path | Contents |
|---|---|
| `/usr/bin/angscheidrdiffer` | The application |
| `/usr/share/applications/` | Launcher for the application menu |
| `/usr/share/icons/hicolor/<size>/apps/` | Application icon in nine sizes from 16 to 256 pixels |
| `/usr/share/kio/servicemenus/` | Dolphin service menu |

### 4.2 Direct installation

```bash
sudo cmake --install build
```

This copies the same files below `/usr/local`. It is meant for development and leaves no package management entry behind.

> **Attention:** An installation below `/usr/local` takes precedence over a package installed below `/usr`, because `/usr/local/bin` comes first in the search path. Remove it before installing the package, otherwise the older binary keeps running. Section 7 describes how.

---

## 5. Desktop integration

Both parts become available after an installation according to section 4. They do not work when the application is only started from the build directory, because the desktop files are installed separately.

### 5.1 Application menu

The launcher appears under the category Development. It accepts text files by drag and drop.

### 5.2 Dolphin service menu

The service menu adds three entries to the context menu of the file manager:

| Entry | Selection | Effect |
|---|---|---|
| Compare with AnGscheidrDiffer | exactly two files or folders | Opens the comparison directly |
| Select for Compare | one file or folder | Remembers the path without opening a window |
| Compare with Selected File | one file or folder | Compares the remembered path with the selected one |

The second and third entry belong together and cover the case where both sides live in different directories. The remembered path survives until it is used.

If the entries do not appear after installing the package, log out of the session once. Dolphin reads the service menus when it starts.

---

## 6. Use as an external diff tool

Other applications can call the binary as their comparison tool. Two calling conventions are supported.

**Two-way comparison.** The tool is called with two paths, which is the convention most version control clients use:

```bash
angscheidrdiffer <left> <right>
```

**Three-way merge.** For conflicts with a common ancestor:

```bash
angscheidrdiffer --merge <base> <mine> <theirs> -o <target>
```

Without `--batch` the merge tab opens for manual resolution. The application then reports exit code 0 only when the result was saved and no conflict remains, which lets the caller decide whether to mark the conflict as resolved. With `--batch` no window opens. The result is written only when it is free of conflicts, otherwise the application reports exit code 1 and writes nothing.

The binary must be reachable through the search path for a calling application to find it. Both ways described in section 4 achieve this.

---

## 7. Removing the application

```bash
sudo apt remove angscheidrdiffer     # package installation
./scripts/uninstall-local.sh         # direct installation below /usr/local
```

The script removes the binary, the launcher, the service menu and the installed icons. Run it before installing the package if the application was previously installed directly, for the reason given in section 4.2.

Settings are stored per user through the Qt settings mechanism and are not touched by either command.

---

## 8. Cleaning the working tree

Building creates three directories:

| Directory | Contents | Safe to delete |
|---|---|---|
| `build/` | Development build | at any time |
| `build-deb/` | Intermediate state of the package build | at any time |
| `release/` | Finished packages | once they are no longer needed |

```bash
rm -rf build build-deb                  # remove the build, keep the packages
rm -rf build build-deb release          # remove everything the build produced
cmake --build build --target clean      # keep the CMake configuration
```

> **Attention:** A `build/` directory does not survive being moved. Its `CMakeCache.txt` stores absolute paths, so after relocating the source tree the build has to be removed and configured again.

---

## 9. Troubleshooting

**The application menu shows no icon, or an outdated one.**
Icon themes are cached. Run `sudo gtk-update-icon-cache` for the hicolor theme if your desktop uses that cache, or log out once. The icon inside the running window comes from the binary itself and is unaffected.

**The service menu entries are missing.**
They are installed below `/usr/share/kio/servicemenus`. They appear after Dolphin restarts. Entries are shown only for the number of selected items given in section 5.2.

**A calling application does not find the tool.**
Check that the binary is in the search path with `which angscheidrdiffer`. Without an installation according to section 4, only the path inside the build directory exists.

**CMake reports that syntax highlighting is disabled.**
This is a status message, not an error. See section 2.

**The comparison of two large folders takes a long time.**
The folder comparison reads file contents. Exclude patterns and the quick comparison mode, which compares file size only, are available in the folder tab and reduce the amount of work.



