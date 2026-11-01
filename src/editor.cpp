#include "notbad.h"
#include <string.h>
#include <string>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

static void GetEditSelection(HWND hEdit, int* pStart, int* pEnd)
{
    CHARRANGE cr = {};

    // EM_GETSEL devolve so 16 bits por posicao (quebra acima de 64K chars).
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    *pStart = (int)cr.cpMin;
    *pEnd = (int)cr.cpMax;
}

static int GetEditLineStart(HWND hEdit, int index)
{
    int line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, index, 0);
    int start;

    if (line < 0)
        return 0;

    start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    return start < 0 ? 0 : start;
}

static WCHAR GetEditCharAt(HWND hEdit, int index)
{
    TEXTRANGEW range = {};
    WCHAR szChar[2] = {};

    if (index < 0)
        return L'\0';

    range.chrg.cpMin = index;
    range.chrg.cpMax = index + 1;
    range.lpstrText = szChar;
    SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);

    return szChar[0];
}

static void GetLineIndent(HWND hEdit, int line, LPWSTR lpBuffer, int cchBuffer)
{
    TEXTRANGEW range = {};
    int start;
    int length;
    int count;

    lpBuffer[0] = L'\0';
    if (cchBuffer <= 0)
        return;

    start = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (start < 0)
        return;

    length = (int)SendMessageW(hEdit, EM_LINELENGTH, start, 0);
    if (length <= 0)
        return;

    if (length > cchBuffer - 1)
        length = cchBuffer - 1;

    range.chrg.cpMin = start;
    range.chrg.cpMax = start + length;
    range.lpstrText = lpBuffer;
    SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);
    lpBuffer[length] = L'\0';

    count = 0;
    while (count < length && (lpBuffer[count] == L' ' || lpBuffer[count] == L'\t'))
        ++count;
    lpBuffer[count] = L'\0';
}

static int GetTabWidth(void)
{
    return g_settings.tabWidth > 0 ? g_settings.tabWidth : 4;
}

void EditorNew(void)
{
    DocCreate();
}

BOOL EditorOpen(LPCWSTR lpFileName)
{
    Document* pDoc = DocCreate();

    if (pDoc == NULL)
        return FALSE;

    if (!DocLoad(pDoc, lpFileName))
    {
        DocDestroy(pDoc);

        if (g_documents.empty())
            DocCreate();

        return FALSE;
    }

    return TRUE;
}

BOOL EditorSave(BOOL bSaveAs)
{
    return DocSave(g_pActiveDoc, bSaveAs);
}

BOOL EditorSaveFile(LPCWSTR lpFileName)
{
    return DocSaveFile(g_pActiveDoc, lpFileName);
}

BOOL EditorQuerySaveChanges(void)
{
    return DocQuerySaveChanges(g_pActiveDoc);
}

void EditorSetModified(BOOL bModified)
{
    if (g_hWndEdit != NULL)
        SendMessageW(g_hWndEdit, EM_SETMODIFY, bModified, 0);
}

BOOL EditorIsModified(void)
{
    if (g_hWndEdit == NULL)
        return FALSE;
    return (BOOL)SendMessageW(g_hWndEdit, EM_GETMODIFY, 0, 0);
}

void EditorSetFont(HFONT hFont)
{
    size_t i;

    if (hFont == NULL)
        return;

    for (i = 0; i < g_documents.size(); ++i)
        SendMessageW(g_documents[i]->hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // A largura do tab depende da largura do espaco da fonte.
    EditorApplyTabWidth();
}

// Ajusta a largura visual do caractere Tab (em N espacos da fonte atual)
// via ITextDocument::DefaultTabStop. Nao mexe no texto, no undo nem no
// estado de modificado.
void EditorApplyTabWidthTo(HWND hEdit)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    HDC hdc;
    HGDIOBJ oldFont;
    SIZE size = {};
    int dpi;

    if (hEdit == NULL)
        return;

    hdc = GetDC(hEdit);
    if (hdc == NULL)
        return;

    oldFont = SelectObject(hdc, g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                : GetStockObject(SYSTEM_FIXED_FONT));
    GetTextExtentPoint32W(hdc, L" ", 1, &size);
    dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    SelectObject(hdc, oldFont);
    ReleaseDC(hEdit, hdc);

    if (size.cx <= 0 || dpi <= 0)
        return;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            pTextDoc->SetDefaultTabStop((float)(size.cx * GetTabWidth()) * 72.0f / (float)dpi);
            pTextDoc->Release();
        }
        pOle->Release();
    }
}

void EditorApplyTabWidth(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorApplyTabWidthTo(g_documents[i]->hEdit);
}

// Numero logico da linha (paragrafo) que contem charIndex, 1-based.
// EM_GETLINECOUNT/EM_LINEINDEX contam linhas fisicas (quebras de word wrap),
// mas a numeracao exibida deve pular as continuacoes. O TOM entrega o indice
// de paragrafo exato. Retorna 0 em caso de falha.
int EditorGetParagraphNumber(HWND hEdit, int charIndex)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long para = 0;

    if (hEdit == NULL || charIndex < 0)
        return 0;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(charIndex, charIndex, &pRange)) && pRange != NULL)
            {
                pRange->GetIndex(tomParagraph, &para);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)para;
}

// Posicao do inicio do paragrafo que contem charIndex (0 se falhar).
int EditorGetParagraphStart(HWND hEdit, int charIndex)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long start = 0;
    long delta = 0;

    if (hEdit == NULL || charIndex < 0)
        return 0;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(charIndex, charIndex, &pRange)) && pRange != NULL)
            {
                if (SUCCEEDED(pRange->StartOf(tomParagraph, 0, &delta)))
                    pRange->GetStart(&start);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)start;
}

int EditorGetParagraphCount(HWND hEdit)
{
    GETTEXTLENGTHEX gtl = {};
    int length;
    int count;

    if (hEdit == NULL)
        return 1;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;
    length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (length < 0)
        length = 0;

    count = EditorGetParagraphNumber(hEdit, length);
    return count > 0 ? count : 1;
}

int EditorGetParagraphStartByNumber(HWND hEdit, int lineNumber)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;
    ITextRange* pRange = NULL;
    long start = 0;
    long moved = 0;

    if (hEdit == NULL)
        return 0;
    if (lineNumber < 1)
        lineNumber = 1;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (SUCCEEDED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)) &&
            pTextDoc != NULL)
        {
            if (SUCCEEDED(pTextDoc->Range(0, 0, &pRange)) && pRange != NULL)
            {
                if (SUCCEEDED(pRange->Move(tomParagraph, lineNumber - 1, &moved)))
                    pRange->GetStart(&start);
                pRange->Release();
            }
            pTextDoc->Release();
        }
        pOle->Release();
    }

    return (int)start;
}

// No RichEdit o estilo sozinho nao controla a quebra: e preciso mandar
// EM_SETTARGETDEVICE com lParam 0 (quebra na janela) ou 1 (sem quebra).
void EditorApplyWordWrapTo(HWND hEdit, BOOL bWrap)
{
    LONG_PTR style;

    if (hEdit == NULL)
        return;

    style = GetWindowLongPtrW(hEdit, GWL_STYLE);
    if (bWrap)
        style &= ~(WS_HSCROLL | ES_AUTOHSCROLL);
    else
        style |= WS_HSCROLL | ES_AUTOHSCROLL;

    SetWindowLongPtrW(hEdit, GWL_STYLE, style);
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, bWrap ? 0 : 1);
    SetWindowPos(hEdit, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect(hEdit, NULL, TRUE);
}

void EditorApplyWordWrap(BOOL bWrap)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorApplyWordWrapTo(g_documents[i]->hEdit, bWrap);
}

void EditorSelectAll(void)
{
    if (g_hWndEdit == NULL)
        return;

    SendMessageW(g_hWndEdit, EM_SETSEL, 0, -1);
    SetFocus(g_hWndEdit);
}

void EditorInsertTimeDate(void)
{
    SYSTEMTIME st;
    WCHAR szDate[128];
    WCHAR szTime[128];
    WCHAR szResult[256];

    if (g_hWndEdit == NULL)
        return;

    GetLocalTime(&st);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL,
                   szTime, ARRAYSIZE(szTime));
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL,
                   szDate, ARRAYSIZE(szDate));

    StringCchPrintfW(szResult, ARRAYSIZE(szResult), L"%s %s", szTime, szDate);

    SendMessageW(g_hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)szResult);
}

void EditorSetReadOnly(BOOL bReadOnly)
{
    if (g_hWndEdit != NULL)
        SendMessageW(g_hWndEdit, EM_SETREADONLY, bReadOnly, 0);
}

void EditorUpdateEncodingStatus(void)
{
    UpdateStatusBar();
}

// O TranslateMessage ja enfileirou o WM_CHAR da tecla antes de o WM_KEYDOWN
// chegar aqui; se nao for removido, o RichEdit age sobre ele depois de nos.
static void RemovePendingChar(HWND hEdit, WCHAR ch)
{
    MSG chMsg;

    if (PeekMessageW(&chMsg, hEdit, WM_CHAR, WM_CHAR, PM_NOREMOVE) &&
        chMsg.wParam == (WPARAM)ch)
    {
        PeekMessageW(&chMsg, hEdit, WM_CHAR, WM_CHAR, PM_REMOVE);
    }
}

// O RichEdit 5.0 trata o Enter ja no WM_KEYDOWN (insere a quebra de linha
// sozinho), por isso o auto-indent precisa agir aqui e nao no WM_CHAR.
static BOOL EditorHandleEnter(HWND hEdit)
{
    WCHAR szIndent[256];
    WCHAR szInsert[1024];
    int start;
    int end;
    int line;
    int caret;

    if (!g_settings.autoIndent)
        return FALSE;

    // Shift/Ctrl/Alt+Enter mantem o comportamento padrao do RichEdit.
    if ((GetKeyState(VK_SHIFT) | GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);

    line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, start, 0);
    if (line < 0)
        line = 0;
    GetLineIndent(hEdit, line, szIndent, ARRAYSIZE(szIndent));

    StringCchPrintfW(szInsert, ARRAYSIZE(szInsert), L"\r%s", szIndent);
    caret = start + 1 + lstrlenW(szIndent);

    RemovePendingChar(hEdit, L'\r');

    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)szInsert);
    SendMessageW(hEdit, EM_SETSEL, caret, caret);
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Home com auto-indent ativo: primeiro vai ao inicio do texto da linha
// (apos a indentacao); estando ali, o Home padrao leva ao inicio real.
static BOOL EditorHandleHome(HWND hEdit)
{
    WCHAR szLine[2048];
    int start;
    int end;
    int line;
    int lineStart;
    int lineLen;
    int indent;
    int target;

    if (!g_settings.autoIndent)
        return FALSE;

    if ((GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return FALSE;

    line = (int)SendMessageW(hEdit, EM_LINEFROMCHAR, start, 0);
    if (line < 0)
        return FALSE;

    lineStart = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    if (lineStart < 0)
        return FALSE;

    lineLen = (int)SendMessageW(hEdit, EM_LINELENGTH, lineStart, 0);
    if (lineLen <= 0)
        return FALSE;
    if (lineLen > (int)ARRAYSIZE(szLine) - 1)
        lineLen = (int)ARRAYSIZE(szLine) - 1;

    *(WORD*)szLine = (WORD)lineLen;
    lineLen = (int)SendMessageW(hEdit, EM_GETLINE, line, (LPARAM)szLine);
    if (lineLen <= 0)
        return FALSE;

    for (indent = 0; indent < lineLen; ++indent)
    {
        if (szLine[indent] != L' ' && szLine[indent] != L'\t')
            break;
    }

    target = lineStart + indent;
    if (start == target || (start == lineStart && indent == 0))
        return FALSE;

    // Com Shift (sem selecao previa) estende a selecao ate o destino.
    if (GetKeyState(VK_SHIFT) & 0x8000)
        SendMessageW(hEdit, EM_SETSEL, start, target);
    else
        SendMessageW(hEdit, EM_SETSEL, target, target);

    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    UpdateStatusBar();
    return TRUE;
}

// Operacoes em lote: sem notificacoes (EN_CHANGE/EN_SELCHANGE por linha) e
// sem repintura intermediaria, para nao piscar nem ficar lento.
static DWORD EditorBeginBatch(HWND hEdit)
{
    DWORD oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    return oldMask;
}

static void EditorEndBatch(HWND hEdit, DWORD oldMask)
{
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, (LPARAM)oldMask);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

// Texto interno do RichEdit (quebras so com CR), alinhado aos indices de
// EM_EXGETSEL/EM_EXSETSEL. Quem chama libera com LocalFree.
static LPWSTR EditorGetText(HWND hEdit, int* pcch)
{
    GETTEXTLENGTHEX gtl = {};
    GETTEXTEX gt = {};
    LPWSTR lpText;
    int length;
    int copied;

    *pcch = 0;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;    // CP_UTF16
    length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (length < 0)
        length = 0;

    lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpText == NULL)
        return NULL;

    gt.cb = (DWORD)(((SIZE_T)length + 1) * sizeof(WCHAR));
    gt.flags = GT_DEFAULT;
    gt.codepage = 1200;
    copied = (int)SendMessageW(hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)lpText);
    if (copied < 0 || copied > length)
        copied = length;
    lpText[copied] = L'\0';

    *pcch = copied;
    return lpText;
}

// Tab / Shift+Tab em bloco. As linhas sao paragrafos logicos (nao as linhas
// visuais do word wrap) e o bloco inteiro e trocado por uma unica
// substituicao: um unico passo de undo e sem piscar.
static BOOL EditorIndentBlock(HWND hEdit, BOOL shift)
{
    LPWSTR lpText;
    int cchText;
    int start;
    int end;
    int effEnd;
    int rangeStart;
    int rangeEnd;
    int tabWidth = GetTabWidth();
    std::vector<int> lineStarts;
    std::vector<int> removes;
    std::wstring block;
    int pos;
    int newStart;
    int newEnd;
    DWORD oldMask;
    size_t i;

    lpText = EditorGetText(hEdit, &cchText);
    if (lpText == NULL)
        return TRUE;

    GetEditSelection(hEdit, &start, &end);
    if (start < 0)
        start = 0;
    if (end > cchText)
        end = cchText;
    if (start > end)
        start = end;

    // Selecao que termina no inicio de uma linha nao inclui essa linha.
    effEnd = end;
    if (end > start && end > 0 && lpText[end - 1] == L'\r')
        --effEnd;

    rangeStart = start;
    while (rangeStart > 0 && lpText[rangeStart - 1] != L'\r')
        --rangeStart;

    rangeEnd = effEnd;
    while (rangeEnd < cchText && lpText[rangeEnd] != L'\r')
        ++rangeEnd;

    pos = rangeStart;
    for (;;)
    {
        int lineEnd = pos;
        int remove = 0;

        while (lineEnd < rangeEnd && lpText[lineEnd] != L'\r')
            ++lineEnd;

        lineStarts.push_back(pos);

        if (!shift)
        {
            block.push_back(L'\t');
        }
        else
        {
            if (pos < lineEnd && lpText[pos] == L'\t')
            {
                remove = 1;
            }
            else
            {
                while (remove < tabWidth && pos + remove < lineEnd &&
                       lpText[pos + remove] == L' ')
                    ++remove;
            }
        }

        removes.push_back(remove);
        block.append(lpText + pos + remove, (size_t)(lineEnd - pos - remove));

        if (lineEnd >= rangeEnd)
            break;

        block.push_back(L'\r');
        pos = lineEnd + 1;
    }

    // Selecao resultante.
    if (!shift)
    {
        newStart = start + 1;
        newEnd = end + (int)lineStarts.size();
    }
    else
    {
        // Cada ponta recua so o que foi removido ANTES dela (nunca alem do
        // inicio da propria linha), senao a selecao vaza para a linha de cima.
        newStart = start;
        newEnd = end;
        for (i = 0; i < lineStarts.size(); ++i)
        {
            int before = start - lineStarts[i];

            if (before < 0)
                before = 0;
            if (before > removes[i])
                before = removes[i];
            newStart -= before;

            before = end - lineStarts[i];
            if (before < 0)
                before = 0;
            if (before > removes[i])
                before = removes[i];
            newEnd -= before;
        }

        if (newStart < 0)
            newStart = 0;
        if (newEnd < newStart)
            newEnd = newStart;
    }

    LocalFree(lpText);

    {
        BOOL changed = FALSE;

        for (i = 0; i < removes.size(); ++i)
        {
            if (!shift || removes[i] > 0)
            {
                changed = TRUE;
                break;
            }
        }

        // Shift+Tab sem nada para remover: so preserva a selecao.
        if (!changed)
            return TRUE;
    }

    oldMask = EditorBeginBatch(hEdit);

    SendMessageW(hEdit, EM_SETSEL, rangeStart, rangeEnd);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)block.c_str());
    SendMessageW(hEdit, EM_SETSEL, newStart, newEnd);

    EditorEndBatch(hEdit, oldMask);

    if (DocFromEdit(hEdit) != NULL)
        DocUpdateTabTitle(DocFromEdit(hEdit));
    UpdateWindowTitle();
    InvalidateLineNumbers();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

BOOL EditorHandleKeyDown(HWND hEdit, WPARAM wParam)
{
    BOOL shift;
    int start;
    int end;

    if (hEdit == NULL)
        return FALSE;

    if (wParam == VK_RETURN)
        return EditorHandleEnter(hEdit);

    if (wParam == VK_HOME)
        return EditorHandleHome(hEdit);

    if (wParam != VK_TAB)
        return FALSE;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    GetEditSelection(hEdit, &start, &end);

    if (!shift && start == end)
        return FALSE;

    // Sem remover o WM_CHAR('\t') o RichEdit trocaria a selecao por um Tab
    // logo depois do nosso tratamento.
    RemovePendingChar(hEdit, L'\t');

    return EditorIndentBlock(hEdit, shift);
}

void EditorConvertTabs(BOOL bTabsToSpaces)
{
    int length;
    int tabWidth;
    int i;
    int o = 0;
    LPWSTR lpSrc;
    LPWSTR lpDst;
    SIZE_T capacity;
    int selStart;
    int selEnd;

    if (g_hWndEdit == NULL)
        return;

    {
        // Contagem em caracteres, com quebras de linha como o RichEdit as
        // guarda (CR), igual ao que EM_GETTEXTEX devolve abaixo.
        GETTEXTLENGTHEX gtl = {};
        gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
        gtl.codepage = 1200;    // CP_UTF16
        length = (int)SendMessageW(g_hWndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    }
    if (length <= 0)
        return;

    tabWidth = GetTabWidth();
    capacity = (SIZE_T)length + 1;
    if (bTabsToSpaces)
    {
        if ((SIZE_T)length > (SIZE_T)(-1) / (SIZE_T)tabWidth)
            return;
        capacity = (SIZE_T)length * (SIZE_T)tabWidth + 1;
    }

    lpSrc = (LPWSTR)LocalAlloc(LMEM_FIXED, ((SIZE_T)length + 1) * sizeof(WCHAR));
    if (lpSrc == NULL)
        return;

    lpDst = (LPWSTR)LocalAlloc(LMEM_FIXED, capacity * sizeof(WCHAR));
    if (lpDst == NULL)
    {
        LocalFree(lpSrc);
        return;
    }

    {
        GETTEXTEX gt = {};
        gt.cb = (DWORD)(((SIZE_T)length + 1) * sizeof(WCHAR));
        gt.flags = GT_DEFAULT;
        gt.codepage = 1200;     // CP_UTF16
        length = (int)SendMessageW(g_hWndEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)lpSrc);
        lpSrc[length] = L'\0';
    }

    i = 0;
    while (i < length)
    {
        int indentEnd = i;

        while (indentEnd < length &&
               (lpSrc[indentEnd] == L' ' || lpSrc[indentEnd] == L'\t'))
        {
            ++indentEnd;
        }

        if (bTabsToSpaces)
        {
            int k;
            for (k = i; k < indentEnd; ++k)
            {
                if (lpSrc[k] == L'\t')
                {
                    int s;
                    for (s = 0; s < tabWidth; ++s)
                        lpDst[o++] = L' ';
                }
                else
                {
                    lpDst[o++] = lpSrc[k];
                }
            }
        }
        else
        {
            int k = i;
            while (k < indentEnd)
            {
                if (lpSrc[k] == L'\t')
                {
                    lpDst[o++] = L'\t';
                    ++k;
                }
                else
                {
                    int run = 0;
                    int tabs;
                    int rem;
                    int s;
                    while (k + run < indentEnd && lpSrc[k + run] == L' ')
                        ++run;
                    tabs = run / tabWidth;
                    rem = run % tabWidth;
                    for (s = 0; s < tabs; ++s)
                        lpDst[o++] = L'\t';
                    for (s = 0; s < rem; ++s)
                        lpDst[o++] = L' ';
                    k += run;
                }
            }
        }

        i = indentEnd;

        while (i < length && lpSrc[i] != L'\r' && lpSrc[i] != L'\n')
            lpDst[o++] = lpSrc[i++];

        if (i < length && lpSrc[i] == L'\r')
        {
            lpDst[o++] = lpSrc[i++];
            if (i < length && lpSrc[i] == L'\n')
                lpDst[o++] = lpSrc[i++];
        }
        else if (i < length && lpSrc[i] == L'\n')
        {
            lpDst[o++] = lpSrc[i++];
        }
    }

    lpDst[o] = L'\0';

    // Nada a converter: nao substituir o texto (evita undo/modificado a toa)
    // e avisar, para a pessoa saber que o comando rodou.
    if (o == length && memcmp(lpSrc, lpDst, (size_t)o * sizeof(WCHAR)) == 0)
    {
        LocalFree(lpSrc);
        LocalFree(lpDst);
        MessageBoxW(g_hWndMain,
                    bTabsToSpaces ? L"There are no tabs in the indentation of this tab."
                                  : L"There are no runs of spaces long enough to convert to tabs.",
                    NOTBAD_NAME, MB_OK | MB_ICONINFORMATION);
        return;
    }

    GetEditSelection(g_hWndEdit, &selStart, &selEnd);

    // Troca unica, sem notificacoes nem repintura intermediaria, mantendo a
    // posicao de rolagem (um passo de undo e sem piscar).
    int firstVisible = (int)SendMessageW(g_hWndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    DWORD oldMask = EditorBeginBatch(g_hWndEdit);

    SendMessageW(g_hWndEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_hWndEdit, EM_REPLACESEL, TRUE, (LPARAM)lpDst);

    int newStart = selStart;
    int newEnd = selEnd;
    if (newStart > o)
        newStart = o;
    if (newEnd > o)
        newEnd = o;
    SendMessageW(g_hWndEdit, EM_SETSEL, newStart, newEnd);

    {
        int nowVisible = (int)SendMessageW(g_hWndEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
        if (nowVisible != firstVisible)
            SendMessageW(g_hWndEdit, EM_LINESCROLL, 0, firstVisible - nowVisible);
    }

    EditorEndBatch(g_hWndEdit, oldMask);

    LocalFree(lpSrc);
    LocalFree(lpDst);

    if (g_pActiveDoc != NULL)
        DocUpdateTabTitle(g_pActiveDoc);
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    InvalidateLineNumbers();
}
