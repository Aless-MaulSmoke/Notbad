#include "notbad.h"
#include <string.h>
#include <string>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

#define NOTBAD_HIGHLIGHT_COLOR RGB(240, 240, 240)

static BOOL s_bUpdatingHighlight = FALSE;

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

// Colunas visuais de uma sequencia de espacos/tabs, expandindo cada tab ate
// o proximo multiplo da largura configurada.
static int GetIndentColumns(const WCHAR* lpText, int cchText, int tabWidth)
{
    int column = 0;
    int i;

    for (i = 0; i < cchText; ++i)
    {
        if (lpText[i] == L'\t')
            column += tabWidth - (column % tabWidth);
        else
            column += 1;
    }

    return column;
}

// Monta a indentacao no estilo escolhido (espacos por padrao ou tabs).
static int BuildIndentString(int columns, int tabWidth, BOOL useTabs,
                             LPWSTR lpBuffer, int cchBuffer)
{
    int length = 0;

    if (cchBuffer <= 0)
        return 0;

    if (useTabs)
    {
        while (columns >= tabWidth && length < cchBuffer - 1)
        {
            lpBuffer[length++] = L'\t';
            columns -= tabWidth;
        }
    }

    while (columns > 0 && length < cchBuffer - 1)
    {
        lpBuffer[length++] = L' ';
        --columns;
    }

    lpBuffer[length] = L'\0';
    return length;
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

// Reaplica ao editor ativo todas as configuracoes de indentacao carregadas
// do registro (largura do tab e estilo espacos/tabs). Chamado no startup e
// apos o dialogo de indentacao.
void EditorApplyIndentation(void)
{
    EditorApplyTabWidth();
    InvalidateLineNumbers();
    UpdateStatusBar();
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

void EditorPlaceCaretAtPoint(HWND hEdit, POINT pt)
{
    RECT rc;
    LRESULT position;

    if (hEdit == NULL)
        return;

    GetClientRect(hEdit, &rc);
    if (!PtInRect(&rc, pt))
        return;

    position = SendMessageW(hEdit, EM_CHARFROMPOS, 0, (LPARAM)&pt);
    if (position < 0)
        return;

    SendMessageW(hEdit, EM_SETSEL, position, position);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    InvalidateLineNumbers();
    UpdateStatusBar();
}

// Ao reativar a janela com um clique sobre o texto, o primeiro clique tambem
// posiciona o cursor no local apontado. So age quando o botao esquerdo esta
// pressionado e o ponto esta dentro do editor, para nao mover o cursor em
// ativacoes por teclado, menus, abas ou barra de status.
void EditorPlaceCaretAtClick(HWND hEdit)
{
    POINT pt;

    if (hEdit == NULL)
        return;

    if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0)
        return;

    if (!GetCursorPos(&pt))
        return;

    ScreenToClient(hEdit, &pt);
    EditorPlaceCaretAtPoint(hEdit, pt);
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

// Pergunta o que fazer quando "converter" e "reinterpretar" dariam resultados
// diferentes. Retorna IDYES (converter), IDNO (reinterpretar) ou IDCANCEL.
static int AskEncodingChoice(TextEncoding target)
{
    WCHAR szFormat[512];
    WCHAR szMessage[1024];

    LoadStringResource(IDS_ENC_ASK, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat,
                     EncodingName(target), EncodingName(target));

    return MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                       MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
}

static BOOL ConfirmLossyEncoding(TextEncoding target)
{
    WCHAR szFormat[512];
    WCHAR szMessage[1024];

    LoadStringResource(IDS_ENC_LOSS, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat, EncodingName(target));

    return MessageBoxW(g_hWndMain, szMessage, NOTBAD_NAME,
                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK;
}

// Troca o texto do documento mantendo, de forma aproximada, a selecao e a
// rolagem. O documento fica marcado como modificado (asterisco na aba): o que
// aparece na tela mudou e ainda nao foi salvo, e sumir com o asterisco aqui
// faria parecer que o arquivo foi gravado. Sem historico de desfazer.
static void ReplaceTextAfterReinterpret(Document* pDoc, LPCWSTR lpNew)
{
    CHARRANGE cr = {};
    POINT scroll = {};

    SendMessageW(pDoc->hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    SendMessageW(pDoc->hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);

    EditorClearCurrentLineHighlight(pDoc);
    SetWindowTextW(pDoc->hEdit, lpNew);

    // Trocar o texto pode redefinir a largura do tab do RichEdit.
    EditorApplyTabWidthTo(pDoc->hEdit);

    // O comprimento pode ter mudado; o RichEdit limita a selecao ao fim.
    SendMessageW(pDoc->hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(pDoc->hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);

    SendMessageW(pDoc->hEdit, EM_SETMODIFY, TRUE, 0);
    SendMessageW(pDoc->hEdit, EM_EMPTYUNDOBUFFER, 0, 0);

    EditorUpdateCurrentLineHighlight(pDoc->hEdit);
}

// Format > Encoding. Muda a codificacao do documento ativo:
//  - Converter: o texto na tela nao muda; so a forma de gravar. O documento
//    passa a modificado, pois os bytes em disco serao outros.
//  - Reinterpretar: os bytes do arquivo sao lidos de outro modo (o texto na
//    tela muda) e o documento fica modificado. So e oferecido para arquivo com
//    caminho que esteja sem edicoes ou que ja tenha sido reinterpretado desde
//    a ultima gravacao (para poder voltar), quando o resultado difere do texto
//    atual; nos demais casos so existe converter.
void EditorSetEncoding(TextEncoding encoding)
{
    Document* pDoc = g_pActiveDoc;
    LPWSTR lpText = NULL;
    LPWSTR lpNew = NULL;
    int length;
    BOOL bModified;
    BOOL bReinterpret = FALSE;

    if (pDoc == NULL || pDoc->hEdit == NULL || pDoc->encoding == encoding)
        return;

    bModified = (BOOL)SendMessageW(pDoc->hEdit, EM_GETMODIFY, 0, 0);
    length = GetWindowTextLengthW(pDoc->hEdit);

    if (length > 0)
    {
        lpText = (LPWSTR)LocalAlloc(LMEM_ZEROINIT, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (lpText == NULL)
            return;
        GetWindowTextW(pDoc->hEdit, lpText, length + 1);
    }

    if (lpText != NULL)
    {
        if (pDoc->bHasPath && (!bModified || pDoc->bReinterpreted) &&
            ReinterpretText(lpText, pDoc->encoding, encoding, &lpNew) &&
            lstrcmpW(lpText, lpNew) != 0)
        {
            int answer = AskEncodingChoice(encoding);

            if (answer != IDYES && answer != IDNO)
            {
                LocalFree(lpNew);
                LocalFree(lpText);
                return;
            }

            bReinterpret = (answer == IDNO);
        }

        // Ao converter, o texto atual precisa caber na nova codificacao.
        if (!bReinterpret && !CanEncodeLossless(lpText, encoding) &&
            !ConfirmLossyEncoding(encoding))
        {
            if (lpNew != NULL)
                LocalFree(lpNew);
            LocalFree(lpText);
            return;
        }
    }

    if (bReinterpret)
    {
        ReplaceTextAfterReinterpret(pDoc, lpNew);
        pDoc->bReinterpreted = TRUE;
    }
    else if (length > 0 || pDoc->bHasPath)
    {
        // Documento novo e vazio nao vira "sujo" so por trocar de codificacao.
        SendMessageW(pDoc->hEdit, EM_SETMODIFY, TRUE, 0);
    }

    pDoc->encoding = encoding;

    if (lpNew != NULL)
        LocalFree(lpNew);
    if (lpText != NULL)
        LocalFree(lpText);

    DocUpdateTabTitle(pDoc);
    InvalidateLineNumbers();
    UpdateWindowTitle();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
}

// ---------------------------------------------------------------------------
// Modo Insert / Overwrite (tecla Insert). O estado fica em Document::bOverwrite.
// No Overwrite o caret vira um bloco do tamanho do caractere sob o cursor e o
// caractere digitado substitui esse caractere; no Insert volta a barra fina.
// ---------------------------------------------------------------------------
static HWND s_hCaretOwner = NULL;   // editor cujo caret foi recriado aqui
static int  s_caretWidth  = 0;
static int  s_caretHeight = 0;

// Esquece o caret recriado: o RichEdit refaz o proprio caret no foco e na
// troca de fonte, entao a forma precisa ser reaplicada depois.
void EditorResetCaretShape(void)
{
    s_hCaretOwner = NULL;
    s_caretWidth = 0;
    s_caretHeight = 0;
}

static int GetNormalCaretWidth(void)
{
    DWORD width = 0;

    if (!SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0) || width < 1)
        width = 1;

    return (int)width;
}

// Largura e altura do caret. No Overwrite a largura e a do caractere sob o
// cursor (um espaco no fim da linha, em Tab ou no fim do texto).
static BOOL GetCaretMetrics(HWND hEdit, int caret, BOOL bOverwrite, int* pWidth, int* pHeight)
{
    HDC hdc;
    HGDIOBJ oldFont;
    TEXTMETRICW tm = {};
    SIZE size = {};
    WCHAR chars[2] = {};
    int count = 1;

    hdc = GetDC(hEdit);
    if (hdc == NULL)
        return FALSE;

    oldFont = SelectObject(hdc, g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                : GetStockObject(SYSTEM_FIXED_FONT));
    GetTextMetricsW(hdc, &tm);

    if (bOverwrite)
    {
        chars[0] = GetEditCharAt(hEdit, caret);

        if (chars[0] >= 0xD800 && chars[0] <= 0xDBFF)
        {
            chars[1] = GetEditCharAt(hEdit, caret + 1);
            if (chars[1] >= 0xDC00 && chars[1] <= 0xDFFF)
                count = 2;
        }

        if (chars[0] < L' ')    // inclui o fim do texto (NUL), CR, LF e Tab
        {
            chars[0] = L' ';
            count = 1;
        }

        GetTextExtentPoint32W(hdc, chars, count, &size);
        *pWidth = size.cx > 1 ? (int)size.cx : (int)tm.tmAveCharWidth;
    }
    else
    {
        *pWidth = GetNormalCaretWidth();
    }

    *pHeight = (int)tm.tmHeight;

    SelectObject(hdc, oldFont);
    ReleaseDC(hEdit, hdc);

    return *pWidth > 0 && *pHeight > 0;
}

// Aplica a forma do caret conforme o modo do documento. Chamado quando a
// selecao ou o texto mudam, no foco e depois de a tecla Insert alternar o modo.
// No modo Insert so age se antes houve um caret em bloco a desfazer.
void EditorUpdateCaretShape(HWND hEdit)
{
    Document* pDoc;
    POINT pt = {};
    int start;
    int end;
    int width;
    int height;
    BOOL bOverwrite;

    if (hEdit == NULL || GetFocus() != hEdit)
        return;

    pDoc = DocFromEdit(hEdit);
    if (pDoc == NULL)
        return;

    bOverwrite = pDoc->bOverwrite;
    if (!bOverwrite && s_hCaretOwner != hEdit)
        return;

    // Com texto selecionado nao ha caret visivel.
    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return;

    if (!GetCaretMetrics(hEdit, start, bOverwrite, &width, &height))
        return;

    // COMENTADO OU REMOVIDO: Este IF impedia o cursor de ser reaplicado 
    // quando você digitava letras seguidas de mesmo tamanho físico.
    /*
    if (s_hCaretOwner == hEdit && s_caretWidth == width && s_caretHeight == height)
        return;
    */

    SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, start);

    // CreateCaret destroi o caret anterior do RichEdit e o recria com o nosso tamanho
    if (!CreateCaret(hEdit, NULL, width, height))
        return;

    SetCaretPos(pt.x, pt.y);
    ShowCaret(hEdit);

    if (bOverwrite)
    {
        s_hCaretOwner = hEdit;
        s_caretWidth = width;
        s_caretHeight = height;
    }
    else
    {
        EditorResetCaretShape();
    }
}

// Chamado antes de o RichEdit processar um caractere digitado no modo
// Overwrite: seleciona o caractere sob o cursor para que o digitado o
// substitua. No fim da linha ou do texto nada e selecionado (insere normal).
void EditorHandleOverwriteChar(HWND hEdit, WCHAR ch)
{
    WCHAR current;
    int start;
    int end;
    int length = 1;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return;

    if (ch < L' ' || ch == 0x7F)
        return;

    // A segunda metade de um par substituto completa o par ja digitado.
    if (ch >= 0xDC00 && ch <= 0xDFFF)
        return;

    GetEditSelection(hEdit, &start, &end);
    if (start != end)
        return;

    current = GetEditCharAt(hEdit, start);
    if (current == L'\0' || current == L'\r' || current == L'\n')
        return;

    if (current >= 0xD800 && current <= 0xDBFF)
    {
        WCHAR next = GetEditCharAt(hEdit, start + 1);
        if (next >= 0xDC00 && next <= 0xDFFF)
            length = 2;
    }

    SendMessageW(hEdit, EM_SETSEL, start, start + length);
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
    int columns;
    int indentLength;

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

    // Reaplica a indentacao no estilo configurado, para que uma linha nova
    // nao misture tabs e espacos com o restante do arquivo.
    columns = GetIndentColumns(szIndent, lstrlenW(szIndent), GetTabWidth());
    indentLength = BuildIndentString(columns, GetTabWidth(),
                                     g_settings.indentWithTabs,
                                     szIndent, ARRAYSIZE(szIndent));

    StringCchPrintfW(szInsert, ARRAYSIZE(szInsert), L"\r%s", szIndent);
    caret = start + 1 + indentLength;

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

static ITextDocument* EditorGetTextDocument(HWND hEdit)
{
    IRichEditOle* pOle = NULL;
    ITextDocument* pTextDoc = NULL;

    if (hEdit == NULL)
        return NULL;

    if (SendMessageW(hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&pOle) && pOle != NULL)
    {
        if (FAILED(pOle->QueryInterface(__uuidof(ITextDocument), (void**)&pTextDoc)))
            pTextDoc = NULL;
        pOle->Release();
    }

    return pTextDoc;
}

static void EditorSetTextRangeBackColor(ITextRange* pRange, long color)
{
    ITextFont* pFont = NULL;

    if (pRange == NULL)
        return;

    if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont != NULL)
    {
        pFont->SetBackColor(color);
        pFont->Release();
    }
}

void EditorClearCurrentLineHighlight(Document* pDoc)
{
    ITextDocument* pTextDoc;
    DWORD oldMask;
    BOOL modified;
    HWND hEdit;

    if (pDoc == NULL || pDoc->hEdit == NULL || pDoc->pHighlightRange == NULL ||
        s_bUpdatingHighlight)
    {
        return;
    }

    if (!IsWindow(pDoc->hEdit))
    {
        pDoc->pHighlightRange->Release();
        pDoc->pHighlightRange = NULL;
        return;
    }

    s_bUpdatingHighlight = TRUE;
    hEdit = pDoc->hEdit;
    pTextDoc = EditorGetTextDocument(hEdit);
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);

    if (pTextDoc != NULL)
        pTextDoc->Undo(tomSuspend, NULL);

    EditorSetTextRangeBackColor(pDoc->pHighlightRange, tomAutoColor);
    pDoc->pHighlightRange->Release();
    pDoc->pHighlightRange = NULL;

    if (pTextDoc != NULL)
    {
        pTextDoc->Undo(tomResume, NULL);
        pTextDoc->Release();
    }

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
    s_bUpdatingHighlight = FALSE;
}

void EditorUpdateCurrentLineHighlight(HWND hEdit)
{
    Document* pDoc = DocFromEdit(hEdit);
    ITextDocument* pTextDoc;
    ITextRange* pRange = NULL;
    ITextFont* pFont = NULL;
    GETTEXTLENGTHEX gtl = {};
    int selStart;
    int selEnd;
    int caret;
    int para;
    int start;
    int nextStart;
    int end;
    int textLen;
    int storyEnd;
    DWORD oldMask;
    BOOL modified;

    if (hEdit == NULL || pDoc == NULL || s_bUpdatingHighlight)
        return;

    s_bUpdatingHighlight = TRUE;

    GetEditSelection(hEdit, &selStart, &selEnd);
    caret = selEnd;
    (void)selStart;

    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;
    textLen = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (textLen < 0)
        textLen = 0;

    para = EditorGetParagraphNumber(hEdit, caret);
    start = EditorGetParagraphStart(hEdit, caret);
    nextStart = EditorGetParagraphStartByNumber(hEdit, para + 1);

    // O RichEdit guarda um CR final alem do texto (a historia tem textLen + 1
    // caracteres). A ultima linha vazia (documento novo, ou Enter no fim do
    // arquivo) so tem esse CR: com o fim limitado a textLen o trecho ficava
    // vazio e o destaque nunca aparecia ate a linha receber algum caractere.
    storyEnd = textLen + 1;
    end = nextStart > start ? nextStart : storyEnd;
    if (end > storyEnd)
        end = storyEnd;
    if (end <= start)
        end = start + 1;

    if (g_settings.highlightCurrentLine && pDoc->pHighlightRange != NULL)
    {
        long oldStart = 0;
        long oldEnd = 0;

        if (SUCCEEDED(pDoc->pHighlightRange->GetStart(&oldStart)) &&
            SUCCEEDED(pDoc->pHighlightRange->GetEnd(&oldEnd)) &&
            oldStart == start && oldEnd == end)
        {
            s_bUpdatingHighlight = FALSE;
            return;
        }
    }

    pTextDoc = EditorGetTextDocument(hEdit);
    oldMask = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    modified = (BOOL)SendMessageW(hEdit, EM_GETMODIFY, 0, 0);

    if (pTextDoc != NULL)
        pTextDoc->Undo(tomSuspend, NULL);

    if (pDoc->pHighlightRange != NULL)
    {
        EditorSetTextRangeBackColor(pDoc->pHighlightRange, tomAutoColor);
        pDoc->pHighlightRange->Release();
        pDoc->pHighlightRange = NULL;
    }

    if (g_settings.highlightCurrentLine && pTextDoc != NULL && end > start)
    {
        if (SUCCEEDED(pTextDoc->Range(start, end, &pRange)) && pRange != NULL)
        {
            if (SUCCEEDED(pRange->GetFont(&pFont)) && pFont != NULL)
            {
                pFont->SetBackColor((long)NOTBAD_HIGHLIGHT_COLOR);
                pFont->Release();
            }
            pDoc->pHighlightRange = pRange;
        }
    }

    if (pTextDoc != NULL)
    {
        pTextDoc->Undo(tomResume, NULL);
        pTextDoc->Release();
    }

    SendMessageW(hEdit, EM_SETMODIFY, modified, 0);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, oldMask);
    s_bUpdatingHighlight = FALSE;
}

void EditorApplyCurrentLineHighlight(void)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
        EditorUpdateCurrentLineHighlight(g_documents[i]->hEdit);
}

// Substitui um trecho sem pintar a selecao intermediaria. Sem isso, o
// Delete/Backspace de indentacao pisca marcando os caracteres antes de apagar.
static void EditorReplaceSilently(HWND hEdit, int start, int end, LPCWSTR lpText)
{
    CHARRANGE cr = { start, end };

    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)lpText);
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
    WCHAR szUnit[64];
    int unitLength;
    std::vector<int> lineStarts;
    std::vector<int> removes;
    std::wstring block;
    int pos;
    int newStart;
    int newEnd;
    DWORD oldMask;
    size_t i;

    unitLength = BuildIndentString(tabWidth, tabWidth, g_settings.indentWithTabs,
                                   szUnit, ARRAYSIZE(szUnit));

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
            block.append(szUnit, (size_t)unitLength);
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
        newStart = start + unitLength;
        newEnd = end + unitLength * (int)lineStarts.size();
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

// Backspace com auto-indent ativo: dentro da indentacao (so espacos/tabs entre
// o inicio da linha e o cursor) recua um nivel inteiro: um Tab, ou os espacos
// ate o multiplo anterior da largura do tab. Fora disso, Backspace normal.
static BOOL EditorHandleBackspace(HWND hEdit)
{
    WCHAR szBefore[512] = {};
    TEXTRANGEW range = {};
    int start;
    int end;
    int paraStart;
    int count;
    int tabWidth;
    int remove;

    if (!g_settings.autoIndent)
        return FALSE;

    if ((GetKeyState(VK_CONTROL) | GetKeyState(VK_MENU)) & 0x8000)
        return FALSE;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start != end || start <= 0)
        return FALSE;

    paraStart = EditorGetParagraphStart(hEdit, start);
    count = start - paraStart;
    if (count <= 0 || count >= (int)ARRAYSIZE(szBefore))
        return FALSE;

    range.chrg.cpMin = paraStart;
    range.chrg.cpMax = start;
    range.lpstrText = szBefore;
    if ((int)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range) != count)
        return FALSE;

    for (int i = 0; i < count; ++i)
    {
        if (szBefore[i] != L' ' && szBefore[i] != L'\t')
            return FALSE;
    }

    if (szBefore[count - 1] == L'\t')
    {
        remove = 1;
    }
    else
    {
        int spaces = 0;

        while (spaces < count && szBefore[count - 1 - spaces] == L' ')
            ++spaces;

        tabWidth = GetTabWidth();
        remove = spaces % tabWidth;
        if (remove == 0)
            remove = tabWidth;
        if (remove > spaces)
            remove = spaces;
    }

    // O WM_CHAR(Backspace) ja foi enfileirado pelo TranslateMessage.
    RemovePendingChar(hEdit, L'\b');

    EditorReplaceSilently(hEdit, start - remove, start, L"");
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Tab sem selecao: insere a indentacao no estilo configurado (espacos por
// padrao), avancando ate o proximo tab stop visual.
static BOOL EditorHandleTabAtCaret(HWND hEdit)
{
    WCHAR szBefore[4096];
    WCHAR szSpaces[64];
    TEXTRANGEW range = {};
    int start;
    int end;
    int paraStart;
    int length;
    int spaces;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    RemovePendingChar(hEdit, L'\t');

    if (g_settings.indentWithTabs)
    {
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\t");
        InvalidateLineNumbers();
        UpdateStatusBar();
        return TRUE;
    }

    GetEditSelection(hEdit, &start, &end);
    paraStart = EditorGetParagraphStart(hEdit, start);
    length = start - paraStart;
    if (length <= 0)
    {
        spaces = GetTabWidth();
    }
    else
    {
        if (length > (int)ARRAYSIZE(szBefore) - 1)
            length = (int)ARRAYSIZE(szBefore) - 1;

        range.chrg.cpMin = paraStart;
        range.chrg.cpMax = paraStart + length;
        range.lpstrText = szBefore;
        SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);
        szBefore[length] = L'\0';

        spaces = GetTabWidth() - (GetIndentColumns(szBefore, length, GetTabWidth()) %
                                 GetTabWidth());
    }

    if (spaces < 1)
        spaces = GetTabWidth();
    if (spaces > (int)ARRAYSIZE(szSpaces) - 1)
        spaces = (int)ARRAYSIZE(szSpaces) - 1;

    for (int i = 0; i < spaces; ++i)
        szSpaces[i] = L' ';
    szSpaces[spaces] = L'\0';

    SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)szSpaces);
    InvalidateLineNumbers();
    UpdateStatusBar();
    return TRUE;
}

// Delete para frente dentro da indentacao feita com espacos: consome os
// espacos ate o proximo nivel, como o Delete faz com um tab. So vale quando
// o auto-indent esta ligado e o estilo configurado e espacos.
static int GetForwardIndentDeleteCount(HWND hEdit, int start)
{
    WCHAR szPrefix[512];
    TEXTRANGEW range = {};
    int paraStart;
    int column;
    int remove;
    int spaces;
    int tabWidth = GetTabWidth();

    if (!g_settings.autoIndent || g_settings.indentWithTabs)
        return 0;

    paraStart = EditorGetParagraphStart(hEdit, start);
    column = start - paraStart;
    if (column < 0 || column >= (int)ARRAYSIZE(szPrefix))
        return 0;

    if (column > 0)
    {
        int i;

        range.chrg.cpMin = paraStart;
        range.chrg.cpMax = start;
        range.lpstrText = szPrefix;
        if ((int)SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range) != column)
            return 0;

        for (i = 0; i < column; ++i)
        {
            if (szPrefix[i] != L' ')
                return 0;
        }
    }

    remove = tabWidth - (column % tabWidth);
    if (remove <= 0)
        remove = tabWidth;

    spaces = 0;
    while (spaces < remove && GetEditCharAt(hEdit, start + spaces) == L' ')
        ++spaces;

    return spaces;
}

// Delete explicito: o RichEdit deixa de apagar o caractere a direita quando
// a tecla e consumida por outros caminhos. Aqui a acao e garantida tanto
// para selecao quanto para o caractere sob o cursor.
static BOOL EditorHandleDelete(HWND hEdit)
{
    int start;
    int end;
    int length;
    int deleteEnd;
    int indentDelete;
    WCHAR ch;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    GetEditSelection(hEdit, &start, &end);
    if (start < 0)
        start = 0;

    if (start == end)
    {
        indentDelete = GetForwardIndentDeleteCount(hEdit, start);
        if (indentDelete > 0)
        {
            deleteEnd = start + indentDelete;
        }
        else
        {
            GETTEXTLENGTHEX gtl = {};
            gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
            gtl.codepage = 1200;
            length = (int)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
            if (length <= start)
                return TRUE;

            deleteEnd = start + 1;
            ch = GetEditCharAt(hEdit, start);
            if (ch >= 0xD800 && ch <= 0xDBFF)
            {
                WCHAR next = GetEditCharAt(hEdit, start + 1);
                if (next >= 0xDC00 && next <= 0xDFFF)
                    deleteEnd = start + 2;
            }
        }
    }
    else
    {
        deleteEnd = end;
    }

    EditorReplaceSilently(hEdit, start, deleteEnd, L"");
    InvalidateLineNumbers();
    UpdateStatusBar();
    UpdateEditMenuState(g_hWndMain);
    return TRUE;
}

BOOL EditorHandleKeyDown(HWND hEdit, WPARAM wParam)
{
    BOOL shift;
    BOOL ctrl;
    BOOL alt;
    int start;
    int end;

    if (hEdit == NULL)
        return FALSE;

    if (wParam == VK_RETURN)
        return EditorHandleEnter(hEdit);

    if (wParam == VK_HOME)
        return EditorHandleHome(hEdit);

    if (wParam == VK_BACK)
        return EditorHandleBackspace(hEdit);

    if (wParam == VK_DELETE)
    {
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Deixa Ctrl+Delete/Alt+Delete para o RichEdit (apagar palavra etc.).
        if (ctrl || alt)
            return FALSE;

        // Shift+Delete e o atalho padrao de recortar: precisa copiar para a
        // area de transferencia antes de remover a selecao.
        if (shift)
        {
            GetEditSelection(hEdit, &start, &end);
            if (start == end)
                return TRUE;

            SendMessageW(hEdit, WM_CUT, 0, 0);
            InvalidateLineNumbers();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        return EditorHandleDelete(hEdit);
    }

    if (wParam == VK_INSERT)
    {
        ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (alt)
            return FALSE;

        // Shift+Insert cola e Ctrl+Insert copia (atalhos padrao de teclado).
        if (shift && !ctrl)
        {
            SendMessageW(hEdit, WM_PASTE, 0, 0);
            InvalidateLineNumbers();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        if (ctrl && !shift)
        {
            SendMessageW(hEdit, WM_COPY, 0, 0);
            UpdateEditMenuState(g_hWndMain);
            return TRUE;
        }

        // Insert sozinho alterna Insert/Overwrite e troca a forma do caret.
        if (!shift && !ctrl)
        {
            Document* pDoc = DocFromEdit(hEdit);

            if (pDoc != NULL)
            {
                pDoc->bOverwrite = !pDoc->bOverwrite;
                EditorUpdateCaretShape(hEdit);
            }
            return TRUE;
        }

        return FALSE;
    }

    if (wParam != VK_TAB)
        return FALSE;

    if (GetWindowLongPtrW(hEdit, GWL_STYLE) & ES_READONLY)
        return FALSE;

    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    GetEditSelection(hEdit, &start, &end);

    if (!shift && start == end)
        return EditorHandleTabAtCaret(hEdit);

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
