# Notbad

> **A modern and *notbad* reimagining of the traditional Notepad text editor—retaining the lightweight spirit you love, while adding the powerful features you need.**

---

**Notbad** is a lightweight, minimalist text editor built natively for Windows (C++/Win32), directly inspired by the classic look of the **Windows 10 Notepad**. The project originated from a detailed reverse-engineering of Microsoft’s original tool using Ghidra, migrating the raw C code into modern C++.

While preserving its lightning-fast nature and clutter-free interface—devoid of intrusive toolbars—Notbad has evolved into a powerful, compact code editor, incorporating essential features without sacrificing its signature lightweight performance.

---

## 🚀 Key Features

The project upholds classic interface principles (Editing Window, Menu, and Status Bar) while expanding the editor’s internal capabilities with the following implementations:

### 📑 Advanced Tabbed Interface
* **Multiple Documents:** No more juggling dozens of separate windows. Notbad manages files via tabs, featuring native support for drag-and-drop reordering.
* **Modification Indicator:** Unsaved changes are marked with a visual `*` next to the filename in the tab.
* **Batch Saving:** Includes a universal "Save All" shortcut (`Ctrl+Alt+S`) and the ability to close individual tabs (`Ctrl+F4`).

### 📂 On-Demand File Panel (TreeView)
* **Project Exploration:** Opening a folder (`Ctrl+Shift+O`) triggers a dynamic, tree-based side panel that utilizes the operating system’s native icons. You can now edit entire projects without ever leaving Notbad. ### 🔢 Dynamic Line Number Gutter
* **Logical Numbering:** The gutter calculates line numbers intelligently. Long lines that undergo automatic wrapping (*Word Wrap*) share the same line number, preserving the document's logical line count.

### ✍️ Intelligent Editing & Coding Features
* **Advanced RichEdit Engine:** Migration from the classic Edit control to RichEdit, enabling native support for **multi-level Undo** (multiple `Ctrl+Z` operations).
* **Advanced Indentation:** A dedicated dialog for managing spacing, automatic conversion between tabs and spaces, and support for indenting/unindenting selected blocks using `Tab` / `Shift+Tab`.

### 🔍 Advanced Multi-Tab Search (Modeless)
* **Global Scope:** The new Find and Replace dialog allows for refined searches and batch replacements that scan **all open files across all tabs** simultaneously.


## 👥 Credits and Project Team

The development and evolution of Notbad are driven by:

* 👑 **Aless (MaulSmoke)** — Project Manager, Concept & Code Review
* 💻 **opencode** — Core C++ Development, Initial Architecture & Refactoring (v141 Toolset)
* ⚡ **claudecode** — Development of Advanced Features, Win32 Optimization & Bug Fixing

---
*Notbad Editor — Lightweight soul, power features.*
