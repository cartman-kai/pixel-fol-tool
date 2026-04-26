#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h> 
#include <string>
#include <thread>
#include "resource.h"
#include "FolCore.hpp"

// 自定义消息：用于线程向主界面汇报 (WPARAM=Progress, LPARAM=StringPtr)
#define WM_USER_LOG (WM_USER + 100)

// 全局实例句柄
HINSTANCE g_hInst = NULL;

// 工具函数：从资源 ID 加载字符串
std::wstring LoadStr(UINT id) {
    WCHAR buf[512];
    LoadStringW(g_hInst, id, buf, 512);
    return std::wstring(buf);
}

// 工具函数：打开文件/文件夹选择器
// isFolder: true选文件夹, false选文件
// isSave: true为保存对话框, false为打开
// filter: 文件扩展名过滤器 (如 L"*.fol")
std::wstring OpenDialog(HWND hwnd, bool isFolder, bool isSave, const wchar_t* filter = nullptr) {
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

// 界面更新函数 (在主线程运行)
void AppendLog(HWND hDlg, const std::wstring& msg) {
    HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_LOG);
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    std::wstring fullMsg = msg + L"\r\n";
    SendMessageW(hEdit, EM_REPLACESEL, 0, (LPARAM)fullMsg.c_str());
}

// 线程工作函数包装
void ThreadWorker(HWND hDlg, bool isUnpack, std::wstring p1, std::wstring p2) {
    // 禁用按钮
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_RUN_UNPACK), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_RUN_PACK), FALSE);

    // 定义回调
    auto callback = [hDlg](int progress, const std::wstring& msg) {
        // 发送消息给主窗口 (必须动态分配字符串，主窗口负责释放)
        std::wstring* pMsg = new std::wstring(msg);
        PostMessage(hDlg, WM_USER_LOG, (WPARAM)progress, (LPARAM)pMsg);
        };

    int result = isUnpack ? FolCore::Unpack(p1, p2, callback) : FolCore::Pack(p1, p2, callback);
    PostMessage(hDlg, WM_USER_LOG, (WPARAM)(result == FOL_SUCCESS ? -1 : -2), 0);
}

// 对话框过程
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // 设置图标
        HICON hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_MAIN_ICON));
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

        // 加载多语言字符串
        SetWindowTextW(hDlg, LoadStr(IDS_APP_TITLE).c_str());
        SetDlgItemTextW(hDlg, IDC_GRP_UNPACK, LoadStr(IDS_GRP_UNPACK).c_str());
        SetDlgItemTextW(hDlg, IDC_GRP_PACK, LoadStr(IDS_GRP_PACK).c_str());
        SetDlgItemTextW(hDlg, IDC_BTN_RUN_UNPACK, LoadStr(IDS_BTN_UNPACK).c_str());
        SetDlgItemTextW(hDlg, IDC_BTN_RUN_PACK, LoadStr(IDS_BTN_PACK).c_str());

        // 默认日志
        SetDlgItemTextW(hDlg, IDC_EDIT_LOG, LoadStr(IDS_READY).c_str());

        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);

        // Unpack 浏览
        if (id == IDC_BTN_BROWSE_FOL) {
            std::wstring path = OpenDialog(hDlg, false, false, L"*.fol");
            if (!path.empty()) SetDlgItemTextW(hDlg, IDC_EDIT_FOL_PATH, path.c_str());
        }
        else if (id == IDC_BTN_BROWSE_OUT) {
            std::wstring path = OpenDialog(hDlg, true, false);
            if (!path.empty()) SetDlgItemTextW(hDlg, IDC_EDIT_OUT_DIR, path.c_str());
        }
        // Pack 浏览
        else if (id == IDC_BTN_BROWSE_IN) {
            std::wstring path = OpenDialog(hDlg, true, false);
            if (!path.empty()) SetDlgItemTextW(hDlg, IDC_EDIT_IN_DIR, path.c_str());
        }
        else if (id == IDC_BTN_BROWSE_SAVE) {
            std::wstring path = OpenDialog(hDlg, false, true, L"*.fol");
            if (!path.empty()) SetDlgItemTextW(hDlg, IDC_EDIT_FOL_OUT, path.c_str());
        }
        // 执行 Unpack
        else if (id == IDC_BTN_RUN_UNPACK) {
            WCHAR path[MAX_PATH], dir[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_FOL_PATH, path, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_OUT_DIR, dir, MAX_PATH);

            if (wcslen(path) == 0) { MessageBoxW(hDlg, LoadStr(IDS_ERR_NO_FOL).c_str(), L"错误", MB_ICONWARNING); return TRUE; }
            if (wcslen(dir) == 0) { MessageBoxW(hDlg, LoadStr(IDS_ERR_NO_DIR).c_str(), L"错误", MB_ICONWARNING); return TRUE; }

            SetDlgItemTextW(hDlg, IDC_EDIT_LOG, L"开始解包...\r\n");
            std::thread(ThreadWorker, hDlg, true, std::wstring(path), std::wstring(dir)).detach();
        }
        // 执行 Pack
        else if (id == IDC_BTN_RUN_PACK) {
            WCHAR dir[MAX_PATH], file[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_IN_DIR, dir, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_FOL_OUT, file, MAX_PATH);

            if (wcslen(dir) == 0) { MessageBoxW(hDlg, LoadStr(IDS_ERR_NO_DIR).c_str(), L"错误", MB_ICONWARNING); return TRUE; }
            if (wcslen(file) == 0) { MessageBoxW(hDlg, L"请指定输出文件路径", L"错误", MB_ICONWARNING); return TRUE; }

            SetDlgItemTextW(hDlg, IDC_EDIT_LOG, L"开始打包...\r\n");
            std::thread(ThreadWorker, hDlg, false, std::wstring(dir), std::wstring(file)).detach();
        }
    }
    break;

    case WM_USER_LOG:
    {
        int progress = (int)wParam;
        std::wstring* pMsg = (std::wstring*)lParam;

        if (progress == -1 || progress == -2) {
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_RUN_UNPACK), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_RUN_PACK), TRUE);
            if (progress == -1) {
                MessageBoxW(hDlg, LoadStr(IDS_SUCCESS).c_str(), L"提示", MB_OK);
            }
            else {
                MessageBoxW(hDlg, L"任务失败，请查看日志。", L"错误", MB_OK | MB_ICONERROR);
            }
        }
        else {
            if (pMsg) {
                AppendLog(hDlg, *pMsg);
                delete pMsg; // 释放内存
            }
            SendDlgItemMessageW(hDlg, IDC_PROGRESS_BAR, PBM_SETPOS, progress, 0);
        }
    }
    break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

// 程序入口
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    g_hInst = hInstance;

    // 初始化 COM (用于文件对话框)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // 初始化公共控件 (进度条需要)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // 创建模态对话框
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, DlgProc);

    CoUninitialize();
    return 0;
}
