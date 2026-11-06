# Notbad

> **The text editor that is not bad, but exactly what you need. The perfect balance for your daily workflow.**

---

**Notbad** is a lightweight, minimalist text editor built natively for Windows (C++/Win32), heavily inspired by the classic Notepad and clean visual style of traditional Win32 text editors. Developed from the ground up using native APIs, it aims to recapture that nostalgia-inducing, lightning-fast text editing experience.

While classic Notepad have paused in time and modern development environments (like VSCode and Notepad++) have become complex and heavy, Notbad proudly embraces pragmatism: delivering only the essential layout and tools you actually need, without sacrificing performance, startup speed, and lightweight efficiency.

---

## 🎯 The Philosophy: Simple and OK

The market is full of heavy, overly complex text editors. They often come with cluttered interfaces and an enormous footprint that legacy PCs absolutely hate. Notbad chose the path of balance:

* **Not as empty** as Notepad.
* **Not as bloated** as VSCode.

It is a predictable, honest tool, free of complex setting screens and instantly ready for use—whether you need to write a quick script, review source code, or just take notes.

---

## 🎨 Conceptual Design: Turning the Cover

Notbad’s visual identity translates its usability philosophy through its official icon:

* **Ready for Action:** Unlike the traditional icon of a closed notebook, Notbad displays a notepad with its cover flipped completely to the back. It is the ultimate symbol of agility: zero barriers between your click and the start of your typing.
* **Discrete Evolution:** The inclusion of colored tabs on the side of the icon instantly communicates the ability to work with multiple documents, without breaking the classic aesthetic.
* **The Substitution:** The presence of the stylized `N_` introduces the underscore character after the letter N, symbolizing the blinking cursor that types to replace and evolve old editing standards.

---

## 🚀 Key Features

The project adheres to strict rules of the classic interface (Editing Window, Menu, and Status Bar), eliminating messy toolbars or buttons. Everything is within reach of the menus and shortcuts:

### 📑 Advanced Tabbed Interface
* **Multiple Documents:** Manage multiple files in a single window with native drag-and-drop tab reordering.
* **Modification Indicator:** Files with unsaved changes display a `*` before the filename on the tab.
* **Batch Operations:** Universal shortcut to "Save All" (`Ctrl+Alt+S`) and quick tab closure (`Ctrl+F4`).

### 📂 On-Demand File Panel (TreeView)
* **Project Exploration:** Opening a folder (`Ctrl+Shift+O`) creates a side panel with a file tree view on demand, using the system's native icons. Toggle the panel quickly with `Ctrl+T`.

### 🔢 Dynamic Line Numbers (Gutter)
* **Logical Lines:** The gutter calculates numbers intelligently. Long lines affected by word wrap share the same line number, preserving the document's logical line count. This is in addition to highlighting the currently indented blocks(`Ctrl+Shift+N`).

### ✍️ Intelligent Editing & Coding
* **Advanced RichEdit Engine:** Powered by the Windows RichEdit engine, ensuring a robust, native multi-level *Undo* history per tab.
* **Smart Indentation:** Dedicated dialog for Tab width adjustments, automatic conversion between tabs/spaces, and support for block indenting/unindenting with `Tab` / `Shift+Tab`.
* **Line Highlight:** Option to visually highlight the current cursor line with a subtle light gray background (`Ctrl+Shift+L`).
* **Code highlighting:** An option to visually highlight elements of source code. It uses a minimalist, universal approach applicable to all code (`Ctrl+Shift+H`).

### 🔍 Advanced Modeless Search
* **Global Scope:** A custom Find/Replace dialog that allows for refined searches and batch replacements scanning **all open files** simultaneously.

---

## 👥 Credits and Project Team

The development and evolution of Notbad are driven by:

* 👑 **Aless (MaulSmoke)** — Project Manager, Concept & Code Review
* 💻 **opencode** — Core C++ Development, Initial Architecture & Refactoring (Toolset v141)
* ⚡ **claudecode** — Advanced Features Development, Win32 Optimization & Bug Fixing

---
_Notbad Editor — Lightweight soul, essential features._
