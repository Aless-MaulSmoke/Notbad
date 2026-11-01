# Notbad

> **A modern and *notbad* reimagining of the traditional Notepad text editor, keeping the lightweight soul you love with the power features you need.**

---

**Notbad** is a lightweight, minimalist text editor built natively for Windows (C++/Win32), inspired by the classic look of the **Windows 10 Notepad**.

It keeps the fast, clutter-free interface (no toolbars, just the editing window, menu and status bar) while adding the features of a compact code editor, without giving up its lightweight performance.

---

## 🚀 Key Features

### 📑 Tabbed interface

- **Multiple documents:** files are managed in tabs, with drag-and-drop reordering and a close button on each tab.
- **Modification indicator:** unsaved changes are marked with `* ` before the file name in the tab.
- **Batch saving:** *Save All* (`Ctrl+Alt+S`) and *Close Tab* (`Ctrl+F4`).

### 📂 On-demand file panel (TreeView)

- **Project exploration:** *Open Folder* (`Ctrl+Shift+O`) creates a side panel that uses the system's native icons. The panel stays hidden until a folder is opened.
- **Toggle:** show or hide the panel with `Ctrl+T`.
- **Always up to date:** the contents are reloaded from disk whenever the panel is shown again or Notbad regains focus, keeping expanded folders and the selected item.
- **Easy to read:** folders are listed first, then files, and files that are open in a tab are shown in **bold**.

### 🔢 Line number gutter

- **Logical numbering:** lines wrapped by *Word Wrap* share the number of their paragraph, so the gutter always reflects the document's real line count.
- Toggle with `Ctrl+Shift+L`.

### ✍️ Editing and coding features

- **RichEdit engine:** native **multi-level undo** (several `Ctrl+Z`), per tab.
- **Indentation:** auto-indent, configurable tab size, `Tab` / `Shift+Tab` on selected blocks and tabs ⇄ spaces conversion (*Format > Indentation...*). Each operation is a single undo step.
- **Smart Home:** with auto-indent on, `Home` goes to the first non-blank character; pressing it again goes to the real start of the line.

### 🔍 Find and Replace across tabs (modeless)

- **Global scope:** search and replace in the current tab or in **all open files** at once.
- Options: match case, whole word, search up or down.

---

## 💻 Requirements

- Windows 10 or 11, **64-bit (x64)**.
- No installation: `Notbad.exe` is a single portable file.

Settings are stored only under `HKEY_CURRENT_USER\Software\Notbad` and are not shared with the Windows Notepad.

---

## 🛠️ Building

- Visual Studio 2017 (platform toolset **v141**) and the Windows SDK.
- Target: **x64** (Debug or Release).

---

## 👥 Credits and project team

- 👑 **Aless (MaulSmoke)**: Project Manager, Concept & Code Review
- 💻 **opencode**: Core C++ Development, Initial Architecture & Refactoring (v141 Toolset)
- ⚡ **claudecode**: Development of Advanced Features, Win32 Optimization & Bug Fixing

---

*Notbad Editor: lightweight soul, power features.*
