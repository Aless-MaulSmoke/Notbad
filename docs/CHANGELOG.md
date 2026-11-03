# CHANGE LOG

==============================================
## v1.0.2 — completed on 11/03/2026

### FIXES ----------------------------
* **Editing**
- Delete key: Explicit handling of the Delete key, which now removes the current selection or the character to the right (including surrogate pairs). With auto-indentation enabled and the "Spaces" style selected, pressing Delete at the beginning of a line removes spaces up to the next indentation level (defined by `tabWidth`), limited to the number of existing spaces. With auto-indentation disabled or the "Tabs" style selected, it deletes one character at a time. Intermediate repainting has been suppressed to prevent flickering (highlighting the text before deletion); the same logic has been applied to Backspace-based indentation removal. 
- Click-to-focus: When reactivating an inactive window, the first click on the text now positions the cursor exactly where the click occurred. Holding the Shift key while clicking preserves any existing selection. 
- Clipboard shortcuts: Shift+Delete now cuts the selection, Shift+Insert pastes, and Ctrl+Insert copies. Previously, the Delete key handler would intercept Shift+Delete and perform a deletion without copying, which caused Shift+Insert to paste outdated content. Ctrl+Delete and Alt+Delete continue to be handled by the RichEdit component (e.g., deleting a word). 
- Fixed an issue where pressing Shift+Ctrl in a clean (unmodified) file would incorrectly mark the file as "dirty" (modified).

* **Interface**
- Status bar components now adjust their size dynamically when the window is resized. 
- Enabling auto-indentation mode now triggers an indicator in the status bar.
- The state of the File Tree is now saved upon closing the application.

* **Indentation**
- Indentation settings (enabled/disabled status, width, and style) are now loaded and applied to the current document upon opening the editor. - Added an "Indent using" group to the indentation settings, with options for "Spaces" (default) and "Tabs". 
- Pressing Enter with auto-indentation enabled copies the indentation from the previous line, normalized to the configured style (spaces or tabs). 
- Pressing Tab (without a selection) inserts the configured indentation: it advances to the next tab stop using spaces, or inserts a tab character if the "Tabs" style is selected. 
- Using Tab/Shift+Tab on a selected block now respects the configured indentation unit. 
- Existing options for converting Tabs to Spaces and Spaces to Tabs have been retained.

* **Line numbers**
- Line numbering now continuously tracks the vertical scroll position of the text area, using a short-duration timer while the numbers are visible. This ensures accuracy during smooth scrolling (via mouse wheel), auto-scrolling while typing or pasting, pressing Enter at the bottom of the visible area, or using Page Down—actions that previously caused the line numbers to become misaligned because the UI update occurred before the actual displacement. The timer triggers a repaint only when the position changes and stops once the line numbering is hidden.

* **IMPLEMENTATIONS -----------------------**

* **View menu:**
- Added a "Highlight Current Line" option to the View menu; when enabled, the current line in the editor is highlighted in a light gray tone.

* **Format menu:**
- Added an "Encoding" option to the Format menu, allowing you to set the text codec for the current document (e.g., ANSI (Win-1252), UTF-8, UTF-8 BOM, or UTF-16 LE (Unicode)). When changing the encoding for an existing document, you can choose whether to keep the current text as it appears (saving it in the new format) or reload the file using the new encoding. New or empty documents now default to UTF-8. ==============================================
## v1.0.1 — completed on 11/01/2026

### IMPLEMENTATIONS -----------------------
- Migration to a multi-document interface with tabs and RichEdit.
- "Save All" and "Close Tab" functions; drag-and-drop to open in a new tab; CLI support replacing the initial instance.
- Custom Find/Replace feature with global scope and case-sensitivity options.
- On-demand TreeView.
- Line numbering.
- Auto-indentation, block indentation, and a Tab <-> Space converter.
- Multi-level undo (per tab).
- Updated "About" section, version information, and documentation.

==============================================
## v1.0 — initial base

### IMPLEMENTATIONS -----------------------
- Recompilation of Notepad via Ghidra.
- Initial C++ version (toolset v141 / VS 2017) developed using open-source code.
- Single-document interface, classic Edit control, menus and shortcuts, encoding support, native Find/Replace, printing, and font/settings storage in the Windows Registry.
