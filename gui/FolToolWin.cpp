// 刀剑封魔录系列 .fol 资源工具 Windows GUI
// 主窗口：解包/打包 Tab 切换；路径可手动输入；支持拖拽 .fol 文件/目录；
// 归档内容预览；彩色分级日志；暗色模式跟随系统；窗口可缩放；支持命令行打开。
// 控件全部在代码中创建（.rc 仅保留图标、字符串表、Manifest 与版本信息）。

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "version.lib")

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "resource.h"
#include "FolCore.hpp"

// 自定义消息：线程向主界面汇报日志 (WPARAM=LogMessage*)
#define WM_USER_LOG (WM_USER + 100)
// 任务结束 (WPARAM=0 成功 / 1 失败)
#define WM_USER_TASK_DONE (WM_USER + 101)

// 兼容旧 SDK 缺失的 DWM 属性常量
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
// 日志颜色（深浅主题下均保证可读）
constexpr COLORREF kColorInfoDark = RGB(0xDC, 0xDC, 0xDC);  // 暗色主题正文
constexpr COLORREF kColorInfoLight = RGB(0x1E, 0x1E, 0x1E); // 亮色主题正文
constexpr COLORREF kColorOk = RGB(0x1E, 0x8E, 0x3E);
constexpr COLORREF kColorWarn = RGB(0xE0, 0x90, 0x00);
constexpr COLORREF kColorErr = RGB(0xD0, 0x30, 0x30);

// 线程 -> 主界面 的日志消息载荷
struct LogMessage
{
    int progress = 0;
    FolLogLevel level = FOL_LOG_INFO;
    std::wstring text;
};

HINSTANCE g_hInst = nullptr;

// 窗口与控件状态
struct AppState
{
    HWND hwnd = nullptr;
    HWND tab = nullptr;
    // 解包页
    HWND lblFol = nullptr, editFol = nullptr, btnBrowseFol = nullptr;
    HWND lblOut = nullptr, editOut = nullptr, btnBrowseOut = nullptr;
    HWND btnRunUnpack = nullptr, listPreview = nullptr;
    // 打包页
    HWND lblIn = nullptr, editIn = nullptr, btnBrowseIn = nullptr;
    HWND lblSave = nullptr, editFolOut = nullptr, btnBrowseSave = nullptr;
    HWND btnRunPack = nullptr;
    // 关于页
    HWND about = nullptr;
    // 公共区域
    HWND lblLog = nullptr, log = nullptr, progress = nullptr, status = nullptr;

    HFONT font = nullptr;
    HBRUSH background = nullptr;
    std::wstring previewedPath; // 已加载预览的路径，避免重复刷新
    int dpi = 96;
    bool dark = false;  // 系统暗色主题
    bool rich = false;  // RichEdit 可用（否则回退普通 EDIT）
    bool busy = false;  // 是否有任务在运行
} g_app;

// 工具函数：从资源 ID 加载字符串
std::wstring LoadStr(UINT id)
{
    WCHAR buffer[512] = {};
    LoadStringW(g_hInst, id, buffer, 512);
    return std::wstring(buffer);
}

// 从当前 EXE 的 VS_VERSION_INFO 读取字符串，关于页与文件属性始终使用同一份信息。
std::wstring LoadVersionString(const wchar_t* name)
{
    std::vector<wchar_t> modulePath(32768, L'\0');
    const DWORD pathLength = GetModuleFileNameW(nullptr, modulePath.data(),
                                                static_cast<DWORD>(modulePath.size()));
    if (pathLength == 0 || pathLength >= modulePath.size())
    {
        return L"";
    }

    DWORD handle = 0;
    const DWORD infoSize = GetFileVersionInfoSizeW(modulePath.data(), &handle);
    if (infoSize == 0)
    {
        return L"";
    }

    std::vector<BYTE> versionInfo(infoSize);
    if (!GetFileVersionInfoW(modulePath.data(), 0, infoSize, versionInfo.data()))
    {
        return L"";
    }

    struct Translation
    {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT translationBytes = 0;
    WORD language = 0x0804;
    WORD codePage = 1200;
    if (VerQueryValueW(versionInfo.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<void**>(&translations), &translationBytes) &&
        translations != nullptr && translationBytes >= sizeof(Translation))
    {
        language = translations[0].language;
        codePage = translations[0].codePage;
    }

    wchar_t query[128] = {};
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", language, codePage, name);
    wchar_t* value = nullptr;
    UINT valueLength = 0;
    if (!VerQueryValueW(versionInfo.data(), query, reinterpret_cast<void**>(&value), &valueLength) ||
        value == nullptr || valueLength == 0)
    {
        return L"";
    }
    return std::wstring(value);
}

std::wstring BuildAboutText()
{
    const std::wstring product = LoadVersionString(L"ProductName");
    const std::wstring description = LoadVersionString(L"FileDescription");
    const std::wstring version = LoadVersionString(L"ProductVersion");
    const std::wstring author = LoadVersionString(L"CompanyName");
    const std::wstring copyright = LoadVersionString(L"LegalCopyright");

    std::wstring text = product;
    if (!description.empty()) text += L"\r\n\r\n" + description;
    if (!version.empty()) text += L"\r\n\r\n" + LoadStr(IDS_ABOUT_VERSION) + L"：" + version;
    if (!author.empty()) text += L"\r\n" + LoadStr(IDS_ABOUT_AUTHOR) + L"：" + author;
    if (!copyright.empty()) text += L"\r\n\r\n" + copyright;
    return text;
}

// 工具函数：读取编辑框文本
std::wstring GetEditText(HWND edit)
{
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    if (length > 0)
    {
        GetWindowTextW(edit, text.data(), length + 1);
    }
    return text;
}

// 工具函数：打开文件/文件夹选择器
// isFolder: true选文件夹, false选文件；isSave: true为保存对话框
// filter: 文件扩展名过滤器 (如 L"*.fol")
std::wstring OpenDialog(HWND hwnd, bool isFolder, bool isSave, const wchar_t* filter = nullptr)
{
    std::wstring result = L"";
    IFileDialog* pfd = NULL;
    HRESULT hr;

    if (isFolder) hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    else if (isSave) hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    else hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));

    if (FAILED(hr)) return L"";

    DWORD dwOptions;
    pfd->GetOptions(&dwOptions);
    if (isFolder) pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);

    // 设置过滤器
    if (filter && !isFolder) {
        COMDLG_FILTERSPEC rgSpec[] = { { L"FOL Files", filter }, { L"All Files", L"*.*" } };
        pfd->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
        pfd->SetDefaultExtension(L"fol");
    }

    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* psi;
        if (SUCCEEDED(pfd->GetResult(&psi))) {
            PWSTR pszPath;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
                result = pszPath;
                CoTaskMemFree(pszPath);
            }
            psi->Release();
        }
    }
    pfd->Release();
    return result;
}

// ============================================================
// 主题（暗色模式跟随系统）
// ============================================================

bool IsDarkThemeEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS status = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && value == 0;
}

void ApplyTheme()
{
    if (g_app.hwnd == nullptr)
    {
        return;
    }

    // 标题栏深浅色（Win10 1809+）
    BOOL darkTitle = g_app.dark;
    DwmSetWindowAttribute(g_app.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitle, sizeof(darkTitle));

    // 客户区使用实色背景。原先在 Mica 模式下跳过 WM_ERASEBKGND，透明 STATIC
    // 控件移动后旧文字像素无法被清除，实时缩放窗口时会形成文字重影。
    if (g_app.background != nullptr)
    {
        DeleteObject(g_app.background);
        g_app.background = nullptr;
    }
    g_app.background = CreateSolidBrush(g_app.dark ? RGB(32, 32, 32) : RGB(240, 240, 240));

    // 公共控件切暗色主题（Win11 起生效，旧系统保持默认外观）
    const wchar_t* theme = g_app.dark ? L"DarkMode_Explorer" : L"Explorer";
    const HWND controls[] = {
        g_app.tab, g_app.editFol, g_app.editOut, g_app.editIn, g_app.editFolOut,
        g_app.btnBrowseFol, g_app.btnBrowseOut, g_app.btnBrowseIn, g_app.btnBrowseSave,
        g_app.btnRunUnpack, g_app.btnRunPack, g_app.listPreview,
    };
    for (HWND control : controls)
    {
        if (control != nullptr)
        {
            SetWindowTheme(control, theme, nullptr);
        }
    }

    // 日志与进度条配色
    if (g_app.log != nullptr)
    {
        SendMessageW(g_app.log, EM_SETBKGNDCOLOR, 0, g_app.dark ? RGB(24, 24, 24) : RGB(255, 255, 255));
    }
    if (g_app.progress != nullptr)
    {
        SendMessageW(g_app.progress, PBM_SETBKCOLOR, 0, g_app.dark ? RGB(45, 45, 45) : RGB(220, 220, 220));
        SendMessageW(g_app.progress, PBM_SETBARCOLOR, 0, RGB(0, 120, 215));
    }

    InvalidateRect(g_app.hwnd, nullptr, TRUE);
}

// ============================================================
// 字体与布局
// ============================================================

int DpiScale(int value)
{
    return MulDiv(value, g_app.dpi, 96);
}

HFONT CreateFonts()
{
    const int height = -MulDiv(9, g_app.dpi, 72);
    HFONT newFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (newFont == nullptr)
    {
        return nullptr;
    }

    HFONT oldFont = g_app.font;
    g_app.font = newFont;
    return oldFont;
}

void SetFontOnChildren()
{
    const HWND children[] = {
        g_app.tab, g_app.lblFol, g_app.editFol, g_app.btnBrowseFol,
        g_app.lblOut, g_app.editOut, g_app.btnBrowseOut, g_app.btnRunUnpack,
        g_app.listPreview, g_app.lblIn, g_app.editIn, g_app.btnBrowseIn,
        g_app.lblSave, g_app.editFolOut, g_app.btnBrowseSave, g_app.btnRunPack,
        g_app.about,
        g_app.lblLog, g_app.log, g_app.progress, g_app.status,
    };
    for (HWND child : children)
    {
        if (child != nullptr)
        {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
        }
    }
}

void LayoutControls()
{
    if (g_app.hwnd == nullptr || g_app.tab == nullptr || g_app.log == nullptr ||
        g_app.progress == nullptr || g_app.status == nullptr || g_app.listPreview == nullptr)
    {
        return;
    }

    RECT client = {};
    GetClientRect(g_app.hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    const int margin = DpiScale(10);
    const int gap = DpiScale(4);
    const int rowHeight = DpiScale(24);
    const int labelWidth = DpiScale(72);
    const int buttonWidth = DpiScale(56);
    const int runButtonWidth = DpiScale(110);

    // 底部公共区域：日志标签 + 日志 + 进度条 + 状态
    const int labelH = DpiScale(14);
    const int logH = DpiScale(110);
    const int progressH = DpiScale(14);
    const int statusH = DpiScale(16);
    const int pageBottom = height - margin - (labelH + gap + logH + gap + progressH + gap + statusH);

    HDWP positions = BeginDeferWindowPos(22);
    auto place = [&positions](HWND control, int x, int y, int controlWidth, int controlHeight) {
        if (positions != nullptr)
        {
            positions = DeferWindowPos(positions, control, nullptr, x, y, controlWidth, controlHeight,
                                       SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOREDRAW);
        }
        else
        {
            SetWindowPos(control, nullptr, x, y, controlWidth, controlHeight,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOREDRAW);
        }
    };

    // Tab
    int y = DpiScale(6);
    place(g_app.tab, margin, y, width - 2 * margin, DpiScale(26));
    y += DpiScale(28);

    // 输入行：标签 + 编辑框 + 浏览按钮
    const int editX = margin + labelWidth + DpiScale(4);
    const int editWidth = width - margin - editX - (buttonWidth + DpiScale(4)) - margin;
    const int buttonX = width - margin - buttonWidth;

    // 输入行：解包页与打包页共用同一组行位置（同一时刻仅一页可见，避免切 Tab 时跳位）
    struct Row { HWND label; HWND edit; HWND button; };
    const Row rows[2][2] = {
        { { g_app.lblFol, g_app.editFol, g_app.btnBrowseFol },
          { g_app.lblOut, g_app.editOut, g_app.btnBrowseOut } },
        { { g_app.lblIn, g_app.editIn, g_app.btnBrowseIn },
          { g_app.lblSave, g_app.editFolOut, g_app.btnBrowseSave } },
    };

    RECT rowRects[2] = {};
    int pageY = y;
    for (int r = 0; r < 2; ++r)
    {
        rowRects[r] = { margin, pageY, editX, pageY + rowHeight };
        pageY += rowHeight + DpiScale(6);
    }
    pageY += DpiScale(4);

    for (int page = 0; page < 2; ++page)
    {
        for (int r = 0; r < 2; ++r)
        {
            const Row& row = rows[page][r];
            const RECT& rect = rowRects[r];
            place(row.label, rect.left, rect.top + DpiScale(4), labelWidth, rowHeight - DpiScale(4));
            place(row.edit, editX, rect.top, editWidth, rowHeight);
            place(row.button, buttonX, rect.top, buttonWidth, rowHeight);
        }
    }

    // 执行按钮（右对齐）与预览列表（充满按钮上方空间）
    const int runY = pageBottom - DpiScale(28);
    place(g_app.btnRunUnpack, width - margin - runButtonWidth, runY, runButtonWidth, DpiScale(26));
    place(g_app.btnRunPack, width - margin - runButtonWidth, runY, runButtonWidth, DpiScale(26));
    place(g_app.listPreview, margin, pageY, width - 2 * margin, runY - pageY - DpiScale(4));

    // 关于页占用整个内容区域；切到关于页时底部日志区会隐藏。
    place(g_app.about, margin + DpiScale(30), y + DpiScale(35),
          width - 2 * margin - DpiScale(60), height - y - DpiScale(70));

    // 底部公共区域
    int bottomY = pageBottom;
    place(g_app.lblLog, margin, bottomY, DpiScale(100), labelH);
    bottomY += labelH + gap;
    place(g_app.log, margin, bottomY, width - 2 * margin, logH);
    bottomY += logH + gap;
    place(g_app.progress, margin, bottomY, width - 2 * margin, progressH);
    bottomY += progressH + gap;
    place(g_app.status, margin, bottomY, width - 2 * margin, statusH);

    if (positions != nullptr)
    {
        EndDeferWindowPos(positions);
    }

    if (g_app.listPreview != nullptr)
    {
        ListView_SetColumnWidth(g_app.listPreview, 0, width - 2 * margin - DpiScale(94));
        ListView_SetColumnWidth(g_app.listPreview, 1, DpiScale(90));
    }

    // 所有控件定位完成后只重绘一次，避免实时缩放时绘制中间布局状态。
    RedrawWindow(g_app.hwnd, nullptr, nullptr,
                 RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

// ============================================================
// 控件创建与页面切换
// ============================================================

bool CreateControls(HWND hwnd)
{
    auto create = [hwnd](const wchar_t* className, const std::wstring& text,
                         DWORD style, DWORD exStyle, int id) -> HWND {
        return CreateWindowExW(exStyle, className, text.c_str(), style,
                               0, 0, 0, 0, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_hInst, nullptr);
    };

    g_app.tab = create(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, IDC_TAB_MAIN);
    if (g_app.tab == nullptr)
    {
        return false;
    }

    // 解包页
    g_app.lblFol = create(L"STATIC", LoadStr(IDS_LBL_FOL_FILE), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC);
    g_app.editFol = create(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDIT_FOL_PATH);
    g_app.btnBrowseFol = create(L"BUTTON", LoadStr(IDS_BTN_BROWSE), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_BROWSE_FOL);
    g_app.lblOut = create(L"STATIC", LoadStr(IDS_LBL_OUT_DIR), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC);
    g_app.editOut = create(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDIT_OUT_DIR);
    g_app.btnBrowseOut = create(L"BUTTON", LoadStr(IDS_BTN_BROWSE), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_BROWSE_OUT);
    g_app.btnRunUnpack = create(L"BUTTON", LoadStr(IDS_BTN_UNPACK), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_RUN_UNPACK);

    // 归档内容预览
    g_app.listPreview = create(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOSORTHEADER, WS_EX_CLIENTEDGE, IDC_LIST_PREVIEW);
    if (g_app.listPreview != nullptr)
    {
        ListView_SetExtendedListViewStyle(g_app.listPreview, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        const std::wstring pathColumnText = LoadStr(IDS_COL_PATH);
        column.cx = DpiScale(300);
        column.pszText = const_cast<wchar_t*>(pathColumnText.c_str());
        ListView_InsertColumn(g_app.listPreview, 0, &column);
        const std::wstring sizeColumnText = LoadStr(IDS_COL_SIZE);
        column.cx = DpiScale(90);
        column.pszText = const_cast<wchar_t*>(sizeColumnText.c_str());
        ListView_InsertColumn(g_app.listPreview, 1, &column);
    }

    // 打包页
    g_app.lblIn = create(L"STATIC", LoadStr(IDS_LBL_IN_DIR), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC);
    g_app.editIn = create(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDIT_IN_DIR);
    g_app.btnBrowseIn = create(L"BUTTON", LoadStr(IDS_BTN_BROWSE), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_BROWSE_IN);
    g_app.lblSave = create(L"STATIC", LoadStr(IDS_LBL_SAVE_TO), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC);
    g_app.editFolOut = create(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_EDIT_FOL_OUT);
    g_app.btnBrowseSave = create(L"BUTTON", LoadStr(IDS_BTN_BROWSE), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_BROWSE_SAVE);
    g_app.btnRunPack = create(L"BUTTON", LoadStr(IDS_BTN_PACK), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_BTN_RUN_PACK);

    // 关于页：内容直接读取 FolToolWin.rc 中的版本资源。
    g_app.about = create(L"STATIC", BuildAboutText(), WS_CHILD | WS_VISIBLE | SS_CENTER, 0, IDC_STATIC_ABOUT);

    // 公共区域：日志（RichEdit 4.1，不可用时回退普通 EDIT）、进度条、状态
    g_app.lblLog = create(L"STATIC", LoadStr(IDS_LBL_LOG), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC);
    WNDCLASSW windowClass = {};
    g_app.rich = GetClassInfoW(nullptr, MSFTEDIT_CLASS, &windowClass) != FALSE;
    g_app.log = create(g_app.rich ? MSFTEDIT_CLASS : L"EDIT", L"",
                       WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                       WS_EX_CLIENTEDGE, IDC_EDIT_LOG);
    g_app.progress = create(PROGRESS_CLASS, L"", WS_CHILD | WS_VISIBLE, 0, IDC_PROGRESS_BAR);
    g_app.status = create(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATIC_STATUS);
    if (g_app.log == nullptr || g_app.progress == nullptr)
    {
        return false;
    }
    SendMessageW(g_app.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    // Tab 项
    TCITEMW tabItem = {};
    tabItem.mask = TCIF_TEXT;
    {
        const std::wstring text = LoadStr(IDS_TAB_UNPACK);
        tabItem.pszText = const_cast<wchar_t*>(text.c_str());
        TabCtrl_InsertItem(g_app.tab, 0, &tabItem);
    }
    {
        const std::wstring text = LoadStr(IDS_TAB_PACK);
        tabItem.pszText = const_cast<wchar_t*>(text.c_str());
        TabCtrl_InsertItem(g_app.tab, 1, &tabItem);
    }
    {
        const std::wstring text = LoadStr(IDS_TAB_ABOUT);
        tabItem.pszText = const_cast<wchar_t*>(text.c_str());
        TabCtrl_InsertItem(g_app.tab, 2, &tabItem);
    }

    SetFontOnChildren();
    return true;
}

void ShowTab(int index)
{
    const HWND unpackPage[] = {
        g_app.lblFol, g_app.editFol, g_app.btnBrowseFol,
        g_app.lblOut, g_app.editOut, g_app.btnBrowseOut,
        g_app.btnRunUnpack, g_app.listPreview,
    };
    const HWND packPage[] = {
        g_app.lblIn, g_app.editIn, g_app.btnBrowseIn,
        g_app.lblSave, g_app.editFolOut, g_app.btnBrowseSave,
        g_app.btnRunPack,
    };

    for (HWND control : unpackPage)
    {
        ShowWindow(control, index == 0 ? SW_SHOW : SW_HIDE);
    }
    for (HWND control : packPage)
    {
        ShowWindow(control, index == 1 ? SW_SHOW : SW_HIDE);
    }
    ShowWindow(g_app.about, index == 2 ? SW_SHOW : SW_HIDE);

    const HWND commonArea[] = { g_app.lblLog, g_app.log, g_app.progress, g_app.status };
    for (HWND control : commonArea)
    {
        ShowWindow(control, index == 2 ? SW_HIDE : SW_SHOW);
    }
    LayoutControls();
}

// ============================================================
// 日志与状态
// ============================================================

void SetStatus(const std::wstring& text)
{
    if (g_app.status != nullptr)
    {
        SetWindowTextW(g_app.status, text.c_str());
    }
}

void AppendLog(const std::wstring& text, COLORREF color)
{
    if (g_app.log == nullptr)
    {
        return;
    }

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t stamp[32] = {};
    swprintf_s(stamp, L"[%02u:%02u:%02u] ", now.wHour, now.wMinute, now.wSecond);

    const std::wstring line = stamp + text + L"\r\n";
    const int length = GetWindowTextLengthW(g_app.log);
    SendMessageW(g_app.log, EM_SETSEL, length, length);

    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = color;
    SendMessageW(g_app.log, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));

    SendMessageW(g_app.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

COLORREF DefaultTextColor()
{
    return g_app.dark ? kColorInfoDark : kColorInfoLight;
}

void LogInfo(const std::wstring& text) { AppendLog(text, DefaultTextColor()); }
void LogOk(const std::wstring& text) { AppendLog(text, kColorOk); }
void LogWarn(const std::wstring& text) { AppendLog(text, kColorWarn); }
void LogError(const std::wstring& text) { AppendLog(text, kColorErr); }

std::wstring FormatSize(std::uint32_t size)
{
    wchar_t buffer[32] = {};
    if (size >= 1024ull * 1024)
    {
        swprintf_s(buffer, L"%.2f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    }
    else if (size >= 1024)
    {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(size) / 1024.0);
    }
    else
    {
        swprintf_s(buffer, L"%u B", size);
    }
    return buffer;
}

// ============================================================
// 归档内容预览
// ============================================================

void ReloadPreview()
{
    if (g_app.busy || g_app.listPreview == nullptr)
    {
        return;
    }

    const std::wstring path = GetEditText(g_app.editFol);
    if (path == g_app.previewedPath)
    {
        return; // 已加载过同一路径，避免重复刷新
    }
    g_app.previewedPath = path;

    ListView_DeleteAllItems(g_app.listPreview);
    if (path.empty())
    {
        SetStatus(LoadStr(IDS_ST_NO_FILE));
        return;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        SetStatus(L"文件不存在或无法读取。");
        return;
    }

    std::uint32_t count = 0;
    const int result = FolCore::List(path, [&](const std::wstring& entryPath, std::uint32_t size) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(count);
        item.pszText = const_cast<wchar_t*>(entryPath.c_str());
        const int insertedIndex = ListView_InsertItem(g_app.listPreview, &item);
        if (insertedIndex >= 0)
        {
            const std::wstring sizeText = FormatSize(size);
            ListView_SetItemText(g_app.listPreview, insertedIndex, 1, const_cast<wchar_t*>(sizeText.c_str()));
        }
        ++count;
    }, [](int, FolLogLevel, const std::wstring& message) {
        LogError(message);
    });

    if (result != FOL_SUCCESS)
    {
        SetStatus(L"读取归档失败。");
        return;
    }

    wchar_t statusBuffer[64] = {};
    swprintf_s(statusBuffer, LoadStr(IDS_ST_FILES).c_str(), static_cast<int>(count));
    SetStatus(statusBuffer);
}

// ============================================================
// 任务执行（解包/打包）
// ============================================================

// 线程工作函数：核心调用是同步阻塞的，进度经 PostMessage 回主界面
void ThreadWorker(bool isUnpack, std::wstring p1, std::wstring p2)
{
    auto callback = [](int progress, FolLogLevel level, const std::wstring& message) {
        auto* logMessage = new LogMessage();
        logMessage->progress = progress;
        logMessage->level = level;
        logMessage->text = message;
        PostMessageW(g_app.hwnd, WM_USER_LOG, reinterpret_cast<WPARAM>(logMessage), 0);
    };

    const int result = isUnpack ? FolCore::Unpack(p1, p2, callback) : FolCore::Pack(p1, p2, callback);
    PostMessageW(g_app.hwnd, WM_USER_TASK_DONE, result == FOL_SUCCESS ? 0 : 1, 0);
}

void SetBusy(bool busy)
{
    g_app.busy = busy;
    const BOOL enabled = busy ? FALSE : TRUE;
    EnableWindow(g_app.btnRunUnpack, enabled);
    EnableWindow(g_app.btnRunPack, enabled);
    EnableWindow(g_app.btnBrowseFol, enabled);
    EnableWindow(g_app.btnBrowseOut, enabled);
    EnableWindow(g_app.btnBrowseIn, enabled);
    EnableWindow(g_app.btnBrowseSave, enabled);
    EnableWindow(g_app.tab, enabled);
}

void StartTask(bool unpack)
{
    if (g_app.busy)
    {
        MessageBoxW(g_app.hwnd, LoadStr(IDS_ERR_BUSY).c_str(), L"提示", MB_ICONINFORMATION);
        return;
    }

    const std::wstring path1 = GetEditText(unpack ? g_app.editFol : g_app.editIn);
    const std::wstring path2 = GetEditText(unpack ? g_app.editOut : g_app.editFolOut);
    if (path1.empty())
    {
        MessageBoxW(g_app.hwnd, LoadStr(unpack ? IDS_ERR_NO_FOL : IDS_ERR_NO_DIR).c_str(), L"错误", MB_ICONWARNING);
        return;
    }
    if (path2.empty())
    {
        MessageBoxW(g_app.hwnd, LoadStr(IDS_ERR_NO_DIR).c_str(), L"错误", MB_ICONWARNING);
        return;
    }

    SetBusy(true);
    SendMessageW(g_app.progress, PBM_SETPOS, 0, 0);
    LogInfo(unpack ? L"开始解包..." : L"开始打包...");
    SetStatus(unpack ? L"正在解包..." : L"正在打包...");
    std::thread(ThreadWorker, unpack, path1, path2).detach();
}

// ============================================================
// 主窗口过程
// ============================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        g_app.hwnd = hwnd;
        g_app.dpi = GetDpiForWindow(hwnd);
        g_app.dark = IsDarkThemeEnabled();
        CreateFonts();
        if (!CreateControls(hwnd))
        {
            return -1;
        }

        // 初始尺寸按当前 DPI 缩放并限制在工作区内：
        // 布局按 DPI 放大，窗口若不跟着放大，高 DPI 下控件会互相重叠
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo))
        {
            const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
            const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
            const int width = std::min(DpiScale(680), workWidth * 9 / 10);
            const int height = std::min(DpiScale(560), workHeight * 9 / 10);
            SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        DragAcceptFiles(hwnd, TRUE);
        ApplyTheme();
        ShowTab(0);
        LogInfo(LoadStr(IDS_READY));
        SetStatus(LoadStr(IDS_ST_NO_FILE));
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            LayoutControls();
        }
        return 0;

    case WM_DPICHANGED:
    {
        g_app.dpi = HIWORD(wParam);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        HFONT oldFont = CreateFonts();
        SetFontOnChildren();
        if (oldFont != nullptr)
        {
            DeleteObject(oldFont);
        }
        LayoutControls();
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = DpiScale(560);
        info->ptMinTrackSize.y = DpiScale(440);
        return 0;
    }

    case WM_THEMECHANGED:
        g_app.dark = IsDarkThemeEnabled();
        ApplyTheme();
        return 0;

    case WM_ERASEBKGND:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT client = {};
        GetClientRect(hwnd, &client);
        HBRUSH brush = g_app.background
            ? g_app.background
            : GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &client, brush);
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_app.dark ? RGB(0xE0, 0xE0, 0xE0) : GetSysColor(COLOR_WINDOWTEXT));
        HBRUSH brush = g_app.background
            ? g_app.background
            : GetSysColorBrush(COLOR_BTNFACE);
        return reinterpret_cast<LRESULT>(brush);
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        if (g_app.dark)
        {
            SetTextColor(hdc, RGB(0xDC, 0xDC, 0xDC));
            SetBkColor(hdc, RGB(32, 32, 32));
            return reinterpret_cast<LRESULT>(g_app.background ? g_app.background : GetStockObject(HOLLOW_BRUSH));
        }
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }

    case WM_DROPFILES:
    {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count > 0 && !g_app.busy)
        {
            const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length), L'\0');
            DragQueryFileW(drop, 0, path.data(), length + 1);

            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES)
            {
                if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    SetWindowTextW(g_app.editIn, path.c_str());
                    LogInfo(L"已拖入目录，已填入「资源目录」。");
                }
                else
                {
                    SetWindowTextW(g_app.editFol, path.c_str());
                    LogInfo(L"已拖入文件，已填入「FOL 文件」。");
                    ReloadPreview();
                }
            }
        }
        DragFinish(drop);
        return 0;
    }

    case WM_NOTIFY:
    {
        NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->code == TCN_SELCHANGE && header->hwndFrom == g_app.tab)
        {
            ShowTab(TabCtrl_GetCurSel(g_app.tab));
        }
        return 0;
    }

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);

        switch (id)
        {
        case IDC_BTN_BROWSE_FOL:
        {
            const std::wstring path = OpenDialog(g_app.hwnd, false, false, L"*.fol");
            if (!path.empty())
            {
                SetWindowTextW(g_app.editFol, path.c_str());
                ReloadPreview();
            }
            return 0;
        }
        case IDC_BTN_BROWSE_OUT:
        {
            const std::wstring path = OpenDialog(g_app.hwnd, true, false);
            if (!path.empty()) SetWindowTextW(g_app.editOut, path.c_str());
            return 0;
        }
        case IDC_BTN_BROWSE_IN:
        {
            const std::wstring path = OpenDialog(g_app.hwnd, true, false);
            if (!path.empty()) SetWindowTextW(g_app.editIn, path.c_str());
            return 0;
        }
        case IDC_BTN_BROWSE_SAVE:
        {
            const std::wstring path = OpenDialog(g_app.hwnd, false, true, L"*.fol");
            if (!path.empty()) SetWindowTextW(g_app.editFolOut, path.c_str());
            return 0;
        }
        case IDC_BTN_RUN_UNPACK:
            StartTask(true);
            return 0;
        case IDC_BTN_RUN_PACK:
            StartTask(false);
            return 0;
        case IDC_EDIT_FOL_PATH:
            if (code == EN_KILLFOCUS)
            {
                ReloadPreview();
            }
            return 0;
        }
        break;
    }

    case WM_USER_LOG:
    {
        std::unique_ptr<LogMessage> logMessage(reinterpret_cast<LogMessage*>(wParam));
        if (logMessage != nullptr)
        {
            SendMessageW(g_app.progress, PBM_SETPOS, logMessage->progress, 0);
            switch (logMessage->level)
            {
            case FOL_LOG_WARNING:
                LogWarn(logMessage->text);
                break;
            case FOL_LOG_ERROR:
                LogError(logMessage->text);
                break;
            default:
                LogInfo(logMessage->text);
                break;
            }
        }
        return 0;
    }

    case WM_USER_TASK_DONE:
    {
        SetBusy(false);
        if (wParam == 0)
        {
            LogOk(L"任务完成。");
            SetStatus(L"完成。");
            MessageBoxW(hwnd, LoadStr(IDS_SUCCESS).c_str(), L"提示", MB_OK);
        }
        else
        {
            LogError(L"任务失败，请查看日志。");
            SetStatus(L"任务失败。");
            MessageBoxW(hwnd, L"任务失败，请查看日志。", L"错误", MB_OK | MB_ICONERROR);
        }
        return 0;
    }

    case WM_CLOSE:
        if (g_app.busy)
        {
            MessageBoxW(hwnd, LoadStr(IDS_ERR_BUSY).c_str(), L"提示", MB_ICONINFORMATION);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_app.font != nullptr) DeleteObject(g_app.font);
        if (g_app.background != nullptr) DeleteObject(g_app.background);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// ============================================================
// 程序入口
// ============================================================

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    g_hInst = hInstance;

    // 初始化 COM (用于文件对话框)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // 初始化公共控件
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // 加载 RichEdit 4.1 (彩色日志)
    LoadLibraryW(L"msftedit.dll");

    const wchar_t kWindowClass[] = L"FolToolMainWindow";
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    // 尺寸变化时整体失效重绘，避免残留旧位置像素
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0)
    {
        CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kWindowClass, LoadStr(IDS_APP_TITLE).c_str(),
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                680, 560, nullptr, nullptr, hInstance, nullptr);
    if (hwnd == nullptr)
    {
        CoUninitialize();
        return 1;
    }

    // 命令行参数：拖拽到 exe 图标或 .fol 关联打开时自动载入
    // 注意：wWinMain 的 lpCmdLine 已剥离程序名，首个用户参数从 argv[0] 开始
    // CommandLineToArgvW 对空字符串会返回当前 EXE 路径，因此无参数时不要调用，
    // 保证“FOL 文件”输入框初始保持为空。
    if (lpCmdLine != nullptr && lpCmdLine[0] != L'\0')
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        if (argv != nullptr)
        {
            if (argc >= 1 && argv[0] != nullptr)
            {
                const DWORD attributes = GetFileAttributesW(argv[0]);
                if (attributes != INVALID_FILE_ATTRIBUTES)
                {
                    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        SetWindowTextW(g_app.editIn, argv[0]);
                    }
                    else
                    {
                        SetWindowTextW(g_app.editFol, argv[0]);
                        ReloadPreview();
                    }
                }
            }
            LocalFree(argv);
        }
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
