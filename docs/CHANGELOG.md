# CHANGELOG IN STEPS

## v1.0.4

### FIXES

#### Font
* **Change Font:** When editing a code with active HighLighting, changing the font causes all text to lose its highlight.
* **Window Sync:** When changing the font in one window, the other open windows fail to maintain the same zoom and size that should be standard for all windows.

#### Tabs
* **File Duplication:** Dragging an already open file into Notbad would open another tab with a replicated file. Now, when attempting this procedure, the tab containing the file simply receives focus; however, if the file is dirty, the user is asked whether it should be replaced (discarding changes) or not opened.

#### Line Numbers
* **Block Highlighting:** A bug occurs when placing the cursor on an empty line below the block above, causing that line to incorrectly become part of the block.

#### Code Highlighting
* **Formatting Inheritance:** When disabling HighLighting Code while inside a word that was highlighted, continuing to type would cause the new text to inherit the formatting of the old highlight.
* **Shortcut Inconsistency:** With more than one tab open and focus on the File Tree, turning off HighLighting via the shortcut makes the editing across tabs inconsistent.

### IMPLEMENTATIONS

#### File Tree
* **Folder Drag and Drop:** Dragging a folder into Notbad will make it open the File Tree exactly at the dragged folder.

#### Indentation
* **Shortcut Key:** Added a shortcut to turn indentation on and off. The shortcut reference is visible in the Format -> Indent menu on the right side of the Auto-indent combo, using the shortcut "Ctrl+Shift+I".

#### Code Highlighting
* **Universal Formatting:** New formatting rules for the Universal HighLighting Code. Words to the left of " = " are now rendered in bold italic, and words to the left of " : " are rendered in italic.

#### Compatibility
* **Backward Compatibility:** Compiled in Visual Studio 2017 to guarantee compatible components starting from Windows 7, adopting only the x64 version.

#### Localization
* **Language Support:** Adjusted all dialogs and user messages within the `/res/notbad.rc` file. The language must be set based on the default Windows language, and Notbad will look into a `/lang` folder starting from the executable path; if the language is, for example, `es-AR` and `AR` is not in the list, it will fallback to standard `es`. If the language does not exist in the folder, Notbad will check the internal JSON files from the compilation to see if the specific language file exists; if the language file is still missing, it will use the internal `en` from the compilation.

---


## v1.0.3 — Completed on 11/04/2026

### FIXES

#### Editing
* **Insert:** Revised the keyboard cursor system for "insert" mode: when zooming or scrolling, the cursor reverts to its default appearance while remaining in "insert" mode.
* **Replace area:** When selecting an area and typing to replace the content, the text is replaced; however, subsequent characters typed would incorrectly appear as "selected."
* **Selecting area with Mouse:** When selecting text with the mouse by clicking in a paragraph and dragging downward, each new line the mouse reaches becomes the highlighted line. Previously, when performing the same action by dragging upward, the highlighted line would remain stuck on the line where the selection began.

#### Indentation
* **Replace area with ENTER:** The keyboard cursor no longer positions itself within the indentation area when "indent" mode is enabled and the ENTER key is pressed within a selected block.

#### Line Numbers
* **Zooming:** When zooming in the editor with "Word Wrap" enabled, the line height would become excessive. Previously, the line number was aligned to the top; it is now centered.

### IMPLEMENTATIONS

#### View Menu
* **Highlighting Code:** An option to visually highlight elements of source code. It uses a minimalist, universal approach applicable to all code (`Ctrl+Shift+K`).
* **Line Numers with HighLight:** This is in addition to highlighting the currently indented blocks.

---


## v1.0.2 — Completed on 11/03/2026

### FIXES

#### Editing
* **Delete:** Explicit handling of the key, deleting the selection or the character to the right (including surrogate pairs). With auto-indentation enabled and the style set to "Spaces", at the absolute beginning of the line, Delete consumes up to the next indentation level (tabWidth spaces), limited to the existing spaces. With auto-indentation disabled or the style set to "Tabs", it deletes one character at a time. Intermediate repainting was suppressed to prevent flickering by marking the text snippet before deleting; indentation Backspace received the same treatment.
* **Click Focus:** When reactivating an inactive window, the first click on the text positions the cursor at the clicked point. Clicking with Shift preserves the selection.
* **Clipboard Shortcuts:** Shift+Delete cuts the selection, Shift+Insert pastes, and Ctrl+Insert copies. Previously, the Delete key handling intercepted Shift+Delete and deleted without copying, causing Shift+Insert to paste outdated content. Ctrl+Delete and Alt+Delete continue to be handled by RichEdit (delete word, etc.).
* **Unsaved State:** In a clean file, pressing the Shift+Ctrl keys and releasing them left the file in a dirty/unsaved state.

#### Interface
* **Status Bar:** Adjusted the size of the status bar components according to the window resizing.
* **Auto-Indent Notification:** When activating auto-indent mode, the status bar displays that the mode is active.
* **Tree File State:** Saves the state of the Tree File when closing the program.

#### Indentation
* **Configuration Loading:** Indentation settings (enabled/disabled, width, and style) are loaded and applied when opening the editor for the current editing session.
* **Indent Using Group:** Added the "Indent using" group to the indentation configuration, with "Spaces" (default) and "Tabs" buttons.
* **Enter Key:** Enter with auto-indentation copies the indentation of the previous line, already normalized to the configured style (spaces or tabs).
* **Tab Key (No Selection):** Tab without selection inserts the configured indentation: advances to the next tab stop with spaces or inserts a tab when the style is "Tabs".
* **Block Indentation:** Tab/Shift+Tab in blocks use the configured indentation unit.
* **Conversion Options:** The conversion options for Tabs -> Spaces and Spaces -> Tabs were maintained.

#### Line Numbers
* **Vertical Scroll Tracking:** Numbering now continuously follows the vertical scroll position of the text area using a short timer while numbers are visible. This covers smooth mouse wheel scrolling, auto-scroll when typing/pasting, Enter at the end of the visible area, and Page Down, which previously left the numbering outdated because invalidation occurred before the actual displacement. The timer only repaints when the position actually changes and is terminated when numbering is hidden.

### IMPLEMENTATIONS

#### View Menu
* **Highlight Current Line:** Added the "Highlight Current Line" option to the View menu. With this option active, the current line in the editor is highlighted with a light gray tone.

#### Format Menu
* **Encoding:** Added the "Encoding" option to the Format menu, allowing the definition of the text codec in the current edit for formats such as: ANSI (Win-1252), UTF-8, UTF-8 BOM, UTF-16 LE (Unicode). When choosing the format for already edited text, it is possible to choose between: reading the text as it appears and just saving in the new format, or reloading the file with the new format. New documents or empty files now automatically use the UTF-8 standard.

---


## v1.0.1 — Completed on 11/01/2026

### IMPLEMENTATIONS
* **Multi-Document Support:** Migration to multiple documents with tabs and RichEdit.
* **Tab Management & CLI:** Save All, Close Tab, drag-and-drop into a new tab, and CLI replacing the initial one.
* **Find/Replace:** Built-in Find/Replace with global scope and case sensitivity.
* **TreeView:** On-demand TreeView.
* **Line Numbering:** Implementation of line numbers.
* **Indentation Tools:** Auto-indent, block indentation, and Tabs <-> Spaces converter.
* **Undo History:** Multi-level Undo per tab.
* **Documentation:** Update of About section, version, and documentation.

---


## v1.0 — Initial Base

### IMPLEMENTATIONS
* **Notepad Recompilation:** Recompilation of Notepad via Ghidra.
* **Initial Version:** Initial version in C++ (toolset v141 / VS 2017) produced with opencode.
* **Core Features:** Single document, classic Edit, menu and shortcuts, encoding, native find/replace, printing, font, and registry configurations.