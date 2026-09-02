#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kInstallerClass[] = L"JAYCEELotteryInstallerWindow";
constexpr wchar_t kProductName[] = L"JAYCEE Lottery";
constexpr wchar_t kVersion[] = L"2.2.2";
constexpr int kIconResource = 101;
constexpr int kAppResource = 201;
constexpr int kReadmeResource = 202;
constexpr int kCsvResource = 203;

enum class InstallState { Ready, Installing, Complete, Failed };

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw))) {
        result = raw;
        CoTaskMemFree(raw);
    }
    return result;
}

std::filesystem::path InstallDirectory() {
    return KnownFolder(FOLDERID_LocalAppData) / L"Programs" / kProductName;
}

bool WriteResourceToFile(int resourceId, const std::filesystem::path& destination) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) return false;
    const void* bytes = LockResource(loaded);
    const DWORD size = SizeofResource(nullptr, resource);
    if (!bytes || size == 0) return false;

    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool okay = WriteFile(file, bytes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return okay;
}

bool CreateShortcut(const std::filesystem::path& shortcut,
                    const std::filesystem::path& target,
                    const std::wstring& arguments = L"") {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link)))) return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(target.parent_path().c_str());
    link->SetDescription(L"JAYCEE Lottery event drawing studio");
    link->SetIconLocation(target.c_str(), 0);
    if (!arguments.empty()) link->SetArguments(arguments.c_str());
    IPersistFile* persist = nullptr;
    const HRESULT query = link->QueryInterface(IID_PPV_ARGS(&persist));
    bool okay = false;
    if (SUCCEEDED(query)) {
        std::error_code error;
        std::filesystem::create_directories(shortcut.parent_path(), error);
        okay = SUCCEEDED(persist->Save(shortcut.c_str(), TRUE));
        persist->Release();
    }
    link->Release();
    return okay;
}

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

bool RegisterUninstaller(const std::filesystem::path& directory) {
    HKEY key = nullptr;
    constexpr wchar_t keyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\JAYCEE Lottery";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const auto app = directory / L"JAYCEE Lottery.exe";
    const auto uninstaller = directory / L"Uninstall JAYCEE Lottery.exe";
    SetRegistryString(key, L"DisplayName", kProductName);
    SetRegistryString(key, L"DisplayVersion", kVersion);
    SetRegistryString(key, L"Publisher", L"JAYCEE");
    SetRegistryString(key, L"InstallLocation", directory.wstring());
    SetRegistryString(key, L"DisplayIcon", app.wstring());
    SetRegistryString(key, L"UninstallString", L"\"" + uninstaller.wstring() + L"\" --uninstall");
    const DWORD one = 1;
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegCloseKey(key);
    return true;
}

bool ExtractPayload(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory / L"Templates", error);
    if (error) return false;
    return WriteResourceToFile(kAppResource, directory / L"JAYCEE Lottery.exe") &&
           WriteResourceToFile(kReadmeResource, directory / L"README.md") &&
           WriteResourceToFile(kCsvResource, directory / L"Templates" / L"participants-template.csv");
}

bool Install(bool desktopShortcut) {
    const auto directory = InstallDirectory();
    if (!ExtractPayload(directory)) return false;

    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    const auto uninstaller = directory / L"Uninstall JAYCEE Lottery.exe";
    if (!CopyFileW(selfPath, uninstaller.c_str(), FALSE)) return false;

    const auto app = directory / L"JAYCEE Lottery.exe";
    const auto startFolder = KnownFolder(FOLDERID_Programs) / kProductName;
    if (!CreateShortcut(startFolder / L"JAYCEE Lottery.lnk", app)) return false;
    if (!CreateShortcut(startFolder / L"Uninstall JAYCEE Lottery.lnk", uninstaller, L"--uninstall")) return false;
    if (desktopShortcut && !CreateShortcut(KnownFolder(FOLDERID_Desktop) / L"JAYCEE Lottery.lnk", app)) return false;
    return RegisterUninstaller(directory);
}

void Uninstall() {
    if (MessageBoxW(nullptr,
                    L"Remove JAYCEE Lottery from this computer?\n\nYour saved draw history in AppData will be kept.",
                    L"Uninstall JAYCEE Lottery", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;

    const auto directory = InstallDirectory();
    const auto startFolder = KnownFolder(FOLDERID_Programs) / kProductName;
    std::error_code ignored;
    std::filesystem::remove(startFolder / L"JAYCEE Lottery.lnk", ignored);
    std::filesystem::remove(startFolder / L"Uninstall JAYCEE Lottery.lnk", ignored);
    std::filesystem::remove(startFolder, ignored);
    std::filesystem::remove(KnownFolder(FOLDERID_Desktop) / L"JAYCEE Lottery.lnk", ignored);
    std::filesystem::remove(directory / L"JAYCEE Lottery.exe", ignored);
    std::filesystem::remove(directory / L"README.md", ignored);
    std::filesystem::remove(directory / L"Templates" / L"participants-template.csv", ignored);
    std::filesystem::remove(directory / L"Templates", ignored);
    RegDeleteTreeW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\JAYCEE Lottery");

    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    MoveFileExW(selfPath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(directory.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MessageBoxW(nullptr, L"JAYCEE Lottery has been removed. Your saved draw data was kept.",
                L"Uninstall complete", MB_ICONINFORMATION | MB_OK);
}

class InstallerWindow {
public:
    explicit InstallerWindow(HINSTANCE instance) : instance_(instance) {}

    int Run() {
        WNDCLASSEXW klass{};
        klass.cbSize = sizeof(klass);
        klass.lpfnWndProc = WindowProc;
        klass.hInstance = instance_;
        klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        klass.hbrBackground = CreateSolidBrush(RGB(7, 16, 31));
        klass.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(kIconResource), IMAGE_ICON,
                                                    32, 32, LR_DEFAULTCOLOR));
        klass.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(kIconResource), IMAGE_ICON,
                                                      16, 16, LR_DEFAULTCOLOR));
        klass.lpszClassName = kInstallerClass;
        klass.style = CS_HREDRAW | CS_VREDRAW;
        if (!RegisterClassExW(&klass)) return 1000 + static_cast<int>(GetLastError());

        hwnd_ = CreateWindowExW(0, kInstallerClass, L"JAYCEE Lottery Setup",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 700, 520,
                                nullptr, nullptr, instance_, this);
        if (!hwnd_) return 2000 + static_cast<int>(GetLastError());
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    HINSTANCE instance_{};
    HWND hwnd_{};
    InstallState state_ = InstallState::Ready;
    bool desktopShortcut_ = true;
    RECT installButton_{430, 416, 650, 466};
    RECT cancelButton_{278, 416, 418, 466};
    RECT checkbox_{50, 365, 310, 395};

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        InstallerWindow* self = reinterpret_cast<InstallerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<InstallerWindow*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->Handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static bool Contains(const RECT& rect, int x, int y) {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    }

    void Rounded(HDC dc, const RECT& rect, COLORREF color, int radius) {
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    void OutlineRounded(HDC dc, const RECT& rect, COLORREF color, int radius, int width = 1) {
        HPEN pen = CreatePen(PS_SOLID, width, color);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    void VerticalGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
        const int height = std::max(1L, rect.bottom - rect.top);
        for (int y = 0; y < height; y += 3) {
            const float t = static_cast<float>(y) / static_cast<float>(height);
            const int red = static_cast<int>(GetRValue(top) + (GetRValue(bottom) - GetRValue(top)) * t);
            const int green = static_cast<int>(GetGValue(top) + (GetGValue(bottom) - GetGValue(top)) * t);
            const int blue = static_cast<int>(GetBValue(top) + (GetBValue(bottom) - GetBValue(top)) * t);
            RECT strip{rect.left, rect.top + y, rect.right, std::min(rect.bottom, rect.top + y + 3)};
            HBRUSH brush = CreateSolidBrush(RGB(red, green, blue));
            FillRect(dc, &strip, brush);
            DeleteObject(brush);
        }
    }

    void DrawTextLine(HDC dc, const std::wstring& text, RECT rect, int size, int weight,
                      COLORREF color, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
        HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
        SelectObject(dc, oldFont);
        DeleteObject(font);
    }

    void Paint() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        VerticalGradient(dc, client, RGB(7, 16, 31), RGB(24, 16, 42));

        HBRUSH coralGlow = CreateSolidBrush(RGB(63, 31, 47));
        HGDIOBJ oldGlow = SelectObject(dc, coralGlow);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, 480, -82, 790, 210);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldGlow);
        DeleteObject(coralGlow);

        Rounded(dc, RECT{29, 24, 671, 122}, RGB(28, 38, 60), 28);
        OutlineRounded(dc, RECT{29, 24, 671, 122}, RGB(109, 126, 157), 28);
        OutlineRounded(dc, RECT{31, 26, 669, 120}, RGB(49, 65, 92), 26);
        Rounded(dc, RECT{50, 49, 94, 93}, RGB(255, 120, 103), 14);
        OutlineRounded(dc, RECT{50, 49, 94, 93}, RGB(255, 214, 204), 14);
        DrawTextLine(dc, L"J", RECT{50, 49, 94, 93}, 25, FW_BOLD, RGB(255, 255, 255),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextLine(dc, L"JAYCEE LOTTERY", RECT{112, 46, 470, 76}, 24, FW_SEMIBOLD, RGB(249, 251, 255));
        DrawTextLine(dc, L"Liquid Glass Edition · Version 2.2.2", RECT{112, 77, 470, 102}, 14, FW_NORMAL, RGB(184, 194, 213));
        DrawTextLine(dc, L"NATIVE WINDOWS APP", RECT{500, 58, 648, 88}, 12, FW_SEMIBOLD, RGB(255, 185, 133),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DrawTextLine(dc, state_ == InstallState::Complete ? L"You’re ready for the draw." : L"Install JAYCEE Lottery",
                     RECT{50, 158, 650, 198}, 28, FW_SEMIBOLD, RGB(246, 247, 251));
        const std::wstring detail = state_ == InstallState::Complete
            ? L"The app, Start Menu shortcut, uninstaller, and CSV template are now available."
            : state_ == InstallState::Failed
                ? L"Setup could not finish. Close any running copy and try again."
                : L"A modern lottery draw studio with participant names, prizes, history, and an audience display.";
        DrawTextLine(dc, detail, RECT{50, 202, 650, 246}, 15, FW_NORMAL,
                     state_ == InstallState::Failed ? RGB(255, 125, 125) : RGB(151, 157, 171), DT_LEFT | DT_WORDBREAK);

        Rounded(dc, RECT{50, 266, 650, 340}, RGB(28, 39, 60), 22);
        OutlineRounded(dc, RECT{50, 266, 650, 340}, RGB(88, 106, 139), 22);
        OutlineRounded(dc, RECT{52, 268, 648, 338}, RGB(43, 58, 84), 20);
        DrawTextLine(dc, L"INSTALL LOCATION", RECT{70, 277, 620, 299}, 11, FW_SEMIBOLD, RGB(151, 165, 190));
        DrawTextLine(dc, InstallDirectory().wstring(), RECT{70, 301, 620, 328}, 14, FW_NORMAL, RGB(230, 233, 240));

        if (state_ != InstallState::Complete) {
            Rounded(dc, RECT{50, 367, 70, 387}, desktopShortcut_ ? RGB(255, 120, 103) : RGB(55, 65, 83), 8);
            if (desktopShortcut_) DrawTextLine(dc, L"✓", RECT{50, 366, 70, 388}, 14, FW_BOLD, RGB(255, 255, 255),
                                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextLine(dc, L"Create a desktop shortcut", RECT{82, 361, 330, 393}, 14, FW_NORMAL, RGB(207, 211, 220));
        } else {
            DrawTextLine(dc, L"Installation complete", RECT{50, 360, 330, 395}, 14, FW_SEMIBOLD, RGB(103, 222, 159));
        }

        Rounded(dc, cancelButton_, RGB(40, 51, 72), 18);
        OutlineRounded(dc, cancelButton_, RGB(96, 112, 143), 18);
        DrawTextLine(dc, state_ == InstallState::Complete ? L"CLOSE" : L"CANCEL", cancelButton_,
                     13, FW_SEMIBOLD, RGB(210, 214, 223), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        const COLORREF primary = state_ == InstallState::Failed ? RGB(88, 94, 108) : RGB(255, 120, 103);
        Rounded(dc, installButton_, primary, 18);
        OutlineRounded(dc, installButton_, state_ == InstallState::Failed ? RGB(120, 126, 139) : RGB(255, 213, 203), 18);
        const wchar_t* label = state_ == InstallState::Complete ? L"LAUNCH JAYCEE LOTTERY"
                              : state_ == InstallState::Installing ? L"INSTALLING…" : L"INSTALL";
        DrawTextLine(dc, label, installButton_, 13, FW_SEMIBOLD, RGB(255, 255, 255),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd_, &paint);
    }

    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const bool hand = Contains(installButton_, x, y) || Contains(cancelButton_, x, y) ||
                              (state_ == InstallState::Ready && Contains(checkbox_, x, y));
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (Contains(cancelButton_, x, y)) {
                DestroyWindow(hwnd_);
            } else if (state_ == InstallState::Ready && Contains(checkbox_, x, y)) {
                desktopShortcut_ = !desktopShortcut_;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (Contains(installButton_, x, y)) {
                if (state_ == InstallState::Complete) {
                    ShellExecuteW(hwnd_, L"open", (InstallDirectory() / L"JAYCEE Lottery.exe").c_str(),
                                  nullptr, InstallDirectory().c_str(), SW_SHOWNORMAL);
                    DestroyWindow(hwnd_);
                } else if (state_ == InstallState::Ready || state_ == InstallState::Failed) {
                    state_ = InstallState::Installing;
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    UpdateWindow(hwnd_);
                    state_ = Install(desktopShortcut_) ? InstallState::Complete : InstallState::Failed;
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    int result = 0;
    if (argumentCount >= 2 && std::wstring(arguments[1]) == L"--uninstall") {
        Uninstall();
    } else if (argumentCount >= 3 && std::wstring(arguments[1]) == L"--extract-test") {
        result = ExtractPayload(arguments[2]) ? 0 : 2;
    } else {
        InstallerWindow window(instance);
        result = window.Run();
    }
    if (arguments) LocalFree(arguments);
    CoUninitialize();
    return result;
}
