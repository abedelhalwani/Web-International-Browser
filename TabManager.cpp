#include "TabManager.h"

#include <shlwapi.h>
#include <algorithm>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

TabManager::TabManager(HWND host, Callbacks cb) : m_host(host), m_cb(std::move(cb)) {
    GetClientRect(host, &m_bounds);
}

TabManager::~TabManager() {
    *m_alive = false;
    CloseAll();
}

void TabManager::Initialize() {
    auto alive = m_alive;
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, alive](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (!*alive) return S_OK;
                if (FAILED(hr) || !env) {
                    MessageBoxW(m_host, L"Failed to create the WebView2 environment.\n"
                                        L"Is the WebView2 Runtime installed?",
                                L"Web International Browser", MB_ICONERROR);
                    return S_OK;
                }
                m_env = env;
                // Create controllers for tabs requested before the environment was ready.
                std::vector<TabId> pending;
                for (auto& t : m_tabs) pending.push_back(t->id);
                for (TabId id : pending) StartCreate(id);
                return S_OK;
            }).Get());
}

TabManager::Tab* TabManager::Find(TabId id) {
    for (auto& t : m_tabs)
        if (t->id == id) return t.get();
    return nullptr;
}

int TabManager::IndexOf(TabId id) const {
    for (size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i]->id == id) return static_cast<int>(i);
    return -1;
}

TabManager::TabId TabManager::IdAt(int index) const {
    return (index >= 0 && index < static_cast<int>(m_tabs.size())) ? m_tabs[index]->id : 0;
}

std::wstring TabManager::TitleAt(int index) const {
    return (index >= 0 && index < static_cast<int>(m_tabs.size())) ? m_tabs[index]->title : L"";
}

TabManager::TabId TabManager::CreateTab(const std::wstring& url) {
    auto tab = std::make_unique<Tab>();
    tab->id = m_nextId++;
    tab->url = NormalizeUrl(url);
    TabId id = tab->id;
    m_tabs.push_back(std::move(tab));
    m_activeId = id;
    if (m_env) StartCreate(id);
    if (m_cb.onTabsChanged) m_cb.onTabsChanged();
    return id;
}

void TabManager::StartCreate(TabId id) {
    auto alive = m_alive;
    m_env->CreateCoreWebView2Controller(
        m_host,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, alive, id](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                if (!*alive) return S_OK;
                if (SUCCEEDED(hr) && controller) OnControllerReady(id, controller);
                return S_OK;
            }).Get());
}

void TabManager::OnControllerReady(TabId id, ICoreWebView2Controller* controller) {
    Tab* tab = Find(id);
    if (!tab) { // tab was closed while the controller was being created
        controller->Close();
        return;
    }
    tab->controller = controller;
    controller->get_CoreWebView2(&tab->webview);
    controller->put_Bounds(m_bounds);

    auto alive = m_alive;
    tab->webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this, alive, id](ICoreWebView2* wv, IUnknown*) -> HRESULT {
                if (!*alive) return S_OK;
                if (Tab* t = Find(id)) {
                    LPWSTR title = nullptr;
                    if (SUCCEEDED(wv->get_DocumentTitle(&title)) && title) {
                        t->title = *title ? title : L"New Tab";
                        CoTaskMemFree(title);
                    }
                    if (m_cb.onTabsChanged) m_cb.onTabsChanged();
                }
                return S_OK;
            }).Get(), &tab->titleToken);

    tab->webview->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [this, alive, id](ICoreWebView2* wv, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (!*alive) return S_OK;
                if (Tab* t = Find(id)) {
                    LPWSTR src = nullptr;
                    if (SUCCEEDED(wv->get_Source(&src)) && src) {
                        t->url = src;
                        CoTaskMemFree(src);
                    }
                    if (id == m_activeId && m_cb.onUrlChanged) m_cb.onUrlChanged(t->url);
                }
                return S_OK;
            }).Get(), &tab->sourceToken);

    // Open target=_blank / window.open links in a new tab instead of a popup window.
    tab->webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, alive](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (!*alive) return S_OK;
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    std::wstring u = uri;
                    CoTaskMemFree(uri);
                    CreateTab(u);
                }
                return S_OK;
            }).Get(), &tab->newWindowToken);

    tab->webview->Navigate(tab->url.empty() ? L"about:blank" : tab->url.c_str());
    ApplyVisibility();
    if (id == m_activeId && m_cb.onUrlChanged) m_cb.onUrlChanged(tab->url);
}

void TabManager::SwitchTo(TabId id) {
    if (!Find(id) || id == m_activeId) return;
    m_activeId = id;
    ApplyVisibility();
    if (m_cb.onTabsChanged) m_cb.onTabsChanged();
    if (Tab* t = Active(); t && m_cb.onUrlChanged) m_cb.onUrlChanged(t->url);
}

void TabManager::ApplyVisibility() {
    for (auto& t : m_tabs) {
        if (!t->controller) continue;
        bool active = t->id == m_activeId;
        t->controller->put_IsVisible(active);
        if (active) {
            t->controller->put_Bounds(m_bounds);
            t->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
    }
}

void TabManager::Destroy(Tab& tab) {
    if (tab.webview) {
        tab.webview->remove_DocumentTitleChanged(tab.titleToken);
        tab.webview->remove_SourceChanged(tab.sourceToken);
        tab.webview->remove_NewWindowRequested(tab.newWindowToken);
    }
    if (tab.controller) tab.controller->Close(); // releases the browser process resources
    tab.webview.Reset();
    tab.controller.Reset();
}

void TabManager::CloseTab(TabId id) {
    int idx = IndexOf(id);
    if (idx < 0) return;
    Destroy(*m_tabs[idx]);
    m_tabs.erase(m_tabs.begin() + idx);

    if (id == m_activeId) {
        m_activeId = m_tabs.empty() ? 0 : m_tabs[std::min<size_t>(idx, m_tabs.size() - 1)]->id;
        ApplyVisibility();
    }
    if (m_cb.onTabsChanged) m_cb.onTabsChanged();
    if (m_cb.onUrlChanged) {
        Tab* t = Active();
        m_cb.onUrlChanged(t ? t->url : L"");
    }
}

void TabManager::CloseAll() {
    for (auto& t : m_tabs) Destroy(*t);
    m_tabs.clear();
    m_activeId = 0;
}

void TabManager::GoBack() {
    Tab* t = Active();
    BOOL can = FALSE;
    if (t && t->webview && SUCCEEDED(t->webview->get_CanGoBack(&can)) && can) t->webview->GoBack();
}

void TabManager::GoForward() {
    Tab* t = Active();
    BOOL can = FALSE;
    if (t && t->webview && SUCCEEDED(t->webview->get_CanGoForward(&can)) && can) t->webview->GoForward();
}

void TabManager::Reload() {
    if (Tab* t = Active(); t && t->webview) t->webview->Reload();
}

void TabManager::Navigate(const std::wstring& input) {
    Tab* t = Active();
    if (!t) return;
    t->url = NormalizeUrl(input);
    if (t->webview) t->webview->Navigate(t->url.c_str());
}

void TabManager::Resize(const RECT& bounds) {
    m_bounds = bounds;
    for (auto& t : m_tabs)
        if (t->controller) t->controller->put_Bounds(bounds);
}

// Adds https:// to bare hosts, and falls back to a search for text that isn't a URL.
std::wstring TabManager::NormalizeUrl(const std::wstring& input) {
    size_t b = input.find_first_not_of(L" \t");
    if (b == std::wstring::npos) return L"about:blank";
    std::wstring s = input.substr(b, input.find_last_not_of(L" \t") - b + 1);

    if (s.find(L"://") != std::wstring::npos || s.rfind(L"about:", 0) == 0) return s;
    bool looksLikeHost = s.find(L' ') == std::wstring::npos &&
                         (s.find(L'.') != std::wstring::npos || s.rfind(L"localhost", 0) == 0);
    if (looksLikeHost) return L"https://" + s;

    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    std::wstring q;
    for (unsigned char c : utf8) {
        if (c == ' ') q += L'+';
        else if (isalnum(c)) q += static_cast<wchar_t>(c);
        else { wchar_t buf[8]; swprintf_s(buf, L"%%%02X", c); q += buf; }
    }
    return L"https://www.bing.com/search?q=" + q;
}
