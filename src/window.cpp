#include "notbad.h"
#include <commctrl.h>
#include <richedit.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shobjidl.h>
#include <algorithm>
#include <string>
#include <vector>

// Depois de um Undo o RichEdit costuma restaurar a selecao do trecho desfeito.
// O usuario espera ver o texto restaurado normal, com o cursor no inicio.
static void CollapseSelectionAfterUndo(HWND hEdit)
{
    CHARRANGE cr = {};

    if (hEdit == NULL)
        return;

    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax > cr.cpMin)
        SendMessageW(hEdit, EM_SETSEL, cr.cpMin, cr.cpMin);
}

static int GetTabBarHeight(void)
{
    RECT rc;

    if (g_hWndTabs == NULL)
        return 0;

    if (TabCtrl_GetItemCount(g_hWndTabs) > 0 &&
        TabCtrl_GetItemRect(g_hWndTabs, 0, &rc))
    {
        return rc.bottom + 4;
    }

    return 24;
}

static int GetLineNumberWidth(void)
{
    int lines = 1;
    int digits = 1;
    HDC hdc;
    HGDIOBJ oldFont;
    WCHAR szSample[16];
    SIZE size = {};
    int width;
    int i;

    if (g_hWndEdit != NULL)
    {
        // Conta paragrafos, nao linhas fisicas: com word wrap o total fisico
        // pode ser muito maior e alargaria o gutter sem necessidade.
        lines = EditorGetParagraphCount(g_hWndEdit);
    }
    if (lines < 1)
        lines = 1;

    while (lines >= 10)
    {
        lines /= 10;
        ++digits;
    }
    if (digits < 2)
        digits = 2;

    hdc = GetDC(g_hWndMain);
    if (hdc == NULL)
        return 40;

    oldFont = SelectObject(hdc, g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                : GetStockObject(SYSTEM_FIXED_FONT));

    for (i = 0; i < digits; ++i)
        szSample[i] = L'9';
    szSample[digits] = L'\0';
    GetTextExtentPoint32W(hdc, szSample, digits, &size);

    SelectObject(hdc, oldFont);
    ReleaseDC(g_hWndMain, hdc);

    width = size.cx + 12;
    if (width < 32)
        width = 32;
    return width;
}

#define LINE_NUMBER_SCROLL_TIMER   0x4C4E
#define LINE_NUMBER_SCROLL_POLL_MS 30

static int g_lineNumberScrollY = 0;
static HFONT g_hFontBlock = NULL;

static void ResetLineNumberBlockFont(void)
{
    if (g_hFontBlock != NULL)
    {
        DeleteObject(g_hFontBlock);
        g_hFontBlock = NULL;
    }
}

static HFONT EnsureLineNumberBlockFont(void)
{
    LOGFONTW lf = {};

    if (g_hFontBlock != NULL)
        return g_hFontBlock;

    if (g_hFont == NULL)
        return NULL;

    if (GetObjectW(g_hFont, sizeof(lf), &lf) != sizeof(lf))
        return NULL;

    lf.lfWeight = FW_BOLD;
    g_hFontBlock = CreateFontIndirectW(&lf);
    return g_hFontBlock;
}

// O RichEdit faz scroll suave (roda do mouse, auto-scroll ao digitar, Page
// Down etc.): a posicao so muda algum tempo depois do evento. Um timer curto
// compara continuamente a posicao vertical de scroll e repinta o gutter
// durante a animacao, sem depender do instante exato em que cada
// mensagem/notificacao chega. EM_GETSCROLLPOS e em pixels e detecta tanto o
// deslocamento parcial quanto o de uma linha inteira.
static int GetEditScrollY(HWND hEdit)
{
    POINT pt = { 0, 0 };

    if (hEdit == NULL)
        return 0;

    SendMessageW(hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
    return (int)pt.y;
}

#define TREE_SPLITTER_WIDTH  5
#define TREE_MIN_WIDTH       100
#define EDITOR_MIN_WIDTH     150

static BOOL g_bTreeDrag = FALSE;
static int  g_treeDragOffset = 0;

// Arrasto de item do tree com o mouse (ver TVN_BEGINDRAG). O alvo de soltura
// valido fica em g_bTreeDragDropItem (NULL = nenhum ou invalido).
static BOOL        g_bTreeItemDrag = FALSE;
static HTREEITEM   g_bTreeDragDropItem = NULL;
static HIMAGELIST  g_hTreeDragImage = NULL;
static std::wstring g_treeItemDragSrc;

static BOOL IsTreeVisible(void)
{
    return g_hWndTree != NULL &&
           (GetWindowLongW(g_hWndTree, GWL_STYLE) & WS_VISIBLE) != 0;
}

static int ClampTreeWidth(int width, int clientWidth)
{
    int maxWidth = clientWidth - TREE_SPLITTER_WIDTH - EDITOR_MIN_WIDTH;

    if (width > maxWidth)
        width = maxWidth;
    if (width < TREE_MIN_WIDTH)
        width = TREE_MIN_WIDTH;
    return width;
}

// Area da barra de divisao (coordenadas do cliente da janela principal).
static BOOL GetTreeSplitterRect(RECT* prc)
{
    RECT rcTree;

    if (!IsTreeVisible() || g_hWndMain == NULL)
        return FALSE;

    GetWindowRect(g_hWndTree, &rcTree);
    MapWindowPoints(NULL, g_hWndMain, (POINT*)&rcTree, 2);

    prc->left   = rcTree.right;
    prc->right  = rcTree.right + TREE_SPLITTER_WIDTH;
    prc->top    = rcTree.top;
    prc->bottom = rcTree.bottom;
    return TRUE;
}

// Move/redimensiona um filho dentro de um lote. Se o lote nao existe (ou
// falhou), cai para SetWindowPos direto.
//
// SWP_NOCOPYBITS: sem isso o Windows copia a imagem antiga do cliente para a
// nova posicao e so invalida a parte nova. No RichEdit (que refaz a quebra de
// linha ao mudar de largura) e no gutter isso deixava "fantasmas" do conteudo
// anterior durante o arrasto do splitter da TreeView; com a flag cada filho
// invalida e repinta o cliente inteiro, uma vez por movimento.
static HDWP DeferMove(HDWP hdwp, HWND hWnd, int x, int y, int cx, int cy)
{
    const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;

    if (hdwp == NULL)
    {
        SetWindowPos(hWnd, NULL, x, y, cx, cy, flags);
        return NULL;
    }

    return DeferWindowPos(hdwp, hWnd, NULL, x, y, cx, cy, flags);
}

// Barra de status: Ln/Col | auto-indent | codificacao | porcentagem.
// A parte de Ln/Col ocupa o espaco que sobra; as demais tem largura fixa
// (em pixels a 96 DPI, escalada pelo DPI da tela) ancorada a direita.
#define STATUS_INDENT_WIDTH   90
#define STATUS_ENCODING_WIDTH 110
#define STATUS_PERCENT_WIDTH  70

static int s_statusPartsWidth = -1;

static void UpdateStatusBarParts(int width)
{
    int parts[4];
    int dpi = 96;
    int minFirst;
    HDC hdc;
    int i;

    if (g_hWndStatus == NULL || width <= 0 || width == s_statusPartsWidth)
        return;

    hdc = GetDC(NULL);
    if (hdc != NULL)
    {
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
    }
    if (dpi <= 0)
        dpi = 96;

    parts[3] = -1;
    parts[2] = width - MulDiv(STATUS_PERCENT_WIDTH, dpi, 96);
    parts[1] = parts[2] - MulDiv(STATUS_ENCODING_WIDTH, dpi, 96);
    parts[0] = parts[1] - MulDiv(STATUS_INDENT_WIDTH, dpi, 96);

    // Janela estreita: Ln/Col mantem ao menos um terco e as outras encolhem.
    minFirst = width / 3;
    if (parts[0] < minFirst)
        parts[0] = minFirst;

    for (i = 1; i < 3; ++i)
    {
        if (parts[i] < parts[i - 1])
            parts[i] = parts[i - 1];
        if (parts[i] > width)
            parts[i] = width;
    }

    SendMessageW(g_hWndStatus, SB_SETPARTS, 4, (LPARAM)parts);
    s_statusPartsWidth = width;
}

static void LayoutChildren(HWND hWnd)
{
    RECT rc;
    int statusHeight = 0;
    int tabHeight = 0;
    int treeWidth = 0;
    int gutterWidth = 0;
    int contentBottom;
    size_t i;

    GetClientRect(hWnd, &rc);

    if (g_hWndStatus != NULL && g_settings.statusBar)
    {
        RECT rcStatus;
        GetWindowRect(g_hWndStatus, &rcStatus);
        statusHeight = rcStatus.bottom - rcStatus.top;

        if (!g_bTreeDrag)
        {
            SendMessageW(g_hWndStatus, WM_SIZE, 0, 0);
            SetWindowPos(g_hWndStatus, NULL, 0, rc.bottom - statusHeight,
                         rc.right, statusHeight, SWP_NOZORDER);
            UpdateStatusBarParts(rc.right);
        }
    }

    tabHeight = GetTabBarHeight();
    if (g_hWndTabs != NULL && !g_bTreeDrag)
    {
        SetWindowPos(g_hWndTabs, NULL, 0, 0, rc.right, tabHeight,
                     SWP_NOZORDER);
    }

    contentBottom = rc.bottom - statusHeight;
    if (contentBottom < tabHeight)
        contentBottom = tabHeight;

    if (IsTreeVisible())
        treeWidth = ClampTreeWidth(g_settings.treeWidth, rc.right) +
                    TREE_SPLITTER_WIDTH;

    if (g_hWndLineNumbers != NULL && g_settings.showLineNumbers)
        gutterWidth = GetLineNumberWidth();

    int contentHeight = contentBottom - tabHeight;
    int editorX = treeWidth + gutterWidth;
    int editorWidth = rc.right - editorX;

    if (contentHeight < 0)
        contentHeight = 0;
    if (editorWidth < 0)
        editorWidth = 0;

    HDWP hdwp = BeginDeferWindowPos((int)g_documents.size() + 2);

    if (g_hWndTree != NULL)
    {
        hdwp = DeferMove(hdwp, g_hWndTree, 0, tabHeight,
                         treeWidth > 0 ? treeWidth - TREE_SPLITTER_WIDTH : 0,
                         contentHeight);
    }

    if (g_hWndLineNumbers != NULL)
    {
        hdwp = DeferMove(hdwp, g_hWndLineNumbers, treeWidth, tabHeight,
                         gutterWidth, contentHeight);
    }

    for (i = 0; i < g_documents.size(); ++i)
    {
        // Durante o arrasto do splitter so a aba visivel e redimensionada;
        // as ocultas sao ajustadas ao soltar o mouse ou em DocActivate.
        if (g_bTreeDrag && g_documents[i] != g_pActiveDoc)
            continue;

        hdwp = DeferMove(hdwp, g_documents[i]->hEdit, editorX, tabHeight,
                         editorWidth, contentHeight);
    }

    if (hdwp != NULL)
        EndDeferWindowPos(hdwp);

    // Durante o arrasto do splitter, pinta TreeView, gutter e editor no mesmo
    // instante (em vez de cada um no seu proprio WM_PAINT, em momentos
    // diferentes), para um nao ficar atras do outro e parecer piscar.
    if (g_bTreeDrag)
    {
        RedrawWindow(hWnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

void LayoutMainWindow(void)
{
    LayoutChildren(g_hWndMain);
}

static void CreateStatusBar(HWND hWnd)
{
    int parts[4];

    g_hWndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, NULL,
                                   WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                   0, 0, 0, 0,
                                   hWnd, (HMENU)(INT_PTR)ID_STATUS,
                                   g_hInstance, NULL);

    if (g_hWndStatus == NULL)
        return;

    // Larguras reais sao definidas por UpdateStatusBarParts (LayoutChildren).
    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;
    parts[3] = -1;
    SendMessageW(g_hWndStatus, SB_SETPARTS, 4, (LPARAM)parts);

    if (g_hFontStatus != NULL)
        SendMessageW(g_hWndStatus, WM_SETFONT, (WPARAM)g_hFontStatus, TRUE);

    ShowWindow(g_hWndStatus, g_settings.statusBar ? SW_SHOW : SW_HIDE);
}

// ---------------------------------------------------------------------------
// Barra de abas: botao de fechar em cada aba e reordenacao por arrastar
// ---------------------------------------------------------------------------
#define TAB_CLOSE_SIZE    14
#define TAB_CLOSE_MARGIN  5

static WCHAR g_szTabPad[16] = L"    ";
static int   g_tabHotClose   = -1;
static int   g_tabPressClose = -1;
static int   g_tabDragIndex  = -1;
static BOOL  g_tabDragging   = FALSE;
static BOOL  g_tabTracking   = FALSE;
static POINT g_tabDownPt     = {};

static int TabScale(int value)
{
    static int s_dpi = 0;

    if (s_dpi == 0)
    {
        HDC hdc = GetDC(NULL);
        s_dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc != NULL)
            ReleaseDC(NULL, hdc);
        if (s_dpi <= 0)
            s_dpi = 96;
    }
    return MulDiv(value, s_dpi, 96);
}

// Calcula quantos espacos acrescentar ao titulo para reservar o botao.
static void TabUpdatePadding(void)
{
    HDC hdc = g_hWndTabs != NULL ? GetDC(g_hWndTabs) : NULL;
    int need = TabScale(TAB_CLOSE_SIZE + TAB_CLOSE_MARGIN + 3);
    int n = 4;
    int i;

    if (hdc != NULL)
    {
        HGDIOBJ oldFont = SelectObject(hdc, g_hFontMenu != NULL ? (HGDIOBJ)g_hFontMenu
                                                                : (HGDIOBJ)g_hFontStatus);
        SIZE size = {};

        if (GetTextExtentPoint32W(hdc, L" ", 1, &size) && size.cx > 0)
            n = (need + size.cx - 1) / size.cx;

        SelectObject(hdc, oldFont);
        ReleaseDC(g_hWndTabs, hdc);
    }

    if (n < 1)
        n = 1;
    if (n > (int)ARRAYSIZE(g_szTabPad) - 1)
        n = (int)ARRAYSIZE(g_szTabPad) - 1;

    for (i = 0; i < n; ++i)
        g_szTabPad[i] = L' ';
    g_szTabPad[n] = L'\0';
}

LPCWSTR TabGetClosePadding(void)
{
    return g_szTabPad;
}

static BOOL TabGetCloseRect(HWND hTabs, int index, RECT* prc)
{
    RECT ri;
    int size = TabScale(TAB_CLOSE_SIZE);

    if (!TabCtrl_GetItemRect(hTabs, index, &ri))
        return FALSE;

    prc->right  = ri.right - TabScale(TAB_CLOSE_MARGIN);
    prc->left   = prc->right - size;
    prc->top    = ri.top + ((ri.bottom - ri.top) - size) / 2 + TabScale(2);
    prc->bottom = prc->top + size;
    return TRUE;
}

static int TabHitClose(HWND hTabs, POINT pt)
{
    TCHITTESTINFO hti = {};
    RECT rc;
    int index;

    hti.pt = pt;
    index = TabCtrl_HitTest(hTabs, &hti);
    if (index >= 0 && TabGetCloseRect(hTabs, index, &rc) && PtInRect(&rc, pt))
        return index;

    return -1;
}

static void TabInvalidateClose(HWND hTabs, int index)
{
    RECT rc;

    if (index >= 0 && TabGetCloseRect(hTabs, index, &rc))
    {
        InflateRect(&rc, 1, 1);
        InvalidateRect(hTabs, &rc, FALSE);
    }
}

static void TabSetHot(HWND hTabs, int index)
{
    if (index == g_tabHotClose)
        return;

    TabInvalidateClose(hTabs, g_tabHotClose);
    g_tabHotClose = index;
    TabInvalidateClose(hTabs, g_tabHotClose);
}

static void TabDrawClose(HWND hTabs, HDC hdc, int index, const RECT* prc)
{
    BOOL selected = (TabCtrl_GetCurSel(hTabs) == index);
    BOOL hot      = (g_tabHotClose == index);
    BOOL pressed  = hot && (g_tabPressClose == index);
    int  inset    = TabScale(4);
    int  d;
    HPEN pen;
    HGDIOBJ oldPen;

    if (hot || pressed)
    {
        HBRUSH hbr = CreateSolidBrush(pressed ? RGB(190, 190, 190) : RGB(222, 222, 222));
        FillRect(hdc, prc, hbr);
        DeleteObject(hbr);
    }

    pen = CreatePen(PS_SOLID, 1,
                    GetSysColor((selected || hot) ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    oldPen = SelectObject(hdc, pen);

    for (d = 0; d < 2; ++d)
    {
        MoveToEx(hdc, prc->left + inset + d, prc->top + inset, NULL);
        LineTo(hdc, prc->right - inset + d, prc->bottom - inset);
        MoveToEx(hdc, prc->left + inset + d, prc->bottom - inset - 1, NULL);
        LineTo(hdc, prc->right - inset + d, prc->top + inset - 1);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void TabDrawAllClose(HWND hTabs, HDC hdc)
{
    RECT rcClient, rcUpDown = {}, rc, rcTmp;
    HWND hUpDown;
    BOOL hasUpDown = FALSE;
    int count = TabCtrl_GetItemCount(hTabs);
    int i;

    GetClientRect(hTabs, &rcClient);

    // Quando as abas excedem a largura, o controle mostra setas de rolagem;
    // nao desenhar o botao por cima delas.
    hUpDown = FindWindowExW(hTabs, NULL, L"msctls_updown32", NULL);
    if (hUpDown != NULL && IsWindowVisible(hUpDown))
    {
        GetWindowRect(hUpDown, &rcUpDown);
        MapWindowPoints(NULL, hTabs, (POINT*)&rcUpDown, 2);
        hasUpDown = TRUE;
    }

    for (i = 0; i < count; ++i)
    {
        if (!TabGetCloseRect(hTabs, i, &rc))
            continue;
        if (rc.left < rcClient.left || rc.right > rcClient.right)
            continue;
        if (hasUpDown && IntersectRect(&rcTmp, &rc, &rcUpDown))
            continue;

        TabDrawClose(hTabs, hdc, i, &rc);
    }
}

// Reordena enquanto o botao esta pressionado: troca de posicao com a aba sob
// o cursor assim que o cursor alcanca a area que a aba arrastada passaria a ocupar.
static void TabDragTo(HWND hTabs, POINT pt)
{
    TCHITTESTINFO hti = {};
    RECT rcTarget, rcDrag;
    int count = TabCtrl_GetItemCount(hTabs);
    int target;
    BOOL ok;

    if (count < 2 || g_tabDragIndex < 0 || g_tabDragIndex >= count)
        return;
    if (!TabCtrl_GetItemRect(hTabs, g_tabDragIndex, &rcDrag))
        return;

    hti.pt = pt;
    target = TabCtrl_HitTest(hTabs, &hti);

    if (target < 0)
    {
        // Cursor fora das abas: encostar nas extremidades.
        RECT rcFirst, rcLast;

        if (TabCtrl_GetItemRect(hTabs, 0, &rcFirst) && pt.x < rcFirst.left)
            target = 0;
        else if (TabCtrl_GetItemRect(hTabs, count - 1, &rcLast) && pt.x > rcLast.right)
            target = count - 1;
        else
            return;

        ok = (target != g_tabDragIndex);
    }
    else
    {
        int dragWidth = rcDrag.right - rcDrag.left;

        if (target == g_tabDragIndex || !TabCtrl_GetItemRect(hTabs, target, &rcTarget))
            return;

        ok = (target > g_tabDragIndex) ? (pt.x >= rcTarget.right - dragWidth)
                                       : (pt.x <= rcTarget.left + dragWidth);
    }

    if (ok)
    {
        DocMove(g_tabDragIndex, target);
        g_tabDragIndex = target;
        g_tabHotClose = -1;
        InvalidateRect(hTabs, NULL, FALSE);
    }
}

static LRESULT CALLBACK TabsSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)dwRefData;

    switch (msg)
    {
    case WM_PAINT:
    case WM_PRINTCLIENT:
    {
        LRESULT result = DefSubclassProc(hWnd, msg, wParam, lParam);
        HDC hdc = wParam != 0 ? (HDC)wParam : GetDC(hWnd);

        if (hdc != NULL)
        {
            TabDrawAllClose(hWnd, hdc);
            if (wParam == 0)
                ReleaseDC(hWnd, hdc);
        }
        return result;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        int closeIdx = TabHitClose(hWnd, pt);

        if (closeIdx >= 0)
        {
            g_tabPressClose = closeIdx;
            g_tabHotClose = closeIdx;
            SetCapture(hWnd);
            TabInvalidateClose(hWnd, closeIdx);
            return 0;
        }

        {
            TCHITTESTINFO hti = {};
            hti.pt = pt;
            g_tabDragIndex = TabCtrl_HitTest(hWnd, &hti);
            g_tabDragging = FALSE;
            g_tabDownPt = pt;
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

        if (g_tabDragIndex >= 0 && (wParam & MK_LBUTTON))
        {
            int dx = pt.x > g_tabDownPt.x ? pt.x - g_tabDownPt.x
                                          : g_tabDownPt.x - pt.x;

            if (!g_tabDragging && dx >= GetSystemMetrics(SM_CXDRAG))
            {
                g_tabDragging = TRUE;
                SetCapture(hWnd);
                SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
            }

            if (g_tabDragging)
            {
                TabDragTo(hWnd, pt);
                return 0;
            }
        }

        {
            int hot = TabHitClose(hWnd, pt);
            TabSetHot(hWnd, hot);

            if (!g_tabTracking)
            {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
                if (TrackMouseEvent(&tme))
                    g_tabTracking = TRUE;
            }
        }
        break;
    }

    case WM_SETCURSOR:
        // Cursor de "mover" enquanto a aba esta sendo arrastada.
        if (g_tabDragging)
        {
            SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;

    case WM_MOUSELEAVE:
        g_tabTracking = FALSE;
        TabSetHot(hWnd, -1);
        break;

    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

        if (g_tabPressClose >= 0)
        {
            int index = g_tabPressClose;
            BOOL hit = (TabHitClose(hWnd, pt) == index);

            ReleaseCapture();   // WM_CAPTURECHANGED limpa o estado
            if (hit)
                PostMessageW(GetParent(hWnd), WM_NOTBAD_CLOSETAB, (WPARAM)index, 0);
            return 0;
        }

        if (g_tabDragging)
            ReleaseCapture();

        g_tabDragIndex = -1;
        g_tabDragging = FALSE;
        break;
    }

    case WM_CAPTURECHANGED:
        g_tabPressClose = -1;
        g_tabDragIndex = -1;
        g_tabDragging = FALSE;
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, TabsSubclassProc, uIdSubclass);
        break;
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void CreateTabControl(HWND hWnd)
{
    g_hWndTabs = CreateWindowExW(0, WC_TABCONTROLW, NULL,
                                 WS_CHILD | WS_VISIBLE | TCS_TOOLTIPS | TCS_HOTTRACK,
                                 0, 0, 0, 0,
                                 hWnd, (HMENU)(INT_PTR)ID_TABS,
                                 g_hInstance, NULL);

    if (g_hWndTabs == NULL)
        return;

    SendMessageW(g_hWndTabs, WM_SETFONT,
                 (WPARAM)(g_hFontMenu != NULL ? g_hFontMenu : g_hFontStatus), TRUE);
    TabUpdatePadding();
    SetWindowSubclass(g_hWndTabs, TabsSubclassProc, 1, 0);
}

// Repintar o gutter a cada tecla e desnecessario: os numeros so mudam
// quando a quantidade de linhas do RichEdit muda (quebra/insercao de linha).
static void InvalidateLineNumbersOnEdit(HWND hEdit)
{
    static HWND s_hLastEdit = NULL;
    static int  s_lastCount = -1;
    int count;

    if (hEdit == NULL)
        return;

    count = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0);
    if (hEdit != s_hLastEdit || count != s_lastCount)
    {
        s_hLastEdit = hEdit;
        s_lastCount = count;
        InvalidateLineNumbers();
    }
}

LRESULT CALLBACK LineNumbersWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        HDC hdcMem;
        HBITMAP hbmMem;
        HGDIOBJ oldBmp;

        GetClientRect(hWnd, &rc);

        // Double buffering: o gutter repinta a cada scroll e o flicker do
        // RichEdit ao lado fica muito visivel sem isso.
        hdcMem = CreateCompatibleDC(hdc);
        hbmMem = hdcMem != NULL ? CreateCompatibleBitmap(hdc, rc.right, rc.bottom) : NULL;
        if (hdcMem == NULL || hbmMem == NULL)
        {
            if (hbmMem != NULL)
                DeleteObject(hbmMem);
            if (hdcMem != NULL)
                DeleteDC(hdcMem);
            hdcMem = hdc;
            oldBmp = NULL;
        }
        else
        {
            oldBmp = SelectObject(hdcMem, hbmMem);
        }

        FillRect(hdcMem, &rc, (HBRUSH)(COLOR_BTNFACE + 1));

        if (g_hWndEdit != NULL)
        {
            HGDIOBJ hNormalFont = g_hFont != NULL ? (HGDIOBJ)g_hFont
                                                  : GetStockObject(SYSTEM_FIXED_FONT);
            HFONT hBlockFont = EnsureLineNumberBlockFont();
            HGDIOBJ oldFont = SelectObject(hdcMem, hNormalFont);
            TEXTMETRICW tm;
            RECT rcEdit;
            int lineHeight;
            int lineCount;
            int firstLine = 0;
            int start;
            int line;
            int logical = 0;
            int zoomNum = 0;
            int zoomDen = 0;

            SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, GetSysColor(COLOR_GRAYTEXT));

            GetTextMetricsW(hdcMem, &tm);
            lineHeight = tm.tmHeight + tm.tmExternalLeading;
            if (lineHeight <= 0)
                lineHeight = 16;

            SendMessageW(g_hWndEdit, EM_GETRECT, 0, (LPARAM)&rcEdit);

            // Zoom do conteudo: a altura de cada linha do texto acompanha o zoom,
            // mas a fonte do gutter nao. A altura real da linha e usada abaixo
            // para centralizar o numero verticalmente.
            SendMessageW(g_hWndEdit, EM_GETZOOM, (WPARAM)&zoomNum, (LPARAM)&zoomDen);
            if (zoomNum <= 0 || zoomDen <= 0)
            {
                zoomNum = 1;
                zoomDen = 1;
            }

            // RichEdit: lParam de EM_CHARFROMPOS e um ponteiro para POINTL
            // (MAKELPARAM, como no Edit, vira ponteiro invalido e causa AV).
            POINTL ptTop;
            ptTop.x = rcEdit.left;
            ptTop.y = rcEdit.top + 1;
            LRESULT charIndex = SendMessageW(g_hWndEdit, EM_CHARFROMPOS, 0, (LPARAM)&ptTop);

            // O RichEdit tem borda (WS_EX_CLIENTEDGE): converte o Y do cliente
            // dele para o Y do cliente do gutter.
            POINT origin = { 0, 0 };
            ClientToScreen(g_hWndEdit, &origin);
            ScreenToClient(hWnd, &origin);
            if ((int)charIndex >= 0)
                firstLine = (int)SendMessageW(g_hWndEdit, EM_LINEFROMCHAR, charIndex, 0);
            if (firstLine < 0)
                firstLine = 0;

            lineCount = (int)SendMessageW(g_hWndEdit, EM_GETLINECOUNT, 0, 0);
            start = firstLine > 1 ? firstLine - 1 : 0;

            // O numero de cada linha logica fica centralizado verticalmente no
            // bloco inteiro dela (todas as linhas visuais do word wrap), nao so
            // na primeira. Cada numero e pintado quando o bloco termina, ou seja,
            // ao chegar ao inicio da proxima linha logica (ou ao fim da area).
            // Se parte do bloco esta fora da tela, centraliza na parte visivel
            // para o numero nao sumir enquanto a linha esta na tela.
            int pendingLogical = 0;
            int pendingTop = 0;
            int endBottom = 0;
            const int fallbackRow = MulDiv(lineHeight, zoomNum, zoomDen);

            auto flushNumber = [&](int blockBottom)
            {
                WCHAR szNumber[32];
                RECT rcNumber;
                int top = pendingTop;
                int bottom = blockBottom;

                if (pendingLogical <= 0)
                    return;

                if (bottom > 0 && top < rc.bottom)
                {
                    if (top < 0)
                        top = 0;
                    if (bottom > rc.bottom)
                        bottom = rc.bottom;

                    StringCchPrintfW(szNumber, ARRAYSIZE(szNumber), L"%d", pendingLogical);
                    rcNumber.left = rc.left;
                    rcNumber.top = top;
                    rcNumber.right = rc.right - 5;
                    rcNumber.bottom = bottom;

                    if (g_settings.autoIndent &&
                        g_pActiveDoc != NULL &&
                        g_pActiveDoc->hEdit == g_hWndEdit &&
                        g_pActiveDoc->bIndentBlockValid &&
                        pendingLogical >= g_pActiveDoc->indentBlockFirst &&
                        pendingLogical <= g_pActiveDoc->indentBlockLast)
                    {
                        if (hBlockFont != NULL)
                            SelectObject(hdcMem, hBlockFont);
                        SetTextColor(hdcMem, GetSysColor(COLOR_WINDOWTEXT));
                    }
                    else
                    {
                        SelectObject(hdcMem, hNormalFont);
                        SetTextColor(hdcMem, GetSysColor(COLOR_GRAYTEXT));
                    }

                    DrawTextW(hdcMem, szNumber, -1, &rcNumber,
                              DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                }

                pendingLogical = 0;
            };

            for (line = start; line < lineCount; ++line)
            {
                int index = (int)SendMessageW(g_hWndEdit, EM_LINEINDEX, line, 0);
                int y;
                BOOL isContinuation = FALSE;

                if (index < 0)
                    break;

                // Continuacao de word wrap: o caractere anterior nao e
                // quebra de linha; a linha visual nao recebe numero proprio.
                if (index > 0)
                {
                    TEXTRANGEW tr = {};
                    WCHAR szPrev[2] = {};
                    tr.chrg.cpMin = index - 1;
                    tr.chrg.cpMax = index;
                    tr.lpstrText = szPrev;
                    if (SendMessageW(g_hWndEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr) &&
                        szPrev[0] != L'\r' && szPrev[0] != L'\n')
                    {
                        isContinuation = TRUE;
                    }
                }

                if (line == start)
                {
                    logical = EditorGetParagraphNumber(g_hWndEdit, index);
                    if (logical <= 0)
                        logical = line + 1;
                }
                else if (!isContinuation)
                {
                    ++logical;
                }

                POINTL ptLine = { 0, 0 };

                // RichEdit: wParam = POINTL* de saida, lParam = indice do caractere.
                SendMessageW(g_hWndEdit, EM_POSFROMCHAR, (WPARAM)&ptLine, (LPARAM)index);

                y = (int)ptLine.y + (int)origin.y;
                if (y > rc.bottom)
                {
                    endBottom = y;
                    break;
                }
                endBottom = y + fallbackRow;

                if (!isContinuation)
                {
                    // Inicio de uma nova linha logica: fecha o bloco anterior.
                    flushNumber(y);
                    pendingLogical = logical;
                    pendingTop = y;
                }
                else if (line == start)
                {
                    // Primeira linha visual da area e continuacao de uma linha
                    // logica que comecou acima: o bloco visivel comeca aqui.
                    pendingLogical = logical;
                    pendingTop = y;
                }
            }

            flushNumber(endBottom);

            SelectObject(hdcMem, oldFont);
        }

        HPEN pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        if (pen != NULL)
        {
            HGDIOBJ oldPen = SelectObject(hdcMem, pen);
            MoveToEx(hdcMem, rc.right - 1, 0, NULL);
            LineTo(hdcMem, rc.right - 1, rc.bottom);
            SelectObject(hdcMem, oldPen);
            DeleteObject(pen);
        }

        if (hdcMem != hdc)
        {
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, oldBmp);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);
        }

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == LINE_NUMBER_SCROLL_TIMER)
        {
            int scrollY = GetEditScrollY(g_hWndEdit);

            if (scrollY != g_lineNumberScrollY)
            {
                g_lineNumberScrollY = scrollY;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_SETFONT:
        ResetLineNumberBlockFont();
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void StartLineNumbersScrollTimer(void)
{
    if (g_hWndLineNumbers == NULL || !IsWindow(g_hWndLineNumbers))
        return;
    if (!g_settings.showLineNumbers)
        return;

    g_lineNumberScrollY = GetEditScrollY(g_hWndEdit);
    SetTimer(g_hWndLineNumbers, LINE_NUMBER_SCROLL_TIMER,
             LINE_NUMBER_SCROLL_POLL_MS, NULL);
}

static void CreateLineNumbers(HWND hWnd)
{
    g_hWndLineNumbers = CreateWindowExW(0, NOTBAD_LINENUMBERS_CLASS, NULL,
                                        WS_CHILD | WS_CLIPSIBLINGS,
                                        0, 0, 0, 0,
                                        hWnd, NULL, g_hInstance, NULL);
    if (g_hWndLineNumbers == NULL)
        return;

    if (g_hFont != NULL)
        SendMessageW(g_hWndLineNumbers, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    ShowWindow(g_hWndLineNumbers, g_settings.showLineNumbers ? SW_SHOW : SW_HIDE);
    if (g_settings.showLineNumbers)
        StartLineNumbersScrollTimer();
}

void InvalidateLineNumbers(void)
{
    if (g_hWndLineNumbers == NULL || !IsWindow(g_hWndLineNumbers))
        return;
    if (!g_settings.showLineNumbers)
        return;

    RECT rc;
    GetWindowRect(g_hWndLineNumbers, &rc);
    if ((rc.right - rc.left) != GetLineNumberWidth())
        LayoutMainWindow();

    InvalidateRect(g_hWndLineNumbers, NULL, FALSE);
}

static void ToggleLineNumbers(void)
{
    g_settings.showLineNumbers = !g_settings.showLineNumbers;
    if (g_hWndLineNumbers != NULL)
    {
        ShowWindow(g_hWndLineNumbers, g_settings.showLineNumbers ? SW_SHOW : SW_HIDE);
        if (g_settings.showLineNumbers)
            StartLineNumbersScrollTimer();
        else
            KillTimer(g_hWndLineNumbers, LINE_NUMBER_SCROLL_TIMER);
    }

    LayoutMainWindow();
    InvalidateLineNumbers();
    UpdateEditMenuState(g_hWndMain);
    SettingsSave();
}

static void ToggleCurrentLineHighlight(void)
{
    g_settings.highlightCurrentLine = !g_settings.highlightCurrentLine;
    EditorApplyCurrentLineHighlight();
    UpdateEditMenuState(g_hWndMain);
    SettingsSave();
}

static void ToggleAutoIndent(void)
{
    g_settings.autoIndent = !g_settings.autoIndent;
    SettingsSave();

    // Invalida o bloco destacado, repinta a numeracao e atualiza a barra de status.
    EditorApplyIndentation();
    UpdateEditMenuState(g_hWndMain);
}

static void ToggleCodeHighlighting(void)
{
    size_t i;

    g_settings.codeHighlighting = !g_settings.codeHighlighting;

    EditorCancelUniversalHighlight();

    // So o documento ativo e processado agora. Os demais (editores ocultos)
    // apenas ficam marcados como nao sincronizados e se ajustam em
    // DocActivate -> EditorSyncUniversalHighlight, quando a aba receber o foco.
    for (i = 0; i < g_documents.size(); ++i)
        g_documents[i]->bCodeHighlightSynced = FALSE;

    if (g_pActiveDoc != NULL)
    {
        EditorSyncUniversalHighlight(g_pActiveDoc);
        EditorUpdateCurrentLineHighlight(g_pActiveDoc->hEdit);
    }

    UpdateEditMenuState(g_hWndMain);
    SettingsSave();
}

// Fwd decls: maquinaria de desfazer (definida mais abaixo), usada pelos
// ops do tree antes de sua posicao no arquivo.
enum TreeUndoKind
{
    TREE_UNDO_NONE = 0,
    TREE_UNDO_RENAME,
    TREE_UNDO_MOVE,
    TREE_UNDO_COPY,
    TREE_UNDO_CREATE,
    TREE_UNDO_DELETE
};
static void TreeUndoRecord(TreeUndoKind kind, LPCWSTR lpSrc, LPCWSTR lpDst);
static BOOL TreeUndoLastOperation(void);
static void TreeRestoreFocus(void);

static void TreeRestoreFocus(void)
{
    if (g_hWndTree != NULL)
        SetFocus(g_hWndTree);
}

static WCHAR* GetTreeItemPath(HTREEITEM hItem)
{
    TVITEMW item = {};

    if (g_hWndTree == NULL || hItem == NULL)
        return NULL;

    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    if (!TreeView_GetItem(g_hWndTree, &item))
        return NULL;

    return (WCHAR*)item.lParam;
}

static void FreeTreeItem(HTREEITEM hItem)
{
    HTREEITEM child;
    TVITEMW item = {};

    if (g_hWndTree == NULL || hItem == NULL)
        return;

    child = TreeView_GetChild(g_hWndTree, hItem);
    while (child != NULL)
    {
        HTREEITEM next = TreeView_GetNextSibling(g_hWndTree, child);
        FreeTreeItem(child);
        child = next;
    }

    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    if (TreeView_GetItem(g_hWndTree, &item))
        delete[] (WCHAR*)item.lParam;
}

static void ClearFileTree(void)
{
    HTREEITEM root;

    if (g_hWndTree == NULL)
        return;

    root = TreeView_GetRoot(g_hWndTree);
    while (root != NULL)
    {
        HTREEITEM next = TreeView_GetNextSibling(g_hWndTree, root);
        FreeTreeItem(root);
        root = next;
    }

    TreeView_DeleteAllItems(g_hWndTree);
}

static HIMAGELIST GetShellImageList(void)
{
    SHFILEINFOW sfi = {};

    return (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
                                      SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
}

static int GetTreeIconIndex(LPCWSTR lpPath, DWORD dwAttributes)
{
    SHFILEINFOW sfi = {};

    if (SHGetFileInfoW(lpPath, dwAttributes, &sfi, sizeof(sfi),
                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
        return sfi.iIcon;

    return 0;
}

static BOOL CreateFileTree(HWND hWnd)
{
    if (g_hWndTree != NULL)
        return TRUE;

    g_hWndTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, NULL,
                                  WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS |
                                  TVS_HASLINES | TVS_LINESATROOT |
                                  TVS_SHOWSELALWAYS | TVS_EDITLABELS,
                                 0, 0, 0, 0,
                                 hWnd, (HMENU)(INT_PTR)ID_TREE,
                                 g_hInstance, NULL);
    if (g_hWndTree == NULL)
        return FALSE;

    SendMessageW(g_hWndTree, WM_SETFONT,
                 (WPARAM)(g_hFontMenu != NULL ? g_hFontMenu : g_hFontStatus), TRUE);

    // Double buffering da TreeView: sem ele ela apaga o fundo e pinta os itens
    // em seguida a cada redimensionamento, o que pisca no arrasto do splitter.
#ifndef TVS_EX_DOUBLEBUFFER
#define TVS_EX_DOUBLEBUFFER 0x0004
#endif
    SendMessageW(g_hWndTree, TVM_SETEXTENDEDSTYLE,
                 TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

    HIMAGELIST himl = GetShellImageList();
    if (himl != NULL)
        TreeView_SetImageList(g_hWndTree, himl, TVSIL_NORMAL);

    return TRUE;
}

static void JoinTreePath(LPWSTR lpBuffer, size_t cchBuffer,
                         LPCWSTR lpDirectory, LPCWSTR lpName)
{
    size_t length = lstrlenW(lpDirectory);

    if (length > 0 &&
        (lpDirectory[length - 1] == L'\\' || lpDirectory[length - 1] == L'/'))
    {
        StringCchPrintfW(lpBuffer, cchBuffer, L"%s%s", lpDirectory, lpName);
    }
    else
    {
        StringCchPrintfW(lpBuffer, cchBuffer, L"%s\\%s", lpDirectory, lpName);
    }
}

struct TreeEntry
{
    std::wstring name;
    BOOL         bDirectory;
    DWORD        attributes;
};

static BOOL TreePathIsOpenDoc(LPCWSTR lpPath)
{
    size_t i;

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (g_documents[i]->bHasPath &&
            lstrcmpiW(g_documents[i]->szFilePath, lpPath) == 0)
            return TRUE;
    }
    return FALSE;
}

// Pastas primeiro, depois arquivos; cada grupo em ordem alfabetica.
static bool TreeEntryLess(const TreeEntry& a, const TreeEntry& b)
{
    if (a.bDirectory != b.bDirectory)
        return a.bDirectory != FALSE;

    return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
}

static void PopulateTreeItem(HTREEITEM hParent, LPCWSTR lpPath)
{
    WCHAR szPattern[MAX_FILE_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    std::vector<TreeEntry> entries;
    size_t i;

    JoinTreePath(szPattern, ARRAYSIZE(szPattern), lpPath, L"*");
    hFind = FindFirstFileW(szPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        TreeEntry entry;

        if (lstrcmpW(fd.cFileName, L".") == 0 ||
            lstrcmpW(fd.cFileName, L"..") == 0)
            continue;

        entry.name = fd.cFileName;
        entry.bDirectory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.attributes = fd.dwFileAttributes;
        entries.push_back(entry);
    }
    while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    std::sort(entries.begin(), entries.end(), TreeEntryLess);

    for (i = 0; i < entries.size(); ++i)
    {
        WCHAR* pszItemPath;
        int icon;
        TVINSERTSTRUCTW tvis = {};

        pszItemPath = new WCHAR[MAX_FILE_PATH];
        JoinTreePath(pszItemPath, MAX_FILE_PATH, lpPath, entries[i].name.c_str());

        icon = GetTreeIconIndex(pszItemPath, entries[i].attributes);

        tvis.hParent = hParent;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE |
                         TVIF_PARAM | TVIF_CHILDREN | TVIF_STATE;
        tvis.item.pszText = (LPWSTR)entries[i].name.c_str();
        tvis.item.iImage = icon;
        tvis.item.iSelectedImage = icon;
        tvis.item.lParam = (LPARAM)pszItemPath;
        tvis.item.cChildren = entries[i].bDirectory ? 1 : 0;
        tvis.item.stateMask = TVIS_BOLD;
        tvis.item.state = (!entries[i].bDirectory && TreePathIsOpenDoc(pszItemPath))
                        ? TVIS_BOLD : 0;

        if (TreeView_InsertItem(g_hWndTree, &tvis) == NULL)
            delete[] pszItemPath;
    }
}

// Negrito nos arquivos da arvore que estao abertos em alguma aba.
static void UpdateTreeMarksFrom(HTREEITEM hItem)
{
    while (hItem != NULL)
    {
        HTREEITEM hChild = TreeView_GetChild(g_hWndTree, hItem);
        WCHAR* pszPath = GetTreeItemPath(hItem);

        if (hChild != NULL)
        {
            UpdateTreeMarksFrom(hChild);
        }
        else if (pszPath != NULL)
        {
            BOOL bOpen = TreePathIsOpenDoc(pszPath);
            BOOL bBold = (TreeView_GetItemState(g_hWndTree, hItem, TVIS_BOLD) & TVIS_BOLD) != 0;

            if (bOpen != bBold)
                TreeView_SetItemState(g_hWndTree, hItem, bOpen ? TVIS_BOLD : 0, TVIS_BOLD);
        }

        hItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    }
}

void UpdateTreeOpenMarks(void)
{
    if (g_hWndTree == NULL)
        return;

    UpdateTreeMarksFrom(TreeView_GetRoot(g_hWndTree));
}

static BOOL GetTreeTargetDirectory(HTREEITEM hItem, LPWSTR lpDir, int cchDir)
{
    WCHAR* pszPath;

    if (g_hWndTree == NULL || lpDir == NULL || cchDir <= 0)
        return FALSE;

    lpDir[0] = L'\0';

    if (hItem == NULL)
        hItem = TreeView_GetSelection(g_hWndTree);
    if (hItem == NULL)
        hItem = TreeView_GetRoot(g_hWndTree);
    if (hItem == NULL)
        return FALSE;

    pszPath = GetTreeItemPath(hItem);
    if (pszPath == NULL)
        return FALSE;

    if (PathIsDirectoryW(pszPath))
        StringCchCopyW(lpDir, cchDir, pszPath);
    else
    {
        StringCchCopyW(lpDir, cchDir, pszPath);
        PathRemoveFileSpecW(lpDir);
    }

    return lpDir[0] != L'\0';
}

static void RefreshFileTree(void);
static std::wstring g_treeClipboardPath;
static BOOL g_treeCutPending = FALSE;
static void TreeSetCutMark(LPCWSTR lpPath, BOOL bCut);

void RefreshFileTreeView(void)
{
    RefreshFileTree();

    // Reaplica o estado de recortado depois da reconstrucao da arvore.
    if (g_treeCutPending && !g_treeClipboardPath.empty())
        TreeSetCutMark(g_treeClipboardPath.c_str(), TRUE);
}

BOOL GetTreeTargetDirectoryForSave(LPWSTR lpDir, int cchDir)
{
    if (g_hWndTree == NULL || lpDir == NULL || cchDir <= 0)
        return FALSE;
    if (!IsTreeVisible())
        return FALSE;
    if (GetFocus() != g_hWndTree)
        return FALSE;

    return GetTreeTargetDirectory(TreeView_GetSelection(g_hWndTree), lpDir, cchDir);
}

static HTREEITEM FindTreeItemByPath(HTREEITEM hItem, LPCWSTR lpPath)
{
    while (hItem != NULL)
    {
        WCHAR* pszPath = GetTreeItemPath(hItem);

        if (pszPath != NULL && lstrcmpiW(pszPath, lpPath) == 0)
            return hItem;

        HTREEITEM hFound = FindTreeItemByPath(TreeView_GetChild(g_hWndTree, hItem), lpPath);
        if (hFound != NULL)
            return hFound;

        hItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    }

    return NULL;
}

static BOOL SetTreeItemPath(HTREEITEM hItem, LPCWSTR lpPath)
{
    TVITEMW item = {};
    WCHAR* pszPath;

    if (g_hWndTree == NULL || hItem == NULL || lpPath == NULL)
        return FALSE;

    pszPath = new WCHAR[MAX_FILE_PATH];
    if (pszPath == NULL)
        return FALSE;

    StringCchCopyW(pszPath, MAX_FILE_PATH, lpPath);

    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    item.lParam = (LPARAM)pszPath;
    if (!TreeView_SetItem(g_hWndTree, &item))
    {
        delete[] pszPath;
        return FALSE;
    }

    return TRUE;
}

// Separa o sufixo numerico " (n)" do final do nome base (ex.: "meu arquivo
// (2).txt" -> stem "meu arquivo", nStart = 3). Sem sufixo, nStart = 2.
static void TreeSplitCopyNumber(LPCWSTR lpStem, LPWSTR lpBase, int cchBase, int* pnStart)
{
    int len = lstrlenW(lpStem);
    int i = len - 1;
    int num = 0;
    int place = 1;

    *pnStart = 2;
    StringCchCopyW(lpBase, cchBase, lpStem);

    if (i < 0 || lpStem[i] != L')')
        return;

    --i;
    if (i < 0 || lpStem[i] < L'0' || lpStem[i] > L'9')
        return;

    while (i >= 0 && lpStem[i] >= L'0' && lpStem[i] <= L'9')
    {
        num += (lpStem[i] - L'0') * place;
        place *= 10;
        --i;
    }

    if (i < 0 || lpStem[i] != L'(')
        return;
    if (i > 0 && lpStem[i - 1] != L' ')
        return;

    if (i > 0)
        StringCchCopyNW(lpBase, cchBase, lpStem, i - 1);
    else
        lpBase[0] = L'\0';

    if (num < 1)
        num = 1;
    *pnStart = num + 1;
}

// "name (<n>)<ext>": resolve conflito de nome no destino, incrementando n.
static BOOL TreeMakeUniquePath(LPCWSTR lpDir, LPCWSTR lpDesired, LPWSTR lpOut, int cchOut)
{
    WCHAR szBase[MAX_PATH];
    WCHAR szExt[MAX_PATH];
    WCHAR szStem[MAX_PATH];
    WCHAR szName[MAX_PATH];
    WCHAR szPath[MAX_FILE_PATH];
    size_t nameLen;
    size_t extLen;
    int nStart;

    if (lpDir == NULL || lpDesired == NULL || lpOut == NULL || cchOut <= 0)
        return FALSE;

    StringCchCopyW(szName, ARRAYSIZE(szName), lpDesired);

    {
        LPCWSTR lpExt = PathFindExtensionW(szName);

        if (lpExt != NULL && lpExt[0] != L'\0' && lpExt != szName)
        {
            StringCchCopyW(szExt, ARRAYSIZE(szExt), lpExt);
            extLen = lstrlenW(szExt);
            nameLen = lstrlenW(szName);
            if (extLen < nameLen)
                nameLen -= extLen;
            StringCchCopyNW(szBase, ARRAYSIZE(szBase), szName, nameLen);
            szBase[nameLen] = L'\0';
        }
        else
        {
            StringCchCopyW(szBase, ARRAYSIZE(szBase), szName);
            szExt[0] = L'\0';
        }
    }

    TreeSplitCopyNumber(szBase, szStem, ARRAYSIZE(szStem), &nStart);

    // Tentativa 1: o nome desejado.
    StringCchCopyW(szName, ARRAYSIZE(szName), lpDesired);
    JoinTreePath(szPath, ARRAYSIZE(szPath), lpDir, szName);
    if (GetFileAttributesW(szPath) == INVALID_FILE_ATTRIBUTES)
    {
        StringCchCopyW(lpOut, cchOut, szPath);
        return TRUE;
    }

    // Tentativas seguintes: stem (n).ext, n a partir de nStart.
    for (int n = nStart; n < 10000; ++n)
    {
        StringCchPrintfW(szName, ARRAYSIZE(szName), L"%s (%d)%s", szStem, n, szExt);
        JoinTreePath(szPath, ARRAYSIZE(szPath), lpDir, szName);
        if (GetFileAttributesW(szPath) == INVALID_FILE_ATTRIBUTES)
        {
            StringCchCopyW(lpOut, cchOut, szPath);
            return TRUE;
        }
    }

    return FALSE;
}

static void TreeUpdateOpenDocsRenamed(LPCWSTR lpOld, LPCWSTR lpNew, BOOL bDir)
{
    size_t oldLen;

    if (lpOld == NULL || lpNew == NULL)
        return;

    oldLen = lstrlenW(lpOld);

    for (size_t i = 0; i < g_documents.size(); ++i)
    {
        Document* pDoc = g_documents[i];
        WCHAR szNew[MAX_FILE_PATH] = {};
        BOOL changed = FALSE;

        if (!pDoc->bHasPath)
            continue;

        if (bDir)
        {
            size_t docLen = lstrlenW(pDoc->szFilePath);

            if (docLen >= oldLen &&
                _wcsnicmp(pDoc->szFilePath, lpOld, oldLen) == 0 &&
                (pDoc->szFilePath[oldLen] == L'\0' || pDoc->szFilePath[oldLen] == L'\\' ||
                 pDoc->szFilePath[oldLen] == L'/'))
            {
                StringCchCopyW(szNew, ARRAYSIZE(szNew), lpNew);
                StringCchCatW(szNew, ARRAYSIZE(szNew), pDoc->szFilePath + oldLen);
                changed = TRUE;
            }
        }
        else if (lstrcmpiW(pDoc->szFilePath, lpOld) == 0)
        {
            StringCchCopyW(szNew, ARRAYSIZE(szNew), lpNew);
            changed = TRUE;
        }

        if (!changed)
            continue;

        StringCchCopyW(pDoc->szFilePath, ARRAYSIZE(pDoc->szFilePath), szNew);
        StringCchCopyW(pDoc->szFileTitle, ARRAYSIZE(pDoc->szFileTitle),
                       PathFindFileNameW(szNew));
        DocUpdateTabTitle(pDoc);

        if (pDoc == g_pActiveDoc)
        {
            UpdateWindowTitle();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
        }
    }
}

static void TreeUpdateItemPathPrefix(HTREEITEM hItem, LPCWSTR lpOldPrefix, LPCWSTR lpNewPrefix)
{
    size_t oldLen = lstrlenW(lpOldPrefix);
    size_t newLen = lstrlenW(lpNewPrefix);

    while (hItem != NULL)
    {
        WCHAR* pszPath = GetTreeItemPath(hItem);

        if (pszPath != NULL && _wcsnicmp(pszPath, lpOldPrefix, oldLen) == 0 &&
            (pszPath[oldLen] == L'\0' || pszPath[oldLen] == L'\\'))
        {
            WCHAR szNew[MAX_FILE_PATH] = {};

            StringCchCopyW(szNew, ARRAYSIZE(szNew), lpNewPrefix);
            StringCchCatW(szNew, ARRAYSIZE(szNew), pszPath + oldLen);
            SetTreeItemPath(hItem, szNew);
        }

        TreeUpdateItemPathPrefix(TreeView_GetChild(g_hWndTree, hItem),
                                 lpOldPrefix, lpNewPrefix);

        hItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    }
}

static void TreeBeginRename(HTREEITEM hItem)
{
    if (g_hWndTree == NULL || hItem == NULL)
        return;
    if (hItem == TreeView_GetRoot(g_hWndTree))
        return;   // a raiz não é renomeável

    SetFocus(g_hWndTree);
    TreeView_EditLabel(g_hWndTree, hItem);
}

static HTREEITEM TreeSelectionNotRoot(void)
{
    HTREEITEM hItem;

    if (g_hWndTree == NULL)
        return NULL;

    hItem = TreeView_GetSelection(g_hWndTree);
    if (hItem == NULL || hItem == TreeView_GetRoot(g_hWndTree))
        return NULL;

    return hItem;
}

static void TreeCreateNewFile(void)
{
    WCHAR szDir[MAX_FILE_PATH];
    WCHAR szPath[MAX_FILE_PATH];
    WCHAR szName[MAX_PATH];
    WCHAR szUntitled[128];
    DWORD attrs;
    int n;
    HANDLE hFile;
    HTREEITEM hItem;

    if (g_hWndTree == NULL)
        return;

    // A criação acontece na pasta selecionada; arquivo selecionado vai para a
    // pasta pai; nenhuma seleção usa a raiz.
    wmemset(szDir, 0, ARRAYSIZE(szDir));
    {
        HTREEITEM hTarget = TreeView_GetSelection(g_hWndTree);
        WCHAR* pszSel = (hTarget != NULL) ? GetTreeItemPath(hTarget) : NULL;

        if (pszSel != NULL)
        {
            if (PathIsDirectoryW(pszSel))
                StringCchCopyW(szDir, ARRAYSIZE(szDir), pszSel);
            else
            {
                StringCchCopyW(szDir, ARRAYSIZE(szDir), pszSel);
                PathRemoveFileSpecW(szDir);
            }
        }
        else
        {
            HTREEITEM hRoot = TreeView_GetRoot(g_hWndTree);
            WCHAR* pszRoot = (hRoot != NULL) ? GetTreeItemPath(hRoot) : NULL;

            if (pszRoot != NULL)
                StringCchCopyW(szDir, ARRAYSIZE(szDir), pszRoot);
        }
    }

    if (szDir[0] == L'\0')
        return;

    LoadStringResource(IDS_UNTITLED, szUntitled, ARRAYSIZE(szUntitled));

    // Nome pela regra da aba (Untitled, Untitled 2...); ignora os já usados.
    for (n = 1; n < 10000; ++n)
    {
        if (n == 1)
            StringCchCopyW(szName, ARRAYSIZE(szName), szUntitled);
        else
            StringCchPrintfW(szName, ARRAYSIZE(szName), L"%s %d", szUntitled, n);

        JoinTreePath(szPath, ARRAYSIZE(szPath), szDir, szName);
        attrs = GetFileAttributesW(szPath);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            break;
    }
    if (n >= 10000)
        return;

    hFile = CreateFileW(szPath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    CloseHandle(hFile);

    TreeUndoRecord(TREE_UNDO_CREATE, szPath, NULL);

    RefreshFileTree();
    EditorOpen(szPath);

    hItem = FindTreeItemByPath(TreeView_GetRoot(g_hWndTree), szPath);
    if (hItem != NULL)
    {
        TreeView_SelectItem(g_hWndTree, hItem);
        TreeView_EnsureVisible(g_hWndTree, hItem);
        TreeBeginRename(hItem);
    }
}

static BOOL HandleTreeBeginLabelEdit(NMTVDISPINFOW* ptvdi)
{
    if (ptvdi == NULL)
        return TRUE;   // cancela por seguranca
    if (ptvdi->item.hItem == TreeView_GetRoot(g_hWndTree))
        return TRUE;   // a raiz nao e renomeavel

    return FALSE;   // permite edicao
}

static BOOL HandleTreeEndLabelEdit(NMTVDISPINFOW* ptvdi)
{
    WCHAR szOldPath[MAX_FILE_PATH];
    WCHAR szDir[MAX_FILE_PATH];
    WCHAR szNewPath[MAX_FILE_PATH] = {};
    WCHAR szDesired[MAX_PATH];
    LPCWSTR lpName;
    BOOL bDir;
    TVITEMW item = {};

    if (ptvdi == NULL || ptvdi->item.hItem == NULL)
        return FALSE;
    if (ptvdi->item.pszText == NULL || ptvdi->item.pszText[0] == L'\0')
        return FALSE;   // cancelou ou texto vazio

    {
        WCHAR* pszOld = GetTreeItemPath(ptvdi->item.hItem);

        if (pszOld == NULL)
            return FALSE;
        StringCchCopyW(szOldPath, ARRAYSIZE(szOldPath), pszOld);
    }

    bDir = PathIsDirectoryW(szOldPath);

    StringCchCopyW(szDir, ARRAYSIZE(szDir), szOldPath);
    PathRemoveFileSpecW(szDir);
    StringCchCopyW(szDesired, ARRAYSIZE(szDesired), ptvdi->item.pszText);

    JoinTreePath(szNewPath, ARRAYSIZE(szNewPath), szDir, szDesired);
    if (lstrcmpiW(szNewPath, szOldPath) == 0)
        return FALSE;

    if (GetFileAttributesW(szNewPath) != INVALID_FILE_ATTRIBUTES)
    {
        if (!TreeMakeUniquePath(szDir, szDesired, szNewPath, ARRAYSIZE(szNewPath)))
            return FALSE;
    }

    if (!MoveFileW(szOldPath, szNewPath))
        return FALSE;

    TreeUndoRecord(TREE_UNDO_RENAME, szOldPath, szNewPath);

    TreeUpdateOpenDocsRenamed(szOldPath, szNewPath, bDir);

    // Pasta renomeada: atualiza os caminhos dos itens filhos.
    if (bDir)
        TreeUpdateItemPathPrefix(TreeView_GetChild(g_hWndTree, ptvdi->item.hItem),
                                 szOldPath, szNewPath);

    // Atualiza o lParam do item com o caminho novo.
    {
        WCHAR* pszOld = GetTreeItemPath(ptvdi->item.hItem);
        WCHAR* pszNewItem = new WCHAR[MAX_FILE_PATH];

        if (pszNewItem != NULL)
        {
            StringCchCopyW(pszNewItem, MAX_FILE_PATH, szNewPath);
            item.mask = TVIF_PARAM;
            item.hItem = ptvdi->item.hItem;
            item.lParam = (LPARAM)pszNewItem;

            if (TreeView_SetItem(g_hWndTree, &item) && pszOld != NULL)
                delete[] pszOld;
            else
                delete[] pszNewItem;
        }
    }

    // Texto e icone.
    lpName = PathFindFileNameW(szNewPath);
    item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    item.hItem = ptvdi->item.hItem;
    item.pszText = (LPWSTR)lpName;
    {
        int icon = GetTreeIconIndex(szNewPath,
                                    bDir ? FILE_ATTRIBUTE_DIRECTORY
                                         : FILE_ATTRIBUTE_NORMAL);
        item.iImage = icon;
        item.iSelectedImage = icon;
    }
    TreeView_SetItem(g_hWndTree, &item);

    UpdateTreeOpenMarks();

    // Reconstruir a arvore para que nao aparecam estago stale/mescladas
    // (dois itens com o mesmo nome quando um conflito foi resolvido).
    RefreshFileTree();

    {
        HTREEITEM hNew = FindTreeItemByPath(TreeView_GetRoot(g_hWndTree), szNewPath);

        if (hNew != NULL)
        {
            TreeView_SelectItem(g_hWndTree, hNew);
            TreeView_EnsureVisible(g_hWndTree, hNew);
        }
    }
    TreeRestoreFocus();

    return TRUE;
}

#ifndef TVIS_CUT
#define TVIS_CUT 0x0004
#endif

// Area de transferencia do tree: item pendente recortado/copiado (o caminho),
// a marca de recortado (icone translucido) e a flag associada ao tree exigida
// pelo requisito — se o usuario colocar algo na Area de Transferencia global,
// o item volta ao normal.
static BOOL g_treeClipboardCut = FALSE;

static BOOL TreePathMatchesOrContains(LPCWSTR lpBase, LPCWSTR lpPath)
{
    size_t baseLen;

    if (lpBase == NULL || lpPath == NULL)
        return FALSE;

    baseLen = lstrlenW(lpBase);
    if (_wcsnicmp(lpBase, lpPath, baseLen) != 0)
        return FALSE;

    return lpPath[baseLen] == L'\0' || lpPath[baseLen] == L'\\' || lpPath[baseLen] == L'/';
}

static void TreeSetCutMark(LPCWSTR lpPath, BOOL bCut)
{
    HTREEITEM hItem;

    if (g_hWndTree == NULL || lpPath == NULL)
        return;

    hItem = FindTreeItemByPath(TreeView_GetRoot(g_hWndTree), lpPath);
    if (hItem != NULL)
        TreeView_SetItemState(g_hWndTree, hItem, bCut ? TVIS_CUT : 0, TVIS_CUT);
}

static void TreeClearClipboardMark(void)
{
    if (g_treeCutPending)
        TreeSetCutMark(g_treeClipboardPath.c_str(), FALSE);

    g_treeCutPending = FALSE;
    g_treeClipboardCut = FALSE;
    g_treeClipboardPath.clear();
}

static void TreeCopyOrMoveItem(LPCWSTR lpSrc, LPCWSTR lpDestDir, BOOL bMove)
{
    WCHAR szSrcDir[MAX_FILE_PATH];
    WCHAR szName[MAX_PATH];
    WCHAR szDest[MAX_FILE_PATH];
    WCHAR szFrom[MAX_FILE_PATH + 2] = {};
    WCHAR szTo[MAX_FILE_PATH + 2] = {};
    SHFILEOPSTRUCTW op = {};
    BOOL bDir;

    if (lpSrc == NULL || lpDestDir == NULL)
        return;

    bDir = PathIsDirectoryW(lpSrc);

    // Colar na mesma pasta de um recorte nao faz nada.
    if (bMove)
    {
        StringCchCopyW(szSrcDir, ARRAYSIZE(szSrcDir), lpSrc);
        PathRemoveFileSpecW(szSrcDir);
        if (lstrcmpiW(szSrcDir, lpDestDir) == 0)
            return;
    }

    // Nao entra dentro de si mesmo nem de subpasta dele.
    if (TreePathMatchesOrContains(lpSrc, lpDestDir))
        return;

    StringCchCopyW(szName, ARRAYSIZE(szName), PathFindFileNameW(lpSrc));
    if (!TreeMakeUniquePath(lpDestDir, szName, szDest, ARRAYSIZE(szDest)))
        return;

    StringCchCopyW(szFrom, ARRAYSIZE(szFrom) - 1, lpSrc);
    StringCchCopyW(szTo, ARRAYSIZE(szTo) - 1, szDest);

    op.hwnd = g_hWndMain;
    op.wFunc = bMove ? FO_MOVE : FO_COPY;
    op.pFrom = szFrom;
    op.pTo = szTo;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted)
        return;

    if (bMove)
        TreeUpdateOpenDocsRenamed(lpSrc, szDest, bDir);

    TreeUndoRecord(bMove ? TREE_UNDO_MOVE : TREE_UNDO_COPY,
                   bMove ? lpSrc : szDest,
                   bMove ? szDest : NULL);

    // SO o recorte e consumido ao colar; a copia fica no tree para
    // colacoes repetidas (cada colagem gera o proximo numero).
    if (bMove)
        TreeClearClipboardMark();

    RefreshFileTree();

    {
        HTREEITEM hNew = FindTreeItemByPath(TreeView_GetRoot(g_hWndTree), szDest);

        if (hNew != NULL)
        {
            TreeView_SelectItem(g_hWndTree, hNew);
            TreeView_EnsureVisible(g_hWndTree, hNew);
        }
    }
    SetFocus(g_hWndTree);
}

static void HandleTreeClipboardKey(HTREEITEM hItem, WCHAR wVKey)
{
    WCHAR* pszPath;

    if (g_hWndTree == NULL || (GetKeyState(VK_CONTROL) & 0x8000) == 0)
        return;

    if (wVKey == 'X' || wVKey == 'C')
    {
        if (hItem == NULL || hItem == TreeView_GetRoot(g_hWndTree))
            return;

        TreeClearClipboardMark();      // remove qualquer marca pendente antes

        pszPath = GetTreeItemPath(hItem);
        if (pszPath == NULL)
            return;

        g_treeClipboardPath = pszPath;
        g_treeClipboardCut = (wVKey == 'X');
        g_treeCutPending = g_treeClipboardCut;

        if (g_treeClipboardCut)
            TreeSetCutMark(g_treeClipboardPath.c_str(), TRUE);
        return;
    }

    if (wVKey == 'V')
    {
        WCHAR szDir[MAX_FILE_PATH];

        if (g_treeClipboardPath.empty())
            return;
        if (!GetTreeTargetDirectory(hItem, szDir, ARRAYSIZE(szDir)))
            return;

        TreeCopyOrMoveItem(g_treeClipboardPath.c_str(), szDir, g_treeClipboardCut);
        return;
    }
}

// Desfazer da ultima operacao de arquivo feita no tree (uma so, a mais
// recente). Registrada pelas operacoes do tree e consumida pelo Ctrl+Z com o
// foco no tree.
struct TreeUndoEntry
{
    TreeUndoKind kind;
    std::wstring pathSrc;   // origem da operacao (RENAME/MOVE: caminho original)
    std::wstring pathDst;   // destino (RENAME/MOVE); vazio para COPY/CREATE/DELETE
    FILETIME     time;
};

static TreeUndoEntry g_treeUndo = {};

static void TreeUndoRecord(TreeUndoKind kind, LPCWSTR lpSrc, LPCWSTR lpDst)
{
    g_treeUndo.kind = kind;
    g_treeUndo.pathSrc = (lpSrc != NULL) ? lpSrc : L"";
    g_treeUndo.pathDst = (lpDst != NULL) ? lpDst : L"";
    GetSystemTimeAsFileTime(&g_treeUndo.time);
}

static void TreeUndoClear(void)
{
    g_treeUndo.kind = TREE_UNDO_NONE;
    g_treeUndo.pathSrc.clear();
    g_treeUndo.pathDst.clear();
    g_treeUndo.time = {};
}

static void TreeUndoFailed(void)
{
    WCHAR szMsg[128];

    LoadStringResource(IDS_TREE_UNDO_FAILED, szMsg, ARRAYSIZE(szMsg));
    MessageBoxW(g_hWndMain, szMsg, NOTBAD_NAME, MB_OK | MB_ICONWARNING);
    TreeRestoreFocus();
}

static void TreeSelectItemByPath(LPCWSTR lpPath)
{
    HTREEITEM hItem;

    if (g_hWndTree == NULL || lpPath == NULL)
        return;

    hItem = FindTreeItemByPath(TreeView_GetRoot(g_hWndTree), lpPath);
    if (hItem != NULL)
    {
        TreeView_SelectItem(g_hWndTree, hItem);
        TreeView_EnsureVisible(g_hWndTree, hItem);
    }
}

// Fecha documentos abertos cujo caminho esta dentro de lpPath. Retorna FALSE
// quando o usuario cancela o salvamento de algum documento (a operacao que
// apagaria o arquivo deve ser abortada); TRUE caso contrario.
static BOOL TreeCloseDocsUnderPath(LPCWSTR lpPath)
{
    size_t i = 0;

    while (i < g_documents.size())
    {
        Document* pDoc = g_documents[i];

        if (pDoc->bHasPath && TreePathMatchesOrContains(lpPath, pDoc->szFilePath))
        {
            if (!DocClose(pDoc))
                return FALSE;
            i = 0;
        }
        else
        {
            ++i;
        }
    }

    return TRUE;
}

// Acha na Lixeira o item cuja pasta de origem coincide com lpDir e cujo nome
// (tolerante a extensao oculta) coincide com lpName; entre os candidatos,
// prefere o excluido mais recentemente. Retorna o pidl do item (a liberar
// com CoTaskMemFree) ou NULL.
static PITEMID_CHILD TreeFindBinItem(IShellFolder2* psf, LPCWSTR lpDir, LPCWSTR lpName)
{
    IEnumIDList* penum = NULL;
    PITEMID_CHILD pidlBest = NULL;
    LARGE_INTEGER ftBest = {};
    BOOL hasBestDate = FALSE;
    size_t dirLen;

    if (FAILED(psf->EnumObjects(NULL, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &penum)) ||
        penum == NULL)
        return NULL;

    dirLen = lstrlenW(lpDir);

    while (TRUE)
    {
        PITEMID_CHILD pidl = NULL;
        WCHAR szDisplaced[MAX_FILE_PATH] = {};
        WCHAR szBinName[MAX_PATH] = {};
        VARIANT vtFrom = {};
        VARIANT vtDate = {};
        BOOL dirOk = FALSE;
        BOOL nameOk = FALSE;
        const SHCOLUMNID scidFrom = { PSGUID_DISPLACED, PID_DISPLACED_FROM };
        const SHCOLUMNID scidDate = { PSGUID_DISPLACED, PID_DISPLACED_DATE };

        if (penum->Next(1, &pidl, NULL) != S_OK || pidl == NULL)
            break;

        VariantInit(&vtFrom);
        VariantInit(&vtDate);

        // Pasta de origem do item excluido (ou o caminho completo, tolerado).
        if (SUCCEEDED(psf->GetDetailsEx(pidl, &scidFrom, &vtFrom)) &&
            SUCCEEDED(VariantChangeType(&vtFrom, &vtFrom, 0, VT_BSTR)) &&
            vtFrom.bstrVal != NULL)
        {
            StringCchCopyW(szDisplaced, ARRAYSIZE(szDisplaced), vtFrom.bstrVal);
            size_t rawLen = lstrlenW(szDisplaced);

            while (rawLen > 0 && (szDisplaced[rawLen - 1] == L'\\' || szDisplaced[rawLen - 1] == L'/'))
                szDisplaced[--rawLen] = L'\0';

            // A coluna pode trazer o caminho completo; se terminar com o
            // nosso nome, a pasta de origem e o diretorio do caminho.
            LPCWSTR lpNameFromCol = PathFindFileNameW(szDisplaced);
            if (lpNameFromCol != NULL && lstrcmpiW(lpNameFromCol, lpName) == 0)
                PathRemoveFileSpecW(szDisplaced);
        }
        {
            size_t dsLen = lstrlenW(szDisplaced);
            dirOk = (dirLen == dsLen) && _wcsnicmp(lpDir, szDisplaced, dirLen) == 0;
        }
        VariantClear(&vtFrom);

        // Nome como aparece na Lixeira (pode omitir a extensao oculta).
        if (dirOk)
        {
            STRRET sr = {};

            if (SUCCEEDED(psf->GetDisplayNameOf(pidl, SHGDN_INFOLDER, &sr)) &&
                SUCCEEDED(StrRetToBufW(&sr, pidl, szBinName, ARRAYSIZE(szBinName))))
            {
                if (lstrcmpiW(szBinName, lpName) == 0)
                    nameOk = TRUE;
                else
                {
                    LPCWSTR lpExt = PathFindExtensionW(lpName);

                    if (lpExt != NULL && lpExt[0] != L'\0')
                    {
                        size_t baseLen = (size_t)(lstrlenW(lpName) - lstrlenW(lpExt));

                        if (_wcsnicmp(szBinName, lpName, baseLen) == 0 &&
                            szBinName[baseLen] == L'\0')
                        {
                            nameOk = TRUE;
                        }
                    }
                }
            }
        }

        if (dirOk && nameOk)
        {
            LARGE_INTEGER ftCand;
            BOOL hasDate = FALSE;

            ftCand.QuadPart = 0;
            if (SUCCEEDED(psf->GetDetailsEx(pidl, &scidDate, &vtDate)) &&
                SUCCEEDED(VariantChangeType(&vtDate, &vtDate, 0, VT_DATE)))
            {
                // VT_DATE (dias desde 1899-12-30) -> FILETIME aproximado
                double days = vtDate.date;
                ftCand.QuadPart = (LONGLONG)((days + 109205.0) * 86400.0 * 10000000.0);
                hasDate = TRUE;
            }

            if (pidlBest == NULL)
            {
                pidlBest = pidl;
                ftBest = ftCand;
                hasBestDate = hasDate;
                pidl = NULL;
            }
            else if (hasDate && (!hasBestDate || ftCand.QuadPart >= ftBest.QuadPart))
            {
                CoTaskMemFree(pidlBest);
                pidlBest = pidl;
                ftBest = ftCand;
                hasBestDate = TRUE;
                pidl = NULL;
            }
        }
        VariantClear(&vtDate);

        if (pidl != NULL)
            CoTaskMemFree(pidl);
    }

    penum->Release();
    return pidlBest;
}

// Desfaz exclusao: restaura da Lixeira o item excluido mais recentemente.
static BOOL TreeUndoDeleteRestore(void)
{
    WCHAR szDir[MAX_FILE_PATH];
    WCHAR szName[MAX_PATH];
    LPCWSTR lpName;
    PIDLIST_ABSOLUTE pidlBin = NULL;
    IShellFolder2* psf = NULL;
    PITEMID_CHILD pidlItem = NULL;
    IShellItem* binItem = NULL;
    IShellItem* destFolder = NULL;
    IFileOperation* pOp = NULL;
    BOOL ok = FALSE;
    HRESULT hr;

    if (g_treeUndo.pathSrc.empty())
        return FALSE;

    lpName = PathFindFileNameW(g_treeUndo.pathSrc.c_str());
    if (lpName == NULL || lpName[0] == L'\0')
        return FALSE;
    StringCchCopyW(szName, ARRAYSIZE(szName), lpName);

    StringCchCopyW(szDir, ARRAYSIZE(szDir), g_treeUndo.pathSrc.c_str());
    PathRemoveFileSpecW(szDir);

    if (GetFileAttributesW(szDir) == INVALID_FILE_ATTRIBUTES ||
        (GetFileAttributesW(szDir) & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return FALSE;   // a pasta de origem nao existe mais

    if (GetFileAttributesW(g_treeUndo.pathSrc.c_str()) != INVALID_FILE_ATTRIBUTES)
        return FALSE;   // algo reapareceu no caminho original; nao sobrescrever

    hr = SHGetSpecialFolderLocation(NULL, CSIDL_BITBUCKET, &pidlBin);
    if (FAILED(hr) || pidlBin == NULL)
        goto cleanup;

    {
        IShellFolder* psfDesktop = NULL;

        hr = SHGetDesktopFolder(&psfDesktop);
        if (FAILED(hr) || psfDesktop == NULL)
            goto cleanup;
        hr = psfDesktop->BindToObject(pidlBin, NULL, IID_PPV_ARGS(&psf));
        psfDesktop->Release();
        if (FAILED(hr) || psf == NULL)
            goto cleanup;
    }

    pidlItem = TreeFindBinItem(psf, szDir, szName);
    if (pidlItem == NULL)
        goto cleanup;

    {
        PIDLIST_ABSOLUTE pidlAbs = ILCombine(pidlBin, pidlItem);

        if (pidlAbs != NULL)
        {
            hr = SHCreateItemFromIDList(pidlAbs, IID_PPV_ARGS(&binItem));
            ILFree(pidlAbs);
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }
    }
    if (FAILED(hr) || binItem == NULL)
        goto cleanup;

    hr = SHCreateItemFromParsingName(szDir, NULL, IID_PPV_ARGS(&destFolder));
    if (FAILED(hr) || destFolder == NULL)
        goto cleanup;

    hr = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_ALL,
                          IID_PPV_ARGS(&pOp));
    if (FAILED(hr) || pOp == NULL)
        goto cleanup;

    pOp->SetOperationFlags(FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI);

    hr = pOp->MoveItem(binItem, destFolder, szName, NULL);
    if (FAILED(hr))
        goto cleanup;

    hr = pOp->PerformOperations();
    if (FAILED(hr))
        goto cleanup;

    {
        BOOL aborted = FALSE;

        pOp->GetAnyOperationsAborted(&aborted);
        if (aborted)
            goto cleanup;
    }

    RefreshFileTree();
    TreeSelectItemByPath(g_treeUndo.pathSrc.c_str());
    TreeRestoreFocus();
    ok = TRUE;

cleanup:
    if (pOp != NULL)
        pOp->Release();
    if (destFolder != NULL)
        destFolder->Release();
    if (binItem != NULL)
        binItem->Release();
    if (pidlItem != NULL)
        CoTaskMemFree(pidlItem);
    if (psf != NULL)
        psf->Release();
    if (pidlBin != NULL)
        CoTaskMemFree(pidlBin);
    return ok;
}

// Desfaz exclusao/copia/criacao enviando o item gerado para a Lixeira; nao
// pede confirmacao de novo — a confirmacao da operacao desfeita ja foi dada.
static BOOL TreeUndoRecycleCreated(LPCWSTR lpPath)
{
    WCHAR szFrom[MAX_FILE_PATH + 2] = {};
    SHFILEOPSTRUCTW op = {};

    if (lpPath == NULL)
        return FALSE;
    if (lstrlenW(lpPath) >= MAX_FILE_PATH + 1)
        return FALSE;
    if (GetFileAttributesW(lpPath) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    if (!TreeCloseDocsUnderPath(lpPath))
        return FALSE;

    StringCchCopyW(szFrom, ARRAYSIZE(szFrom) - 1, lpPath);

    op.hwnd = g_hWndMain;
    op.wFunc = FO_DELETE;
    op.pFrom = szFrom;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted)
        return FALSE;

    RefreshFileTree();
    TreeRestoreFocus();
    return TRUE;
}

// Desfaz renomeacao/movimentacao: move pathDst de volta para pathSrc.
static BOOL TreeUndoRenameOrMove(void)
{
    WCHAR szDir[MAX_FILE_PATH];
    BOOL isDir;

    if (g_treeUndo.pathSrc.empty() || g_treeUndo.pathDst.empty())
        return FALSE;
    if (GetFileAttributesW(g_treeUndo.pathDst.c_str()) == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    if (GetFileAttributesW(g_treeUndo.pathSrc.c_str()) != INVALID_FILE_ATTRIBUTES)
        return FALSE;

    StringCchCopyW(szDir, ARRAYSIZE(szDir), g_treeUndo.pathSrc.c_str());
    PathRemoveFileSpecW(szDir);
    if (GetFileAttributesW(szDir) == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    // MOVEFILE_COPY_ALLOWED cobre volumes diferentes.
    if (!MoveFileExW(g_treeUndo.pathDst.c_str(), g_treeUndo.pathSrc.c_str(),
                     MOVEFILE_COPY_ALLOWED))
        return FALSE;

    isDir = PathIsDirectoryW(g_treeUndo.pathSrc.c_str());
    TreeUpdateOpenDocsRenamed(g_treeUndo.pathDst.c_str(), g_treeUndo.pathSrc.c_str(), isDir);

    if (g_treeClipboardCut && !g_treeClipboardPath.empty() &&
        lstrcmpiW(g_treeClipboardPath.c_str(), g_treeUndo.pathDst.c_str()) == 0)
        g_treeClipboardPath = g_treeUndo.pathSrc;

    RefreshFileTree();
    TreeSelectItemByPath(g_treeUndo.pathSrc.c_str());
    TreeRestoreFocus();
    return TRUE;
}

// Ctrl+Z com o foco no tree: pergunta se desfaz a ultima operacao de arquivo
// do tree e executa. Retorna TRUE quando a tecla foi consumida; FALSE quando
// nao ha operacao registrada (deixa o Ctrl+Z ir para o editor).
static BOOL TreeUndoLastOperation(void)
{
    WCHAR szMsg[MAX_FILE_PATH + 512];
    WCHAR szFormat[256];
    UINT idsFormat;
    BOOL ok = FALSE;
    int result;

    if (g_hWndTree == NULL || g_treeUndo.kind == TREE_UNDO_NONE)
        return FALSE;

    switch (g_treeUndo.kind)
    {
    case TREE_UNDO_RENAME: idsFormat = IDS_TREE_UNDO_RENAME; break;
    case TREE_UNDO_MOVE:   idsFormat = IDS_TREE_UNDO_MOVE;   break;
    case TREE_UNDO_COPY:   idsFormat = IDS_TREE_UNDO_COPY;   break;
    case TREE_UNDO_CREATE: idsFormat = IDS_TREE_UNDO_CREATE; break;
    case TREE_UNDO_DELETE: idsFormat = IDS_TREE_UNDO_DELETE; break;
    default: return FALSE;
    }

    LoadStringResource(idsFormat, szFormat, ARRAYSIZE(szFormat));

    switch (g_treeUndo.kind)
    {
    case TREE_UNDO_RENAME:
    case TREE_UNDO_MOVE:
        StringCchPrintfW(szMsg, ARRAYSIZE(szMsg), szFormat,
                         PathFindFileNameW(g_treeUndo.pathSrc.c_str()),
                         g_treeUndo.pathDst.c_str());
        break;
    default:
        StringCchPrintfW(szMsg, ARRAYSIZE(szMsg), szFormat,
                         PathFindFileNameW(g_treeUndo.pathSrc.c_str()));
        break;
    }

    result = MessageBoxW(g_hWndMain, szMsg, NOTBAD_NAME,
                         MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    TreeRestoreFocus();
    if (result != IDYES)
        return TRUE;

    switch (g_treeUndo.kind)
    {
    case TREE_UNDO_RENAME:
    case TREE_UNDO_MOVE:   ok = TreeUndoRenameOrMove(); break;
    case TREE_UNDO_COPY:
    case TREE_UNDO_CREATE: ok = TreeUndoRecycleCreated(g_treeUndo.pathSrc.c_str()); break;
    case TREE_UNDO_DELETE: ok = TreeUndoDeleteRestore(); break;
    }

    if (!ok)
        TreeUndoFailed();
    else
        TreeUndoClear();

    return TRUE;
}

// Delete com o foco no tree: confirma e move para a Lixeira, guardando um
// vizinho para devolver o foco.
static BOOL TreeDeleteSelected(void)
{
    HTREEITEM hItem;
    HTREEITEM hSiblingItem;
    WCHAR* pszPath;
    WCHAR szPath[MAX_FILE_PATH];
    WCHAR szName[MAX_PATH];
    WCHAR szMsg[MAX_FILE_PATH + 512];
    WCHAR szFormat[256];
    WCHAR szSiblingPath[MAX_FILE_PATH] = L"";
    SHFILEOPSTRUCTW op = {};
    WCHAR szFrom[MAX_FILE_PATH + 2] = {};
    int result;

    if (g_hWndTree == NULL)
        return FALSE;

    hItem = TreeView_GetSelection(g_hWndTree);
    if (hItem == NULL || hItem == TreeView_GetRoot(g_hWndTree))
        return FALSE;

    pszPath = GetTreeItemPath(hItem);
    if (pszPath == NULL)
        return FALSE;
    StringCchCopyW(szPath, ARRAYSIZE(szPath), pszPath);

    // Vizinho a guardar para devolver o foco depois.
    hSiblingItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    if (hSiblingItem == NULL)
        hSiblingItem = TreeView_GetPrevSibling(g_hWndTree, hItem);
    if (hSiblingItem != NULL)
    {
        WCHAR* pszSibling = GetTreeItemPath(hSiblingItem);
        if (pszSibling != NULL)
            StringCchCopyW(szSiblingPath, ARRAYSIZE(szSiblingPath), pszSibling);
    }

    // Confirmação localizada.
    {
        LPCWSTR lpName = PathFindFileNameW(szPath);

        StringCchCopyW(szName, ARRAYSIZE(szName),
                       (lpName != NULL && lpName[0] != L'\0') ? lpName : szPath);
        LoadStringResource(IDS_TREE_RECYCLE, szFormat, ARRAYSIZE(szFormat));
        StringCchPrintfW(szMsg, ARRAYSIZE(szMsg), szFormat, szName);
    }

    result = MessageBoxW(g_hWndMain, szMsg, NOTBAD_NAME,
                         MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    TreeRestoreFocus();
    if (result != IDYES)
        return TRUE;   // consumida sem fazer nada

    if (!TreeCloseDocsUnderPath(szPath))
    {
        TreeRestoreFocus();
        return TRUE;   // o usuario cancelou o salvamento: aborta a exclusao
    }

    StringCchCopyW(szFrom, ARRAYSIZE(szFrom) - 1, szPath);
    op.hwnd = g_hWndMain;
    op.wFunc = FO_DELETE;
    op.pFrom = szFrom;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted)
    {
        TreeUndoFailed();
        return TRUE;
    }

    TreeUndoRecord(TREE_UNDO_DELETE, szPath, NULL);

    if (g_treeClipboardCut && !g_treeClipboardPath.empty() &&
        TreePathMatchesOrContains(szPath, g_treeClipboardPath.c_str()))
    {
        TreeClearClipboardMark();
    }

    RefreshFileTree();

    // Foco no vizinho previamente guardado.
    if (szSiblingPath[0] != L'\0')
        TreeSelectItemByPath(szSiblingPath);

    TreeRestoreFocus();
    return TRUE;
}

static void TreeItemDragEnd(void)
{
    if (g_hTreeDragImage != NULL)
    {
        ImageList_DragLeave(g_hWndTree);
        ImageList_EndDrag();
        ImageList_Destroy(g_hTreeDragImage);
        g_hTreeDragImage = NULL;
    }
    if (g_hWndTree != NULL)
        TreeView_SelectDropTarget(g_hWndTree, NULL);
    g_bTreeItemDrag = FALSE;
    g_bTreeDragDropItem = NULL;
    g_treeItemDragSrc.clear();
    ReleaseCapture();   // dispara WM_CAPTURECHANGED (idempotente)
}

// Alvo valido quando soltar faz algo util: a pasta de destino e a pasta do
// item (ou a pasta pai, se for arquivo) e, para mover, o destino nao e o
// local atual nem uma subpasta da origem.
static BOOL TreeDropTargetIsValid(HTREEITEM hTarget, LPCWSTR lpSrcPath, BOOL bMove)
{
    WCHAR szDir[MAX_FILE_PATH];
    WCHAR szSrcDir[MAX_FILE_PATH];

    if (hTarget == NULL || lpSrcPath == NULL)
        return FALSE;
    if (!GetTreeTargetDirectory(hTarget, szDir, ARRAYSIZE(szDir)))
        return FALSE;
    if (!bMove)
        return TRUE;

    StringCchCopyW(szSrcDir, ARRAYSIZE(szSrcDir), lpSrcPath);
    PathRemoveFileSpecW(szSrcDir);
    if (lstrcmpiW(szSrcDir, szDir) == 0)
        return FALSE;                       // no-op: ja esta aqui
    if (TreePathMatchesOrContains(lpSrcPath, szDir))
        return FALSE;                       // dentro de si mesmo
    return TRUE;
}

// Ctrl+X/C/V com o foco no tree: nossas aceleradoras globais (IDM_CUT/COPY/
// PASTE do editor) consomem as teclas antes de o tree as ver; por isso essas
// teclas são roteadas aqui no loop de mensagens, com o tree visível e o foco
// nele, antes do TranslateAcceleratorW.
BOOL HandleTreeAccelerator(MSG* pMsg)
{
    HTREEITEM hSel;
    BOOL bCtrl;

    if (pMsg == NULL || g_hWndTree == NULL)
        return FALSE;
    if (!IsWindowVisible(g_hWndTree))
        return FALSE;
    if (GetFocus() != g_hWndTree)
        return FALSE;
    if (pMsg->message != WM_KEYDOWN && pMsg->message != WM_SYSKEYDOWN)
        return FALSE;
    if (TreeView_GetEditControl(g_hWndTree) != NULL)
        return FALSE;   // durante renomeacao, o campo de texto age sozinho

    bCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!bCtrl)
        return FALSE;

    switch (pMsg->wParam)
    {
    case 'C':
    case 'X':
        hSel = TreeView_GetSelection(g_hWndTree);
        if (hSel == NULL || hSel == TreeView_GetRoot(g_hWndTree))
            return FALSE;
        HandleTreeClipboardKey(hSel, (WCHAR)pMsg->wParam);
        return TRUE;

    case 'V':
        if (g_treeClipboardPath.empty())
            return FALSE;    // nada recortado/copiado no tree: deixa o editor colar
        hSel = TreeView_GetSelection(g_hWndTree);
        HandleTreeClipboardKey(hSel, 'V');
        return TRUE;

    case 'Z':
        return TreeUndoLastOperation();
    }

    return FALSE;
}


static HTREEITEM BuildTreeRoot(LPCWSTR lpPath)
{
    LPCWSTR lpName;
    WCHAR* pszRoot;
    HTREEITEM hRoot;
    TVINSERTSTRUCTW tvis = {};

    pszRoot = new WCHAR[MAX_FILE_PATH];
    if (pszRoot == NULL)
        return NULL;

    StringCchCopyW(pszRoot, MAX_FILE_PATH, lpPath);

    lpName = PathFindFileNameW(lpPath);
    if (lpName == NULL || lpName[0] == L'\0')
        lpName = lpPath;

    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_ROOT;
    tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE |
                     TVIF_PARAM | TVIF_CHILDREN;
    tvis.item.pszText = (LPWSTR)lpName;
    tvis.item.iImage = GetTreeIconIndex(lpPath, FILE_ATTRIBUTE_DIRECTORY);
    tvis.item.iSelectedImage = tvis.item.iImage;
    tvis.item.lParam = (LPARAM)pszRoot;
    tvis.item.cChildren = 1;

    hRoot = TreeView_InsertItem(g_hWndTree, &tvis);
    if (hRoot == NULL)
        delete[] pszRoot;

    return hRoot;
}

static void OpenFolderInTree(LPCWSTR lpPath)
{
    HTREEITEM hRoot;

    if (!CreateFileTree(g_hWndMain))
        return;

    ClearFileTree();

    hRoot = BuildTreeRoot(lpPath);
    if (hRoot == NULL)
        return;

    ShowWindow(g_hWndTree, SW_SHOW);
    TreeView_Expand(g_hWndTree, hRoot, TVE_EXPAND);
    LayoutMainWindow();
    UpdateEditMenuState(g_hWndMain);
}

// Guarda os caminhos das pastas expandidas (e o item selecionado) antes de
// reconstruir a arvore.
static void CollectTreeState(HTREEITEM hItem, std::vector<std::wstring>& expanded)
{
    while (hItem != NULL)
    {
        UINT state = TreeView_GetItemState(g_hWndTree, hItem, TVIS_EXPANDED);

        if (state & TVIS_EXPANDED)
        {
            WCHAR* pszPath = GetTreeItemPath(hItem);
            if (pszPath != NULL)
                expanded.push_back(pszPath);
            CollectTreeState(TreeView_GetChild(g_hWndTree, hItem), expanded);
        }

        hItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    }
}

static BOOL TreePathIsExpanded(const std::vector<std::wstring>& expanded, LPCWSTR lpPath)
{
    size_t i;

    for (i = 0; i < expanded.size(); ++i)
    {
        if (lstrcmpiW(expanded[i].c_str(), lpPath) == 0)
            return TRUE;
    }
    return FALSE;
}

// Reexpande as pastas que estavam abertas (o conteudo e relido do disco pelo
// TVN_ITEMEXPANDING) e reencontra o item selecionado.
static void RestoreTreeState(HTREEITEM hItem, const std::vector<std::wstring>& expanded,
                             LPCWSTR lpSelected, HTREEITEM* phSelected)
{
    while (hItem != NULL)
    {
        WCHAR* pszPath = GetTreeItemPath(hItem);

        if (pszPath != NULL)
        {
            if (lpSelected[0] != L'\0' && lstrcmpiW(pszPath, lpSelected) == 0)
                *phSelected = hItem;

            if (TreePathIsExpanded(expanded, pszPath))
            {
                TreeView_Expand(g_hWndTree, hItem, TVE_EXPAND);
                RestoreTreeState(TreeView_GetChild(g_hWndTree, hItem), expanded,
                                 lpSelected, phSelected);
            }
        }

        hItem = TreeView_GetNextSibling(g_hWndTree, hItem);
    }
}

static void RefreshFileTree(void)
{
    std::vector<std::wstring> expanded;
    std::wstring rootPath;
    std::wstring selPath;
    HTREEITEM hRoot;
    HTREEITEM hSelected = NULL;
    WCHAR* pszPath;

    if (g_hWndTree == NULL)
        return;

    hRoot = TreeView_GetRoot(g_hWndTree);
    pszPath = GetTreeItemPath(hRoot);
    if (pszPath == NULL)
        return;

    rootPath = pszPath;

    pszPath = GetTreeItemPath(TreeView_GetSelection(g_hWndTree));
    if (pszPath != NULL)
        selPath = pszPath;

    CollectTreeState(hRoot, expanded);

    SendMessageW(g_hWndTree, WM_SETREDRAW, FALSE, 0);

    ClearFileTree();
    hRoot = BuildTreeRoot(rootPath.c_str());
    if (hRoot != NULL)
    {
        // A raiz sempre fica expandida.
        if (!TreePathIsExpanded(expanded, rootPath.c_str()))
            expanded.push_back(rootPath);

        RestoreTreeState(hRoot, expanded, selPath.c_str(), &hSelected);
        if (hSelected != NULL)
            TreeView_SelectItem(g_hWndTree, hSelected);
    }

    SendMessageW(g_hWndTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hWndTree, NULL, TRUE);
}

static void OpenFolderDialog(HWND hWnd)
{
    IFileOpenDialog* pDialog = NULL;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&pDialog));
    if (FAILED(hr) || pDialog == NULL)
        return;

    DWORD options = 0;
    if (SUCCEEDED(pDialog->GetOptions(&options)))
    {
        pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                            FOS_PATHMUSTEXIST);
    }

    if (SUCCEEDED(pDialog->Show(hWnd)))
    {
        IShellItem* pItem = NULL;
        if (SUCCEEDED(pDialog->GetResult(&pItem)))
        {
            PWSTR pszPath = NULL;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) &&
                pszPath != NULL)
            {
                OpenFolderInTree(pszPath);
                CoTaskMemFree(pszPath);
            }
            pItem->Release();
        }
    }

    pDialog->Release();
}

static void OpenPathFromTree(LPCWSTR lpPath)
{
    DWORD attrs;
    size_t i;

    if (lpPath == NULL || lpPath[0] == L'\0')
        return;

    attrs = GetFileAttributesW(lpPath);
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return;

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (g_documents[i]->bHasPath &&
            lstrcmpiW(g_documents[i]->szFilePath, lpPath) == 0)
        {
            DocActivate(g_documents[i]);
            return;
        }
    }

    EditorOpen(lpPath);
}

// Grava no registro o estado da arvore: pasta raiz, se esta visivel, pastas
// expandidas e item selecionado. Chamado ao fechar o programa, antes de a
// arvore ser destruida. Sem arvore (nenhuma pasta aberta) limpa o que havia.
static void SaveFileTreeState(void)
{
    std::vector<std::wstring> expanded;
    std::wstring rootPath;
    std::wstring selPath;
    HTREEITEM hRoot;
    WCHAR* pszPath;

    if (g_hWndTree == NULL)
    {
        SettingsSaveTree(L"", FALSE, expanded, L"");
        return;
    }

    hRoot = TreeView_GetRoot(g_hWndTree);
    pszPath = GetTreeItemPath(hRoot);
    if (pszPath == NULL)
    {
        SettingsSaveTree(L"", FALSE, expanded, L"");
        return;
    }

    rootPath = pszPath;

    pszPath = GetTreeItemPath(TreeView_GetSelection(g_hWndTree));
    if (pszPath != NULL)
        selPath = pszPath;

    CollectTreeState(hRoot, expanded);

    // IsTreeVisible olha o estilo WS_VISIBLE da propria arvore; IsWindowVisible
    // daria FALSE aqui, pois a janela principal ja foi ocultada ao fechar.
    SettingsSaveTree(rootPath.c_str(), IsTreeVisible(), expanded, selPath.c_str());
}

// Recria a arvore salva na sessao anterior, com as mesmas pastas expandidas e
// o mesmo item selecionado, visivel ou oculta como estava. Se a pasta raiz nao
// existir mais, a arvore nao e criada.
static void RestoreSavedFileTree(void)
{
    std::vector<std::wstring> expanded;
    std::wstring rootPath;
    std::wstring selPath;
    HTREEITEM hRoot;
    HTREEITEM hSelected = NULL;
    BOOL bVisible = TRUE;
    DWORD attrs;

    if (g_hWndTree != NULL)
        return;

    if (!SettingsLoadTree(rootPath, &bVisible, expanded, selPath))
        return;

    attrs = GetFileAttributesW(rootPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
        return;

    if (!CreateFileTree(g_hWndMain))
        return;

    // Criada oculta quando estava oculta, para nao piscar na inicializacao.
    if (!bVisible)
        ShowWindow(g_hWndTree, SW_HIDE);

    SendMessageW(g_hWndTree, WM_SETREDRAW, FALSE, 0);

    ClearFileTree();
    hRoot = BuildTreeRoot(rootPath.c_str());
    if (hRoot != NULL)
    {
        // A raiz sempre fica expandida.
        if (!TreePathIsExpanded(expanded, rootPath.c_str()))
            expanded.push_back(rootPath);

        RestoreTreeState(hRoot, expanded, selPath.c_str(), &hSelected);
        if (hSelected != NULL)
            TreeView_SelectItem(g_hWndTree, hSelected);
    }

    SendMessageW(g_hWndTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hWndTree, NULL, TRUE);

    LayoutMainWindow();
    UpdateEditMenuState(g_hWndMain);
}

static void ToggleFileTree(void)
{
    if (g_hWndTree == NULL)
        return;

    if (IsWindowVisible(g_hWndTree))
    {
        ShowWindow(g_hWndTree, SW_HIDE);
    }
    else
    {
        RefreshFileTree();
        ShowWindow(g_hWndTree, SW_SHOW);
    }
    LayoutMainWindow();
    UpdateEditMenuState(g_hWndMain);
}

static int TrimTrailingSpaces(LPWSTR lpText, int nLen)
{
    while (nLen > 0 && lpText[nLen - 1] == L' ')
        --nLen;

    return nLen;
}

// Alarga um menu popup anexando espacos ao item mais largo. A largura natural
// do popup e estimada pela largura do texto mais a margem do tema; os espacos
// extras fazem o menu crescer sem alterar o visual dos demais itens.
static void WidenMenuByPercent(HMENU hMenu, int nPercent)
{
    HDC hdc;
    HGDIOBJ hOldFont;
    SIZE spaceSize = {};
    SIZE labelSize;
    SIZE accelSize;
    WCHAR szText[512];
    WCHAR szNew[1024];
    WCHAR* pTab;
    int count;
    int i;
    int len;
    int labelLen;
    int accelLen;
    int maxRow = 0;
    int maxItem = -1;
    int menuHeight;
    int margin;
    int extra;
    int spaces;

    if (hMenu == NULL || nPercent <= 0)
        return;

    count = GetMenuItemCount(hMenu);
    if (count <= 0)
        return;

    hdc = GetDC(NULL);
    if (hdc == NULL)
        return;

    hOldFont = SelectObject(hdc, g_hFontMenu != NULL ? (HGDIOBJ)g_hFontMenu
                                                    : GetStockObject(DEFAULT_GUI_FONT));
    GetTextExtentPoint32W(hdc, L" ", 1, &spaceSize);

    for (i = 0; i < count; ++i)
    {
        len = GetMenuStringW(hMenu, (UINT)i, szText, ARRAYSIZE(szText), MF_BYPOSITION);
        if (len <= 0)
            continue;

        pTab = wcschr(szText, L'\t');
        labelLen = pTab != NULL ? (int)(pTab - szText) : len;
        accelLen = pTab != NULL ? len - labelLen - 1 : 0;
        labelLen = TrimTrailingSpaces(szText, labelLen);

        labelSize.cx = 0;
        accelSize.cx = 0;
        if (labelLen > 0)
            GetTextExtentPoint32W(hdc, szText, labelLen, &labelSize);
        if (accelLen > 0)
            GetTextExtentPoint32W(hdc, pTab + 1, accelLen, &accelSize);

        if (labelSize.cx + accelSize.cx > maxRow)
        {
            maxRow = labelSize.cx + accelSize.cx;
            maxItem = i;
        }
    }

    SelectObject(hdc, hOldFont);
    ReleaseDC(NULL, hdc);

    if (maxItem < 0 || spaceSize.cx <= 0)
        return;

    menuHeight = GetSystemMetrics(SM_CYMENU);
    margin = (menuHeight * 31) / 10;
    extra = ((maxRow + margin) * nPercent) / 100;
    spaces = (extra + spaceSize.cx - 1) / spaceSize.cx;
    if (spaces <= 0 || spaces > 256)
        return;

    len = GetMenuStringW(hMenu, (UINT)maxItem, szText, ARRAYSIZE(szText), MF_BYPOSITION);
    if (len <= 0)
        return;

    pTab = wcschr(szText, L'\t');
    labelLen = pTab != NULL ? (int)(pTab - szText) : len;
    labelLen = TrimTrailingSpaces(szText, labelLen);

    if (spaces + len >= (int)ARRAYSIZE(szNew))
        return;

    memcpy(szNew, szText, (size_t)labelLen * sizeof(WCHAR));
    for (i = 0; i < spaces; ++i)
        szNew[labelLen + i] = L' ';

    if (pTab != NULL)
    {
        int tailLen = len - labelLen;

        memcpy(szNew + labelLen + spaces, pTab, (size_t)tailLen * sizeof(WCHAR));
        szNew[labelLen + spaces + tailLen] = L'\0';
    }
    else
    {
        szNew[labelLen + spaces] = L'\0';
    }

    {
        MENUITEMINFOW mii = {};

        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_STRING;
        mii.dwTypeData = szNew;
        SetMenuItemInfoW(hMenu, (UINT)maxItem, TRUE, &mii);
    }
}

BOOL CreateNotbadWindow(HINSTANCE hInstance, int nCmdShow, LPCWSTR lpFilePath)
{
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int cx = CW_USEDEFAULT;
    int cy = CW_USEDEFAULT;

    if (g_settings.hasWindowRect)
    {
        x = g_settings.windowRect.left;
        y = g_settings.windowRect.top;
        cx = g_settings.windowRect.right - g_settings.windowRect.left;
        cy = g_settings.windowRect.bottom - g_settings.windowRect.top;
    }

    g_hWndMain = CreateWindowExW(WS_EX_ACCEPTFILES,
                                 NOTBAD_CLASS, NOTBAD_NAME,
                                 WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 x, y, cx, cy,
                                 NULL, NULL, hInstance, NULL);

    if (g_hWndMain == NULL)
        return FALSE;

    g_hMenu = GetMenu(g_hWndMain);
    LangApplyMenu(g_hWndMain);

    // O menu Format costuma ficar estreito demais para traducoes longas.
    WidenMenuByPercent(GetSubMenu(g_hMenu, 2), 50);
    DrawMenuBar(g_hWndMain);

    ShowWindow(g_hWndMain, nCmdShow);

    EditorNew();

    if (lpFilePath != NULL && lpFilePath[0] != L'\0')
        DocLoad(g_pActiveDoc, lpFilePath);

    // Reabre a arvore de arquivos como estava ao fechar o programa.
    RestoreSavedFileTree();

    // Aplica ao documento inicial a indentacao carregada do registro.
    EditorApplyIndentation();

    UpdateWindow(g_hWndMain);
    UpdateWindowTitle();
    UpdateStatusBar();

    return TRUE;
}

void UpdateWindowTitle(void)
{
    WCHAR szTitle[MAX_FILE_PATH + 64];
    WCHAR szName[MAX_PATH];

    if (g_pActiveDoc == NULL)
    {
        SetWindowTextW(g_hWndMain, NOTBAD_NAME);
        return;
    }

    DocGetDisplayName(g_pActiveDoc, szName, ARRAYSIZE(szName));

    StringCchPrintfW(szTitle, ARRAYSIZE(szTitle), L"%s%s - %s",
                     EditorIsModified() ? L"*" : L"",
                     szName, NOTBAD_NAME);

    SetWindowTextW(g_hWndMain, szTitle);
}

void UpdateStatusBar(void)
{
    TextEncoding encoding = ENC_ANSI;

    if (g_hWndStatus == NULL || !g_settings.statusBar)
        return;

    if (g_hWndEdit == NULL)
        return;

    if (g_pActiveDoc != NULL)
        encoding = g_pActiveDoc->encoding;

    // EM_GETSEL so devolve 16 bits por posicao e quebra acima de 64K.
    CHARRANGE cr = {};
    SendMessageW(g_hWndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int caret = (int)cr.cpMin;
    if (caret < 0)
        caret = 0;

    // Linha/coluna logicas (paragrafo), nao visuais: com word wrap o
    // EM_LINEFROMCHAR apontaria para uma continuacao da mesma linha.
    int line = EditorGetParagraphNumber(g_hWndEdit, caret);
    if (line < 1)
        line = 1;
    int lineIndex = EditorGetParagraphStart(g_hWndEdit, caret);
    if (lineIndex < 0 || lineIndex > caret)
        lineIndex = 0;
    int col = caret - lineIndex;

    WCHAR szLine[128];
    WCHAR szCol[128];
    WCHAR szEnc[64];
    WCHAR szIndent[64];
    WCHAR szFormat[64];

    LoadStringResource(IDS_STATUS_LINE, szFormat, ARRAYSIZE(szFormat));
    StringCchPrintfW(szLine, ARRAYSIZE(szLine), szFormat, (UINT)line, (UINT)(col + 1));

    // Comprimento no mesmo espaco de indices do RichEdit (CR, nao CRLF).
    GETTEXTLENGTHEX gtl = {};
    gtl.flags = GTL_DEFAULT | GTL_NUMCHARS;
    gtl.codepage = 1200;
    int length = (int)SendMessageW(g_hWndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    LoadStringResource(IDS_STATUS_COL, szFormat, ARRAYSIZE(szFormat));
    if (length <= 0)
        StringCchPrintfW(szCol, ARRAYSIZE(szCol), szFormat, 100u);
    else
        StringCchPrintfW(szCol, ARRAYSIZE(szCol), szFormat, (UINT)((caret * 100) / length));

    switch (encoding)
    {
    case ENC_UTF16LE: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"UTF-16 LE"); break;
    case ENC_UTF16BE: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"UTF-16 BE"); break;
    case ENC_UTF8_BOM: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), EncodingName(ENC_UTF8_BOM)); break;
    case ENC_UTF8: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"UTF-8"); break;
    case ENC_GB18030: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"GB18030"); break;
    case ENC_OEM: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"OEM"); break;
    default: StringCchCopyW(szEnc, ARRAYSIZE(szEnc), L"ANSI"); break;
    }

    RECT rc;
    GetClientRect(g_hWndMain, &rc);
    UpdateStatusBarParts(rc.right);

    SendMessageW(g_hWndStatus, SB_SETTEXTW, 0, (LPARAM)szLine);
    szIndent[0] = L'\0';
    if (g_settings.autoIndent)
        LoadStringResource(IDS_STATUS_AUTOINDENT, szIndent, ARRAYSIZE(szIndent));

    SendMessageW(g_hWndStatus, SB_SETTEXTW, 1, (LPARAM)szIndent);
    SendMessageW(g_hWndStatus, SB_SETTEXTW, 2, (LPARAM)szEnc);
    SendMessageW(g_hWndStatus, SB_SETTEXTW, 3, (LPARAM)szCol);
}

void UpdateEditMenuState(HWND hWnd)
{
    HMENU hMenu = GetMenu(hWnd);
    BOOL hasDoc;
    BOOL canUndo;
    BOOL canPaste;
    BOOL hasSelection;
    BOOL anyModified = FALSE;
    CHARRANGE cr = {};
    size_t tabCount;
    size_t i;

    if (hMenu == NULL)
        return;

    hasDoc = (g_pActiveDoc != NULL && g_hWndEdit != NULL);
    tabCount = g_documents.size();

    for (i = 0; i < g_documents.size(); ++i)
    {
        if (SendMessageW(g_documents[i]->hEdit, EM_GETMODIFY, 0, 0))
        {
            anyModified = TRUE;
            break;
        }
    }

    if (hasDoc)
        SendMessageW(g_hWndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    hasSelection = (cr.cpMax > cr.cpMin);
    canUndo = hasDoc && (BOOL)SendMessageW(g_hWndEdit, EM_CANUNDO, 0, 0);
    canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
               IsClipboardFormatAvailable(CF_TEXT);

    EnableMenuItem(hMenu, IDM_UNDO, canUndo ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_CUT, hasSelection ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_COPY, hasSelection ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_PASTE, canPaste ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_DELETE, hasSelection ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_FINDNEXT, MF_ENABLED);
    EnableMenuItem(hMenu, IDM_SAVEALL, anyModified ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_CLOSETAB, hasDoc ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_NEXT_TAB, tabCount > 1 ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_PREV_TAB, tabCount > 1 ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, IDM_TREE, g_hWndTree != NULL ? MF_ENABLED : MF_GRAYED);

    CheckMenuItem(hMenu, IDM_WORDWRAP,
                  MF_BYCOMMAND | (g_settings.wordWrap ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_STATUS,
                  MF_BYCOMMAND | (g_settings.statusBar ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_TREE,
                  MF_BYCOMMAND | (g_hWndTree != NULL && IsWindowVisible(g_hWndTree)
                                  ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_LINENUMBERS,
                  MF_BYCOMMAND | (g_settings.showLineNumbers ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_HIGHLIGHTLINE,
                  MF_BYCOMMAND | (g_settings.highlightCurrentLine ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, IDM_HIGHLIGHTCODE,
                  MF_BYCOMMAND | (g_settings.codeHighlighting ? MF_CHECKED : MF_UNCHECKED));

    // Format > Encoding: marca a codificacao do documento ativo. Se ela for
    // uma das que o menu nao oferece (UTF-16 BE, OEM...), nenhum item fica marcado.
    {
        TextEncoding docEnc = hasDoc ? g_pActiveDoc->encoding : ENC_UTF8;

        EnableMenuItem(hMenu, IDM_ENC_ANSI, hasDoc ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, IDM_ENC_UTF8, hasDoc ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, IDM_ENC_UTF8BOM, hasDoc ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(hMenu, IDM_ENC_UTF16LE, hasDoc ? MF_ENABLED : MF_GRAYED);
        CheckMenuItem(hMenu, IDM_ENC_ANSI,
                      MF_BYCOMMAND | (hasDoc && docEnc == ENC_ANSI ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_ENC_UTF8,
                      MF_BYCOMMAND | (hasDoc && docEnc == ENC_UTF8 ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_ENC_UTF8BOM,
                      MF_BYCOMMAND | (hasDoc && docEnc == ENC_UTF8_BOM ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(hMenu, IDM_ENC_UTF16LE,
                      MF_BYCOMMAND | (hasDoc && docEnc == ENC_UTF16LE ? MF_CHECKED : MF_UNCHECKED));
    }
}

static void ToggleWordWrap(void)
{
    g_settings.wordWrap = !g_settings.wordWrap;
    EditorApplyWordWrap(g_settings.wordWrap);
    InvalidateLineNumbers();
    if (g_pActiveDoc != NULL)
    {
        g_pActiveDoc->bCodeHighlightViewValid = FALSE;
        EditorScheduleUniversalHighlight(g_pActiveDoc);
    }
    SettingsSave();
}

static void ToggleStatusBar(void)
{
    g_settings.statusBar = !g_settings.statusBar;
    ShowWindow(g_hWndStatus, g_settings.statusBar ? SW_SHOW : SW_HIDE);
    LayoutChildren(g_hWndMain);
    UpdateStatusBar();
    SettingsSave();
}

// ---------------------------------------------------------------------------
// Caret do modo Overwrite. O RichEdit recria o proprio caret ao rolar (barra,
// roda) e ao mudar o zoom sem avisar por notificacao, e o bloco voltava a ser
// a barra fina mesmo com o documento ainda em Overwrite. Um timer curto compara
// posicao de scroll e zoom e reaplica a forma do caret quando algo muda.
// ---------------------------------------------------------------------------
#define CARET_SHAPE_TIMER      0x4E43
#define CARET_SHAPE_POLL_MS    40

static HWND  s_hCaretWatchEdit = NULL;
static POINT s_caretWatchScroll = { -1, -1 };
static int   s_caretWatchZoomNum = -1;
static int   s_caretWatchZoomDen = -1;
static int   s_caretWatchSelStart = -1;
static int   s_caretWatchSelEnd = -1;

// Mensagem postada pelo EN_SELCHANGE: o RichEdit recria o proprio caret DEPOIS de
// notificar a mudanca de selecao (clique do mouse, setas), entao a forma do
// Overwrite precisa ser reaplicada quando o tratamento dele ja terminou.
static UINT GetReapplyCaretMessage(void)
{
    static UINT s_msg = 0;

    if (s_msg == 0)
        s_msg = RegisterWindowMessageW(L"Notbad.ReapplyCaret");
    return s_msg;
}

// Mensagem postada pelo TVN_BEGINLABELEDITW: a TreeView seleciona o rótulo
// inteiro depois de esse evento e ele apagaria a seleção que fizemos; aqui a
// seleção do nome base (sem a extensão) é reaplicada quando o controle está
// estabilizado.
static UINT GetLabelSelectBaseMessage(void)
{
    static UINT s_msg = 0;

    if (s_msg == 0)
        s_msg = RegisterWindowMessageW(L"Notbad.LabelSelectBase");
    return s_msg;
}

static void CheckOverwriteCaretShape(void)
{
    Document* pDoc;
    POINT scroll = { 0, 0 };
    int zoomNum = 0;
    int zoomDen = 0;
    int selStart = 0;
    int selEnd = 0;

    if (g_hWndEdit == NULL || !IsWindow(g_hWndEdit) || GetFocus() != g_hWndEdit)
    {
        s_hCaretWatchEdit = NULL;
        return;
    }

    pDoc = DocFromEdit(g_hWndEdit);
    if (pDoc == NULL || !pDoc->bOverwrite)
    {
        s_hCaretWatchEdit = NULL;
        return;
    }

    SendMessageW(g_hWndEdit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(g_hWndEdit, EM_GETZOOM, (WPARAM)&zoomNum, (LPARAM)&zoomDen);
    SendMessageW(g_hWndEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);

    if (s_hCaretWatchEdit == g_hWndEdit &&
        scroll.x == s_caretWatchScroll.x && scroll.y == s_caretWatchScroll.y &&
        zoomNum == s_caretWatchZoomNum && zoomDen == s_caretWatchZoomDen &&
        selStart == s_caretWatchSelStart && selEnd == s_caretWatchSelEnd)
    {
        return;
    }

    s_hCaretWatchEdit = g_hWndEdit;
    s_caretWatchScroll = scroll;
    s_caretWatchZoomNum = zoomNum;
    s_caretWatchZoomDen = zoomDen;
    s_caretWatchSelStart = selStart;
    s_caretWatchSelEnd = selEnd;

    EditorUpdateCaretShape(g_hWndEdit);
}

LRESULT CALLBACK NotbadWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == GetReapplyCaretMessage())
    {
        HWND hEdit = (HWND)lParam;

        if (hEdit != NULL && IsWindow(hEdit))
            EditorUpdateCaretShape(hEdit);
        return 0;
    }

    // Reaplica a selecao do nome base depois de a TreeView estabilizar o
    // controle de edicao do rotulo.
    if (msg == GetLabelSelectBaseMessage())
    {
        HWND hEdit = NULL;

        if (g_hWndTree != NULL)
            hEdit = TreeView_GetEditControl(g_hWndTree);

        if (hEdit != NULL)
        {
            WCHAR szText[MAX_PATH] = {};

            GetWindowTextW(hEdit, szText, ARRAYSIZE(szText));

            int len = lstrlenW(szText);
            int selEnd = len;

            HTREEITEM hItem = TreeView_GetSelection(g_hWndTree);
            WCHAR* pszPath = (hItem != NULL) ? GetTreeItemPath(hItem) : NULL;
            BOOL isDir = (pszPath != NULL && PathIsDirectoryW(pszPath));

            if (!isDir)
            {
                LPCWSTR lpExt = PathFindExtensionW(szText);

                if (lpExt != NULL && lpExt[0] != L'\0' && lpExt != szText)
                {
                    int extLen = lstrlenW(lpExt);

                    if (extLen < len)
                        selEnd = len - extLen;
                }
            }

            SendMessageW(hEdit, EM_SETSEL, 0, (LPARAM)selEnd);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_TIMER:
        if (wParam == CARET_SHAPE_TIMER)
        {
            CheckOverwriteCaretShape();
            return 0;
        }
        if (wParam == NOTBAD_CODE_HIGHLIGHT_TIMER)
        {
            EditorProcessViewportHighlight();
            return 0;
        }
        break;

    case WM_CREATE:
        g_hWndMain = hWnd;
        SetTimer(hWnd, CARET_SHAPE_TIMER, CARET_SHAPE_POLL_MS, NULL);
        CreateStatusBar(hWnd);
        CreateTabControl(hWnd);
        CreateLineNumbers(hWnd);
        LayoutChildren(hWnd);
        DragAcceptFiles(hWnd, TRUE);
        AddClipboardFormatListener(hWnd);
        return 0;

    case WM_SIZE:
        LayoutChildren(hWnd);
        if (g_pActiveDoc != NULL)
        {
            g_pActiveDoc->bCodeHighlightViewValid = FALSE;
            EditorScheduleUniversalHighlight(g_pActiveDoc);
        }
        return 0;

    case WM_CLIPBOARDUPDATE:
        // O usuario colocou algo novo na Area de Transferencia global: se
        // havia um item recortado (translucido) pendente no tree, o estado
        // dele deve voltar ao normal.
        if (g_treeCutPending)
            TreeClearClipboardMark();
        return 0;

    case WM_ERASEBKGND:
    {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        FillRect((HDC)wParam, &rcClient, (HBRUSH)(COLOR_BTNFACE + 1));
        return 1;
    }

    case WM_NOTBAD_CLOSETAB:
    {
        int index = (int)wParam;

        g_tabHotClose = -1;
        if (index >= 0 && index < (int)g_documents.size())
            DocClose(g_documents[index]);
        return 0;
    }

    case WM_NOTBAD_NEWVERSION:
    {
        LPWSTR lpRemote = (LPWSTR)lParam;

        if (lpRemote != NULL)
        {
            WCHAR szFormat[512];
            WCHAR szMessage[640];

            LoadStringResource(IDS_NEW_VERSION, szFormat, ARRAYSIZE(szFormat));
            StringCchPrintfW(szMessage, ARRAYSIZE(szMessage), szFormat,
                             lpRemote, NOTBAD_VERSION);

            if (MessageBoxW(hWnd, szMessage, NOTBAD_NAME,
                            MB_ICONINFORMATION | MB_YESNO) == IDYES)
            {
                ShellExecuteW(hWnd, L"open", NOTBAD_RELEASES_URL,
                              NULL, NULL, SW_SHOWNORMAL);
            }

            LocalFree(lpRemote);
        }
        return 0;
    }

    case WM_SETCURSOR:
    {
        RECT rcSplit;
        POINT pt;

        if (LOWORD(lParam) == HTCLIENT)
        {
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            if (g_bTreeItemDrag)
            {
                SetCursor(LoadCursorW(NULL,
                    g_bTreeDragDropItem != NULL ? IDC_SIZENS : IDC_NO));
                return TRUE;
            }
            if (g_bTreeDrag ||
                (GetTreeSplitterRect(&rcSplit) && PtInRect(&rcSplit, pt)))
            {
                SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        RECT rcSplit;
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };

        if (GetTreeSplitterRect(&rcSplit) && PtInRect(&rcSplit, pt))
        {
            g_bTreeDrag = TRUE;
            g_treeDragOffset = pt.x - rcSplit.left;
            SetCapture(hWnd);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (g_bTreeItemDrag)
        {
            POINT pt;
            HTREEITEM hTarget;
            TVHITTESTINFO ht = {};
            BOOL overInvalid = FALSE;

            GetCursorPos(&pt);
            ScreenToClient(g_hWndTree, &pt);

            if (g_hTreeDragImage != NULL)
                ImageList_DragMove(pt.x, pt.y);

            ht.pt = pt;
            hTarget = TreeView_HitTest(g_hWndTree, &ht);
            if ((ht.flags & TVHT_ONITEM) == 0)
                hTarget = NULL;

            if (hTarget != NULL &&
                !TreeDropTargetIsValid(hTarget, g_treeItemDragSrc.c_str(), TRUE))
            {
                hTarget = NULL;
                overInvalid = TRUE;
            }

            g_bTreeDragDropItem = hTarget;
            if (g_hTreeDragImage != NULL)
                ImageList_DragShowNolock(FALSE);
            TreeView_SelectDropTarget(g_hWndTree, hTarget);
            if (g_hTreeDragImage != NULL)
                ImageList_DragShowNolock(TRUE);

            if (hTarget != NULL)
                SetCursor(LoadCursorW(NULL, IDC_SIZENS));
            else if (overInvalid)
                SetCursor(LoadCursorW(NULL, IDC_NO));
            else
                SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return 0;
        }
        if (g_bTreeDrag)
        {
            RECT rcClient;
            int width = (short)LOWORD(lParam) - g_treeDragOffset;

            GetClientRect(hWnd, &rcClient);
            width = ClampTreeWidth(width, rcClient.right);
            if (width != g_settings.treeWidth)
            {
                g_settings.treeWidth = width;
                LayoutChildren(hWnd);
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (g_bTreeItemDrag)
        {
            HTREEITEM hTarget = g_bTreeDragDropItem;
            std::wstring srcPath = g_treeItemDragSrc;
            WCHAR szDir[MAX_FILE_PATH];

            TreeItemDragEnd();

            if (hTarget != NULL && !srcPath.empty() &&
                GetTreeTargetDirectory(hTarget, szDir, ARRAYSIZE(szDir)))
            {
                TreeCopyOrMoveItem(srcPath.c_str(), szDir, TRUE);
            }
            return 0;
        }
        if (g_bTreeDrag)
        {
            ReleaseCapture();   // dispara WM_CAPTURECHANGED (g_bTreeDrag = FALSE)
            LayoutChildren(hWnd);
            InvalidateLineNumbers();
            SettingsSave();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        g_bTreeDrag = FALSE;
        if (g_bTreeItemDrag)
            TreeItemDragEnd();
        break;

    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETNONCLIENTMETRICS || wParam == 0)
        {
            HFONT hOldMenuFont = g_hFontMenu;

            if (SettingsRefreshMenuFont())
            {
                if (g_hWndTree != NULL)
                    SendMessageW(g_hWndTree, WM_SETFONT, (WPARAM)g_hFontMenu, TRUE);
                if (g_hWndTabs != NULL)
                {
                    size_t k;

                    SendMessageW(g_hWndTabs, WM_SETFONT, (WPARAM)g_hFontMenu, TRUE);
                    TabUpdatePadding();
                    for (k = 0; k < g_documents.size(); ++k)
                        DocUpdateTabTitle(g_documents[k]);
                }
                if (hOldMenuFont != NULL)
                    DeleteObject(hOldMenuFont);
                LayoutChildren(hWnd);
            }
        }
        break;

    case WM_ACTIVATEAPP:
        // Ao voltar o foco para o Notbad, relê as pastas da arvore.
        if (wParam && g_hWndTree != NULL && IsWindowVisible(g_hWndTree))
            RefreshFileTree();
        break;

    case WM_SETFOCUS:
        if (g_hWndEdit != NULL)
            SetFocus(g_hWndEdit);
        return 0;

    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;

        if (hdr->hwndFrom == g_hWndTabs && hdr->code == TCN_SELCHANGE)
        {
            if (!g_bUpdatingTabs)
            {
                int index = TabCtrl_GetCurSel(g_hWndTabs);
                if (index >= 0 && index < (int)g_documents.size())
                    DocActivate(g_documents[index]);
            }
            return 0;
        }

        if (g_hWndTree != NULL && hdr->hwndFrom == g_hWndTree)
        {
            if (hdr->code == TVN_ITEMEXPANDINGW)
            {
                NMTREEVIEWW* pnm = (NMTREEVIEWW*)lParam;

                if (pnm->action == TVE_EXPAND &&
                    TreeView_GetChild(g_hWndTree, pnm->itemNew.hItem) == NULL)
                {
                    WCHAR* pszPath = GetTreeItemPath(pnm->itemNew.hItem);
                    if (pszPath != NULL)
                        PopulateTreeItem(pnm->itemNew.hItem, pszPath);
                }
                return 0;
            }

            if (hdr->code == TVN_BEGINLABELEDITW)
            {
                BOOL cancel = HandleTreeBeginLabelEdit((NMTVDISPINFOW*)lParam);
                if (!cancel)
                    PostMessageW(hWnd, GetLabelSelectBaseMessage(), 0, 0);
                return cancel ? 1 : 0;
            }

            if (hdr->code == TVN_ENDLABELEDITW)
            {
                BOOL bAccepted = HandleTreeEndLabelEdit((NMTVDISPINFOW*)lParam);
                SetWindowLongPtrW(hWnd, DWLP_MSGRESULT, bAccepted ? TRUE : FALSE);
                return TRUE;
            }

            if (hdr->code == TVN_BEGINDRAGW)
            {
                NMTREEVIEWW* pnm = (NMTREEVIEWW*)lParam;
                HTREEITEM hItem = pnm->itemNew.hItem;
                WCHAR* pszPath = GetTreeItemPath(hItem);
                POINT pt;

                if (pszPath == NULL)
                    return 0;
                if (hItem == TreeView_GetRoot(g_hWndTree))
                    return 0;   // a raiz nao se arrasta
                if (TreeView_GetEditControl(g_hWndTree) != NULL)
                    return 0;   // sem arrasto durante renomeacao

                g_treeItemDragSrc = pszPath;
                g_bTreeItemDrag = TRUE;
                g_bTreeDragDropItem = NULL;

                GetCursorPos(&pt);
                ScreenToClient(g_hWndTree, &pt);

                g_hTreeDragImage = TreeView_CreateDragImage(g_hWndTree, hItem);
                if (g_hTreeDragImage != NULL)
                {
                    ImageList_BeginDrag(g_hTreeDragImage, 0, 0, 0);
                    ImageList_DragEnter(g_hWndTree, pt.x, pt.y);
                    ImageList_DragShowNolock(TRUE);
                }
                SetCapture(hWnd);
                return 0;
            }

            if (hdr->code == TVN_KEYDOWN)
            {
                NMTVKEYDOWN* ptvkd = (NMTVKEYDOWN*)lParam;

                if (ptvkd != NULL)
                {
                    HTREEITEM hSel = TreeView_GetSelection(g_hWndTree);

                    if (ptvkd->wVKey == VK_F2 &&
                        TreeView_GetEditControl(g_hWndTree) == NULL)
                        TreeBeginRename(hSel);
                    else if (ptvkd->wVKey == VK_ESCAPE)
                    {
                        if (g_bTreeItemDrag)
                        {
                            TreeItemDragEnd();
                            return 0;
                        }
                    }
                    else if (ptvkd->wVKey == VK_DELETE)
                        TreeDeleteSelected();
                    else if (ptvkd->wVKey == 'X' || ptvkd->wVKey == 'C' ||
                             ptvkd->wVKey == 'V')
                        HandleTreeClipboardKey(hSel, ptvkd->wVKey);
                }
                return 0;
            }

            if (hdr->code == NM_DBLCLK)
            {
                WCHAR* pszPath = GetTreeItemPath(TreeView_GetSelection(g_hWndTree));
                if (pszPath != NULL)
                    OpenPathFromTree(pszPath);
                return 0;
            }
        }

        if (hdr->code == EN_VSCROLL && DocFromEdit(hdr->hwndFrom) == g_pActiveDoc)
        {
            Document* pScrollDoc = DocFromEdit(hdr->hwndFrom);

            InvalidateLineNumbers();
            if (pScrollDoc != NULL)
                EditorScheduleUniversalHighlight(pScrollDoc);
            return 0;
        }

        if (hdr->code == EN_SELCHANGE && DocFromEdit(hdr->hwndFrom) == g_pActiveDoc)
        {
            EditorUpdateCurrentLineHighlight(hdr->hwndFrom);
            EditorUpdateCaretShape(hdr->hwndFrom);
            {
                Document* pSelDoc = DocFromEdit(hdr->hwndFrom);

                // Reaplica de novo depois que o RichEdit terminar de tratar o clique.
                if (pSelDoc != NULL && pSelDoc->bOverwrite)
                    PostMessageW(hWnd, GetReapplyCaretMessage(), 0, (LPARAM)hdr->hwndFrom);

                // Ctrl+Home, PageUp/PageDown e cliques que rolam a tela nao
                // passam por EN_VSCROLL. Comparar a primeira linha visivel
                // cobre esses saltos sem reagendar o viewport a cada tecla.
                if (pSelDoc != NULL)
                {
                    CHARRANGE cr = {};
                    int firstVisible;

                    SendMessageW(hdr->hwndFrom, EM_EXGETSEL, 0, (LPARAM)&cr);
                    firstVisible = (int)SendMessageW(hdr->hwndFrom,
                                                    EM_GETFIRSTVISIBLELINE, 0, 0);

                    if (firstVisible != pSelDoc->codeHighlightViewFirstLine ||
                        cr.cpMax < pSelDoc->codeHighlightViewStart ||
                        cr.cpMax > pSelDoc->codeHighlightViewEnd)
                    {
                        pSelDoc->codeHighlightViewFirstLine = firstVisible;
                        EditorScheduleUniversalHighlight(pSelDoc);
                    }
                }
            }
            UpdateStatusBar();
            UpdateEditMenuState(hWnd);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
    {
        UINT id = LOWORD(wParam);
        UINT code = HIWORD(wParam);
        HWND hCtl = (HWND)lParam;
        Document* pCtlDoc = DocFromEdit(hCtl);

        if (pCtlDoc != NULL)
        {
            if (code == EN_CHANGE)
            {
                // Qualquer edicao nova quebra o agrupamento do Replace All.
                pCtlDoc->bReplaceAllPending = FALSE;
                DocUpdateTabTitle(pCtlDoc);
                EditorUpdateHorizontalScrollBar(hCtl);
                if (pCtlDoc == g_pActiveDoc)
                {
                    UpdateWindowTitle();
                    UpdateStatusBar();
                    InvalidateLineNumbersOnEdit(hCtl);
                    EditorInvalidateUniversalHighlight(pCtlDoc);
                    EditorUpdateCurrentLineHighlight(hCtl);
                    EditorUpdateCaretShape(hCtl);
                }
                UpdateEditMenuState(g_hWndMain);
            }
            return 0;
        }

        switch (id)
        {
        case IDM_NEW:
            if (GetFocus() == g_hWndTree)
                TreeCreateNewFile();
            else
                EditorNew();
            return 0;

        case IDM_OPEN:
        {
            WCHAR szFile[MAX_FILE_PATH] = {};
            WCHAR szFilter[512];
            WCHAR szText[128];
            WCHAR szAll[128];

            LoadStringResource(IDS_FILTER_TEXT, szText, ARRAYSIZE(szText));
            LoadStringResource(IDS_FILTER_ALL, szAll, ARRAYSIZE(szAll));

            StringCchPrintfW(szFilter, ARRAYSIZE(szFilter),
                             L"%s%c*.txt%c%s%c*.*%c",
                             szText, L'\0', L'\0', szAll, L'\0', L'\0');

            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFilter = szFilter;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = ARRAYSIZE(szFile);
            ofn.lpstrDefExt = L"txt";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

            if (GetOpenFileNameW(&ofn))
                EditorOpen(szFile);
            return 0;
        }

        case IDM_OPENFOLDER:
            OpenFolderDialog(hWnd);
            return 0;

        case IDM_TREE:
            ToggleFileTree();
            return 0;

        case IDM_FOCUS_TREE:
            if (g_hWndTree != NULL && IsWindowVisible(g_hWndTree))
                SetFocus(g_hWndTree);
            return 0;

        case IDM_FOCUS_EDIT:
            if (g_hWndEdit != NULL)
                SetFocus(g_hWndEdit);
            return 0;

        case IDM_LINENUMBERS:
            ToggleLineNumbers();
            return 0;

        case IDM_HIGHLIGHTLINE:
            ToggleCurrentLineHighlight();
            return 0;

        case IDM_HIGHLIGHTCODE:
            ToggleCodeHighlighting();
            return 0;

        case IDM_INDENT_TOGGLE:
            ToggleAutoIndent();
            return 0;

        case IDM_SAVE:
            EditorSave(FALSE);
            return 0;

        case IDM_SAVEAS:
            EditorSave(TRUE);
            return 0;

        case IDM_SAVEALL:
            DocSaveAll();
            return 0;

        case IDM_CLOSETAB:
            DocClose(g_pActiveDoc);
            return 0;

        case IDM_NEXT_TAB:
            DocCycle(1);
            return 0;

        case IDM_PREV_TAB:
            DocCycle(-1);
            return 0;

        case IDM_PAGESETUP:
            DoPageSetup(hWnd);
            return 0;

        case IDM_PRINT:
            DoPrint(hWnd);
            return 0;

        case IDM_EXIT:
            SendMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;

        case IDM_UNDO:
            if (g_hWndEdit != NULL)
            {
                Document* pDoc = DocFromEdit(g_hWndEdit);

                // Replace All em todas as abas e uma acao unica: desfaz em cada
                // aba marcada e limpa as marcacoes.
                if (pDoc != NULL && pDoc->bReplaceAllPending)
                {
                    size_t i;

                    for (i = 0; i < g_documents.size(); ++i)
                    {
                        Document* pUndoDoc = g_documents[i];

                        if (!pUndoDoc->bReplaceAllPending)
                            continue;

                        pUndoDoc->bReplaceAllPending = FALSE;
                        SendMessageW(pUndoDoc->hEdit, EM_UNDO, 0, 0);
                        CollapseSelectionAfterUndo(pUndoDoc->hEdit);
                        EditorRestartUniversalHighlight(pUndoDoc, FALSE);
                        EditorUpdateCurrentLineHighlight(pUndoDoc->hEdit);
                        DocUpdateTabTitle(pUndoDoc);
                    }
                }
                else
                {
                    SendMessageW(g_hWndEdit, EM_UNDO, 0, 0);
                    CollapseSelectionAfterUndo(g_hWndEdit);
                }

                // Forca o cursor a redesenhar no formato correto imediatamente
                EditorUpdateCaretShape(g_hWndEdit);

                // Undo pode alterar varias linhas de uma vez; o estado de
                // edicao so cobre o paragrafo atual, entao refaz o viewport.
                if (pDoc != NULL)
                {
                    EditorRestartUniversalHighlight(pDoc, FALSE);
                    EditorProcessViewportHighlight();
                }
            }
            UpdateWindowTitle();
            UpdateStatusBar();
            UpdateEditMenuState(g_hWndMain);
            return 0;

        case IDM_CUT:
            if (g_hWndEdit != NULL)
                SendMessageW(g_hWndEdit, WM_CUT, 0, 0);
            return 0;

        case IDM_COPY:
            if (g_hWndEdit != NULL)
                SendMessageW(g_hWndEdit, WM_COPY, 0, 0);
            return 0;

        case IDM_PASTE:
            if (g_hWndEdit != NULL)
                SendMessageW(g_hWndEdit, WM_PASTE, 0, 0);
            return 0;

        case IDM_DELETE:
            if (g_hWndEdit != NULL)
                SendMessageW(g_hWndEdit, WM_CLEAR, 0, 0);
            return 0;

        case IDM_FIND:
            FindTextDialog(FALSE);
            return 0;

        case IDM_FINDNEXT:
            FindNext();
            return 0;

        case IDM_FINDPREV:
            FindPrevious();
            return 0;

        case IDM_REPLACE:
            FindTextDialog(TRUE);
            return 0;

        case IDM_GOTO:
            EditorGoToLine();
            return 0;

        case IDM_SELECTALL:
            EditorSelectAll();
            return 0;

        case IDM_TIMEDATE:
            EditorInsertTimeDate();
            return 0;

        case IDM_WORDWRAP:
            ToggleWordWrap();
            return 0;

        case IDM_ZOOM_IN:
            EditorZoom(1);
            return 0;

        case IDM_ZOOM_OUT:
            EditorZoom(-1);
            return 0;

        case IDM_ZOOM_RESET:
            EditorZoomReset();
            return 0;

        case IDM_FONT:
        {
            CHOOSEFONTW cf = {};
            cf.lStructSize = sizeof(cf);
            cf.hwndOwner = hWnd;
            cf.lpLogFont = &g_settings.logFont;
            cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;

            if (ChooseFontW(&cf))
            {
                HFONT hFont = CreateFontIndirectW(&g_settings.logFont);
                if (hFont != NULL)
                {
                    if (g_hFont != NULL)
                        DeleteObject(g_hFont);
                    g_hFont = hFont;
                    EditorSetFont(g_hFont);
                    if (g_hWndLineNumbers != NULL)
                        SendMessageW(g_hWndLineNumbers, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                    LayoutMainWindow();
                    InvalidateLineNumbers();
                    SettingsSaveFont();
                }
            }
            return 0;
        }

        case IDM_INDENT:
            ShowIndentDialog(hWnd);
            return 0;

        case IDM_ENC_ANSI:
            EditorSetEncoding(ENC_ANSI);
            return 0;

        case IDM_ENC_UTF8:
            EditorSetEncoding(ENC_UTF8);
            return 0;

        case IDM_ENC_UTF8BOM:
            EditorSetEncoding(ENC_UTF8_BOM);
            return 0;

        case IDM_ENC_UTF16LE:
            EditorSetEncoding(ENC_UTF16LE);
            return 0;

        case IDM_STATUS:
            ToggleStatusBar();
            return 0;

        case IDM_HELP:
            ShowHelp(hWnd);
            return 0;

        case IDM_ABOUT:
            ShowAboutDialog(hWnd);
            return 0;

        default:
            break;
        }
        break;
    }

    case WM_INITMENUPOPUP:
        UpdateEditMenuState(hWnd);
        return 0;

    case WM_DROPFILES:
    {
        HDROP hDrop = (HDROP)wParam;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        WCHAR szFile[MAX_FILE_PATH];
        DWORD attrs;
        UINT i;

        // Uma unica pasta arrastada abre o File Tree exatamente nela. Com mais
        // de um item no arrasto, este atalho nao vale.
        if (count == 1 && DragQueryFileW(hDrop, 0, szFile, ARRAYSIZE(szFile)) != 0)
        {
            attrs = GetFileAttributesW(szFile);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                OpenFolderInTree(szFile);
                DragFinish(hDrop);
                SetForegroundWindow(hWnd);
                return 0;
            }
        }

        for (i = 0; i < count; ++i)
        {
            if (DragQueryFileW(hDrop, i, szFile, ARRAYSIZE(szFile)) == 0)
                continue;

            // Pasta em arrasto misto: ignorada, em vez de tentar abri-la como
            // arquivo e mostrar "Cannot open the file".
            attrs = GetFileAttributesW(szFile);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
                continue;

            EditorOpenDropped(szFile);
        }

        DragFinish(hDrop);
        SetForegroundWindow(hWnd);
        return 0;
    }

    case WM_QUERYENDSESSION:
        return DocQuerySaveAllChanges() ? TRUE : FALSE;

    case WM_CLOSE:
        if (DocQuerySaveAllChanges())
            DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, CARET_SHAPE_TIMER);
        KillTimer(hWnd, NOTBAD_CODE_HIGHLIGHT_TIMER);
        SaveFileTreeState();
        ClearFileTree();
        g_hWndTree = NULL;
        g_hWndLineNumbers = NULL;
        ResetLineNumberBlockFont();
        DocDestroyAll();
        SettingsSaveWindowPlacement(hWnd);
        SettingsSave();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
