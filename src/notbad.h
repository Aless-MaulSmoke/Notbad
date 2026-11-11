#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <strsafe.h>

#include <string>
#include <vector>

#include "resource.h"

#define NOTBAD_NAME        L"Notbad"
#define NOTBAD_CLASS       L"Notbad"
#define NOTBAD_LINENUMBERS_CLASS L"NotbadLineNumbers"
#define NOTBAD_VERSION     L"1.0.9"
#define NOTBAD_HELP_URL    L"https://aless-maulsmoke.github.io/Notbad/"
#define NOTBAD_VERSION_URL L"https://aless-maulsmoke.github.io/Notbad/version.html"
#define NOTBAD_RELEASES_URL L"https://github.com/Aless-MaulSmoke/Notbad/releases"
#define WM_NOTBAD_CLOSETAB (WM_APP + 1)
#define WM_NOTBAD_NEWVERSION (WM_APP + 2)
#define NOTBAD_CODE_HIGHLIGHT_TIMER 0x4C48
#define MAX_EDIT_BUFFER     0x7FFFFFF0u
#define MAX_FILE_PATH       32768

// ---- Cores do editor -------------------------------------------------------
// Todas as cores do editor ficam aqui.
//
// Texto
#define NOTBAD_TEXT_COLOR            RGB(60, 60, 60)     // texto do documento (cinza medio)
#define NOTBAD_ACTIVE_TEXT_COLOR     RGB(0, 0, 0)        // texto na selecao e na linha em destaque
// Fundos
#define NOTBAD_HIGHLIGHT_COLOR       RGB(240, 240, 240)  // fundo da linha em destaque
#define NOTBAD_SELECTION_BACK_COLOR  GetSysColor(COLOR_HIGHLIGHT) // fundo do texto marcado (cor de selecao do sistema; troque por RGB(...) para fixar)

enum TextEncoding
{
    ENC_ANSI = 0,
    ENC_OEM = 1,
    ENC_UTF16LE = 2,
    ENC_UTF16BE = 3,
    ENC_UTF8_BOM = 4,
    ENC_UTF8 = 5,
    ENC_GB18030 = 6
};

struct ITextRange;

struct AppSettings
{
    LOGFONTW logFont;
    BOOL     wordWrap;
    BOOL     statusBar;
    BOOL     saveWindowPositions;
    BOOL     windowsOnlyEol;
    BOOL     pasteOriginalEol;
    BOOL     showLineNumbers;
    BOOL     highlightCurrentLine;
    BOOL     codeHighlighting;
    BOOL     autoIndent;
    BOOL     indentWithTabs;
    int      defaultEncoding;
    int      tabWidth;
    int      treeWidth;
    RECT     windowRect;
    BOOL     hasWindowRect;
};

struct Document
{
    HWND         hEdit;
    WCHAR        szFilePath[MAX_FILE_PATH];
    WCHAR        szFileTitle[MAX_PATH];
    BOOL         bHasPath;
    BOOL         bReadOnly;
    TextEncoding encoding;
    int          untitledNum;   // 1 = "Untitled", 2 = "Untitled 2", ...
    ITextRange*  pHighlightRange;
    ITextRange*  pSelectionRange; // trecho selecionado com NOTBAD_ACTIVE_TEXT_COLOR
    int          fgPrevSelLen;    // tamanho da selecao na ultima atualizacao de cor
    int          fgPrevTextLen;   // tamanho do texto na ultima atualizacao (-1 = desconhecido)
    BOOL         bReinterpreted; // encoding trocado por "reinterpretar"; limpo ao salvar/abrir
    BOOL         bOverwrite;     // modo Overwrite (tecla Insert), por documento
    BOOL         bReplaceAllPending; // ultima acao foi um Replace All em todas as abas
    // Realce de codigo com dois estados independentes:
    //  - edicao: so o paragrafo atual (e o anterior) e refeito, na hora;
    //  - viewport: o bloco visivel e refeito pelo timer quando a tela muda.
    int          codeHighlightViewStart;   // inicio do ultimo trecho visivel processado
    int          codeHighlightViewEnd;     // fim do ultimo trecho visivel processado
    int          codeHighlightViewFirstLine; // primeira linha visivel na ultima atualizacao
    BOOL         bCodeHighlightViewValid;  // o trecho acima ainda reflete a tela atual
    BOOL         bCodeHighlightNeedsClean; // pode haver realce antigo fora da tela
    int          codeHighlightParaStart; // inicio do ultimo trecho realcado na linha atual
    int          codeHighlightParaEnd;   // fim do ultimo trecho realcado na linha atual
    BOOL         bCodeHighlightParaValid; // o trecho acima ainda reflete o texto atual
    BOOL         bCodeHighlightParaDirty; // houve edicao desde o ultimo realce da linha atual
    BOOL         bCodeHighlightSynced;    // estado do realce deste documento reflete g_settings.codeHighlighting
    int          indentBlockFirst;     // primeiro paragrafo (1-based) do bloco de indentacao atual
    int          indentBlockLast;      // ultimo paragrafo (1-based) do bloco de indentacao atual
    int          indentBlockBase;      // indentacao (colunas) que define o bloco
    int          indentBlockParaCount; // numero de paragrafos usado no calculo do bloco
    int          indentBlockKind;      // 0 = regiao comum, 1 = bloco aberto, 2 = bloco fechado
    int          indentBlockAnchorPara; // paragrafo da linha inicial/final do bloco
    BOOL         bIndentBlockValid;    // o bloco acima ainda reflete o cursor/texto atuais
};

extern HINSTANCE    g_hInstance;
extern HWND         g_hWndMain;
extern HWND         g_hWndEdit;
extern HWND         g_hWndStatus;
extern HWND         g_hWndTabs;
extern HWND         g_hWndTree;
extern HWND         g_hWndLineNumbers;
extern HFONT        g_hFont;
extern HFONT        g_hFontStatus;
extern HFONT        g_hFontMenu;
extern HACCEL       g_hAccel;
extern HICON        g_hIcon;
extern HICON        g_hIconSmall;
extern HMENU        g_hMenu;
extern HWND         g_hFindDialog;
extern AppSettings  g_settings;

extern std::vector<Document*> g_documents;
extern Document*              g_pActiveDoc;
extern int                    g_iActiveDoc;
extern BOOL                   g_bUpdatingTabs;

// main.cpp
BOOL InitApplication(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow, LPWSTR lpCmdLine);
int  RunMessageLoop(void);
void LoadStringResource(UINT id, LPWSTR buffer, int cchBuffer);
void StartVersionCheck(HWND hWnd);

// Idiomas (lang.cpp)
void    LangInit(void);
LPCWSTR LangGetString(LPCWSTR lpKey);
LPCWSTR LangGetStringById(UINT id);
LPCWSTR LangCurrentTag(void);
void    LangApplyMenu(HWND hWnd);
void    LangApplyDialog(HWND hDlg, UINT idd);

// window.cpp
LRESULT CALLBACK NotbadWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LineNumbersWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL CreateNotbadWindow(HINSTANCE hInstance, int nCmdShow, LPCWSTR lpFilePath);
void UpdateWindowTitle(void);
void UpdateStatusBar(void);
void UpdateEditMenuState(HWND hWnd);
void LayoutMainWindow(void);
void InvalidateLineNumbers(void);
void UpdateTreeOpenMarks(void);
void RefreshFileTreeView(void);
BOOL GetTreeTargetDirectoryForSave(LPWSTR lpDir, int cchDir);
BOOL HandleTreeAccelerator(MSG* pMsg);
LPCWSTR TabGetClosePadding(void);

// document.cpp
Document* DocActive(void);
Document* DocFromEdit(HWND hEdit);
int       DocIndexOf(Document* pDoc);
Document* DocCreate(void);
Document* DocFindByPath(LPCWSTR lpFileName);
void      DocDestroy(Document* pDoc);
void      DocDestroyAll(void);
BOOL      DocLoad(Document* pDoc, LPCWSTR lpFileName);
BOOL      DocLoadCurrent(LPCWSTR lpFileName);
BOOL      DocSave(Document* pDoc, BOOL bSaveAs);
BOOL      DocSaveAll(void);
BOOL      DocSaveFile(Document* pDoc, LPCWSTR lpFileName);
BOOL      DocQuerySaveChanges(Document* pDoc);
BOOL      DocQuerySaveAllChanges(void);
BOOL      DocClose(Document* pDoc);
void      DocActivate(Document* pDoc);
void      DocCycle(int direction);
void      DocMove(int from, int to);
void      DocUpdateTabTitle(Document* pDoc);
void      DocGetDisplayName(Document* pDoc, LPWSTR lpBuffer, int cchBuffer);

// editor.cpp
void EditorNew(void);
BOOL EditorOpen(LPCWSTR lpFileName);
BOOL EditorOpenDropped(LPCWSTR lpFileName);
BOOL EditorSave(BOOL bSaveAs);
BOOL EditorSaveFile(LPCWSTR lpFileName);
BOOL EditorQuerySaveChanges(void);
void EditorSetModified(BOOL bModified);
BOOL EditorIsModified(void);
void EditorSetFont(HFONT hFont);
void EditorSyncZoomFrom(HWND hSource);
void EditorApplyGlobalZoomTo(HWND hEdit);
void EditorZoom(int direction);
void EditorZoomReset(void);
void EditorApplyWordWrap(BOOL bWrap);
void EditorApplyWordWrapTo(HWND hEdit, BOOL bWrap);
void EditorUpdateHorizontalScrollBar(HWND hEdit);
int  EditorGetParagraphNumber(HWND hEdit, int charIndex);
int  EditorGetParagraphStart(HWND hEdit, int charIndex);
int  EditorGetParagraphCount(HWND hEdit);
int  EditorGetParagraphStartByNumber(HWND hEdit, int lineNumber);
void EditorSelectAll(void);
void EditorInsertTimeDate(void);
void EditorGoToLine(void);
void EditorSetReadOnly(BOOL bReadOnly);
void EditorUpdateEncodingStatus(void);
void EditorSetEncoding(TextEncoding encoding);
void EditorConvertTabs(BOOL bTabsToSpaces);
void EditorApplyTabWidth(void);
void EditorApplyTabWidthTo(HWND hEdit);
void EditorApplyIndentation(void);
void EditorApplyTextColorTo(HWND hEdit);
void EditorApplyCurrentLineHighlight(void);
void EditorUpdateCurrentLineHighlight(HWND hEdit);
void EditorUpdateIndentBlock(HWND hEdit, int caret, int textLen);
void EditorClearCurrentLineHighlight(Document* pDoc);
void EditorUpdateCaretShape(HWND hEdit);
void EditorResetCaretShape(void);
void EditorPaintEmptyLineHighlight(HWND hEdit, Document* pDoc);
void EditorHandleOverwriteChar(HWND hEdit, WCHAR ch);
BOOL EditorTryOverwriteChar(HWND hEdit, WCHAR ch);
void EditorPlaceCaretAtClick(HWND hEdit);
void EditorPlaceCaretAtPoint(HWND hEdit, POINT pt);
BOOL EditorHandleKeyDown(HWND hEdit, WPARAM wParam);
BOOL EditorCopyToClipboard(HWND hEdit, BOOL bCut);
BOOL EditorPasteFromClipboard(HWND hEdit);
void EditorResetCaretStyle(HWND hEdit);
void EditorScheduleUniversalHighlight(Document* pDoc);
void EditorRestartUniversalHighlight(Document* pDoc, BOOL bClear);
void EditorSyncUniversalHighlight(Document* pDoc);
void EditorInvalidateUniversalHighlight(Document* pDoc);
void EditorCancelUniversalHighlight(void);
void EditorCancelUniversalHighlightFor(Document* pDoc);
BOOL EditorProcessViewportHighlight(void);


// settings.cpp
void SettingsLoad(void);
void SettingsSave(void);
void SettingsLoadWindowPlacement(HWND hWnd);
void SettingsSaveWindowPlacement(HWND hWnd);
void SettingsLoadFont(void);
BOOL SettingsRefreshMenuFont(void);
void SettingsSaveFont(void);
void SettingsLoadDefaults(void);
void SettingsSaveTree(LPCWSTR lpRoot, BOOL bVisible,
                      const std::vector<std::wstring>& expanded, LPCWSTR lpSelected);
BOOL SettingsLoadTree(std::wstring& root, BOOL* pbVisible,
                      std::vector<std::wstring>& expanded, std::wstring& selected);

// findreplace.cpp
void FindTextDialog(BOOL bReplace);
void FindNext(void);
void FindPrevious(void);

// dialogs.cpp
void ShowAboutDialog(HWND hWnd);
void ShowIndentDialog(HWND hWnd);
void DoPageSetup(HWND hWnd);
void DoPrint(HWND hWnd);
void ShowHelp(HWND hWnd);

// encoding.cpp
BOOL LoadTextFile(LPCWSTR lpFileName, LPWSTR *ppText, TextEncoding *pEncoding);
BOOL SaveTextFile(LPCWSTR lpFileName, LPCWSTR lpText, TextEncoding encoding);
BOOL DecodeText(const BYTE *pData, DWORD cbData, TextEncoding encoding, LPWSTR *ppText);
BOOL EncodeText(LPCWSTR lpText, TextEncoding encoding, BYTE **ppBytes, DWORD *pcbBytes);
BOOL CanEncodeLossless(LPCWSTR lpText, TextEncoding encoding);
BOOL ReinterpretText(LPCWSTR lpText, TextEncoding from, TextEncoding to, LPWSTR *ppNew);
LPCWSTR EncodingName(TextEncoding encoding);
