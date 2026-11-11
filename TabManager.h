#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Owns every WebView2 tab. All methods must be called on the UI thread.
class TabManager {
public:
    using TabId = int;

    struct Callbacks {
        std::function<void()> onTabsChanged;                 // tab added/removed/renamed/switched
        std::function<void(const std::wstring&)> onUrlChanged; // active tab URL changed
    };

    explicit TabManager(HWND host, Callbacks cb);
    ~TabManager();
    TabManager(const TabManager&) = delete;
    TabManager& operator=(const TabManager&) = delete;

    // Starts the shared WebView2 environment. Tabs requested earlier are created once ready.
    void Initialize();

    TabId CreateTab(const std::wstring& url);
    void SwitchTo(TabId id);
    void CloseTab(TabId id);
    void CloseAll();

    // Navigation on the active tab; no-ops if there is none.
    void GoBack();
    void GoForward();
    void Reload();
    void Navigate(const std::wstring& input);

    void Resize(const RECT& bounds);

    size_t Count() const { return m_tabs.size(); }
    TabId ActiveId() const { return m_activeId; }
    int IndexOf(TabId id) const;
    TabId IdAt(int index) const;
    std::wstring TitleAt(int index) const;

private:
    struct Tab {
        TabId id = 0;
        std::wstring title = L"New Tab";
        std::wstring url;
        Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
        Microsoft::WRL::ComPtr<ICoreWebView2> webview;
        EventRegistrationToken titleToken{};
        EventRegistrationToken sourceToken{};
        EventRegistrationToken newWindowToken{};
    };

    Tab* Find(TabId id);
    Tab* Active() { return Find(m_activeId); }
    void StartCreate(TabId id);
    void OnControllerReady(TabId id, ICoreWebView2Controller* controller);
    void Destroy(Tab& tab);
    void ApplyVisibility();
    static std::wstring NormalizeUrl(const std::wstring& input);

    HWND m_host;
    Callbacks m_cb;
    RECT m_bounds{};
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> m_env;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    TabId m_activeId = 0;
    TabId m_nextId = 1;
    bool m_shuttingDown = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true); // guards async callbacks
};
