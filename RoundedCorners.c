/*
 * Rounded Corners - black antialiased screen corners for Windows.
 *
 * Plain Win32, no C runtime. Each monitor gets four click-through layered
 * windows. The top-left corner is rendered once (per-pixel alpha, 16x16
 * supersampled) and mirrored for the other three, so all four corners match.
 * Instead of polling, the corners are put back on top only when the
 * foreground window changes or the taskbar shows, moves or is reordered.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <string.h>
#include <intrin.h>

#define APP_NAME     L"Rounded Corners"
#define MAIN_CLASS   L"RoundedCornersMain"
#define CORNER_CLASS L"RoundedCornersCorner"
#define SET_CLASS    L"RoundedCornersSettings"

#define MAX_CORNERS  64          /* 16 monitors */
#define MAX_RADIUS   120
#define DEF_RADIUS   24

#define WM_TRAY          (WM_APP + 1)
#define WM_SHOWSETTINGS  (WM_APP + 2)
#define ID_SETTINGS  1
#define ID_EXIT      2
#define ID_HIDE      3
#define ID_TRACK     4
#define IDI_APP      101

/* ---- the few CRT pieces the compiler may emit calls to ---- */
#pragma function(memset)
void *__cdecl memset(void *d, int c, size_t n) { __stosb((unsigned char *)d, (unsigned char)c, n); return d; }
#pragma function(memcpy)
void *__cdecl memcpy(void *d, const void *s, size_t n) { __movsb((unsigned char *)d, (const unsigned char *)s, n); return d; }

static HINSTANCE g_inst;
static HWND g_main, g_settings, g_label, g_track, g_btnHide, g_btnExit;
static HFONT g_font;
static HWND g_corner[MAX_CORNERS];
static RECT g_mon[MAX_CORNERS];
static int g_count;
static int g_radius = DEF_RADIUS;
static NOTIFYICONDATAW g_nid;
static UINT g_msgTaskbarCreated;
static HWINEVENTHOOK g_hookFg, g_hookTbOrder, g_hookTbMove;
static DWORD g_explorerPid;
static WCHAR g_cfgPath[MAX_PATH];
static HICON g_iconSmall;

/* ---------------------------------------------------------------- config */

static void InitConfigPath(void)
{
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, g_cfgPath)))
        lstrcatW(g_cfgPath, L"\\RoundedCorners.cfg");
    else
        g_cfgPath[0] = 0;
}

static int LoadRadius(void)
{
    char buf[32];
    DWORD got = 0, i = 0;
    int v = 0, digits = 0;
    HANDLE f;
    if (!g_cfgPath[0]) return DEF_RADIUS;
    f = CreateFileW(g_cfgPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return DEF_RADIUS;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) i = 3;
    while (i < got && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;
    while (i < got && buf[i] >= '0' && buf[i] <= '9' && digits < 6) { v = v * 10 + (buf[i] - '0'); i++; digits++; }
    while (i < got && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;
    if (!digits || i != got) return DEF_RADIUS;
    return v > MAX_RADIUS ? MAX_RADIUS : v;
}

static void SaveRadius(int r)
{
    char buf[8];
    int n = wsprintfA(buf, "%d", r);
    DWORD put;
    HANDLE f;
    if (!g_cfgPath[0]) return;
    f = CreateFileW(g_cfgPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, buf, (DWORD)n, &put, NULL);
    CloseHandle(f);
}

/* --------------------------------------------------------------- corners */

/* Coverage of the black area (outside the circle centred at (r, r)) for each
   pixel of the top-left r x r box, 16x16 samples per pixel, in 1/32 px units. */
static void RenderTopLeft(BYTE *a, int r)
{
    int R = 32 * r, R2 = R * R, x, y, sx, sy;
    for (y = 0; y < r; y++) {
        for (x = 0; x < r; x++) {
            int out = 0;
            for (sy = 0; sy < 16; sy++) {
                int dy = R - (32 * y + 2 * sy + 1), dy2 = dy * dy;
                for (sx = 0; sx < 16; sx++) {
                    int dx = R - (32 * x + 2 * sx + 1);
                    if (dx * dx + dy2 > R2) out++;
                }
            }
            a[y * r + x] = (BYTE)((out * 255 + 128) >> 8);
        }
    }
}

static void Reassert(void)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (IsWindowVisible(g_corner[i]))
            SetWindowPos(g_corner[i], HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

static void ApplyRadius(void)
{
    int r = g_radius, i, c, x, y;
    BYTE *a;
    HDC screen, mem;
    HBITMAP bmp[4] = { 0 };
    DWORD *bits[4] = { 0 };
    BITMAPINFO bi;
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT src = { 0, 0 };
    SIZE size;

    if (r < 1) {
        for (i = 0; i < g_count; i++) ShowWindow(g_corner[i], SW_HIDE);
        return;
    }
    a = (BYTE *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)r * r);
    if (!a) return;
    RenderTopLeft(a, r);

    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = r;
    bi.bmiHeader.biHeight = -r;            /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    screen = GetDC(NULL);
    mem = CreateCompatibleDC(screen);
    /* corner c: bit 0 = right edge, bit 1 = bottom edge (0 TL, 1 TR, 2 BL, 3 BR) */
    for (c = 0; c < 4; c++) {
        bmp[c] = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&bits[c], NULL, 0);
        if (!bmp[c]) continue;
        for (y = 0; y < r; y++) {
            int sy = (c & 2) ? r - 1 - y : y;
            for (x = 0; x < r; x++) {
                int sx = (c & 1) ? r - 1 - x : x;
                bits[c][y * r + x] = (DWORD)a[sy * r + sx] << 24;   /* premultiplied black */
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, a);

    size.cx = r; size.cy = r;
    for (i = 0; i < g_count; i++) {
        RECT m = g_mon[i];
        POINT pt;
        HGDIOBJ old;
        c = i & 3;
        if (!bmp[c]) continue;
        pt.x = (c & 1) ? m.right - r : m.left;
        pt.y = (c & 2) ? m.bottom - r : m.top;
        old = SelectObject(mem, bmp[c]);
        UpdateLayeredWindow(g_corner[i], screen, &pt, &size, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old);
        if (!IsWindowVisible(g_corner[i])) ShowWindow(g_corner[i], SW_SHOWNOACTIVATE);
    }
    for (c = 0; c < 4; c++) if (bmp[c]) DeleteObject(bmp[c]);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    Reassert();
}

static BOOL CALLBACK AddMonitor(HMONITOR hm, HDC hdc, LPRECT rc, LPARAM lp)
{
    int c;
    UNREFERENCED_PARAMETER(hm); UNREFERENCED_PARAMETER(hdc); UNREFERENCED_PARAMETER(lp);
    for (c = 0; c < 4 && g_count < MAX_CORNERS; c++) {
        HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                 CORNER_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_inst, NULL);
        if (!h) return FALSE;
        g_corner[g_count] = h;
        g_mon[g_count] = *rc;
        g_count++;
    }
    return g_count < MAX_CORNERS;
}

static void BuildCorners(void)
{
    int i;
    for (i = 0; i < g_count; i++) DestroyWindow(g_corner[i]);
    g_count = 0;
    EnumDisplayMonitors(NULL, NULL, AddMonitor, 0);
    ApplyRadius();
}

/* ------------------------------------------------- keeping them on top */

static void CALLBACK OnWinEvent(HWINEVENTHOOK hook, DWORD ev, HWND hwnd, LONG obj, LONG child, DWORD tid, DWORD time)
{
    UNREFERENCED_PARAMETER(hook); UNREFERENCED_PARAMETER(tid); UNREFERENCED_PARAMETER(time);
    if (!hwnd || obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    if (ev != EVENT_SYSTEM_FOREGROUND) {
        /* taskbar events: only the taskbar's own top-level windows matter */
        WCHAR cls[32];
        if (GetAncestor(hwnd, GA_ROOT) != hwnd) return;
        if (!GetClassNameW(hwnd, cls, 32)) return;
        if (lstrcmpW(cls, L"Shell_TrayWnd") && lstrcmpW(cls, L"Shell_SecondaryTrayWnd")) return;
    }
    Reassert();
}

static void HookTaskbar(void)
{
    HWND tb = FindWindowW(L"Shell_TrayWnd", NULL);
    DWORD pid = 0;
    if (tb) GetWindowThreadProcessId(tb, &pid);
    if (pid == g_explorerPid && g_hookTbOrder) return;
    if (g_hookTbOrder) { UnhookWinEvent(g_hookTbOrder); g_hookTbOrder = NULL; }
    if (g_hookTbMove) { UnhookWinEvent(g_hookTbMove); g_hookTbMove = NULL; }
    g_explorerPid = pid;
    if (!pid) return;
    g_hookTbOrder = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_REORDER, NULL, OnWinEvent, pid, 0, WINEVENT_OUTOFCONTEXT);
    g_hookTbMove = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, OnWinEvent, pid, 0, WINEVENT_OUTOFCONTEXT);
}

/* ------------------------------------------------------------------ tray */

static void AddTrayIcon(void)
{
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_iconSmall;
    lstrcpyW(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void TrimMemory(void)
{
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

/* -------------------------------------------------------------- settings */

static void UpdateLabel(void)
{
    WCHAR t[48];
    if (!g_label) return;
    wsprintfW(t, L"Corner radius: %d px", g_radius);
    SetWindowTextW(g_label, t);
}

static int Sc(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }

/* Lay out the settings window for a DPI: 340 x 110 client area at 96 DPI. */
static void LayoutSettings(UINT dpi)
{
    NONCLIENTMETRICSW ncm;
    HFONT f;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
    f = CreateFontIndirectW(&ncm.lfMessageFont);
    MoveWindow(g_label,   Sc(10, dpi),  Sc(10, dpi), Sc(320, dpi), Sc(20, dpi), FALSE);
    MoveWindow(g_track,   Sc(10, dpi),  Sc(32, dpi), Sc(320, dpi), Sc(40, dpi), FALSE);
    MoveWindow(g_btnHide, Sc(10, dpi),  Sc(78, dpi), Sc(155, dpi), Sc(25, dpi), FALSE);
    MoveWindow(g_btnExit, Sc(175, dpi), Sc(78, dpi), Sc(155, dpi), Sc(25, dpi), FALSE);
    SendMessageW(g_label, WM_SETFONT, (WPARAM)f, FALSE);
    SendMessageW(g_btnHide, WM_SETFONT, (WPARAM)f, FALSE);
    SendMessageW(g_btnExit, WM_SETFONT, (WPARAM)f, FALSE);
    if (g_font) DeleteObject(g_font);
    g_font = f;
    InvalidateRect(g_settings, NULL, TRUE);
}

static void QuitApp(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_settings) DestroyWindow(g_settings);
    PostQuitMessage(0);
}

static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_HSCROLL:
        if ((HWND)l == g_track) {
            int v = (int)SendMessageW(g_track, TBM_GETPOS, 0, 0);
            if (v != g_radius) {
                g_radius = v;
                UpdateLabel();
                ApplyRadius();
            }
            if (LOWORD(w) != TB_THUMBTRACK) SaveRadius(g_radius);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == ID_HIDE) DestroyWindow(h);
        else if (LOWORD(w) == ID_EXIT) QuitApp();
        return 0;
    case WM_DPICHANGED: {
        RECT *r = (RECT *)l;
        LayoutSettings(HIWORD(w));
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_CLOSE:                 /* closing only hides to the tray */
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        SaveRadius(g_radius);
        g_settings = g_label = g_track = g_btnHide = g_btnExit = NULL;
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        PostMessageW(g_main, WM_NULL, 0, 0);
        TrimMemory();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowSettings(void)
{
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    UINT dpi;
    RECT rc, wa;
    HMONITOR mon;
    MONITORINFO mi;

    if (g_settings) {
        ShowWindow(g_settings, SW_RESTORE);
        SetForegroundWindow(g_settings);
        return;
    }
    g_settings = CreateWindowExW(0, SET_CLASS, APP_NAME, style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                 NULL, NULL, g_inst, NULL);
    if (!g_settings) return;
    g_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_settings, NULL, g_inst, NULL);
    g_track = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
                              0, 0, 0, 0, g_settings, (HMENU)ID_TRACK, g_inst, NULL);
    g_btnHide = CreateWindowExW(0, L"BUTTON", L"Hide to tray", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                0, 0, 0, 0, g_settings, (HMENU)ID_HIDE, g_inst, NULL);
    g_btnExit = CreateWindowExW(0, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, 0, 0, 0, g_settings, (HMENU)ID_EXIT, g_inst, NULL);
    SendMessageW(g_track, TBM_SETRANGE, FALSE, MAKELPARAM(0, MAX_RADIUS));
    SendMessageW(g_track, TBM_SETTICFREQ, 10, 0);
    SendMessageW(g_track, TBM_SETPAGESIZE, 0, 5);
    SendMessageW(g_track, TBM_SETPOS, TRUE, g_radius);
    UpdateLabel();

    /* size for this DPI and centre on the monitor */
    dpi = GetDpiForWindow(g_settings);
    LayoutSettings(dpi);
    rc.left = 0; rc.top = 0; rc.right = Sc(340, dpi); rc.bottom = Sc(110, dpi);
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
    mon = MonitorFromWindow(g_settings, MONITOR_DEFAULTTOPRIMARY);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    wa = mi.rcWork;
    SetWindowPos(g_settings, NULL,
                 wa.left + ((wa.right - wa.left) - (rc.right - rc.left)) / 2,
                 wa.top + ((wa.bottom - wa.top) - (rc.bottom - rc.top)) / 2,
                 rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
    ShowWindow(g_settings, SW_SHOW);
    SetForegroundWindow(g_settings);
    SetFocus(g_track);
}

/* ------------------------------------------------------------ main window */

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == g_msgTaskbarCreated && m) {  /* Explorer restarted */
        AddTrayIcon();
        HookTaskbar();
        Reassert();
        return 0;
    }
    switch (m) {
    case WM_TRAY:
        if (l == WM_LBUTTONDBLCLK) ShowSettings();
        else if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) {
            POINT pt;
            HMENU menu = CreatePopupMenu();
            UINT cmd;
            AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"Settings");
            AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
            GetCursorPos(&pt);
            SetForegroundWindow(h);
            cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == ID_SETTINGS) ShowSettings();
            else if (cmd == ID_EXIT) QuitApp();
        }
        return 0;
    case WM_SHOWSETTINGS:
        ShowSettings();
        return 0;
    case WM_DISPLAYCHANGE:
        BuildCorners();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int Run(void)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    HWND other;

    /* one copy at a time: a second launch opens the first one's settings */
    other = FindWindowW(MAIN_CLASS, NULL);
    if (other) {
        DWORD pid = 0;
        GetWindowThreadProcessId(other, &pid);
        AllowSetForegroundWindow(pid);
        PostMessageW(other, WM_SHOWSETTINGS, 0, 0);
        return 0;
    }

    g_inst = GetModuleHandleW(NULL);
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_APP));
    if (FAILED(LoadIconMetric(g_inst, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &g_iconSmall)))
        g_iconSmall = wc.hIcon;
    wc.hIconSm = g_iconSmall;

    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = MAIN_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = CORNER_CLASS;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = SettingsProc;
    wc.lpszClassName = SET_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    /* hidden top-level window: owns the tray icon and gets WM_DISPLAYCHANGE */
    g_main = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, APP_NAME, WS_POPUP, 0, 0, 0, 0, NULL, NULL, g_inst, NULL);
    if (!g_main) return 1;
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    InitConfigPath();
    g_radius = LoadRadius();
    AddTrayIcon();
    BuildCorners();
    SaveRadius(g_radius);

    g_hookFg = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, OnWinEvent, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    HookTaskbar();

    ShowSettings();

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_settings && IsDialogMessageW(g_settings, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hookFg) UnhookWinEvent(g_hookFg);
    if (g_hookTbOrder) UnhookWinEvent(g_hookTbOrder);
    if (g_hookTbMove) UnhookWinEvent(g_hookTbMove);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    return (int)msg.wParam;
}

void __stdcall Start(void)
{
    ExitProcess((UINT)Run());
}
