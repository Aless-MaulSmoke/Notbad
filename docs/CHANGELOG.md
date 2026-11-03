CHANGELOG IN STEPS

==============================================
v1.0.2 — completed on 11/03/2026

  FIXES ----------------------------
  * Editing
    - Delete: explicit treatment of the key, deleting the selection or the
      character to the right (including surrogate pairs). With auto-indentation
      enabled and "Spaces" style, at the actual beginning of the line, Delete consumes up to the
      next indentation level (tabWidth spaces), limited to the existing spaces.
      With auto-indentation disabled or "Tabs" style, it deletes one character
      at a time. Intermediate repainting was suppressed so it does not flicker
      marking the segment before deleting; indentation Backspace received the
      same treatment.
    - Click focus: when reactivating an inactive window, the first click on the
      text positions the cursor at the clicked point. Click with Shift preserves the
      selection.
    - Clipboard shortcuts: Shift+Delete cuts the selection,
      Shift+Insert pastes and Ctrl+Insert copies. Before, the treatment of the
      Delete key intercepted Shift+Delete and deleted without copying, causing
      Shift+Insert to paste old content. Ctrl+Delete and Alt+Delete
      continue to be handled by RichEdit (delete word, etc.).
    - In a clean file, when pressing the Shift+Ctrl keys and releasing them, the file became dirty.
      
  * Interface
    - Adjusted the size of the status bar components according to the
      window resizing.
    - When activating auto-indent mode, it is displayed in the status bar that the mode is active.
      - When closing the program, it saves the state of the Tree File.

  * Indentation
    - Indentation settings (enabled/disabled, width, and style) are
      loaded and applied when opening the editor, in the current editing session.
    - Added the "Indent using" group in the indentation configuration, with the
      "Spaces" (default) and "Tabs" buttons.
    - Enter with auto-indentation copies the indentation from the previous line already
      normalized to the configured style (spaces or tabs).
    - Tab without selection inserts the configured indentation: advances to the next tab
      stop with spaces or inserts a tab when the style is "Tabs".
    - Tab/Shift+Tab in blocks use the configured indentation unit.
    - The conversion options Tabs -> Spaces and Spaces -> Tabs were kept.

  * Line numbers
    - Numbering now continuously follows the vertical scroll position of the text
      area with a short timer while the numbers are visible.
      This covers the smooth scroll of the mouse wheel, auto-scroll when typing/pasting,
      Enter at the end of the visible area, and Page Down, which previously left the numbering
      lagging because invalidation occurred before the actual displacement. The timer
      only repaints when the position actually changes and is terminated when hiding the numbering.

  IMPLEMENTATIONS -----------------------

  * View menu:
    - Added the "Highlight Current Line" option in the View menu;
      with this option active, the current line in the editor is highlighted
      with a light gray tone.

  * Format menu:
    - Added the "Encoding" option in the Format menu, where we can
      define the text codec in the current editing session for formats such as:
      ANSI (Win-1252), UTF-8, UTF-8 BOM, UTF-16 LE (Unicode);
      when choosing the format in a text that is already edited, it is possible to choose:
      read the text as it appears and just save in the new format or
      reload the file with the new format. New documents
      or empty files now automatically use the UTF-8 standard.


==============================================
v1.0.1 — completed on 11/01/2026

  IMPLEMENTATIONS -----------------------
    - Migration to multiple documents with tabs and RichEdit.
    - Save All, Close Tab, drag-drop into a new tab, CLI replacing the initial one.
    - Custom Find/Replace with global scope and case sensitive.
    - TreeView on demand.
    - Line numbering.
    - Auto-indent, block indentation, and converter
      Tabs <-> Spaces.
    - Multi-level Undo per tab.
    - Update of About, version, and documentation.

==============================================
v1.0 — initial base

  IMPLEMENTATIONS -----------------------
    - Recompilation of Notepad via Ghidra.
    - Initial version in C++ (toolset v141 / VS 2017) produced with opencode.
    - Single document, classic Edit, menu and shortcuts, encoding, native find/replace,
      printing, font, and settings in the registry.
