#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <memory>
#include <string>

#include "TabManager.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

namespace {
enum : int {
    ID_BACK = 101, ID_FORWARD, ID_REFRESH, ID_ADDRESS, ID_GO, ID_NEWTAB, ID_CLOSETAB, ID_TABS
};
constexpr int kToolbarH = 56;
constexpr int kTabsH = 42;
constexpr int kBtnW = 48;
constexpr int kGoW = 60;
constexpr int kFontPx = 22;
HFONT g_font;
constexpr int kCompactWidth = 600;
constexpr wchar_t kHome[] = L"https://www.bing.com";

HWND g_back, g_fwd, g_refresh, g_addr, g_go, g_new, g_close, g_tabs;
std::unique_ptr<TabManager> g_manager;
WNDPROC g_origEditProc;

void RefreshTabStrip() {
    if (!g_manager) return;
    TabCtrl_DeleteAllItems(g_tabs);
    for (int i = 0; i < static_cast<int>(g_manager->Count()); ++i) {
        std::wstring title = g_manager->TitleAt(i);
        if (title.size() > 24) title = title.substr(0, 24) + L"…";
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = title.data();
        TabCtrl_InsertItem(g_tabs, i, &item);
    }
    int active = g_manager->IndexOf(g_manager->ActiveId());
    if (active >= 0) TabCtrl_SetCurSel(g_tabs, active);
}

void Layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, x = 4;
    auto place = [&](HWND h, int width) { MoveWindow(h, x, 4, width, kToolbarH - 8, TRUE); x += width + 4; };
    // Phone-sized widths hide the Go button (Enter still navigates).
    const bool compact = w < kCompactWidth;
    ShowWindow(g_go, compact ? SW_HIDE : SW_SHOW);
    place(g_back, kBtnW); place(g_fwd, kBtnW); place(g_refresh, kBtnW);
    int rightW = (compact ? 0 : kGoW + 4) + 2 * (kBtnW + 4);
    place(g_addr, std::max(50, w - x - rightW - 4));
    if (!compact) place(g_go, kGoW);
    place(g_new, kBtnW); place(g_close, kBtnW);
    MoveWindow(g_tabs, 0, kToolbarH, w, kTabsH, TRUE);
    int contentTop = kToolbarH + kTabsH;
    LONG contentH = std::max<LONG>(0, rc.bottom - contentTop);
    if (g_manager) g_manager->Resize({0, contentTop, w, contentTop + contentH});
}

void Go() {
    wchar_t buf[2048];
    GetWindowTextW(g_addr, buf, 2048);
    if (g_manager->Count() == 0) g_manager->CreateTab(buf);
    else g_manager->Navigate(buf);
}

// Pressing Enter in the address bar triggers Go.
LRESULT CALLBACK AddrProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { Go(); return 0; }
    if (m == WM_CHAR && w == VK_RETURN) return 0;
    return CallWindowProcW(g_origEditProc, h, m, w, l);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = reinterpret_cast<LPCREATESTRUCTW>(lp)->hInstance;
        auto btn = [&](const wchar_t* t, int id) {
            return CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
        };
        g_back = btn(L"◀", ID_BACK); g_fwd = btn(L"▶", ID_FORWARD); g_refresh = btn(L"⟳", ID_REFRESH);
        g_addr = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
                               hwnd, reinterpret_cast<HMENU>(ID_ADDRESS), hi, nullptr);
        g_origEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_addr, GWLP_WNDPROC,
                                                                     reinterpret_cast<LONG_PTR>(AddrProc)));
        g_go = btn(L"Go", ID_GO); g_new = btn(L"+", ID_NEWTAB); g_close = btn(L"✕", ID_CLOSETAB);
        g_tabs = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH | TCS_FOCUSNEVER,
                               0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_TABS), hi, nullptr);
        g_font = CreateFontW(-kFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        for (HWND c : {g_back, g_fwd, g_refresh, g_addr, g_go, g_new, g_close, g_tabs})
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        TabCtrl_SetItemSize(g_tabs, 220, kTabsH - 6);
        TabManager::Callbacks cb;
        cb.onTabsChanged = RefreshTabStrip;
        cb.onUrlChanged = [](const std::wstring& u) { SetWindowTextW(g_addr, u.c_str()); };
        g_manager = std::make_unique<TabManager>(hwnd, cb);
        g_manager->Initialize();
        g_manager->CreateTab(kHome);
        Layout(hwnd);
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {320, 480};
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BACK: g_manager->GoBack(); break;
        case ID_FORWARD: g_manager->GoForward(); break;
        case ID_REFRESH: g_manager->Reload(); break;
        case ID_GO: Go(); break;
        case ID_NEWTAB: g_manager->CreateTab(kHome); break;
        case ID_CLOSETAB: g_manager->CloseTab(g_manager->ActiveId()); break;
        }
        return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<LPNMHDR>(lp)->hwndFrom == g_tabs && reinterpret_cast<LPNMHDR>(lp)->code == TCN_SELCHANGE)
            g_manager->SwitchTo(g_manager->IdAt(TabCtrl_GetCurSel(g_tabs)));
        return 0;
    case WM_DESTROY:
        g_manager.reset(); // closes every controller before the window goes away
        if (g_font) DeleteObject(g_font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WebInternationalBrowser";
    RegisterClassW(&wc);

    // Chrome-like default: large window centered in the work area.
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int workW = work.right - work.left, workH = work.bottom - work.top;
    const int winW = std::min(1366, workW * 85 / 100), winH = std::min(900, workH * 90 / 100);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Web International Browser", WS_OVERLAPPEDWINDOW,
                              work.left + (workW - winW) / 2, work.top + (workH - winH) / 2, winW, winH,
                              nullptr, nullptr, hi, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    (void)show;
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
