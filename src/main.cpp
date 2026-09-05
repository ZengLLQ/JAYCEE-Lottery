#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <commdlg.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"JAYCEELotteryWindowClass";
constexpr wchar_t kPresentationClass[] = L"JAYCEELotteryPresentationClass";
constexpr wchar_t kWindowTitle[] = L"JAYCEE Lottery — Event Studio";
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kAnimationFrameMs = 8;
constexpr UINT kDrawDelayMs = 1450;
constexpr UINT kShowCountdownMs = 3000;
constexpr int kAppIconResource = 101;
constexpr float kMinimumCanvasHeight = 760.0f;
constexpr unsigned kSelectionBlue = 0x2f7bff;
constexpr unsigned kSelectionBlueLight = 0x76a9ff;

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

D2D1_COLOR_F Color(unsigned hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>((hex >> 16) & 0xff) / 255.0f,
        static_cast<float>((hex >> 8) & 0xff) / 255.0f,
        static_cast<float>(hex & 0xff) / 255.0f,
        alpha);
}

D2D1_COLOR_F MixColor(D2D1_COLOR_F from, D2D1_COLOR_F to, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    return D2D1::ColorF(from.r + (to.r - from.r) * t,
                        from.g + (to.g - from.g) * t,
                        from.b + (to.b - from.b) * t,
                        from.a + (to.a - from.a) * t);
}

bool Contains(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

float Width(const D2D1_RECT_F& rect) { return rect.right - rect.left; }
float Height(const D2D1_RECT_F& rect) { return rect.bottom - rect.top; }

float Saturate(float value) { return std::clamp(value, 0.0f, 1.0f); }

float EaseOutCubic(float value) {
    const float t = 1.0f - Saturate(value);
    return 1.0f - t * t * t;
}

float EaseOutBack(float value) {
    const float t = Saturate(value) - 1.0f;
    constexpr float overshoot = 0.28f;
    return 1.0f + (overshoot + 1.0f) * t * t * t + overshoot * t * t;
}

float EaseInOutSine(float value) {
    return -(std::cos(3.1415926535f * Saturate(value)) - 1.0f) * 0.5f;
}

D2D1_RECT_F OffsetRectF(const D2D1_RECT_F& rect, float x, float y) {
    return D2D1::RectF(rect.left + x, rect.top + y, rect.right + x, rect.bottom + y);
}

D2D1_RECT_F ScaleRectF(const D2D1_RECT_F& rect, float scale) {
    const float centerX = (rect.left + rect.right) * 0.5f;
    const float centerY = (rect.top + rect.bottom) * 0.5f;
    const float halfWidth = Width(rect) * scale * 0.5f;
    const float halfHeight = Height(rect) * scale * 0.5f;
    return D2D1::RectF(centerX - halfWidth, centerY - halfHeight,
                       centerX + halfWidth, centerY + halfHeight);
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) result.push_back(item);
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required);
    return result;
}

std::string EncodeField(const std::wstring& value) {
    const std::string utf8 = WideToUtf8(value);
    const char* digits = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char character : utf8) {
        if (character == '%' || character == '|' || character == '\r' || character == '\n') {
            encoded.push_back('%');
            encoded.push_back(digits[(character >> 4) & 0x0f]);
            encoded.push_back(digits[character & 0x0f]);
        } else {
            encoded.push_back(static_cast<char>(character));
        }
    }
    return encoded;
}

std::wstring DecodeField(const std::string& value) {
    auto hex = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    std::string decoded;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex(value[index + 1]);
            const int low = hex(value[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    return Utf8ToWide(decoded);
}

std::vector<std::string> ParseCsvRow(const std::string& row) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (size_t index = 0; index < row.size(); ++index) {
        const char character = row[index];
        if (character == '"') {
            if (quoted && index + 1 < row.size() && row[index + 1] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(current);
            current.clear();
        } else if (character != '\r') {
            current.push_back(character);
        }
    }
    fields.push_back(current);
    return fields;
}

std::vector<int> ParseNumbers(const std::string& value) {
    std::vector<int> numbers;
    for (const auto& item : Split(value, ',')) {
        if (item.empty()) continue;
        try { numbers.push_back(std::stoi(item)); } catch (...) {}
    }
    return numbers;
}

std::string SerializeNumbers(const std::vector<int>& numbers) {
    std::ostringstream stream;
    for (size_t index = 0; index < numbers.size(); ++index) {
        if (index) stream << ',';
        stream << numbers[index];
    }
    return stream.str();
}

std::wstring JoinNumbers(const std::vector<int>& numbers, size_t limit = SIZE_MAX) {
    std::wostringstream stream;
    const size_t count = std::min(limit, numbers.size());
    for (size_t index = 0; index < count; ++index) {
        if (index) stream << L"  ·  ";
        stream << numbers[index];
    }
    if (count < numbers.size()) stream << L"  ·  +" << (numbers.size() - count);
    return stream.str();
}

std::wstring FormatTimestamp(std::int64_t value) {
    const std::time_t raw = static_cast<std::time_t>(value);
    std::tm local{};
    localtime_s(&local, &raw);
    wchar_t buffer[64]{};
    std::wcsftime(buffer, std::size(buffer), L"%d %b %Y  ·  %H:%M", &local);
    return buffer;
}

std::wstring FormatCompactTimestamp(std::int64_t value) {
    const std::time_t raw = static_cast<std::time_t>(value);
    std::tm local{};
    localtime_s(&local, &raw);
    wchar_t buffer[64]{};
    std::wcsftime(buffer, std::size(buffer), L"%d %b  ·  %H:%M", &local);
    return buffer;
}

struct WinnerRecord {
    int ticket{};
    std::wstring name;
    std::wstring group;
};

struct HistoryEntry {
    std::int64_t timestamp{};
    bool noRepeat{};
    int quantity{};
    int maxValue{};
    std::wstring prizeName;
    std::vector<int> numbers;
    std::vector<WinnerRecord> winners;
};

struct Participant {
    int ticket{};
    std::wstring name;
    std::wstring group;
    bool enabled = true;
    bool won = false;
};

struct Prize {
    int id{};
    std::wstring name;
    std::wstring eligibleGroup = L"All participants";
    int winnerCount = 0;
};

enum class Page { Draw, Participants, Prizes, Show, History };
enum class InputField { None, Quantity, Total };
enum class DialogType {
    None, ResetPool, ClearHistory, OutOfNumbers, ClearParticipants,
    DeletePrize, AddPrize, EditPrize, EditEventName
};
enum class PresentationPhase { Idle, Countdown, Drawing, Reveal, Summary };

struct PersistentState {
    int quantity = 0;
    int total = 0;
    bool noRepeat = false;
    std::wstring eventName;
    int themeIndex = 0;
    float uiScale = 1.0f;
    bool soundEnabled = true;
    bool confettiEnabled = true;
    bool motionEnabled = true;
    int selectedPrize = 0;
    std::unordered_set<int> used;
    std::vector<Participant> participants;
    std::vector<Prize> prizes{{1, L"General Doorprize", L"All participants", 0},
                              {2, L"Grand Prize", L"All participants", 0}};
    std::vector<HistoryEntry> history;
};

struct Layout {
    D2D1_RECT_F drawNav{};
    D2D1_RECT_F participantsNav{};
    D2D1_RECT_F prizesNav{};
    D2D1_RECT_F showNav{};
    D2D1_RECT_F historyNav{};
    D2D1_RECT_F fullScreenButton{};
    D2D1_RECT_F quantityMinus{};
    D2D1_RECT_F quantityValue{};
    D2D1_RECT_F quantityPlus{};
    D2D1_RECT_F totalMinus{};
    D2D1_RECT_F totalValue{};
    D2D1_RECT_F totalPlus{};
    D2D1_RECT_F noRepeatToggle{};
    D2D1_RECT_F drawButton{};
    D2D1_RECT_F resultsCard{};
    D2D1_RECT_F recentCard{};
    D2D1_RECT_F recentViewAll{};
    D2D1_RECT_F poolCard{};
    D2D1_RECT_F resetPoolButton{};
    D2D1_RECT_F clearHistoryButton{};
    D2D1_RECT_F exportHistoryButton{};
    D2D1_RECT_F historyViewport{};
    D2D1_RECT_F confirmWinnersButton{};
    D2D1_RECT_F redrawButton{};
    D2D1_RECT_F importParticipantsButton{};
    D2D1_RECT_F clearParticipantsButton{};
    D2D1_RECT_F participantsViewport{};
    D2D1_RECT_F addPrizeButton{};
    D2D1_RECT_F editPrizeButton{};
    D2D1_RECT_F deletePrizeButton{};
    D2D1_RECT_F prizeEligibilityButton{};
    D2D1_RECT_F prizeCards[8]{};
    D2D1_RECT_F openPresentationButton{};
    D2D1_RECT_F eventNameButton{};
    D2D1_RECT_F soundToggle{};
    D2D1_RECT_F confettiToggle{};
    D2D1_RECT_F motionToggle{};
    D2D1_RECT_F themeButtons[4]{};
    D2D1_RECT_F scaleMinusButton{};
    D2D1_RECT_F scaleValue{};
    D2D1_RECT_F scalePlusButton{};
    D2D1_RECT_F dialogCancel{};
    D2D1_RECT_F dialogConfirm{};
    D2D1_RECT_F dialogTextField{};
};

class JayceeLotteryApp {
public:
    explicit JayceeLotteryApp(HINSTANCE instance) : instance_(instance) {}

    ~JayceeLotteryApp() {
        if (highResolutionTimer_) timeEndPeriod(1);
        SafeRelease(presentationBrush_);
        SafeRelease(presentationTarget_);
        DiscardDeviceResources();
        SafeRelease(formatDisplay_);
        SafeRelease(formatHero_);
        SafeRelease(formatTitle_);
        SafeRelease(formatHeading_);
        SafeRelease(formatBody_);
        SafeRelease(formatBodyMedium_);
        SafeRelease(formatCaption_);
        SafeRelease(formatNumber_);
        SafeRelease(formatTileNumber_);
        SafeRelease(dwriteFactory_);
        SafeRelease(d2dFactory_);
    }

    int Run(int commandShow) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

        if (!InitializeFactories() || !RegisterWindowClass()) {
            CoUninitialize();
            return 1;
        }

        LoadState();
        if (!CreateMainWindow(commandShow)) {
            CoUninitialize();
            return 1;
        }
        highResolutionTimer_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        lastAnimationTick_ = std::chrono::steady_clock::now();
        SetTimer(window_, kAnimationTimer, kAnimationFrameMs, nullptr);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        CoUninitialize();
        return static_cast<int>(message.wParam);
    }

private:
    HINSTANCE instance_{};
    HWND window_{};
    HWND presentationWindow_{};
    ID2D1Factory* d2dFactory_{};
    IDWriteFactory* dwriteFactory_{};
    ID2D1HwndRenderTarget* renderTarget_{};
    ID2D1SolidColorBrush* brush_{};
    ID2D1HwndRenderTarget* presentationTarget_{};
    ID2D1SolidColorBrush* presentationBrush_{};

    IDWriteTextFormat* formatDisplay_{};
    IDWriteTextFormat* formatHero_{};
    IDWriteTextFormat* formatTitle_{};
    IDWriteTextFormat* formatHeading_{};
    IDWriteTextFormat* formatBody_{};
    IDWriteTextFormat* formatBodyMedium_{};
    IDWriteTextFormat* formatCaption_{};
    IDWriteTextFormat* formatNumber_{};
    IDWriteTextFormat* formatTileNumber_{};

    PersistentState data_{};
    Layout layout_{};
    Page page_ = Page::Draw;
    InputField activeInput_ = InputField::None;
    DialogType dialog_ = DialogType::None;
    std::wstring inputBuffer_;
    std::wstring dialogText_;
    int dialogPrizeIndex_ = -1;
    bool inputSelectAll_ = false;
    bool drawing_ = false;
    bool awaitingConfirmation_ = false;
    std::chrono::steady_clock::time_point drawStarted_{};
    std::chrono::steady_clock::time_point slideshowStarted_{};
    std::chrono::steady_clock::time_point appStarted_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point pageTransitionStarted_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point revealStarted_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::chrono::steady_clock::time_point confirmStarted_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::chrono::steady_clock::time_point toastStarted_ = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    std::chrono::steady_clock::time_point dialogAnimationStarted_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastAnimationTick_ = std::chrono::steady_clock::now();
    std::vector<int> latestNumbers_;
    std::vector<WinnerRecord> pendingWinners_;
    PresentationPhase presentationPhase_ = PresentationPhase::Idle;
    float mouseX_ = -1000.0f;
    float mouseY_ = -1000.0f;
    bool mouseTracking_ = false;
    float historyScroll_ = 0.0f;
    float resultScroll_ = 0.0f;
    float historyContentHeight_ = 0.0f;
    float resultContentHeight_ = 0.0f;
    float participantsScroll_ = 0.0f;
    float participantsContentHeight_ = 0.0f;
    float pageScroll_ = 0.0f;
    float pageScrollTarget_ = 0.0f;
    float historyScrollTarget_ = 0.0f;
    float resultScrollTarget_ = 0.0f;
    float participantsScrollTarget_ = 0.0f;
    float frameDeltaSeconds_ = 1.0f / 60.0f;
    bool highResolutionTimer_ = false;
    std::unordered_map<std::uint64_t, float> hoverAnimations_;
    std::wstring toast_;
    std::chrono::steady_clock::time_point toastUntil_{};
    std::filesystem::path statePath_;
    bool fullScreen_ = false;
    DialogType animatedDialog_ = DialogType::None;
    WINDOWPLACEMENT previousPlacement_{};
    LONG_PTR previousStyle_{};

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        JayceeLotteryApp* app = reinterpret_cast<JayceeLotteryApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<JayceeLotteryApp*>(create->lpCreateParams);
            app->window_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        return app ? app->HandleMessage(message, wParam, lParam)
                   : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    static LRESULT CALLBACK PresentationProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        JayceeLotteryApp* app = reinterpret_cast<JayceeLotteryApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<JayceeLotteryApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(hwnd, message, wParam, lParam);
        switch (message) {
        case WM_PAINT:
            app->PaintPresentation(hwnd);
            return 0;
        case WM_SIZE:
            if (app->presentationTarget_) {
                app->presentationTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_F11) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            SafeRelease(app->presentationBrush_);
            SafeRelease(app->presentationTarget_);
            app->presentationWindow_ = nullptr;
            InvalidateRect(app->window_, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool InitializeFactories() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_))) return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                       __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&dwriteFactory_)))) return false;

        return CreateTextFormat(54.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatDisplay_) &&
               CreateTextFormat(92.0f, DWRITE_FONT_WEIGHT_BOLD, &formatHero_) &&
               CreateTextFormat(31.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatTitle_) &&
               CreateTextFormat(19.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatHeading_) &&
               CreateTextFormat(14.0f, DWRITE_FONT_WEIGHT_NORMAL, &formatBody_) &&
               CreateTextFormat(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatBodyMedium_) &&
               CreateTextFormat(11.5f, DWRITE_FONT_WEIGHT_MEDIUM, &formatCaption_) &&
               CreateTextFormat(32.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatNumber_) &&
               CreateTextFormat(35.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &formatTileNumber_);
    }

    bool CreateTextFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** output) {
        const HRESULT result = dwriteFactory_->CreateTextFormat(
            L"Segoe UI Variable Display", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", output);
        if (FAILED(result)) return false;
        (*output)->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        return true;
    }

    bool RegisterWindowClass() {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(kAppIconResource));
        if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        windowClass.hIconSm = windowClass.hIcon;
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0) return false;

        WNDCLASSEXW presentationClass = windowClass;
        presentationClass.lpfnWndProc = PresentationProc;
        presentationClass.lpszClassName = kPresentationClass;
        return RegisterClassExW(&presentationClass) != 0;
    }

    bool CreateMainWindow(int commandShow) {
        window_ = CreateWindowExW(
            0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, 1380, 840, nullptr, nullptr, instance_, this);
        if (!window_) return false;

        const BOOL dark = TRUE;
        const DWORD corner = 2;
        DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(window_, 33, &corner, sizeof(corner));

        ShowWindow(window_, commandShow == SW_SHOWDEFAULT ? SW_MAXIMIZE : commandShow);
        UpdateWindow(window_);
        return true;
    }

    HRESULT CreateDeviceResources() {
        if (renderTarget_) return S_OK;
        RECT client{};
        GetClientRect(window_, &client);
        const D2D1_SIZE_U size = D2D1::SizeU(client.right - client.left, client.bottom - client.top);
        HRESULT result = d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(window_, size), &renderTarget_);
        if (SUCCEEDED(result)) result = renderTarget_->CreateSolidColorBrush(Color(0xffffff), &brush_);
        return result;
    }

    void DiscardDeviceResources() {
        SafeRelease(brush_);
        SafeRelease(renderTarget_);
    }

    void LoadState() {
        PWSTR localPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localPath))) {
            statePath_ = std::filesystem::path(localPath) / L"JAYCEE Lottery" / L"lottery_state.txt";
            CoTaskMemFree(localPath);
        } else {
            statePath_ = std::filesystem::current_path() / L"lottery_state.txt";
        }

        std::ifstream input(statePath_);
        if (!input) return;

        std::string line;
        bool loadedPrizes = false;
        while (std::getline(input, line)) {
            const auto parts = Split(line, '|');
            if (parts.empty()) continue;
            try {
                if (parts[0] == "SETTINGS" && parts.size() >= 4) {
                    data_.quantity = std::clamp(std::stoi(parts[1]), 0, 999999);
                    data_.total = std::clamp(std::stoi(parts[2]), 0, 999999);
                    data_.noRepeat = parts[3] == "1";
                    if (parts.size() >= 9) {
                        data_.eventName = DecodeField(parts[4]);
                        data_.themeIndex = std::clamp(std::stoi(parts[5]), 0, 3);
                        data_.soundEnabled = parts[6] == "1";
                        data_.confettiEnabled = parts[7] == "1";
                        data_.selectedPrize = std::max(0, std::stoi(parts[8]));
                        if (parts.size() >= 10) data_.uiScale = std::clamp(std::stof(parts[9]), 0.75f, 1.35f);
                        if (parts.size() >= 11) data_.motionEnabled = parts[10] == "1";
                    }
                    data_.quantity = std::min(data_.quantity, data_.total);
                } else if (parts[0] == "USED" && parts.size() >= 2) {
                    for (int number : ParseNumbers(parts[1])) if (number > 0) data_.used.insert(number);
                } else if (parts[0] == "PARTICIPANT" && parts.size() >= 6) {
                    Participant participant;
                    participant.ticket = std::stoi(parts[1]);
                    participant.name = DecodeField(parts[2]);
                    participant.group = DecodeField(parts[3]);
                    participant.enabled = parts[4] == "1";
                    participant.won = parts[5] == "1";
                    if (participant.ticket > 0 && !participant.name.empty()) data_.participants.push_back(std::move(participant));
                } else if (parts[0] == "PRIZE" && parts.size() >= 5) {
                    if (!loadedPrizes) {
                        data_.prizes.clear();
                        loadedPrizes = true;
                    }
                    Prize prize;
                    prize.id = std::stoi(parts[1]);
                    prize.name = DecodeField(parts[2]);
                    prize.eligibleGroup = DecodeField(parts[3]);
                    prize.winnerCount = std::clamp(std::stoi(parts[4]), 0, 999);
                    if (!prize.name.empty()) data_.prizes.push_back(std::move(prize));
                } else if (parts[0] == "HISTORY2" && parts.size() >= 7) {
                    HistoryEntry entry;
                    entry.timestamp = std::stoll(parts[1]);
                    entry.noRepeat = parts[2] == "1";
                    entry.quantity = std::stoi(parts[3]);
                    entry.maxValue = std::stoi(parts[4]);
                    entry.prizeName = DecodeField(parts[5]);
                    entry.numbers = ParseNumbers(parts[6]);
                    if (!entry.numbers.empty()) data_.history.push_back(std::move(entry));
                } else if (parts[0] == "HWINNER" && parts.size() >= 5 && !data_.history.empty()) {
                    const size_t historyIndex = static_cast<size_t>(std::stoull(parts[1]));
                    if (historyIndex < data_.history.size()) {
                        data_.history[historyIndex].winners.push_back(
                            WinnerRecord{std::stoi(parts[2]), DecodeField(parts[3]), DecodeField(parts[4])});
                    }
                } else if (parts[0] == "HISTORY" && parts.size() >= 6) {
                    HistoryEntry entry;
                    entry.timestamp = std::stoll(parts[1]);
                    entry.noRepeat = parts[2] == "1";
                    entry.quantity = std::stoi(parts[3]);
                    entry.maxValue = std::stoi(parts[4]);
                    entry.prizeName = L"General Doorprize";
                    entry.numbers = ParseNumbers(parts[5]);
                    if (!entry.numbers.empty()) data_.history.push_back(std::move(entry));
                }
            } catch (...) {}
        }
        if (data_.prizes.empty()) data_.prizes.push_back({1, L"General Doorprize", L"All participants", 0});
        if (data_.eventName == L"JAYCEE Celebration Night") data_.eventName.clear();
        data_.selectedPrize = std::clamp(data_.selectedPrize, 0, static_cast<int>(data_.prizes.size()) - 1);
        if (!data_.participants.empty()) {
            data_.total = static_cast<int>(data_.participants.size());
            data_.quantity = std::min(data_.quantity, data_.total);
        }
        ActivePrize().winnerCount = data_.quantity;
    }

    void SaveState() {
        if (statePath_.empty()) return;
        try {
            std::filesystem::create_directories(statePath_.parent_path());
            const auto temporary = statePath_.wstring() + L".tmp";
            std::ofstream output(std::filesystem::path(temporary), std::ios::trunc);
            output << "JAYCEE_LOTTERY_STATE_V2\n";
            output << "SETTINGS|" << data_.quantity << '|' << data_.total << '|'
                   << (data_.noRepeat ? 1 : 0) << '|' << EncodeField(data_.eventName) << '|'
                    << data_.themeIndex << '|' << (data_.soundEnabled ? 1 : 0) << '|'
                    << (data_.confettiEnabled ? 1 : 0) << '|' << data_.selectedPrize << '|'
                    << std::fixed << std::setprecision(2) << data_.uiScale << '|'
                    << (data_.motionEnabled ? 1 : 0) << '\n';

            std::vector<int> used(data_.used.begin(), data_.used.end());
            std::sort(used.begin(), used.end());
            output << "USED|" << SerializeNumbers(used) << '\n';
            for (const auto& participant : data_.participants) {
                output << "PARTICIPANT|" << participant.ticket << '|' << EncodeField(participant.name)
                       << '|' << EncodeField(participant.group) << '|' << (participant.enabled ? 1 : 0)
                       << '|' << (participant.won ? 1 : 0) << '\n';
            }
            for (const auto& prize : data_.prizes) {
                output << "PRIZE|" << prize.id << '|' << EncodeField(prize.name) << '|'
                       << EncodeField(prize.eligibleGroup) << '|' << prize.winnerCount << '\n';
            }
            for (size_t historyIndex = 0; historyIndex < data_.history.size(); ++historyIndex) {
                const auto& entry = data_.history[historyIndex];
                output << "HISTORY2|" << entry.timestamp << '|' << (entry.noRepeat ? 1 : 0)
                       << '|' << entry.quantity << '|' << entry.maxValue << '|'
                       << EncodeField(entry.prizeName) << '|' << SerializeNumbers(entry.numbers) << '\n';
                for (const auto& winner : entry.winners) {
                    output << "HWINNER|" << historyIndex << '|' << winner.ticket << '|'
                           << EncodeField(winner.name) << '|' << EncodeField(winner.group) << '\n';
                }
            }
            output.close();
            MoveFileExW(temporary.c_str(), statePath_.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        } catch (...) {
            ShowToast(L"Could not save local draw data");
        }
    }

    const Prize& ActivePrize() const {
        return data_.prizes[static_cast<size_t>(std::clamp(data_.selectedPrize, 0, static_cast<int>(data_.prizes.size()) - 1))];
    }

    Prize& ActivePrize() {
        return data_.prizes[static_cast<size_t>(std::clamp(data_.selectedPrize, 0, static_cast<int>(data_.prizes.size()) - 1))];
    }

    bool ParticipantMatchesPrize(const Participant& participant) const {
        const auto& prize = ActivePrize();
        return participant.enabled &&
               (prize.eligibleGroup.empty() || prize.eligibleGroup == L"All participants" ||
                participant.group == prize.eligibleGroup);
    }

    std::vector<size_t> EligibleParticipants(bool respectNoRepeat) const {
        std::vector<size_t> eligible;
        for (size_t index = 0; index < data_.participants.size(); ++index) {
            const auto& participant = data_.participants[index];
            if (!ParticipantMatchesPrize(participant)) continue;
            if (respectNoRepeat && (participant.won || data_.used.contains(participant.ticket))) continue;
            eligible.push_back(index);
        }
        return eligible;
    }

    int ValidUsedCount() const {
        if (!data_.participants.empty()) {
            return static_cast<int>(std::count_if(data_.participants.begin(), data_.participants.end(),
                [this](const Participant& participant) {
                    return ParticipantMatchesPrize(participant) &&
                           (participant.won || data_.used.contains(participant.ticket));
                }));
        }
        return static_cast<int>(std::count_if(data_.used.begin(), data_.used.end(),
            [this](int value) { return value >= 1 && value <= data_.total; }));
    }

    int PoolSize() const {
        if (!data_.participants.empty()) return static_cast<int>(EligibleParticipants(false).size());
        return data_.total;
    }

    int RemainingCount() const {
        if (!data_.participants.empty()) return static_cast<int>(EligibleParticipants(true).size());
        return std::max(0, data_.total - ValidUsedCount());
    }

    std::uint32_t RandomValue(std::uint32_t upperExclusive) const {
        if (upperExclusive <= 1) return 0;
        const std::uint32_t threshold = static_cast<std::uint32_t>(-upperExclusive) % upperExclusive;
        std::uint32_t value = 0;
        do {
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
                value = static_cast<std::uint32_t>(GetTickCount64());
            }
        } while (value < threshold);
        return value % upperExclusive;
    }

    void BeginDraw() {
        CommitInput();
        if (drawing_ || dialog_ != DialogType::None) return;
        if (awaitingConfirmation_) {
            ShowToast(L"Confirm or redraw the current candidates first");
            return;
        }
        if (PoolSize() < 1) {
            ShowToast(data_.participants.empty() ? L"Set total coupons above 0 first"
                                                  : L"Import at least one eligible participant first");
            return;
        }
        if (data_.quantity < 1) {
            ShowToast(L"Set winners above 0 first");
            return;
        }
        const int available = data_.noRepeat ? RemainingCount() : PoolSize();
        if (data_.quantity < 1 || data_.quantity > available) {
            if (!data_.noRepeat) ShowToast(L"Winner count exceeds the eligible pool");
            else dialog_ = DialogType::OutOfNumbers;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (data_.noRepeat && RemainingCount() < data_.quantity) {
            dialog_ = DialogType::OutOfNumbers;
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        drawing_ = true;
        drawStarted_ = std::chrono::steady_clock::now();
        latestNumbers_.clear();
        pendingWinners_.clear();
        if (presentationWindow_) presentationPhase_ = PresentationPhase::Countdown;
        InvalidateRect(window_, nullptr, FALSE);
        if (presentationWindow_) InvalidateRect(presentationWindow_, nullptr, FALSE);
    }

    void FinishDraw() {
        std::vector<int> result;
        result.reserve(static_cast<size_t>(data_.quantity));

        if (!data_.participants.empty()) {
            auto eligible = EligibleParticipants(data_.noRepeat);
            for (int index = 0; index < data_.quantity; ++index) {
                const size_t remaining = eligible.size() - static_cast<size_t>(index);
                const size_t selected = static_cast<size_t>(index) + RandomValue(static_cast<std::uint32_t>(remaining));
                std::swap(eligible[static_cast<size_t>(index)], eligible[selected]);
                const auto& participant = data_.participants[eligible[static_cast<size_t>(index)]];
                result.push_back(participant.ticket);
                pendingWinners_.push_back({participant.ticket, participant.name, participant.group});
            }
        } else {
            std::vector<int> available;
            available.reserve(static_cast<size_t>(data_.total));
            for (int value = 1; value <= data_.total; ++value) {
                if (!data_.noRepeat || !data_.used.contains(value)) available.push_back(value);
            }
            for (int index = 0; index < data_.quantity; ++index) {
                const size_t remaining = available.size() - static_cast<size_t>(index);
                const size_t selected = static_cast<size_t>(index) + RandomValue(static_cast<std::uint32_t>(remaining));
                std::swap(available[static_cast<size_t>(index)], available[selected]);
                result.push_back(available[static_cast<size_t>(index)]);
                pendingWinners_.push_back({available[static_cast<size_t>(index)], L"", L""});
            }
        }

        std::sort(result.begin(), result.end());
        std::sort(pendingWinners_.begin(), pendingWinners_.end(),
                  [](const WinnerRecord& left, const WinnerRecord& right) { return left.ticket < right.ticket; });
        latestNumbers_ = result;
        resultScroll_ = resultScrollTarget_ = 0.0f;
        drawing_ = false;
        awaitingConfirmation_ = true;
        presentationPhase_ = PresentationPhase::Reveal;
        revealStarted_ = std::chrono::steady_clock::now();
        if (data_.soundEnabled) PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
        ShowToast(L"Candidates ready — confirm or redraw");
        if (presentationWindow_) InvalidateRect(presentationWindow_, nullptr, FALSE);
    }

    void ConfirmWinners() {
        if (!awaitingConfirmation_ || pendingWinners_.empty()) return;
        for (const auto& winner : pendingWinners_) {
            data_.used.insert(winner.ticket);
            for (auto& participant : data_.participants) {
                if (participant.ticket == winner.ticket) participant.won = true;
            }
        }
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        HistoryEntry entry;
        entry.timestamp = seconds;
        entry.noRepeat = data_.noRepeat;
        entry.quantity = static_cast<int>(latestNumbers_.size());
        entry.maxValue = data_.total;
        entry.prizeName = ActivePrize().name;
        entry.numbers = latestNumbers_;
        entry.winners = pendingWinners_;
        data_.history.push_back(std::move(entry));
        awaitingConfirmation_ = false;
        presentationPhase_ = PresentationPhase::Summary;
        confirmStarted_ = std::chrono::steady_clock::now();
        SaveState();
        ShowToast(L"Winners confirmed and saved");
        if (presentationWindow_) InvalidateRect(presentationWindow_, nullptr, FALSE);
    }

    void RedrawCandidates() {
        if (!awaitingConfirmation_) return;
        awaitingConfirmation_ = false;
        latestNumbers_.clear();
        pendingWinners_.clear();
        BeginDraw();
    }

    static std::string CsvEscape(const std::wstring& value) {
        std::string utf8 = WideToUtf8(value);
        size_t position = 0;
        while ((position = utf8.find('"', position)) != std::string::npos) {
            utf8.insert(position, 1, '"');
            position += 2;
        }
        return '"' + utf8 + '"';
    }

    void ImportParticipants() {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"CSV participant files (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrTitle = L"Import JAYCEE Lottery participants";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetOpenFileNameW(&dialog)) return;

        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        if (!input) {
            ShowToast(L"Could not open the selected CSV file");
            return;
        }
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) bytes.erase(0, 3);
        std::stringstream rows(bytes);
        std::string row;
        std::vector<Participant> imported;
        std::unordered_set<int> tickets;
        int ticketColumn = 0;
        int nameColumn = 1;
        int groupColumn = 2;
        bool firstRow = true;
        int generatedTicket = 1;

        while (std::getline(rows, row)) {
            auto fields = ParseCsvRow(row);
            if (fields.empty()) continue;
            if (firstRow) {
                firstRow = false;
                bool header = false;
                for (size_t index = 0; index < fields.size(); ++index) {
                    std::string label = fields[index];
                    std::transform(label.begin(), label.end(), label.begin(),
                                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                    if (label == "ticket" || label == "coupon" || label == "number" || label == "no") {
                        ticketColumn = static_cast<int>(index); header = true;
                    } else if (label == "name" || label == "participant") {
                        nameColumn = static_cast<int>(index); header = true;
                    } else if (label == "group" || label == "department" || label == "category") {
                        groupColumn = static_cast<int>(index); header = true;
                    }
                }
                if (header) continue;
            }

            int ticket = generatedTicket;
            if (ticketColumn >= 0 && ticketColumn < static_cast<int>(fields.size())) {
                try { ticket = std::stoi(fields[static_cast<size_t>(ticketColumn)]); } catch (...) {}
            }
            while (ticket <= 0 || tickets.contains(ticket)) ticket = ++generatedTicket;
            generatedTicket = std::max(generatedTicket, ticket);
            const std::wstring name = nameColumn >= 0 && nameColumn < static_cast<int>(fields.size())
                                        ? Utf8ToWide(fields[static_cast<size_t>(nameColumn)]) : L"";
            const std::wstring group = groupColumn >= 0 && groupColumn < static_cast<int>(fields.size())
                                         ? Utf8ToWide(fields[static_cast<size_t>(groupColumn)]) : L"General";
            if (name.empty()) continue;
            tickets.insert(ticket);
            imported.push_back({ticket, name, group.empty() ? L"General" : group, true, false});
            ++generatedTicket;
        }

        if (imported.empty()) {
            ShowToast(L"No valid participants were found in that CSV");
            return;
        }
        data_.participants = std::move(imported);
        data_.total = static_cast<int>(data_.participants.size());
        data_.quantity = std::clamp(data_.quantity, 0, data_.total);
        ActivePrize().winnerCount = data_.quantity;
        data_.used.clear();
        awaitingConfirmation_ = false;
        latestNumbers_.clear();
        pendingWinners_.clear();
        participantsScroll_ = participantsScrollTarget_ = 0.0f;
        SaveState();
        ShowToast(std::to_wstring(data_.participants.size()) + L" participants imported");
    }

    void ExportHistory() {
        wchar_t path[MAX_PATH] = L"JAYCEE-Lottery-Winners.csv";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"CSV report (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"csv";
        dialog.lpstrTitle = L"Export confirmed winners";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
        if (!GetSaveFileNameW(&dialog)) return;

        std::ofstream output(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!output) {
            ShowToast(L"Could not create the report file");
            return;
        }
        output << "\xEF\xBB\xBF";
        output << "Draw,Date,Prize,Ticket,Name,Group,Mode\r\n";
        for (size_t index = 0; index < data_.history.size(); ++index) {
            const auto& entry = data_.history[index];
            if (!entry.winners.empty()) {
                for (const auto& winner : entry.winners) {
                    output << (index + 1) << ',' << CsvEscape(FormatTimestamp(entry.timestamp)) << ','
                           << CsvEscape(entry.prizeName) << ',' << winner.ticket << ',' << CsvEscape(winner.name)
                           << ',' << CsvEscape(winner.group) << ','
                           << CsvEscape(entry.noRepeat ? L"No repeat" : L"Standard") << "\r\n";
                }
            } else {
                for (int number : entry.numbers) {
                    output << (index + 1) << ',' << CsvEscape(FormatTimestamp(entry.timestamp)) << ','
                           << CsvEscape(entry.prizeName) << ',' << number << ",\"\",\"\","
                           << CsvEscape(entry.noRepeat ? L"No repeat" : L"Standard") << "\r\n";
                }
            }
        }
        output.close();
        ShowToast(L"Winner report exported");
    }

    static BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT rect, LPARAM context) {
        static_cast<std::vector<RECT>*>(reinterpret_cast<void*>(context))->push_back(*rect);
        return TRUE;
    }

    unsigned AccentHex() const {
        static constexpr unsigned accents[] = {0x2f7bff, 0x14b8a6, 0x8b5cf6, 0xf59e0b};
        return accents[std::clamp(data_.themeIndex, 0, 3)];
    }

    unsigned SelectionBlueHex() const { return kSelectionBlue; }

    void OpenPresentation() {
        if (presentationWindow_) {
            ShowWindow(presentationWindow_, SW_SHOW);
            SetForegroundWindow(presentationWindow_);
            return;
        }
        std::vector<RECT> monitors;
        EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
        const HMONITOR operatorMonitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO operatorInfo{};
        operatorInfo.cbSize = sizeof(operatorInfo);
        GetMonitorInfoW(operatorMonitor, &operatorInfo);

        std::optional<RECT> audienceMonitor;
        for (const RECT& rect : monitors) {
            if (rect.left != operatorInfo.rcMonitor.left || rect.top != operatorInfo.rcMonitor.top) {
                audienceMonitor = rect;
                break;
            }
        }

        DWORD style = audienceMonitor ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        DWORD extended = audienceMonitor ? WS_EX_TOPMOST | WS_EX_TOOLWINDOW : 0;
        RECT target = audienceMonitor.value_or(RECT{140, 100, 1140, 700});
        presentationWindow_ = CreateWindowExW(
            extended, kPresentationClass, L"JAYCEE Lottery — Audience Display", style,
            target.left, target.top, target.right - target.left, target.bottom - target.top,
            nullptr, nullptr, instance_, this);
        if (!presentationWindow_) {
            ShowToast(L"Could not open the audience display");
            return;
        }
        const BOOL dark = TRUE;
        DwmSetWindowAttribute(presentationWindow_, 20, &dark, sizeof(dark));
        presentationPhase_ = PresentationPhase::Idle;
        slideshowStarted_ = std::chrono::steady_clock::now();
        ShowWindow(presentationWindow_, audienceMonitor ? SW_SHOW : SW_SHOWNORMAL);
        UpdateWindow(presentationWindow_);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void PaintPresentation(HWND hwnd) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        if (!presentationTarget_) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const D2D1_SIZE_U size = D2D1::SizeU(client.right - client.left, client.bottom - client.top);
            if (FAILED(d2dFactory_->CreateHwndRenderTarget(
                    D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, size),
                    &presentationTarget_)) ||
                FAILED(presentationTarget_->CreateSolidColorBrush(Color(0xffffff), &presentationBrush_))) {
                EndPaint(hwnd, &paint);
                return;
            }
        }

        const auto pixels = presentationTarget_->GetPixelSize();
        const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
        const float width = pixels.width * 96.0f / dpi;
        const float height = pixels.height * 96.0f / dpi;
        const unsigned accentHex = AccentHex();
        const float time = data_.motionEnabled ? AnimationSeconds() : 0.0f;
        auto fill = [&](const D2D1_RECT_F& rect, D2D1_COLOR_F color, float radius = 0.0f) {
            presentationBrush_->SetColor(color);
            if (radius > 0.0f) presentationTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), presentationBrush_);
            else presentationTarget_->FillRectangle(rect, presentationBrush_);
        };
        auto text = [&](const std::wstring& value, const D2D1_RECT_F& rect, IDWriteTextFormat* format,
                        D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_CENTER) {
            format->SetTextAlignment(alignment);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            presentationBrush_->SetColor(color);
            presentationTarget_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format, rect,
                                            presentationBrush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        auto radial = [&](D2D1_POINT_2F center, float radius, unsigned color, float alpha) {
            ID2D1GradientStopCollection* localStops = nullptr;
            ID2D1RadialGradientBrush* localGradient = nullptr;
            const D2D1_GRADIENT_STOP values[] = {
                {0.0f, Color(color, alpha)}, {0.48f, Color(color, alpha * 0.25f)},
                {1.0f, Color(color, 0.0f)}
            };
            if (SUCCEEDED(presentationTarget_->CreateGradientStopCollection(values, 3, &localStops))) {
                presentationTarget_->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(), radius, radius),
                    localStops, &localGradient);
            }
            if (localGradient) presentationTarget_->FillRectangle(
                D2D1::RectF(center.x - radius, center.y - radius, center.x + radius, center.y + radius),
                localGradient);
            SafeRelease(localGradient);
            SafeRelease(localStops);
        };
        auto elevatedCard = [&](const D2D1_RECT_F& rect, float radius, bool featured) {
            fill(OffsetRectF(rect, 0.0f, featured ? 9.0f : 6.0f), Color(0x000000, 0.34f), radius + 3.0f);
            fill(rect, featured ? Color(SelectionBlueHex(), 0.72f) : Color(0x141c2a, 0.96f), radius);
            fill(D2D1::RectF(rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2),
                 featured ? Color(0x0d3f99, 0.32f) : Color(0x0a101b, 0.48f),
                 std::max(1.0f, radius - 2.0f));
            presentationBrush_->SetColor(featured ? Color(kSelectionBlueLight, 0.88f) : Color(0xffffff, 0.12f));
            presentationTarget_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), presentationBrush_,
                                                      featured ? 1.6f : 1.0f);
            presentationBrush_->SetColor(Color(0xffffff, featured ? 0.36f : 0.14f));
            presentationTarget_->DrawLine(D2D1::Point2F(rect.left + radius, rect.top + 1),
                                          D2D1::Point2F(rect.right - radius, rect.top + 1),
                                          presentationBrush_, 1.0f);
        };

        presentationTarget_->BeginDraw();
        presentationTarget_->Clear(Color(0x05070d));
        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1LinearGradientBrush* sky = nullptr;
        const D2D1_GRADIENT_STOP skyStops[] = {
            {0.0f, Color(0x05070d)}, {0.48f, Color(0x080d18)},
            {0.78f, Color(0x0b1220)}, {1.0f, Color(0x05070d)}};
        if (SUCCEEDED(presentationTarget_->CreateGradientStopCollection(skyStops, 4, &stops))) {
            presentationTarget_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(width, height)),
                stops, &sky);
        }
        if (sky) presentationTarget_->FillRectangle(D2D1::RectF(0, 0, width, height), sky);
        SafeRelease(sky);
        SafeRelease(stops);

        radial(D2D1::Point2F(width * 0.82f + std::sin(time * 0.09f) * 22.0f,
                             height * 0.18f + std::cos(time * 0.08f) * 16.0f),
               std::max(400.0f, width * 0.35f), SelectionBlueHex(), 0.19f);
        radial(D2D1::Point2F(width * 0.14f + std::cos(time * 0.08f) * 18.0f,
                             height * 0.76f + std::sin(time * 0.10f) * 18.0f),
               std::max(360.0f, width * 0.30f), 0x0d9488, 0.09f);
        radial(D2D1::Point2F(width * 0.54f, height * 0.50f + std::sin(time * 0.07f) * 18.0f),
               std::max(280.0f, width * 0.22f), accentHex, 0.055f);

        const float grid = 72.0f;
        const float drift = data_.motionEnabled ? std::fmod(time * 2.5f, grid) : 0.0f;
        presentationBrush_->SetColor(Color(0x8aa9d6, 0.026f));
        for (float x = -grid + drift; x < width + grid; x += grid) {
            presentationTarget_->DrawLine(D2D1::Point2F(x, 72.0f), D2D1::Point2F(x, height), presentationBrush_, 0.7f);
        }
        for (float y = 72.0f + drift; y < height + grid; y += grid) {
            presentationTarget_->DrawLine(D2D1::Point2F(0.0f, y), D2D1::Point2F(width, y), presentationBrush_, 0.7f);
        }

        const D2D1_RECT_F audienceBar = D2D1::RectF(24, 13, width - 24, 63);
        fill(OffsetRectF(audienceBar, 0, 6), Color(0x000000, 0.32f), 18);
        fill(audienceBar, Color(0x101827, 0.96f), 18);
        fill(D2D1::RectF(audienceBar.left, audienceBar.top, audienceBar.left + 5, audienceBar.bottom),
             Color(SelectionBlueHex(), 0.94f), 2);
        presentationBrush_->SetColor(Color(0xffffff, 0.12f));
        presentationTarget_->DrawRoundedRectangle(D2D1::RoundedRect(audienceBar, 18, 18), presentationBrush_, 1.0f);
        presentationTarget_->DrawLine(D2D1::Point2F(audienceBar.left + 22, audienceBar.top + 1),
                                      D2D1::Point2F(audienceBar.right - 22, audienceBar.top + 1), presentationBrush_, 1.0f);
        text(L"JAYCEE LOTTERY", D2D1::RectF(44, 13, 276, 63), formatBodyMedium_, Color(0xf7f8fb),
             DWRITE_TEXT_ALIGNMENT_LEADING);
        if (!data_.eventName.empty()) {
            text(data_.eventName, D2D1::RectF(width - 520, 13, width - 44, 63), formatCaption_, Color(0xc8ceda),
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        const float top = 78.0f;
        if (presentationPhase_ == PresentationPhase::Idle) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - slideshowStarted_).count();
            const float slideSeconds = std::fmod(static_cast<float>(elapsed) / 1000.0f, 6.0f);
            const int slide = static_cast<int>((elapsed / 6000) % 3);
            const float enter = EaseInOutSine(Saturate(slideSeconds / 0.85f));
            const float leave = EaseInOutSine(Saturate((6.0f - slideSeconds) / 0.85f));
            const float slideOpacity = std::min(enter, leave);
            const float slideOffset = (1.0f - enter) * 34.0f - (1.0f - leave) * 26.0f;
            auto slideRect = [&](const D2D1_RECT_F& rect) { return OffsetRectF(rect, 0.0f, slideOffset); };
            if (slide == 0) {
                text(data_.eventName.empty() ? L"WELCOME" : L"WELCOME TO",
                     slideRect(D2D1::RectF(30, top + 30, width - 30, top + 80)), formatHeading_,
                     Color(accentHex, slideOpacity));
                text(data_.eventName.empty() ? L"JAYCEE LOTTERY" : data_.eventName,
                     slideRect(D2D1::RectF(45, top + 80, width - 45, height - 145)), formatHero_,
                     Color(0xf7f8fb, slideOpacity));
                text(L"Tonight, every number can become a moment.",
                     slideRect(D2D1::RectF(40, height - 140, width - 40, height - 80)),
                     formatBody_, Color(0xa4aab6, slideOpacity));
            } else if (slide == 1) {
                text(L"NEXT PRIZE", slideRect(D2D1::RectF(40, top + 35, width - 40, top + 90)),
                     formatHeading_, Color(accentHex, slideOpacity));
                text(ActivePrize().name, slideRect(D2D1::RectF(45, top + 90, width - 45, height - 150)),
                     formatHero_, Color(0xf7f8fb, slideOpacity));
                text(std::to_wstring(data_.quantity) + L" winner" + (data_.quantity == 1 ? L"" : L"s"),
                     slideRect(D2D1::RectF(40, height - 145, width - 40, height - 85)),
                     formatHeading_, Color(0xc8ccd6, slideOpacity));
            } else {
                text(L"THE POOL IS READY", slideRect(D2D1::RectF(40, top + 35, width - 40, top + 90)),
                     formatHeading_, Color(accentHex, slideOpacity));
                text(std::to_wstring(data_.noRepeat ? RemainingCount() : PoolSize()),
                     slideRect(D2D1::RectF(40, top + 85, width - 40, height - 150)),
                     formatHero_, Color(0xf7f8fb, slideOpacity));
                text(data_.participants.empty() ? L"eligible coupon numbers" : L"eligible participants",
                     slideRect(D2D1::RectF(40, height - 150, width - 40, height - 86)),
                     formatHeading_, Color(0xa4aab6, slideOpacity));
            }
        } else if (presentationPhase_ == PresentationPhase::Countdown) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - drawStarted_).count();
            const int count = std::max(1, 3 - static_cast<int>(elapsed / 1000));
            const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(elapsed) * 0.010f);
            const D2D1_POINT_2F center = D2D1::Point2F(width * 0.5f, height * 0.48f);
            radial(center, 150.0f + pulse * 22.0f, accentHex, 0.15f + pulse * 0.05f);
            for (int ring = 0; ring < 3; ++ring) {
                const float radiusX = 76.0f + ring * 26.0f + pulse * 7.0f;
                const float radiusY = 18.0f + ring * 8.0f + pulse * 2.0f;
                presentationBrush_->SetColor(Color(ring == 0 ? accentHex : 0xffffff, 0.22f - ring * 0.045f));
                presentationTarget_->DrawEllipse(D2D1::Ellipse(center, radiusX, radiusY), presentationBrush_, 1.8f);
            }
            text(L"GET READY", D2D1::RectF(30, top + 30, width - 30, top + 85), formatHeading_, Color(accentHex));
            text(std::to_wstring(count), D2D1::RectF(30, top + 85, width - 30, height - 105), formatHero_, Color(0xffffff));
            text(ActivePrize().name, D2D1::RectF(30, height - 130, width - 30, height - 75), formatHeading_, Color(0xb5bac5));
        } else if (presentationPhase_ == PresentationPhase::Drawing) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - drawStarted_).count();
            const float angle = static_cast<float>(elapsed) * 0.0044f;
            const D2D1_POINT_2F center = D2D1::Point2F(width / 2, height / 2 - 30);
            radial(center, 190.0f, accentHex, 0.18f);
            fill(D2D1::RectF(center.x - 48, center.y - 48, center.x + 48, center.y + 48), Color(0x0b0e16, 0.88f), 48);
            fill(D2D1::RectF(center.x - 31, center.y - 32, center.x + 26, center.y + 25), Color(accentHex, 0.24f), 29);
            for (int ring = 0; ring < 6; ++ring) {
                const float phase = angle * (1.0f + ring * 0.055f) + ring * 0.76f;
                const float radiusX = 65.0f + ring * 15.0f;
                const float radiusY = 17.0f + ring * 5.0f;
                presentationBrush_->SetColor(Color(ring % 2 == 0 ? accentHex : 0xffffff, 0.20f - ring * 0.018f));
                presentationTarget_->DrawEllipse(D2D1::Ellipse(center, radiusX, radiusY), presentationBrush_, ring < 2 ? 2.2f : 1.1f);
                const D2D1_POINT_2F point = D2D1::Point2F(center.x + std::cos(phase) * radiusX,
                                                          center.y + std::sin(phase) * radiusY);
                presentationBrush_->SetColor(Color(ring % 2 == 0 ? 0xffffff : accentHex, 0.90f));
                presentationTarget_->FillEllipse(D2D1::Ellipse(point, 4.0f + ring * 0.32f, 4.0f + ring * 0.32f),
                                                 presentationBrush_);
            }
            text(L"DRAWING THE MOMENT", D2D1::RectF(30, center.y + 98, width - 30, center.y + 155),
                 formatHeading_, Color(0xf7f8fb));
        } else {
            const bool confirmed = presentationPhase_ == PresentationPhase::Summary;
            text(confirmed ? L"WINNERS CONFIRMED" : L"AND THE WINNERS ARE…",
                 D2D1::RectF(30, top + 10, width - 30, top + 65), formatHeading_, Color(accentHex));
            text(ActivePrize().name, D2D1::RectF(30, top + 58, width - 30, top + 108), formatTitle_, Color(0xf7f8fb));
            const int columns = std::clamp(static_cast<int>(pendingWinners_.size()), 1, 4);
            const int rows = (static_cast<int>(pendingWinners_.size()) + columns - 1) / columns;
            const float gap = 16.0f;
            const float areaTop = top + 130;
            const float areaBottom = height - 48;
            const float cardWidth = (width - 80 - gap * (columns - 1)) / columns;
            const float cardHeight = std::min(150.0f, (areaBottom - areaTop - gap * (rows - 1)) / std::max(1, rows));
            for (size_t index = 0; index < pendingWinners_.size(); ++index) {
                const int row = static_cast<int>(index) / columns;
                const int column = static_cast<int>(index) % columns;
                const float left = 40 + column * (cardWidth + gap);
                const float cardTop = areaTop + row * (cardHeight + gap);
                const D2D1_RECT_F card = D2D1::RectF(left, cardTop, left + cardWidth, cardTop + cardHeight);
                const auto itemStarted = revealStarted_ + std::chrono::milliseconds(static_cast<int>(index) * 95);
                const float progress = MotionProgress(itemStarted, 620.0f);
                const float entry = EaseOutBack(progress);
                const D2D1_RECT_F lifted = OffsetRectF(card, 0.0f, (1.0f - EaseOutCubic(progress)) * 78.0f);
                const D2D1_RECT_F visual = ScaleRectF(lifted, 0.80f + 0.20f * entry);
                elevatedCard(visual, 22, index == 0);
                const auto& winner = pendingWinners_[index];
                text(std::to_wstring(winner.ticket), D2D1::RectF(visual.left + 10, visual.top + 10, visual.right - 10,
                     winner.name.empty() ? visual.bottom - 10 : visual.top + 83), formatDisplay_, Color(0xffffff));
                if (!winner.name.empty()) {
                    text(winner.name, D2D1::RectF(visual.left + 12, visual.top + 80, visual.right - 12, visual.top + 112),
                         formatBodyMedium_, Color(0xf0f1f5));
                    text(winner.group, D2D1::RectF(visual.left + 12, visual.top + 112, visual.right - 12, visual.bottom - 8),
                         formatCaption_, Color(0x9299a7));
                }
            }
            if (data_.confettiEnabled) {
                const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                for (int index = 0; index < 54; ++index) {
                    const float x = std::fmod(index * 97.0f + static_cast<float>(milliseconds % 6000) * 0.035f, width) +
                                    std::sin(static_cast<float>(milliseconds) * 0.0018f + index) * 16.0f;
                    const float y = std::fmod(index * 53.0f + static_cast<float>(milliseconds % 5000) * 0.055f, height - 70) + 70;
                    const float width3d = 3.0f + static_cast<float>(index % 4);
                    const float height3d = 7.0f + static_cast<float>((index * 3) % 9);
                    fill(D2D1::RectF(x, y, x + width3d, y + height3d),
                         Color(index % 3 ? accentHex : 0xffffff, 0.66f), 2);
                }
            }
        }

        const HRESULT result = presentationTarget_->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) {
            SafeRelease(presentationBrush_);
            SafeRelease(presentationTarget_);
        }
        EndPaint(hwnd, &paint);
    }

    void ShowToast(std::wstring message) {
        toast_ = std::move(message);
        toastStarted_ = std::chrono::steady_clock::now();
        toastUntil_ = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void SetPage(Page page) {
        CommitInput();
        if (page_ == page) return;
        page_ = page;
        pageTransitionStarted_ = std::chrono::steady_clock::now();
        pageScroll_ = pageScrollTarget_ = 0.0f;
        historyScroll_ = historyScrollTarget_ = 0.0f;
        InvalidateRect(window_, nullptr, FALSE);
    }

    void AdjustUiScale(float delta) {
        data_.uiScale = std::clamp(std::round((data_.uiScale + delta) * 20.0f) / 20.0f, 0.75f, 1.35f);
        pageScroll_ = pageScrollTarget_ = 0.0f;
        SaveState();
        ShowToast(L"Interface scale · " + std::to_wstring(static_cast<int>(std::round(data_.uiScale * 100.0f))) + L"%");
        InvalidateRect(window_, nullptr, FALSE);
    }

    void StartInput(InputField field) {
        CommitInput();
        if (field == InputField::Total && !data_.participants.empty()) {
            ShowToast(L"Total coupons is controlled by the imported participant list");
            return;
        }
        SetFocus(window_);
        activeInput_ = field;
        inputBuffer_ = std::to_wstring(field == InputField::Quantity ? data_.quantity : data_.total);
        inputSelectAll_ = true;
        InvalidateRect(window_, nullptr, FALSE);
    }

    void CommitInput() {
        if (activeInput_ == InputField::None) return;
        if (!inputBuffer_.empty()) {
            try {
                int value = std::stoi(inputBuffer_);
                if (activeInput_ == InputField::Quantity) {
                    data_.quantity = std::clamp(value, 0, std::max(0, PoolSize()));
                    ActivePrize().winnerCount = data_.quantity;
                } else {
                    data_.total = std::clamp(value, 0, 999999);
                    data_.quantity = std::min(data_.quantity, data_.total);
                    ActivePrize().winnerCount = data_.quantity;
                }
                SaveState();
            } catch (...) {}
        }
        activeInput_ = InputField::None;
        inputBuffer_.clear();
        inputSelectAll_ = false;
        InvalidateRect(window_, nullptr, FALSE);
    }

    void AdjustValue(InputField field, int amount) {
        CommitInput();
        if (field == InputField::Quantity) {
            data_.quantity = std::clamp(data_.quantity + amount, 0, std::max(0, PoolSize()));
            ActivePrize().winnerCount = data_.quantity;
        } else {
            if (!data_.participants.empty()) {
                ShowToast(L"Participant import controls the pool size");
                return;
            }
            data_.total = std::clamp(data_.total + amount, 0, 999999);
            data_.quantity = std::min(data_.quantity, data_.total);
            ActivePrize().winnerCount = data_.quantity;
        }
        SaveState();
        InvalidateRect(window_, nullptr, FALSE);
    }

    void ToggleFullScreen() {
        if (!fullScreen_) {
            previousStyle_ = GetWindowLongPtrW(window_, GWL_STYLE);
            previousPlacement_.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(window_, &previousPlacement_);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            SetWindowLongPtrW(window_, GWL_STYLE, previousStyle_ & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window_, HWND_TOP, monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullScreen_ = true;
        } else {
            SetWindowLongPtrW(window_, GWL_STYLE, previousStyle_);
            SetWindowPlacement(window_, &previousPlacement_);
            SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            fullScreen_ = false;
        }
    }

    bool IsTextDialog() const {
        return dialog_ == DialogType::AddPrize || dialog_ == DialogType::EditPrize ||
               dialog_ == DialogType::EditEventName;
    }

    void BeginTextDialog(DialogType type, std::wstring initial = L"", int prizeIndex = -1) {
        dialog_ = type;
        dialogText_ = std::move(initial);
        dialogPrizeIndex_ = prizeIndex;
        SetFocus(window_);
        InvalidateRect(window_, nullptr, FALSE);
    }

    void ConfirmDialog() {
        if (dialog_ == DialogType::ResetPool || dialog_ == DialogType::OutOfNumbers) {
            data_.used.clear();
            for (auto& participant : data_.participants) participant.won = false;
            latestNumbers_.clear();
            pendingWinners_.clear();
            awaitingConfirmation_ = false;
            presentationPhase_ = PresentationPhase::Idle;
            SaveState();
            dialog_ = DialogType::None;
            ShowToast(L"No-repeat pool reset");
        } else if (dialog_ == DialogType::ClearHistory) {
            data_.history.clear();
            latestNumbers_.clear();
            pendingWinners_.clear();
            awaitingConfirmation_ = false;
            presentationPhase_ = PresentationPhase::Idle;
            historyScroll_ = historyScrollTarget_ = 0.0f;
            SaveState();
            dialog_ = DialogType::None;
            ShowToast(L"Draw history cleared");
        } else if (dialog_ == DialogType::ClearParticipants) {
            data_.participants.clear();
            data_.used.clear();
            data_.total = 0;
            data_.quantity = 0;
            ActivePrize().winnerCount = data_.quantity;
            latestNumbers_.clear();
            pendingWinners_.clear();
            awaitingConfirmation_ = false;
            SaveState();
            dialog_ = DialogType::None;
            ShowToast(L"Participant list cleared — numeric mode active");
        } else if (dialog_ == DialogType::DeletePrize) {
            if (data_.prizes.size() > 1 && dialogPrizeIndex_ >= 0 &&
                dialogPrizeIndex_ < static_cast<int>(data_.prizes.size())) {
                data_.prizes.erase(data_.prizes.begin() + dialogPrizeIndex_);
                data_.selectedPrize = std::clamp(data_.selectedPrize, 0, static_cast<int>(data_.prizes.size()) - 1);
                data_.quantity = std::clamp(ActivePrize().winnerCount, 0, std::max(0, PoolSize()));
                SaveState();
                ShowToast(L"Prize removed");
            }
            dialog_ = DialogType::None;
        } else if (IsTextDialog()) {
            if (dialogText_.empty() && dialog_ != DialogType::EditEventName) return;
            if (dialog_ == DialogType::EditEventName) {
                data_.eventName = dialogText_;
                ShowToast(dialogText_.empty() ? L"Event title removed" : L"Event title updated");
            } else if (dialog_ == DialogType::AddPrize) {
                int nextId = 1;
                for (const auto& prize : data_.prizes) nextId = std::max(nextId, prize.id + 1);
                data_.prizes.push_back({nextId, dialogText_, L"All participants", 0});
                data_.selectedPrize = static_cast<int>(data_.prizes.size()) - 1;
                data_.quantity = 0;
                ShowToast(L"Prize added");
            } else if (dialogPrizeIndex_ >= 0 && dialogPrizeIndex_ < static_cast<int>(data_.prizes.size())) {
                data_.prizes[static_cast<size_t>(dialogPrizeIndex_)].name = dialogText_;
                ShowToast(L"Prize renamed");
            }
            SaveState();
            dialog_ = DialogType::None;
            dialogText_.clear();
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_SIZE:
            if (renderTarget_) renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMinTrackSize.x = 1024;
            limits->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_MOUSEMOVE:
            UpdateMouse(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSELEAVE:
            mouseTracking_ = false;
            mouseX_ = -1000.0f;
            mouseY_ = -1000.0f;
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            HandleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL:
            HandleWheel(GET_WHEEL_DELTA_WPARAM(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_CHAR:
            HandleCharacter(static_cast<wchar_t>(wParam));
            return 0;
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR) return TRUE;
            HandleCharacter(static_cast<wchar_t>(wParam));
            return 0;
        case WM_KEYDOWN:
            if (HandleKeyDown(wParam)) return 0;
            break;
        case WM_TIMER:
            {
                const auto now = std::chrono::steady_clock::now();
                frameDeltaSeconds_ = std::clamp(std::chrono::duration<float>(now - lastAnimationTick_).count(),
                                                1.0f / 240.0f, 1.0f / 20.0f);
                lastAnimationTick_ = now;
            }
            if (data_.motionEnabled) {
                const float scrollBlend = 1.0f - std::exp(-11.5f * frameDeltaSeconds_);
                pageScroll_ += (pageScrollTarget_ - pageScroll_) * scrollBlend;
                historyScroll_ += (historyScrollTarget_ - historyScroll_) * scrollBlend;
                resultScroll_ += (resultScrollTarget_ - resultScroll_) * scrollBlend;
                participantsScroll_ += (participantsScrollTarget_ - participantsScroll_) * scrollBlend;
            } else {
                pageScroll_ = pageScrollTarget_;
                historyScroll_ = historyScrollTarget_;
                resultScroll_ = resultScrollTarget_;
                participantsScroll_ = participantsScrollTarget_;
            }
            if (drawing_) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - drawStarted_).count();
                const auto totalDelay = presentationWindow_ ? kShowCountdownMs + kDrawDelayMs : kDrawDelayMs;
                if (presentationWindow_ && elapsed >= kShowCountdownMs) {
                    presentationPhase_ = PresentationPhase::Drawing;
                }
                if (elapsed >= totalDelay) FinishDraw();
                InvalidateRect(window_, nullptr, FALSE);
            }
            if (presentationWindow_) InvalidateRect(presentationWindow_, nullptr, FALSE);
            if (!toast_.empty() && std::chrono::steady_clock::now() >= toastUntil_) {
                toast_.clear();
                InvalidateRect(window_, nullptr, FALSE);
            }
            if (data_.motionEnabled && !IsIconic(window_)) InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            if (presentationWindow_ && IsWindow(presentationWindow_)) DestroyWindow(presentationWindow_);
            SaveState();
            KillTimer(window_, kAnimationTimer);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    void UpdateMouse(int pixelX, int pixelY) {
        if (!mouseTracking_) {
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = window_;
            mouseTracking_ = TrackMouseEvent(&tracking) == TRUE;
        }
        const float dipScale = 96.0f / static_cast<float>(GetDpiForWindow(window_));
        mouseX_ = static_cast<float>(pixelX) * dipScale / data_.uiScale;
        mouseY_ = static_cast<float>(pixelY) * dipScale / data_.uiScale +
                  (dialog_ == DialogType::None ? pageScroll_ : 0.0f);
        const bool overClickable = IsOverClickable(mouseX_, mouseY_);
        SetCursor(LoadCursorW(nullptr, overClickable ? IDC_HAND : IDC_ARROW));
        InvalidateRect(window_, nullptr, FALSE);
    }

    bool IsOverClickable(float x, float y) const {
        if (dialog_ != DialogType::None) {
            return Contains(layout_.dialogCancel, x, y) || Contains(layout_.dialogConfirm, x, y) ||
                   (IsTextDialog() && Contains(layout_.dialogTextField, x, y));
        }
        const float headerY = y - pageScroll_;
        if (Contains(layout_.drawNav, x, headerY) || Contains(layout_.participantsNav, x, headerY) ||
            Contains(layout_.prizesNav, x, headerY) || Contains(layout_.showNav, x, headerY) ||
            Contains(layout_.historyNav, x, headerY) ||
            Contains(layout_.fullScreenButton, x, headerY)) return true;
        if (page_ == Page::Draw) {
            return Contains(layout_.quantityMinus, x, y) || Contains(layout_.quantityValue, x, y) ||
                   Contains(layout_.quantityPlus, x, y) || Contains(layout_.totalMinus, x, y) ||
                   Contains(layout_.totalValue, x, y) || Contains(layout_.totalPlus, x, y) ||
                   Contains(layout_.noRepeatToggle, x, y) || Contains(layout_.drawButton, x, y) ||
                   Contains(layout_.recentViewAll, x, y) || Contains(layout_.confirmWinnersButton, x, y) ||
                   Contains(layout_.redrawButton, x, y);
        }
        if (page_ == Page::Participants) {
            return Contains(layout_.importParticipantsButton, x, y) || Contains(layout_.clearParticipantsButton, x, y);
        }
        if (page_ == Page::Prizes) {
            if (Contains(layout_.addPrizeButton, x, y) || Contains(layout_.editPrizeButton, x, y) ||
                Contains(layout_.deletePrizeButton, x, y) || Contains(layout_.prizeEligibilityButton, x, y)) return true;
            for (const auto& card : layout_.prizeCards) if (Contains(card, x, y)) return true;
            return false;
        }
        if (page_ == Page::Show) {
            if (Contains(layout_.openPresentationButton, x, y) || Contains(layout_.eventNameButton, x, y) ||
                Contains(layout_.soundToggle, x, y) || Contains(layout_.confettiToggle, x, y) ||
                Contains(layout_.motionToggle, x, y) ||
                Contains(layout_.scaleMinusButton, x, y) || Contains(layout_.scalePlusButton, x, y)) return true;
            for (const auto& theme : layout_.themeButtons) if (Contains(theme, x, y)) return true;
            return false;
        }
        return Contains(layout_.resetPoolButton, x, y) || Contains(layout_.clearHistoryButton, x, y) ||
               Contains(layout_.exportHistoryButton, x, y);
    }

    void HandleClick(int pixelX, int pixelY) {
        SetFocus(window_);
        const float dipScale = 96.0f / static_cast<float>(GetDpiForWindow(window_));
        const float x = static_cast<float>(pixelX) * dipScale / data_.uiScale;
        const float viewportY = static_cast<float>(pixelY) * dipScale / data_.uiScale;
        const float y = viewportY +
                        (dialog_ == DialogType::None ? pageScroll_ : 0.0f);

        if (dialog_ != DialogType::None) {
            if (Contains(layout_.dialogCancel, x, y)) {
                dialog_ = DialogType::None;
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.dialogConfirm, x, y)) {
                ConfirmDialog();
            }
            return;
        }

        if (Contains(layout_.drawNav, x, viewportY)) { SetPage(Page::Draw); return; }
        if (Contains(layout_.participantsNav, x, viewportY)) { SetPage(Page::Participants); return; }
        if (Contains(layout_.prizesNav, x, viewportY)) { SetPage(Page::Prizes); return; }
        if (Contains(layout_.showNav, x, viewportY)) { SetPage(Page::Show); return; }
        if (Contains(layout_.historyNav, x, viewportY)) { SetPage(Page::History); return; }
        if (Contains(layout_.fullScreenButton, x, viewportY)) { ToggleFullScreen(); return; }

        if (page_ == Page::Draw) {
            if (Contains(layout_.quantityMinus, x, y)) AdjustValue(InputField::Quantity, -1);
            else if (Contains(layout_.quantityValue, x, y)) StartInput(InputField::Quantity);
            else if (Contains(layout_.quantityPlus, x, y)) AdjustValue(InputField::Quantity, 1);
            else if (Contains(layout_.totalMinus, x, y)) AdjustValue(InputField::Total, -10);
            else if (Contains(layout_.totalValue, x, y)) StartInput(InputField::Total);
            else if (Contains(layout_.totalPlus, x, y)) AdjustValue(InputField::Total, 10);
            else if (Contains(layout_.noRepeatToggle, x, y)) {
                CommitInput();
                data_.noRepeat = !data_.noRepeat;
                SaveState();
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.drawButton, x, y)) {
                if (awaitingConfirmation_) ConfirmWinners();
                else BeginDraw();
            }
            else if (Contains(layout_.confirmWinnersButton, x, y)) ConfirmWinners();
            else if (Contains(layout_.redrawButton, x, y)) RedrawCandidates();
            else if (Contains(layout_.recentViewAll, x, y)) SetPage(Page::History);
            else CommitInput();
        } else if (page_ == Page::Participants) {
            if (Contains(layout_.importParticipantsButton, x, y)) ImportParticipants();
            else if (Contains(layout_.clearParticipantsButton, x, y)) {
                if (data_.participants.empty()) ShowToast(L"No participant list is loaded");
                else { dialog_ = DialogType::ClearParticipants; InvalidateRect(window_, nullptr, FALSE); }
            }
        } else if (page_ == Page::Prizes) {
            if (Contains(layout_.addPrizeButton, x, y)) {
                BeginTextDialog(DialogType::AddPrize, L"New Prize");
            } else if (Contains(layout_.editPrizeButton, x, y)) {
                BeginTextDialog(DialogType::EditPrize, ActivePrize().name, data_.selectedPrize);
            } else if (Contains(layout_.deletePrizeButton, x, y)) {
                if (data_.prizes.size() == 1) ShowToast(L"At least one prize is required");
                else { dialog_ = DialogType::DeletePrize; dialogPrizeIndex_ = data_.selectedPrize; InvalidateRect(window_, nullptr, FALSE); }
            } else if (Contains(layout_.prizeEligibilityButton, x, y)) {
                CyclePrizeEligibility();
            } else {
                for (size_t index = 0; index < std::size(layout_.prizeCards); ++index) {
                    if (!Contains(layout_.prizeCards[index], x, y) || index >= data_.prizes.size()) continue;
                    data_.selectedPrize = static_cast<int>(index);
                    data_.quantity = std::clamp(data_.prizes[index].winnerCount, 0, std::max(0, PoolSize()));
                    SaveState();
                    ShowToast(L"Active prize · " + data_.prizes[index].name);
                    break;
                }
            }
        } else if (page_ == Page::Show) {
            if (Contains(layout_.openPresentationButton, x, y)) {
                if (presentationWindow_) DestroyWindow(presentationWindow_);
                else OpenPresentation();
            } else if (Contains(layout_.eventNameButton, x, y)) {
                BeginTextDialog(DialogType::EditEventName, data_.eventName);
            } else if (Contains(layout_.soundToggle, x, y)) {
                data_.soundEnabled = !data_.soundEnabled;
                SaveState();
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.confettiToggle, x, y)) {
                data_.confettiEnabled = !data_.confettiEnabled;
                SaveState();
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.motionToggle, x, y)) {
                data_.motionEnabled = !data_.motionEnabled;
                pageTransitionStarted_ = std::chrono::steady_clock::now();
                SaveState();
                ShowToast(data_.motionEnabled ? L"Liquid motion enabled" : L"Liquid motion paused");
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.scaleMinusButton, x, y)) {
                AdjustUiScale(-0.10f);
            } else if (Contains(layout_.scalePlusButton, x, y)) {
                AdjustUiScale(0.10f);
            } else {
                for (int index = 0; index < 4; ++index) {
                    if (!Contains(layout_.themeButtons[index], x, y)) continue;
                    data_.themeIndex = index;
                    SaveState();
                    InvalidateRect(window_, nullptr, FALSE);
                    if (presentationWindow_) InvalidateRect(presentationWindow_, nullptr, FALSE);
                    break;
                }
            }
        } else {
            if (Contains(layout_.resetPoolButton, x, y)) {
                dialog_ = DialogType::ResetPool;
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.clearHistoryButton, x, y)) {
                dialog_ = DialogType::ClearHistory;
                InvalidateRect(window_, nullptr, FALSE);
            } else if (Contains(layout_.exportHistoryButton, x, y)) {
                ExportHistory();
            }
        }
    }

    void HandleWheel(short delta, int screenX, int screenY) {
        POINT point{screenX, screenY};
        ScreenToClient(window_, &point);
        const float dipScale = 96.0f / static_cast<float>(GetDpiForWindow(window_));
        const float x = static_cast<float>(point.x) * dipScale / data_.uiScale;
        const float y = static_cast<float>(point.y) * dipScale / data_.uiScale + pageScroll_;
        const float movement = delta > 0 ? -80.0f : 80.0f;
        bool handled = false;
        if (page_ == Page::History && Contains(layout_.historyViewport, x, y)) {
            const float maximum = std::max(0.0f, historyContentHeight_ - Height(layout_.historyViewport));
            if (maximum > 0.0f) { historyScrollTarget_ = std::clamp(historyScrollTarget_ + movement, 0.0f, maximum); handled = true; }
        } else if (page_ == Page::Draw && Contains(layout_.resultsCard, x, y)) {
            const float maximum = std::max(0.0f, resultContentHeight_ - (Height(layout_.resultsCard) - 92.0f));
            if (maximum > 0.0f) { resultScrollTarget_ = std::clamp(resultScrollTarget_ + movement, 0.0f, maximum); handled = true; }
        } else if (page_ == Page::Participants && Contains(layout_.participantsViewport, x, y)) {
            const float maximum = std::max(0.0f, participantsContentHeight_ - Height(layout_.participantsViewport));
            if (maximum > 0.0f) { participantsScrollTarget_ = std::clamp(participantsScrollTarget_ + movement, 0.0f, maximum); handled = true; }
        }
        if (!handled) pageScrollTarget_ = std::clamp(pageScrollTarget_ + movement, 0.0f, MaxPageScroll());
        InvalidateRect(window_, nullptr, FALSE);
    }

    void HandleCharacter(wchar_t character) {
        if (IsTextDialog()) {
            if (character == L'\b') {
                if (!dialogText_.empty()) dialogText_.pop_back();
            } else if (character == L'\r') {
                ConfirmDialog();
            } else if (character >= 32 && dialogText_.size() < 64) {
                dialogText_.push_back(character);
            }
            InvalidateRect(window_, nullptr, FALSE);
            return;
        }
        if (activeInput_ == InputField::None) return;
        if (character >= L'0' && character <= L'9') {
            if (inputSelectAll_) {
                inputBuffer_.clear();
                inputSelectAll_ = false;
            }
            if (inputBuffer_.size() < 6) inputBuffer_.push_back(character);
        } else if (character == L'\b') {
            if (inputSelectAll_) inputBuffer_.clear();
            else if (!inputBuffer_.empty()) inputBuffer_.pop_back();
            inputSelectAll_ = false;
        } else if (character == L'\r') {
            CommitInput();
        }
        InvalidateRect(window_, nullptr, FALSE);
    }

    bool HandleKeyDown(WPARAM key) {
        if (key == VK_F11) { ToggleFullScreen(); return true; }
        if (key == VK_ESCAPE) {
            if (dialog_ != DialogType::None) dialog_ = DialogType::None;
            else if (activeInput_ != InputField::None) { activeInput_ = InputField::None; inputBuffer_.clear(); }
            else if (fullScreen_) ToggleFullScreen();
            InvalidateRect(window_, nullptr, FALSE);
            return true;
        }
        if (IsTextDialog() && key == VK_RETURN) {
            ConfirmDialog();
            return true;
        }
        if (activeInput_ != InputField::None && key == VK_RETURN) {
            CommitInput();
            return true;
        }
        if (activeInput_ != InputField::None) return false;
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (control && key == 'D') { SetPage(Page::Draw); return true; }
        if (control && key == 'I') { SetPage(Page::Participants); return true; }
        if (control && key == 'P') { SetPage(Page::Prizes); return true; }
        if (control && key == 'S') { SetPage(Page::Show); return true; }
        if (control && key == 'H') { SetPage(Page::History); return true; }
        if (control && (key == VK_OEM_PLUS || key == VK_ADD)) { AdjustUiScale(0.10f); return true; }
        if (control && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) { AdjustUiScale(-0.10f); return true; }
        if (control && key == '0') {
            data_.uiScale = 1.0f;
            pageScroll_ = pageScrollTarget_ = 0.0f;
            SaveState();
            ShowToast(L"Interface scale reset · 100%");
            return true;
        }
        if (page_ == Page::Draw && key == VK_SPACE) {
            if (awaitingConfirmation_) ConfirmWinners();
            else BeginDraw();
            return true;
        }
        if (page_ == Page::Draw && key == 'N') {
            data_.noRepeat = !data_.noRepeat;
            SaveState();
            InvalidateRect(window_, nullptr, FALSE);
            return true;
        }
        return false;
    }

    void SetBrush(D2D1_COLOR_F color) { brush_->SetColor(color); }

    void FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
        SetBrush(color);
        renderTarget_->FillRectangle(rect, brush_);
    }

    void FillRounded(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color) {
        SetBrush(color);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_);
    }

    void StrokeRounded(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width = 1.0f) {
        SetBrush(color);
        renderTarget_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_, width);
    }

    void Text(const std::wstring& value, const D2D1_RECT_F& rect, IDWriteTextFormat* format,
              D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT vertical = DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
        format->SetTextAlignment(alignment);
        format->SetParagraphAlignment(vertical);
        SetBrush(color);
        renderTarget_->DrawTextW(value.c_str(), static_cast<UINT32>(value.size()), format,
                                 rect, brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    float AnimationSeconds() const {
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - appStarted_).count();
    }

    float MotionProgress(const std::chrono::steady_clock::time_point& started, float durationMs) const {
        if (!data_.motionEnabled) return 1.0f;
        const float elapsed = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
        return Saturate(elapsed / durationMs);
    }

    std::uint64_t RectAnimationKey(const D2D1_RECT_F& rect, std::uint64_t salt = 0) const {
        auto quantize = [](float value) -> std::uint64_t {
            return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(value * 2.0f)) + 131072);
        };
        std::uint64_t hash = 1469598103934665603ULL ^ salt;
        for (const std::uint64_t value : {quantize(rect.left), quantize(rect.top),
                                          quantize(rect.right), quantize(rect.bottom)}) {
            hash ^= value;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    float SmoothedHover(const D2D1_RECT_F& rect, bool hovered, std::uint64_t salt = 0) {
        if (!data_.motionEnabled) return hovered ? 1.0f : 0.0f;
        const std::uint64_t key = RectAnimationKey(rect, salt);
        if (hoverAnimations_.size() > 512 && !hoverAnimations_.contains(key)) hoverAnimations_.clear();
        float& amount = hoverAnimations_[key];
        const float response = hovered ? 13.0f : 9.0f;
        const float blend = 1.0f - std::exp(-response * frameDeltaSeconds_);
        amount += ((hovered ? 1.0f : 0.0f) - amount) * blend;
        if (amount < 0.001f) amount = 0.0f;
        else if (amount > 0.999f) amount = 1.0f;
        return amount;
    }

    void DrawElevation(const D2D1_RECT_F& rect, float radius, float elevation = 10.0f, float opacity = 0.28f) {
        const float depth = data_.motionEnabled ? elevation : elevation * 0.55f;
        FillRounded(OffsetRectF(ScaleRectF(rect, 1.010f), 0.0f, depth * 0.78f), radius + 5.0f,
                    Color(0x01040b, opacity * 0.34f));
        FillRounded(OffsetRectF(rect, 0.0f, depth * 0.30f), radius + 1.0f,
                    Color(0x000000, opacity * 0.55f));
    }

    void DrawGlassHighlight(const D2D1_RECT_F& rect, float radius, float intensity = 1.0f) {
        SetBrush(Color(0xffffff, 0.12f * intensity));
        renderTarget_->DrawLine(D2D1::Point2F(rect.left + radius, rect.top + 1.0f),
                                D2D1::Point2F(rect.right - radius, rect.top + 1.0f), brush_, 1.0f);
        SetBrush(Color(0xffffff, 0.035f * intensity));
        renderTarget_->DrawLine(D2D1::Point2F(rect.left + 1.0f, rect.top + radius),
                                D2D1::Point2F(rect.left + 1.0f, rect.bottom - radius), brush_, 0.8f);
    }

    void DrawSelectionBlock(const D2D1_RECT_F& rect, float radius, float amount = 1.0f) {
        const float selected = Saturate(amount);
        if (selected <= 0.002f) return;
        DrawElevation(rect, radius, 8.0f + 4.0f * selected, 0.32f * selected);
        FillRounded(rect, radius, Color(SelectionBlueHex(), 0.80f * selected));
        FillRounded(D2D1::RectF(rect.left + 1.0f, rect.top + 1.0f,
                                rect.right - 1.0f, rect.bottom - 1.0f),
                    std::max(1.0f, radius - 1.0f), Color(0x0d3f99, 0.30f * selected));
        SetBrush(Color(0xa8c9ff, 0.72f * selected));
        renderTarget_->DrawLine(D2D1::Point2F(rect.left + radius, rect.top + 1.0f),
                                D2D1::Point2F(rect.right - radius, rect.top + 1.0f), brush_, 1.0f);
        StrokeRounded(rect, radius, Color(kSelectionBlueLight, 0.72f * selected), 1.0f);
    }

    void DrawLiquidGlass(const D2D1_RECT_F& rect, float radius, bool interactive = false,
                         bool thick = false) {
        const bool hovered = interactive && dialog_ == DialogType::None && Contains(rect, mouseX_, mouseY_);
        const float hover = interactive ? SmoothedHover(rect, hovered, 0x4c4951554944474cULL) : 0.0f;
        const D2D1_RECT_F visual = rect;
        const float baseElevation = thick ? 9.0f : 6.0f;
        const float baseOpacity = thick ? 0.30f : 0.20f;
        DrawElevation(visual, radius, baseElevation + 3.0f * hover,
                      baseOpacity + 0.08f * hover);

        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1LinearGradientBrush* glass = nullptr;
        const float density = thick ? 1.0f : 0.82f;
        const D2D1_GRADIENT_STOP glassStops[] = {
            {0.0f, Color(0x1b2435, (0.94f + 0.04f * hover) * density)},
            {0.48f, Color(0x111827, 0.94f * density)},
            {1.0f, Color(0x090e19, thick ? 0.98f : 0.94f)}
        };
        if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(glassStops, 3, &stops))) {
            renderTarget_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(visual.left, visual.top),
                                                     D2D1::Point2F(visual.right, visual.bottom)),
                stops, &glass);
        }
        if (glass) renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(visual, radius, radius), glass);
        SafeRelease(glass);
        SafeRelease(stops);

        const float baseHighlight = thick ? 0.82f : 0.58f;
        DrawGlassHighlight(visual, radius, baseHighlight + 0.34f * hover);
        StrokeRounded(visual, radius,
                      MixColor(Color(0xffffff, 0.085f), Color(SelectionBlueHex(), 0.34f), hover),
                      1.0f);
    }

    void DrawRadialGlow(D2D1_POINT_2F center, float radius, unsigned color, float alpha) {
        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1RadialGradientBrush* gradient = nullptr;
        const D2D1_GRADIENT_STOP gradientStops[] = {
            {0.0f, Color(color, alpha)}, {0.46f, Color(color, alpha * 0.28f)},
            {1.0f, Color(color, 0.0f)}
        };
        if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(gradientStops, 3, &stops))) {
            renderTarget_->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(), radius, radius),
                stops, &gradient);
        }
        if (gradient) renderTarget_->FillRectangle(
            D2D1::RectF(center.x - radius, center.y - radius, center.x + radius, center.y + radius), gradient);
        SafeRelease(gradient);
        SafeRelease(stops);
    }

    void Paint() {
        PAINTSTRUCT paint{};
        BeginPaint(window_, &paint);
        if (FAILED(CreateDeviceResources())) {
            EndPaint(window_, &paint);
            return;
        }

        renderTarget_->BeginDraw();
        renderTarget_->Clear(Color(0x05070d));

        pageScrollTarget_ = std::clamp(pageScrollTarget_, 0.0f, MaxPageScroll());
        pageScroll_ = std::clamp(pageScroll_, 0.0f, MaxPageScroll());
        renderTarget_->SetTransform(D2D1::Matrix3x2F(
            data_.uiScale, 0.0f, 0.0f, data_.uiScale, 0.0f, 0.0f));

        DrawBackground();
        const float pageEase = EaseOutCubic(MotionProgress(pageTransitionStarted_, 440.0f));
        const float pageOffset = (1.0f - pageEase) * 20.0f;
        renderTarget_->SetTransform(D2D1::Matrix3x2F(
            data_.uiScale, 0.0f, 0.0f, data_.uiScale, pageOffset * data_.uiScale,
            -pageScroll_ * data_.uiScale));
        if (page_ == Page::Draw) DrawDrawPage();
        else if (page_ == Page::Participants) DrawParticipantsPage();
        else if (page_ == Page::Prizes) DrawPrizesPage();
        else if (page_ == Page::Show) DrawShowPage();
        else DrawHistoryPage();
        renderTarget_->SetTransform(D2D1::Matrix3x2F(
            data_.uiScale, 0.0f, 0.0f, data_.uiScale, 0.0f, 0.0f));
        const float contentMouseY = mouseY_;
        mouseY_ -= pageScroll_;
        DrawHeader();
        mouseY_ = contentMouseY;
        renderTarget_->SetTransform(D2D1::Matrix3x2F(
            data_.uiScale, 0.0f, 0.0f, data_.uiScale, 0.0f, 0.0f));
        DrawPageScrollIndicator();
        if (dialog_ != DialogType::None) DrawDialog();
        else animatedDialog_ = DialogType::None;
        DrawToast();

        const HRESULT result = renderTarget_->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
        EndPaint(window_, &paint);
    }

    D2D1_SIZE_F ViewportLogicalSize() const {
        const auto pixels = renderTarget_->GetPixelSize();
        const float dpi = static_cast<float>(GetDpiForWindow(window_));
        return D2D1::SizeF(pixels.width * 96.0f / dpi / data_.uiScale,
                           pixels.height * 96.0f / dpi / data_.uiScale);
    }

    D2D1_SIZE_F CanvasSize() const {
        const auto viewport = ViewportLogicalSize();
        return D2D1::SizeF(viewport.width, std::max(kMinimumCanvasHeight, viewport.height));
    }

    float MaxPageScroll() const {
        if (!renderTarget_) return 0.0f;
        return std::max(0.0f, CanvasSize().height - ViewportLogicalSize().height);
    }

    void DrawPageScrollIndicator() {
        const float maximum = MaxPageScroll();
        if (maximum <= 0.5f) return;
        const auto viewport = ViewportLogicalSize();
        const float top = 84.0f;
        const float bottom = viewport.height - 14.0f;
        const float trackHeight = std::max(40.0f, bottom - top);
        const float thumbHeight = std::max(46.0f, trackHeight * viewport.height / CanvasSize().height);
        const float travel = std::max(0.0f, trackHeight - thumbHeight);
        const float thumbTop = top + travel * (pageScroll_ / maximum);
        FillRounded(D2D1::RectF(viewport.width - 8.0f, top, viewport.width - 4.0f, bottom), 2,
                    Color(0xffffff, 0.07f));
        FillRounded(D2D1::RectF(viewport.width - 8.0f, thumbTop, viewport.width - 4.0f,
                                thumbTop + thumbHeight), 2, Color(SelectionBlueHex(), 0.86f));
    }

    void DrawBackground() {
        const auto size = CanvasSize();
        const float time = data_.motionEnabled ? AnimationSeconds() : 0.0f;

        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1LinearGradientBrush* atmosphere = nullptr;
        const D2D1_GRADIENT_STOP atmosphereStops[] = {
            {0.0f, Color(0x05070d)},
            {0.48f, Color(0x080d18)},
            {0.78f, Color(0x0b1220)},
            {1.0f, Color(0x05070d)}
        };
        if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(atmosphereStops, 4, &stops))) {
            renderTarget_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(size.width, size.height)),
                stops, &atmosphere);
        }
        if (atmosphere) renderTarget_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), atmosphere);
        SafeRelease(atmosphere);
        SafeRelease(stops);

        DrawRadialGlow(D2D1::Point2F(size.width * 0.82f + std::sin(time * 0.10f) * 24.0f,
                                    size.height * 0.12f + std::cos(time * 0.09f) * 16.0f),
                       std::max(440.0f, size.width * 0.35f), SelectionBlueHex(), 0.15f);
        DrawRadialGlow(D2D1::Point2F(size.width * 0.16f + std::cos(time * 0.08f) * 18.0f,
                                    size.height * 0.82f + std::sin(time * 0.10f) * 18.0f),
                       std::max(380.0f, size.width * 0.29f), 0x0d9488, 0.08f);
        DrawRadialGlow(D2D1::Point2F(size.width * 0.55f,
                                    size.height * 0.48f + std::sin(time * 0.07f) * 20.0f),
                       std::max(300.0f, size.width * 0.23f), AccentHex(), 0.055f);

        const float grid = 64.0f;
        const float drift = data_.motionEnabled ? std::fmod(time * 3.0f, grid) : 0.0f;
        SetBrush(Color(0x8aa9d6, 0.028f));
        for (float x = -grid + drift; x < size.width + grid; x += grid) {
            renderTarget_->DrawLine(D2D1::Point2F(x, 86.0f), D2D1::Point2F(x, size.height), brush_, 0.7f);
        }
        for (float y = 86.0f + drift; y < size.height + grid; y += grid) {
            renderTarget_->DrawLine(D2D1::Point2F(0.0f, y), D2D1::Point2F(size.width, y), brush_, 0.7f);
        }

        for (int index = 0; index < 8; ++index) {
            const float phase = time * (0.10f + index * 0.005f) + index * 1.47f;
            const float x = size.width * (0.08f + index * 0.12f) + std::sin(phase) * 12.0f;
            const float y = 118.0f + std::fmod(index * 93.0f + std::cos(phase) * 14.0f,
                                               std::max(190.0f, size.height - 150.0f));
            const float radius = 1.2f + static_cast<float>(index % 3);
            SetBrush(Color(index % 2 == 0 ? SelectionBlueHex() : 0xffffff, 0.11f));
            renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush_);
        }
    }

    float ContentMargin(float width) const { return width >= 1500.0f ? 54.0f : 30.0f; }

    void DrawHeader() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        const D2D1_RECT_F glassBar = D2D1::RectF(margin - 8.0f, 11.0f, size.width - margin + 8.0f, 69.0f);
        DrawLiquidGlass(glassBar, 19.0f, false, true);
        FillRounded(D2D1::RectF(glassBar.left + 1.0f, glassBar.top + 1.0f,
                                glassBar.left + 5.0f, glassBar.bottom - 1.0f),
                    2.0f, Color(SelectionBlueHex(), 0.92f));

        const D2D1_RECT_F logo = D2D1::RectF(margin + 4, 22, margin + 38, 56);
        DrawSelectionBlock(logo, 9.0f);
        Text(L"J", logo, formatHeading_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"JAYCEE LOTTERY", D2D1::RectF(margin + 51, 21, margin + 194, 55), formatBodyMedium_,
             Color(0xf7f8fb), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        layout_.drawNav = D2D1::RectF(margin + 208, 20, margin + 282, 60);
        layout_.participantsNav = D2D1::RectF(margin + 288, 20, margin + 400, 60);
        layout_.prizesNav = D2D1::RectF(margin + 406, 20, margin + 492, 60);
        layout_.showNav = D2D1::RectF(margin + 498, 20, margin + 574, 60);
        layout_.historyNav = D2D1::RectF(margin + 580, 20, margin + 672, 60);
        DrawNavItem(L"Draw", layout_.drawNav, page_ == Page::Draw);
        DrawNavItem(L"Participants", layout_.participantsNav, page_ == Page::Participants);
        DrawNavItem(L"Prizes", layout_.prizesNav, page_ == Page::Prizes);
        DrawNavItem(L"Show", layout_.showNav, page_ == Page::Show);
        DrawNavItem(L"History", layout_.historyNav, page_ == Page::History);

        layout_.fullScreenButton = D2D1::RectF(size.width - margin - 46, 20, size.width - margin - 2, 60);
        DrawLiquidGlass(layout_.fullScreenButton, 12, true);
        Text(L"↗", layout_.fullScreenButton, formatHeading_, Color(0xe8eaf2),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const D2D1_RECT_F status = D2D1::RectF(size.width - margin - 174, 23, size.width - margin - 56, 57);
        FillRounded(status, 11.0f, Color(0x0b1220, 0.94f));
        StrokeRounded(status, 11.0f, Color(0xffffff, 0.08f));
        SetBrush(Color(presentationWindow_ ? 0x65e09c : 0x7f8796));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(status.left + 16, 36), 3.5f, 3.5f), brush_);
        Text(presentationWindow_ ? L"AUDIENCE LIVE" : L"READY · LOCAL",
             D2D1::RectF(status.left + 26, status.top, status.right - 8, status.bottom),
             formatCaption_, Color(0xb9beca), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawNavItem(const std::wstring& label, const D2D1_RECT_F& rect, bool active) {
        const bool hovered = Contains(rect, mouseX_, mouseY_);
        const float hover = SmoothedHover(rect, hovered, 0x4e41564947415445ULL);
        const float selected = SmoothedHover(rect, active, 0x53454c4543544e56ULL);
        const D2D1_RECT_F visual = OffsetRectF(rect, 0.0f, -hover * 0.8f);
        if (selected > 0.002f) DrawSelectionBlock(visual, 11.0f, selected);
        if (!active && hover > 0.002f) {
            FillRounded(visual, 11.0f, Color(0xffffff, 0.07f * hover));
            StrokeRounded(visual, 11.0f, Color(0xffffff, 0.10f * hover));
        }
        Text(label, visual, formatBodyMedium_, MixColor(Color(0x8791a4), Color(0xffffff), std::max(hover, selected)),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawDrawPage() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        const float contentWidth = size.width - 2.0f * margin;

        Text(L"LIVE DRAW", D2D1::RectF(margin, 91, margin + 180, 112),
             formatCaption_, Color(SelectionBlueHex()));
        Text(L"Draw studio.", D2D1::RectF(margin, 113, margin + 360, 151),
             formatTitle_, Color(0xf7f9fc));
        const D2D1_RECT_F activePrize = D2D1::RectF(margin + 205, 116, size.width - margin, 148);
        FillRounded(activePrize, 10.0f, Color(SelectionBlueHex(), 0.11f));
        StrokeRounded(activePrize, 10.0f, Color(SelectionBlueHex(), 0.25f));
        Text(L"ACTIVE PRIZE  ·  " + ActivePrize().name, D2D1::RectF(activePrize.left + 12, activePrize.top,
             activePrize.right - 12, activePrize.bottom), formatCaption_, Color(kSelectionBlueLight),
             DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(data_.participants.empty()
                 ? L"Start from zero. Set the winner count and coupon pool, then launch a secure draw."
                 : std::to_wstring(data_.participants.size()) + L" participants loaded · Names are ready for the audience display.",
             D2D1::RectF(margin, 151, size.width - margin, 177), formatBody_, Color(0x8b96a9));

        const D2D1_RECT_F controlCard = D2D1::RectF(margin, 188, size.width - margin, 330);
        DrawSurface(controlCard, 20);

        const float innerLeft = controlCard.left + 22.0f;
        const float innerRight = controlCard.right - 22.0f;
        const float available = innerRight - innerLeft;
        const float stepWidth = std::clamp(available * 0.185f, 172.0f, 245.0f);
        const float toggleWidth = std::clamp(available * 0.22f, 205.0f, 275.0f);
        const float gap = 14.0f;
        const float buttonWidth = std::clamp(available * 0.195f, 205.0f, 250.0f);

        D2D1_RECT_F quantity = D2D1::RectF(innerLeft, 208, innerLeft + stepWidth, 310);
        D2D1_RECT_F total = D2D1::RectF(quantity.right + gap, 208, quantity.right + gap + stepWidth, 310);
        D2D1_RECT_F toggle = D2D1::RectF(total.right + gap, 208, total.right + gap + toggleWidth, 310);
        layout_.drawButton = D2D1::RectF(innerRight - buttonWidth, 208, innerRight, 310);

        DrawStepper(quantity, L"WINNERS", data_.quantity, InputField::Quantity,
                    layout_.quantityMinus, layout_.quantityValue, layout_.quantityPlus);
        DrawStepper(total, data_.participants.empty() ? L"TOTAL COUPONS" : L"PARTICIPANTS", data_.total, InputField::Total,
                    layout_.totalMinus, layout_.totalValue, layout_.totalPlus);
        DrawNoRepeat(toggle);
        DrawPrimaryButton(layout_.drawButton);

        const float mainTop = 350.0f;
        const float bottom = size.height - 28.0f;
        const float rightWidth = std::clamp(contentWidth * 0.30f, 330.0f, 450.0f);
        const float mainGap = 18.0f;
        layout_.resultsCard = D2D1::RectF(margin, mainTop, size.width - margin - rightWidth - mainGap, bottom);
        layout_.recentCard = D2D1::RectF(layout_.resultsCard.right + mainGap, mainTop,
                                        size.width - margin, mainTop + (bottom - mainTop) * 0.68f);
        layout_.poolCard = D2D1::RectF(layout_.recentCard.left, layout_.recentCard.bottom + mainGap,
                                      size.width - margin, bottom);
        DrawResultsCard();
        DrawRecentCard();
        DrawPoolCard();
    }

    void DrawSurface(const D2D1_RECT_F& rect, float radius) {
        DrawLiquidGlass(rect, radius, true, true);
    }

    void DrawStepper(const D2D1_RECT_F& rect, const std::wstring& label, int value, InputField field,
                     D2D1_RECT_F& minus, D2D1_RECT_F& valueRect, D2D1_RECT_F& plus) {
        DrawLiquidGlass(rect, 17, true);
        const float selected = SmoothedHover(rect, activeInput_ == field, 0x494e50555453454cULL);
        if (selected > 0.002f) StrokeRounded(rect, 15, Color(SelectionBlueHex(), 0.92f * selected), 1.7f);
        Text(label, D2D1::RectF(rect.left + 14, rect.top + 9, rect.right - 14, rect.top + 30),
             formatCaption_, selected > 0.5f ? Color(kSelectionBlueLight) : Color(0x7f8a9e));
        minus = D2D1::RectF(rect.left + 10, rect.top + 43, rect.left + 46, rect.bottom - 11);
        plus = D2D1::RectF(rect.right - 46, rect.top + 43, rect.right - 10, rect.bottom - 11);
        valueRect = D2D1::RectF(minus.right + 3, rect.top + 38, plus.left - 3, rect.bottom - 8);
        DrawSmallButton(minus, L"−");
        DrawSmallButton(plus, L"+");
        const std::wstring shown = activeInput_ == field ? inputBuffer_ : std::to_wstring(value);
        if (selected > 0.002f) FillRounded(valueRect, 10.0f, Color(SelectionBlueHex(), 0.11f * selected));
        Text(shown.empty() ? L"|" : shown, valueRect, formatHeading_, Color(0xf5f6fa),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawSmallButton(const D2D1_RECT_F& rect, const std::wstring& label) {
        const bool hovered = Contains(rect, mouseX_, mouseY_);
        const float hover = SmoothedHover(rect, hovered, 0x534d414c4c42544eULL);
        const D2D1_RECT_F visual = rect;
        DrawLiquidGlass(visual, 11, true);
        Text(label, visual, formatHeading_, MixColor(Color(0xcbd0dc), Color(0xffffff), hover), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawNoRepeat(const D2D1_RECT_F& rect) {
        DrawLiquidGlass(rect, 17, true);
        Text(L"DRAW MODE", D2D1::RectF(rect.left + 14, rect.top + 9, rect.right - 14, rect.top + 30),
             formatCaption_, Color(0x7f8695));
        Text(L"No repeat pool", D2D1::RectF(rect.left + 14, rect.top + 37, rect.right - 76, rect.top + 62),
             formatBodyMedium_, Color(0xf0f1f5));
        Text(data_.noRepeat ? L"Across every draw" : L"Numbers may return",
             D2D1::RectF(rect.left + 14, rect.top + 63, rect.right - 76, rect.bottom - 8),
             formatCaption_, Color(0x7f8695));
        layout_.noRepeatToggle = D2D1::RectF(rect.right - 62, rect.top + 43, rect.right - 16, rect.top + 69);
        const float toggleAmount = SmoothedHover(layout_.noRepeatToggle, data_.noRepeat, 0x4e4f524550454154ULL);
        FillRounded(layout_.noRepeatToggle, 13,
                    MixColor(Color(0x2a3342, 0.92f), Color(SelectionBlueHex(), 0.94f), toggleAmount));
        DrawGlassHighlight(layout_.noRepeatToggle, 13, 1.0f);
        StrokeRounded(layout_.noRepeatToggle, 13, Color(0xffffff, 0.28f));
        const float knobX = layout_.noRepeatToggle.left + 13 +
                            (Width(layout_.noRepeatToggle) - 26) * toggleAmount;
        SetBrush(Color(0xffffff));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, layout_.noRepeatToggle.top + 13), 9, 9), brush_);
    }

    void DrawPrimaryButton(const D2D1_RECT_F& rect) {
        const bool hovered = Contains(rect, mouseX_, mouseY_);
        const float hover = SmoothedHover(rect, hovered, 0x5052494d415259ULL);
        const bool configured = awaitingConfirmation_ || (data_.quantity > 0 && PoolSize() > 0);
        const float enabledAmount = configured || drawing_ ? 1.0f : 0.0f;
        const D2D1_RECT_F visual = OffsetRectF(rect, 0.0f, -1.5f * hover * enabledAmount);
        DrawElevation(visual, 18, 9.0f + 4.0f * hover * enabledAmount, 0.28f + 0.08f * hover);
        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1LinearGradientBrush* gradient = nullptr;
        const D2D1_GRADIENT_STOP gradientStops[] = {
            {0.0f, Color(configured && !drawing_ ? 0x5d9cff : 0x303949, 0.98f)},
            {0.52f, Color(configured && !drawing_ ? SelectionBlueHex() : 0x252d3a, 0.98f)},
            {1.0f, Color(configured && !drawing_ ? 0x1755c8 : 0x1b2230, 0.98f)}
        };
        renderTarget_->CreateGradientStopCollection(gradientStops, 3, &stops);
        if (stops) renderTarget_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(visual.left, visual.top), D2D1::Point2F(visual.right, visual.bottom)),
            stops, &gradient);
        if (gradient) renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(visual, 18, 18), gradient);
        DrawGlassHighlight(visual, 18, 0.85f + 0.35f * hover * enabledAmount);
        StrokeRounded(visual, 18, Color(configured ? 0x9bc1ff : 0xffffff, 0.22f + 0.14f * hover), 1.0f);
        const std::wstring label = drawing_ ? L"DRAWING…" : (awaitingConfirmation_ ? L"CONFIRM WINNERS"
                                                                                     : (configured ? L"DRAW WINNERS" : L"SETUP REQUIRED"));
        Text(label, D2D1::RectF(visual.left + 12, visual.top + 24, visual.right - 12, visual.top + 55),
             formatBodyMedium_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(drawing_ ? (presentationWindow_ ? L"Audience countdown live" : L"Finding the moment")
                      : (awaitingConfirmation_ ? L"Save to history" : (configured ? L"Space to launch" : L"Enter values above 0")),
             D2D1::RectF(visual.left + 12, visual.top + 55, visual.right - 12, visual.bottom - 12),
             formatCaption_, Color(0xffffff, 0.72f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        SafeRelease(gradient);
        SafeRelease(stops);
    }

    void DrawResultsCard() {
        DrawSurface(layout_.resultsCard, 20);
        Text(data_.participants.empty() ? L"WINNING NUMBERS" : L"WINNER CANDIDATES",
             D2D1::RectF(layout_.resultsCard.left + 22, layout_.resultsCard.top + 18,
                         layout_.resultsCard.right - 310, layout_.resultsCard.top + 42),
             formatCaption_, Color(0x7f8695));
        const std::wstring badge = std::to_wstring(latestNumbers_.size()) + L" SELECTED";
        const D2D1_RECT_F badgeRect = D2D1::RectF(layout_.resultsCard.left + 156, layout_.resultsCard.top + 15,
                                                  layout_.resultsCard.left + 260, layout_.resultsCard.top + 43);
        FillRounded(badgeRect, 10, Color(SelectionBlueHex(), 0.16f));
        StrokeRounded(badgeRect, 10, Color(SelectionBlueHex(), 0.32f));
        Text(badge, badgeRect, formatCaption_, Color(kSelectionBlueLight), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        if (awaitingConfirmation_) {
            layout_.redrawButton = D2D1::RectF(layout_.resultsCard.right - 246, layout_.resultsCard.top + 11,
                                               layout_.resultsCard.right - 142, layout_.resultsCard.top + 48);
            layout_.confirmWinnersButton = D2D1::RectF(layout_.resultsCard.right - 132, layout_.resultsCard.top + 11,
                                                       layout_.resultsCard.right - 18, layout_.resultsCard.top + 48);
            DrawSecondaryButton(layout_.redrawButton, L"REDRAW");
            const float confirmHover = SmoothedHover(layout_.confirmWinnersButton,
                                                      Contains(layout_.confirmWinnersButton, mouseX_, mouseY_),
                                                      0x434f4e4649524d42ULL);
            DrawSelectionBlock(OffsetRectF(layout_.confirmWinnersButton, 0.0f, -confirmHover), 11, 0.84f + 0.16f * confirmHover);
            Text(L"CONFIRM", layout_.confirmWinnersButton, formatCaption_, Color(0xffffff),
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            layout_.redrawButton = {};
            layout_.confirmWinnersButton = {};
        }
        FillRect(D2D1::RectF(layout_.resultsCard.left + 20, layout_.resultsCard.top + 58,
                            layout_.resultsCard.right - 20, layout_.resultsCard.top + 59), Color(0xffffff, 0.07f));

        const D2D1_RECT_F viewport = D2D1::RectF(layout_.resultsCard.left + 20, layout_.resultsCard.top + 72,
                                                 layout_.resultsCard.right - 20, layout_.resultsCard.bottom - 16);
        renderTarget_->PushAxisAlignedClip(viewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (drawing_) DrawDrawingAnimation(viewport);
        else if (latestNumbers_.empty()) DrawEmptyResults(viewport);
        else DrawNumberGrid(viewport);
        renderTarget_->PopAxisAlignedClip();
    }

    void DrawEmptyResults(const D2D1_RECT_F& rect) {
        const float centerX = (rect.left + rect.right) * 0.5f;
        const float centerY = (rect.top + rect.bottom) * 0.5f - 8.0f;
        SetBrush(Color(0x6579ff, 0.16f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(centerX, centerY - 29), 36, 36), brush_);
        Text(L"✦", D2D1::RectF(centerX - 30, centerY - 59, centerX + 30, centerY + 1), formatTitle_,
             Color(0x98a7ff), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"Ready when you are", D2D1::RectF(rect.left, centerY + 18, rect.right, centerY + 49),
             formatHeading_, Color(0xe9ebf2), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"Your winners will appear here in a clean presentation grid.",
             D2D1::RectF(rect.left + 24, centerY + 53, rect.right - 24, centerY + 80), formatBody_,
             Color(0x7f8695), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        resultContentHeight_ = Height(rect);
    }

    void DrawDrawingAnimation(const D2D1_RECT_F& rect) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - drawStarted_).count();
        if (presentationWindow_ && elapsed < kShowCountdownMs) {
            const int count = std::max(1, 3 - static_cast<int>(elapsed / 1000));
            const float centerY = (rect.top + rect.bottom) * 0.5f - 10.0f;
            Text(L"AUDIENCE COUNTDOWN", D2D1::RectF(rect.left, centerY - 82, rect.right, centerY - 46),
                 formatBodyMedium_, Color(AccentHex()), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(std::to_wstring(count), D2D1::RectF(rect.left, centerY - 45, rect.right, centerY + 55),
                 formatDisplay_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(L"The audience display is live", D2D1::RectF(rect.left, centerY + 58, rect.right, centerY + 88),
                 formatCaption_, Color(0x7f8695), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            resultContentHeight_ = Height(rect);
            return;
        }
        const float angle = static_cast<float>(elapsed) * 0.0042f;
        const float centerX = (rect.left + rect.right) * 0.5f;
        const float centerY = (rect.top + rect.bottom) * 0.5f - 14.0f;
        const D2D1_POINT_2F center = D2D1::Point2F(centerX, centerY - 26.0f);
        DrawRadialGlow(center, 96.0f, AccentHex(), 0.18f);
        SetBrush(Color(0x0d1018, 0.92f));
        renderTarget_->FillEllipse(D2D1::Ellipse(center, 38, 38), brush_);
        SetBrush(Color(AccentHex(), 0.34f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x - 8, center.y - 10), 22, 20), brush_);
        SetBrush(Color(0xffffff, 0.20f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center.x - 14, center.y - 16), 6, 4), brush_);
        for (int ring = 0; ring < 4; ++ring) {
            const float phase = angle + ring * 0.74f;
            const float radiusX = 48.0f + ring * 10.0f;
            const float radiusY = 13.0f + ring * 4.0f;
            SetBrush(Color(ring % 2 == 0 ? AccentHex() : 0xffffff, 0.12f + ring * 0.025f));
            renderTarget_->DrawEllipse(D2D1::Ellipse(center, radiusX, radiusY), brush_, ring == 0 ? 2.2f : 1.0f);
            const D2D1_POINT_2F dot = D2D1::Point2F(center.x + std::cos(phase) * radiusX,
                                                    center.y + std::sin(phase) * radiusY);
            SetBrush(Color(ring % 2 == 0 ? 0xffffff : AccentHex(), 0.86f));
            renderTarget_->FillEllipse(D2D1::Ellipse(dot, 3.4f + ring * 0.35f, 3.4f + ring * 0.35f), brush_);
        }
        Text(L"DRAWING THE MOMENT", D2D1::RectF(rect.left, centerY + 30, rect.right, centerY + 60),
             formatBodyMedium_, Color(0xe9ebf2), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"Securely selecting your winners…", D2D1::RectF(rect.left, centerY + 59, rect.right, centerY + 86),
             formatCaption_, Color(0x7f8695), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        resultContentHeight_ = Height(rect);
    }

    void DrawNumberGrid(const D2D1_RECT_F& viewport) {
        const float gap = 12.0f;
        const float desiredWidth = 132.0f;
        int columns = std::max(2, static_cast<int>((Width(viewport) + gap) / (desiredWidth + gap)));
        columns = std::min(columns, 6);
        const float tileWidth = (Width(viewport) - gap * static_cast<float>(columns - 1)) / columns;
        const bool showNames = !pendingWinners_.empty() && !pendingWinners_.front().name.empty();
        const float tileHeight = showNames ? 126.0f : 92.0f;
        const int rows = (static_cast<int>(latestNumbers_.size()) + columns - 1) / columns;
        resultContentHeight_ = rows * tileHeight + std::max(0, rows - 1) * gap;
        const float maximum = std::max(0.0f, resultContentHeight_ - Height(viewport));
        resultScrollTarget_ = std::clamp(resultScrollTarget_, 0.0f, maximum);
        resultScroll_ = std::clamp(resultScroll_, 0.0f, maximum);

        for (size_t index = 0; index < latestNumbers_.size(); ++index) {
            const int row = static_cast<int>(index) / columns;
            const int column = static_cast<int>(index) % columns;
            const float left = viewport.left + column * (tileWidth + gap);
            const float top = viewport.top + row * (tileHeight + gap) - resultScroll_;
            const D2D1_RECT_F tile = D2D1::RectF(left, top, left + tileWidth, top + tileHeight);
            if (tile.bottom < viewport.top || tile.top > viewport.bottom) continue;
            const auto itemStarted = revealStarted_ + std::chrono::milliseconds(static_cast<int>(index) * 72);
            const float progress = MotionProgress(itemStarted, 540.0f);
            const float entry = EaseOutBack(progress);
            const D2D1_RECT_F lifted = OffsetRectF(tile, 0.0f, (1.0f - EaseOutCubic(progress)) * 34.0f);
            const D2D1_RECT_F visual = ScaleRectF(lifted, 0.84f + 0.16f * entry);
            DrawLiquidGlass(visual, 17, false, index == 0);
            DrawSelectionBlock(visual, 17, index == 0 ? 0.74f : 0.22f);
            FillRounded(D2D1::RectF(visual.left + 10, visual.top + 9, visual.left + 37, visual.top + 28), 8,
                        Color(SelectionBlueHex(), index == 0 ? 0.88f : 0.30f));
            Text(std::to_wstring(index + 1), D2D1::RectF(visual.left + 10, visual.top + 7, visual.left + 38, visual.top + 30),
                 formatCaption_, index == 0 ? Color(0xffffff) : Color(kSelectionBlueLight), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(std::to_wstring(latestNumbers_[index]), D2D1::RectF(visual.left + 8, visual.top + 18, visual.right - 8,
                                                                    showNames ? visual.top + 77 : visual.bottom - 5),
                 formatTileNumber_, Color(0xf7f8fb), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (showNames && index < pendingWinners_.size()) {
                Text(pendingWinners_[index].name, D2D1::RectF(visual.left + 9, visual.top + 75, visual.right - 9, visual.top + 101),
                     formatBodyMedium_, Color(0xe8eaf0), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Text(pendingWinners_[index].group, D2D1::RectF(visual.left + 9, visual.top + 101, visual.right - 9, visual.bottom - 6),
                     formatCaption_, Color(0x737a88), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
    }

    void DrawRecentCard() {
        DrawSurface(layout_.recentCard, 20);
        Text(L"RECENT DRAWS", D2D1::RectF(layout_.recentCard.left + 20, layout_.recentCard.top + 17,
                                           layout_.recentCard.right - 110, layout_.recentCard.top + 42),
             formatCaption_, Color(0x7f8695));
        layout_.recentViewAll = D2D1::RectF(layout_.recentCard.right - 112, layout_.recentCard.top + 12,
                                            layout_.recentCard.right - 14, layout_.recentCard.top + 47);
        Text(L"VIEW ALL →", layout_.recentViewAll, formatCaption_,
             Contains(layout_.recentViewAll, mouseX_, mouseY_) ? Color(0xaab5ff) : Color(0x8496ff),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        FillRect(D2D1::RectF(layout_.recentCard.left + 18, layout_.recentCard.top + 55,
                            layout_.recentCard.right - 18, layout_.recentCard.top + 56), Color(0xffffff, 0.07f));

        if (data_.history.empty()) {
            Text(L"No draws yet", D2D1::RectF(layout_.recentCard.left + 20, layout_.recentCard.top + 76,
                                               layout_.recentCard.right - 20, layout_.recentCard.top + 103),
                 formatBodyMedium_, Color(0xd9dce5));
            Text(L"Completed draws will be saved here automatically.",
                 D2D1::RectF(layout_.recentCard.left + 20, layout_.recentCard.top + 108,
                             layout_.recentCard.right - 20, layout_.recentCard.bottom - 18),
                 formatCaption_, Color(0x727987));
            return;
        }

        const int visible = std::min(3, static_cast<int>(data_.history.size()));
        const float availableHeight = Height(layout_.recentCard) - 72.0f;
        const float rowHeight = availableHeight / visible;
        for (int index = 0; index < visible; ++index) {
            const auto& entry = data_.history[data_.history.size() - 1 - static_cast<size_t>(index)];
            const float top = layout_.recentCard.top + 62.0f + index * rowHeight;
            if (index > 0) FillRect(D2D1::RectF(layout_.recentCard.left + 18, top,
                                                layout_.recentCard.right - 18, top + 1), Color(0xffffff, 0.055f));
            Text(L"DRAW " + std::to_wstring(data_.history.size() - static_cast<size_t>(index)),
                 D2D1::RectF(layout_.recentCard.left + 20, top + 9, layout_.recentCard.left + 110, top + 30),
                 formatCaption_, Color(0x7d8eff));
            Text(FormatCompactTimestamp(entry.timestamp),
                 D2D1::RectF(layout_.recentCard.left + 112, top + 9, layout_.recentCard.right - 19, top + 30),
                 formatCaption_, Color(0x686f7d), DWRITE_TEXT_ALIGNMENT_TRAILING);
            std::wstring summary = JoinNumbers(entry.numbers, 5);
            if (!entry.winners.empty() && !entry.winners.front().name.empty()) {
                summary = entry.winners.front().name;
                if (entry.winners.size() > 1) summary += L"  +" + std::to_wstring(entry.winners.size() - 1);
            }
            Text(summary,
                 D2D1::RectF(layout_.recentCard.left + 20, top + 32, layout_.recentCard.right - 18,
                             std::min(top + rowHeight - 20, layout_.recentCard.bottom - 18)),
                 formatBodyMedium_, Color(0xe8eaf0));
            Text(entry.prizeName,
                 D2D1::RectF(layout_.recentCard.left + 20, std::min(top + rowHeight - 24, layout_.recentCard.bottom - 26),
                             layout_.recentCard.right - 18, std::min(top + rowHeight - 4, layout_.recentCard.bottom - 6)),
                 formatCaption_, Color(0x727987));
        }
    }

    void DrawPoolCard() {
        DrawSurface(layout_.poolCard, 20);
        const int used = ValidUsedCount();
        const int remaining = RemainingCount();
        Text(L"NO-REPEAT POOL", D2D1::RectF(layout_.poolCard.left + 19, layout_.poolCard.top + 15,
                                             layout_.poolCard.right - 18, layout_.poolCard.top + 38),
             formatCaption_, Color(0x7f8695));
        Text(std::to_wstring(remaining), D2D1::RectF(layout_.poolCard.left + 19, layout_.poolCard.top + 40,
                                                     layout_.poolCard.left + 120, layout_.poolCard.bottom - 15),
             formatNumber_, Color(0xf1f3f8), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"remaining", D2D1::RectF(layout_.poolCard.left + 120, layout_.poolCard.top + 49,
                                        layout_.poolCard.right - 18, layout_.poolCard.top + 72),
             formatCaption_, Color(0x747b88));
        const float left = layout_.poolCard.left + 120;
        const float right = layout_.poolCard.right - 19;
        const float y = layout_.poolCard.bottom - 30;
        FillRounded(D2D1::RectF(left, y, right, y + 6), 3, Color(0xffffff, 0.08f));
        const int pool = PoolSize();
        const float progress = pool > 0 ? static_cast<float>(used) / static_cast<float>(pool) : 0.0f;
        FillRounded(D2D1::RectF(left, y, left + (right - left) * std::clamp(progress, 0.0f, 1.0f), y + 6),
                    3, Color(SelectionBlueHex()));
    }

    void DrawParticipantsPage() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        Text(L"PARTICIPANT DIRECTORY", D2D1::RectF(margin, 92, margin + 300, 113), formatCaption_, Color(SelectionBlueHex()));
        Text(L"Bring names into the moment.", D2D1::RectF(margin, 114, size.width - margin, 155),
             formatTitle_, Color(0xf5f6fa));
        Text(L"Import UTF-8 CSV columns: ticket, name, group. Imported lists automatically drive the draw pool.",
             D2D1::RectF(margin, 155, size.width - margin, 180), formatBody_, Color(0x8f95a3));

        const float gap = 14.0f;
        const float cardWidth = (size.width - 2 * margin - 2 * gap) / 3.0f;
        const float statsTop = 194.0f;
        const int enabled = static_cast<int>(std::count_if(data_.participants.begin(), data_.participants.end(),
            [](const Participant& participant) { return participant.enabled; }));
        const int won = static_cast<int>(std::count_if(data_.participants.begin(), data_.participants.end(),
            [](const Participant& participant) { return participant.won; }));
        DrawStatCard(D2D1::RectF(margin, statsTop, margin + cardWidth, statsTop + 88),
                     L"PARTICIPANTS", std::to_wstring(data_.participants.size()), Color(0xdfe6f2));
        DrawStatCard(D2D1::RectF(margin + cardWidth + gap, statsTop, margin + 2 * cardWidth + gap, statsTop + 88),
                     L"ACTIVE / ELIGIBLE", std::to_wstring(enabled), Color(0xdfead7));
        DrawStatCard(D2D1::RectF(margin + 2 * (cardWidth + gap), statsTop, size.width - margin, statsTop + 88),
                     L"CONFIRMED WINNERS", std::to_wstring(won), Color(0xfff0c2));

        const float actionsTop = 298.0f;
        Text(data_.participants.empty() ? L"NUMERIC MODE ACTIVE" : L"CSV PARTICIPANT MODE ACTIVE",
             D2D1::RectF(margin, actionsTop, margin + 320, actionsTop + 42), formatHeading_, Color(0xeff1f6),
             DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        layout_.importParticipantsButton = D2D1::RectF(size.width - margin - 342, actionsTop,
                                                       size.width - margin - 170, actionsTop + 42);
        layout_.clearParticipantsButton = D2D1::RectF(size.width - margin - 158, actionsTop,
                                                      size.width - margin, actionsTop + 42);
        const float importHover = SmoothedHover(layout_.importParticipantsButton,
                                                Contains(layout_.importParticipantsButton, mouseX_, mouseY_),
                                                0x494d504f52544353ULL);
        DrawSelectionBlock(OffsetRectF(layout_.importParticipantsButton, 0.0f, -importHover), 12,
                           0.82f + 0.18f * importHover);
        Text(data_.participants.empty() ? L"IMPORT CSV" : L"REPLACE CSV", layout_.importParticipantsButton,
             formatCaption_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DrawSecondaryButton(layout_.clearParticipantsButton, L"CLEAR LIST", true);

        layout_.participantsViewport = D2D1::RectF(margin, 354, size.width - margin, size.height - 24);
        renderTarget_->PushAxisAlignedClip(layout_.participantsViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (data_.participants.empty()) {
            DrawSurface(layout_.participantsViewport, 20);
            Text(L"Import your participant list", D2D1::RectF(layout_.participantsViewport.left,
                 layout_.participantsViewport.top + 60, layout_.participantsViewport.right,
                 layout_.participantsViewport.top + 100), formatHeading_, Color(0xe8eaf0), DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(L"Example: 1001, Michelle Tan, Marketing", D2D1::RectF(layout_.participantsViewport.left + 30,
                 layout_.participantsViewport.top + 108, layout_.participantsViewport.right - 30,
                 layout_.participantsViewport.top + 140), formatBody_, Color(0x7e8593), DWRITE_TEXT_ALIGNMENT_CENTER);
            participantsContentHeight_ = Height(layout_.participantsViewport);
        } else {
            const float rowHeight = 62.0f;
            const float rowGap = 8.0f;
            participantsContentHeight_ = data_.participants.size() * (rowHeight + rowGap) - rowGap;
            const float maximum = std::max(0.0f, participantsContentHeight_ - Height(layout_.participantsViewport));
            participantsScrollTarget_ = std::clamp(participantsScrollTarget_, 0.0f, maximum);
            participantsScroll_ = std::clamp(participantsScroll_, 0.0f, maximum);
            for (size_t index = 0; index < data_.participants.size(); ++index) {
                const float top = layout_.participantsViewport.top + index * (rowHeight + rowGap) - participantsScroll_;
                const D2D1_RECT_F row = D2D1::RectF(layout_.participantsViewport.left, top,
                                                     layout_.participantsViewport.right, top + rowHeight);
                if (row.bottom < layout_.participantsViewport.top || row.top > layout_.participantsViewport.bottom) continue;
                DrawLiquidGlass(row, 17, true);
                const auto& participant = data_.participants[index];
                Text(std::to_wstring(participant.ticket), D2D1::RectF(row.left + 18, row.top, row.left + 116, row.bottom),
                     formatHeading_, Color(0xf1f3f8), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Text(participant.name, D2D1::RectF(row.left + 128, row.top + 8, row.right - 330, row.top + 35),
                     formatBodyMedium_, Color(0xe8eaf0));
                Text(participant.group, D2D1::RectF(row.left + 128, row.top + 34, row.right - 330, row.bottom - 5),
                     formatCaption_, Color(0x737a88));
                const D2D1_RECT_F status = D2D1::RectF(row.right - 170, row.top + 15, row.right - 18, row.bottom - 15);
                FillRounded(status, 15, participant.won ? Color(SelectionBlueHex(), 0.22f) : Color(0xffffff, 0.055f));
                StrokeRounded(status, 15, participant.won ? Color(SelectionBlueHex(), 0.48f) : Color(0xffffff, 0.08f));
                Text(participant.won ? L"WINNER" : L"ELIGIBLE", status, formatCaption_,
                     participant.won ? Color(kSelectionBlueLight) : Color(0x98a2b3), DWRITE_TEXT_ALIGNMENT_CENTER,
                     DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        renderTarget_->PopAxisAlignedClip();
    }

    std::vector<std::wstring> ParticipantGroups() const {
        std::vector<std::wstring> groups{L"All participants"};
        for (const auto& participant : data_.participants) {
            if (participant.group.empty()) continue;
            if (std::find(groups.begin(), groups.end(), participant.group) == groups.end()) groups.push_back(participant.group);
        }
        std::sort(groups.begin() + 1, groups.end());
        return groups;
    }

    void CyclePrizeEligibility() {
        if (data_.prizes.empty()) return;
        auto groups = ParticipantGroups();
        auto& prize = data_.prizes[static_cast<size_t>(data_.selectedPrize)];
        auto iterator = std::find(groups.begin(), groups.end(), prize.eligibleGroup);
        size_t next = iterator == groups.end() ? 0 : (static_cast<size_t>(iterator - groups.begin()) + 1) % groups.size();
        prize.eligibleGroup = groups[next];
        const int pool = std::max(0, PoolSize());
        data_.quantity = std::min(data_.quantity, pool);
        prize.winnerCount = data_.quantity;
        SaveState();
        ShowToast(L"Eligibility set to " + prize.eligibleGroup);
    }

    void DrawPrizesPage() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        Text(L"PRIZE PROGRAM", D2D1::RectF(margin, 92, margin + 280, 113), formatCaption_, Color(SelectionBlueHex()));
        Text(L"Shape the rhythm of your event.", D2D1::RectF(margin, 114, size.width - margin, 155),
             formatTitle_, Color(0xf5f6fa));
        Text(L"Select a prize before drawing. Eligibility can target everyone or one imported participant group.",
             D2D1::RectF(margin, 155, size.width - margin, 180), formatBody_, Color(0x8f95a3));

        const float actionsTop = 194.0f;
        layout_.addPrizeButton = D2D1::RectF(margin, actionsTop, margin + 142, actionsTop + 42);
        layout_.editPrizeButton = D2D1::RectF(margin + 154, actionsTop, margin + 296, actionsTop + 42);
        layout_.deletePrizeButton = D2D1::RectF(margin + 308, actionsTop, margin + 460, actionsTop + 42);
        const float addHover = SmoothedHover(layout_.addPrizeButton, Contains(layout_.addPrizeButton, mouseX_, mouseY_),
                                             0x4144445052495a45ULL);
        DrawSelectionBlock(OffsetRectF(layout_.addPrizeButton, 0.0f, -addHover), 12, 0.82f + 0.18f * addHover);
        Text(L"+ ADD PRIZE", layout_.addPrizeButton, formatCaption_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DrawSecondaryButton(layout_.editPrizeButton, L"RENAME");
        DrawSecondaryButton(layout_.deletePrizeButton, L"REMOVE", true);
        layout_.prizeEligibilityButton = D2D1::RectF(size.width - margin - 330, actionsTop,
                                                      size.width - margin, actionsTop + 42);
        DrawSecondaryButton(layout_.prizeEligibilityButton, L"ELIGIBILITY · " + ActivePrize().eligibleGroup);

        const float gridTop = 258.0f;
        const float gap = 14.0f;
        const int columns = 2;
        const float cardWidth = (size.width - 2 * margin - gap) / 2.0f;
        const float cardHeight = 112.0f;
        for (auto& rect : layout_.prizeCards) rect = {};
        const size_t visible = std::min<size_t>(8, data_.prizes.size());
        for (size_t index = 0; index < visible; ++index) {
            const int row = static_cast<int>(index) / columns;
            const int column = static_cast<int>(index) % columns;
            const float left = margin + column * (cardWidth + gap);
            const float top = gridTop + row * (cardHeight + gap);
            const D2D1_RECT_F card = D2D1::RectF(left, top, left + cardWidth, top + cardHeight);
            layout_.prizeCards[index] = card;
            const bool selected = static_cast<int>(index) == data_.selectedPrize;
            DrawLiquidGlass(card, 18, true, selected);
            const float selectedAmount = SmoothedHover(card, selected, 0x5052495a4553454cULL);
            if (selectedAmount > 0.002f) DrawSelectionBlock(card, 18, 0.52f * selectedAmount);
            StrokeRounded(card, 18, selected ? Color(kSelectionBlueLight, 0.86f) : Color(0xffffff, 0.075f),
                          selected ? 1.6f : 1.0f);
            Text(L"PRIZE " + std::to_wstring(index + 1), D2D1::RectF(card.left + 18, card.top + 14,
                 card.right - 130, card.top + 36), formatCaption_, selected ? Color(0xffffff) : Color(0x747b88));
            Text(data_.prizes[index].name, D2D1::RectF(card.left + 18, card.top + 40, card.right - 145, card.bottom - 14),
                 formatHeading_, Color(0xf0f2f7), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const D2D1_RECT_F eligible = D2D1::RectF(card.right - 142, card.top + 18, card.right - 18, card.top + 48);
            FillRounded(eligible, 15, Color(0xffffff, 0.055f));
            Text(data_.prizes[index].eligibleGroup, eligible, formatCaption_, Color(0xa3a9b5), DWRITE_TEXT_ALIGNMENT_CENTER,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(std::to_wstring(data_.prizes[index].winnerCount) + L" winner default",
                 D2D1::RectF(card.right - 180, card.top + 63, card.right - 18, card.bottom - 14),
                 formatCaption_, Color(0x747b88), DWRITE_TEXT_ALIGNMENT_TRAILING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (data_.prizes.size() > 8) {
            Text(L"The first 8 prizes are shown. Remove unused prizes to reveal more.",
                 D2D1::RectF(margin, size.height - 48, size.width - margin, size.height - 24),
                 formatCaption_, Color(0x747b88), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    void DrawToggleSetting(const D2D1_RECT_F& row, const std::wstring& title, const std::wstring& detail,
                           bool enabled, D2D1_RECT_F& toggle) {
        DrawLiquidGlass(row, 18, true);
        Text(title, D2D1::RectF(row.left + 17, row.top + 11, row.right - 90, row.top + 36),
             formatBodyMedium_, Color(0xf0f1f5));
        Text(detail, D2D1::RectF(row.left + 17, row.top + 38, row.right - 90, row.bottom - 10),
             formatCaption_, Color(0x747b88));
        toggle = D2D1::RectF(row.right - 67, row.top + 22, row.right - 17, row.top + 50);
        const float toggleAmount = SmoothedHover(toggle, enabled, 0x544f47474c455345ULL);
        FillRounded(toggle, 14, MixColor(Color(0x2a3342, 0.92f), Color(SelectionBlueHex(), 0.94f), toggleAmount));
        DrawGlassHighlight(toggle, 14, 1.0f);
        StrokeRounded(toggle, 14, Color(0xffffff, 0.28f));
        const float knobX = toggle.left + 14 + (Width(toggle) - 28) * toggleAmount;
        SetBrush(Color(0xffffff));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, toggle.top + 14), 9.5f, 9.5f), brush_);
    }

    void DrawCompactToggleSetting(const D2D1_RECT_F& row, const std::wstring& title,
                                  bool enabled, D2D1_RECT_F& toggle) {
        DrawLiquidGlass(row, 17, true);
        Text(title, D2D1::RectF(row.left + 14, row.top, row.right - 72, row.bottom),
             formatBodyMedium_, Color(0xe9ebf1), DWRITE_TEXT_ALIGNMENT_LEADING,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        toggle = D2D1::RectF(row.right - 62, row.top + 20, row.right - 14, row.top + 48);
        const float toggleAmount = SmoothedHover(toggle, enabled, 0x434f4d5041435454ULL);
        FillRounded(toggle, 14, MixColor(Color(0x2a3342, 0.92f), Color(SelectionBlueHex(), 0.94f), toggleAmount));
        DrawGlassHighlight(toggle, 14, 0.85f);
        StrokeRounded(toggle, 14, Color(0xffffff, 0.28f));
        const float knobX = toggle.left + 14 + (Width(toggle) - 28) * toggleAmount;
        SetBrush(Color(0xffffff));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, toggle.top + 14), 9.5f, 9.5f), brush_);
    }

    void DrawShowPage() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        Text(L"AUDIENCE EXPERIENCE", D2D1::RectF(margin, 92, margin + 310, 113), formatCaption_, Color(SelectionBlueHex()));
        Text(L"Turn the draw into a show.", D2D1::RectF(margin, 114, size.width - margin, 155),
             formatTitle_, Color(0xf5f6fa));
        Text(L"The audience window rotates welcome slides, then runs a countdown, draw animation, reveal, and summary.",
             D2D1::RectF(margin, 155, size.width - margin, 180), formatBody_, Color(0x8f95a3));

        const float contentTop = 196.0f;
        const float gap = 18.0f;
        const float leftWidth = (size.width - 2 * margin) * 0.58f;
        const D2D1_RECT_F preview = D2D1::RectF(margin, contentTop, margin + leftWidth, size.height - 28);
        const D2D1_RECT_F settings = D2D1::RectF(preview.right + gap, contentTop, size.width - margin, size.height - 28);
        DrawSurface(preview, 21);
        DrawSurface(settings, 21);

        ID2D1GradientStopCollection* stops = nullptr;
        ID2D1RadialGradientBrush* glow = nullptr;
        const D2D1_GRADIENT_STOP gradientStops[] = {
            {0.0f, Color(AccentHex(), 0.34f)}, {0.55f, Color(AccentHex(), 0.07f)}, {1.0f, Color(0x111318, 0.0f)}};
        if (SUCCEEDED(renderTarget_->CreateGradientStopCollection(gradientStops, 3, &stops))) {
            renderTarget_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
                D2D1::Point2F((preview.left + preview.right) / 2, preview.top + 150), D2D1::Point2F(),
                Width(preview) * 0.55f, Height(preview) * 0.5f), stops, &glow);
        }
        if (glow) renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(preview, 21, 21), glow);
        SafeRelease(glow);
        SafeRelease(stops);
        if (data_.motionEnabled) {
            const float time = AnimationSeconds();
            const D2D1_POINT_2F orbitCenter = D2D1::Point2F((preview.left + preview.right) * 0.5f,
                                                            preview.top + 168.0f);
            for (int ring = 0; ring < 3; ++ring) {
                const float radiusX = Width(preview) * (0.18f + ring * 0.08f);
                const float radiusY = 13.0f + ring * 7.0f;
                SetBrush(Color(ring == 0 ? AccentHex() : 0xffffff, 0.10f - ring * 0.018f));
                renderTarget_->DrawEllipse(D2D1::Ellipse(orbitCenter, radiusX, radiusY), brush_, 1.0f);
                const float phase = time * (0.65f + ring * 0.12f) + ring * 1.8f;
                const D2D1_POINT_2F dot = D2D1::Point2F(orbitCenter.x + std::cos(phase) * radiusX,
                                                        orbitCenter.y + std::sin(phase) * radiusY);
                SetBrush(Color(ring == 0 ? 0xffffff : AccentHex(), 0.72f));
                renderTarget_->FillEllipse(D2D1::Ellipse(dot, 3.0f + ring, 3.0f + ring), brush_);
            }
        }
        Text(L"AUDIENCE PREVIEW", D2D1::RectF(preview.left + 22, preview.top + 18, preview.right - 22, preview.top + 42),
             formatCaption_, Color(0x7f8695));
        Text(data_.eventName.empty() ? L"JAYCEE Lottery" : data_.eventName,
             D2D1::RectF(preview.left + 34, preview.top + 80, preview.right - 34, preview.top + 190),
             formatTitle_, Color(0xf7f8fb), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"NEXT PRIZE", D2D1::RectF(preview.left + 30, preview.top + 204, preview.right - 30, preview.top + 236),
             formatCaption_, Color(kSelectionBlueLight), DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(ActivePrize().name, D2D1::RectF(preview.left + 32, preview.top + 238, preview.right - 32, preview.top + 300),
             formatHeading_, Color(0xe8eaf0), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(L"Welcome · Prize · Countdown · Draw · Reveal · Summary",
             D2D1::RectF(preview.left + 28, preview.bottom - 62, preview.right - 28, preview.bottom - 28),
             formatCaption_, Color(0x838a98), DWRITE_TEXT_ALIGNMENT_CENTER);

        const float sx = settings.left + 20;
        const float sr = settings.right - 20;
        Text(L"SHOW CONTROLS", D2D1::RectF(sx, settings.top + 17, sr, settings.top + 42), formatCaption_, Color(0x7f8695));
        layout_.openPresentationButton = D2D1::RectF(sx, settings.top + 54, sr, settings.top + 108);
        const bool presentationHovered = Contains(layout_.openPresentationButton, mouseX_, mouseY_);
        const float presentationHover = SmoothedHover(layout_.openPresentationButton, presentationHovered,
                                                       0x50524553454e5442ULL);
        const D2D1_RECT_F presentationVisual = OffsetRectF(layout_.openPresentationButton, 0.0f, -presentationHover);
        DrawSelectionBlock(presentationVisual, 14, 0.82f + 0.18f * presentationHover);
        Text(presentationWindow_ ? L"CLOSE AUDIENCE DISPLAY" : L"OPEN AUDIENCE DISPLAY",
             layout_.openPresentationButton, formatBodyMedium_, Color(0xffffff), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        layout_.eventNameButton = D2D1::RectF(sx, settings.top + 124, sr, settings.top + 184);
        DrawLiquidGlass(layout_.eventNameButton, 17, true);
        Text(L"EVENT TITLE", D2D1::RectF(sx + 14, settings.top + 132, sr - 14, settings.top + 153),
             formatCaption_, Color(0x747b88));
        Text((data_.eventName.empty() ? L"Optional event title" : data_.eventName) + L"  →",
             D2D1::RectF(sx + 14, settings.top + 152, sr - 14, settings.top + 180),
             formatBodyMedium_, Color(0xe8eaf0));
        DrawToggleSetting(D2D1::RectF(sx, settings.top + 198, sr, settings.top + 266),
                          L"Liquid motion", L"Ambient glow, spring transitions, eased scrolling, and cinematic reveals.",
                          data_.motionEnabled, layout_.motionToggle);
        const float compactGap = 10.0f;
        const float compactWidth = (sr - sx - compactGap) * 0.5f;
        DrawCompactToggleSetting(D2D1::RectF(sx, settings.top + 278,
                                             sx + compactWidth, settings.top + 346),
                                 L"Reveal sound", data_.soundEnabled, layout_.soundToggle);
        DrawCompactToggleSetting(D2D1::RectF(sx + compactWidth + compactGap, settings.top + 278,
                                             sr, settings.top + 346),
                                 L"Confetti", data_.confettiEnabled, layout_.confettiToggle);
        Text(L"ACCENT THEME", D2D1::RectF(sx, settings.top + 364, sr, settings.top + 388),
             formatCaption_, Color(0x7f8695));
        static constexpr unsigned themes[] = {0x2f7bff, 0x14b8a6, 0x8b5cf6, 0xf59e0b};
        const float themeGap = 12.0f;
        const float themeWidth = (Width(settings) - 40 - 3 * themeGap) / 4.0f;
        for (int index = 0; index < 4; ++index) {
            const float left = sx + index * (themeWidth + themeGap);
            layout_.themeButtons[index] = D2D1::RectF(left, settings.top + 394, left + themeWidth, settings.top + 438);
            const float themeSelected = SmoothedHover(layout_.themeButtons[index], index == data_.themeIndex,
                                                       0x5448454d4553454cULL + static_cast<std::uint64_t>(index));
            DrawElevation(layout_.themeButtons[index], 13, 4.0f + 4.0f * themeSelected,
                          0.14f + 0.14f * themeSelected);
            FillRounded(layout_.themeButtons[index], 13, Color(themes[index], 0.36f + 0.60f * themeSelected));
            if (themeSelected > 0.002f) {
                StrokeRounded(layout_.themeButtons[index], 13, Color(SelectionBlueHex(), 0.94f * themeSelected), 2.2f);
                FillRounded(D2D1::RectF(layout_.themeButtons[index].right - 22, layout_.themeButtons[index].top + 7,
                                        layout_.themeButtons[index].right - 8, layout_.themeButtons[index].top + 21),
                            7, Color(SelectionBlueHex(), themeSelected));
            }
        }

        const D2D1_RECT_F scaleRow = D2D1::RectF(sx, settings.top + 452, sr, settings.top + 520);
        DrawLiquidGlass(scaleRow, 18, true);
        Text(L"INTERFACE SCALE", D2D1::RectF(scaleRow.left + 17, scaleRow.top + 10,
             scaleRow.right - 220, scaleRow.top + 31), formatCaption_, Color(0x747b88));
        Text(L"Zoom the complete workspace · Ctrl + / − / 0",
             D2D1::RectF(scaleRow.left + 17, scaleRow.top + 34, scaleRow.right - 220, scaleRow.bottom - 8),
             formatCaption_, Color(0x9299a7));
        layout_.scaleMinusButton = D2D1::RectF(scaleRow.right - 202, scaleRow.top + 14,
                                               scaleRow.right - 158, scaleRow.bottom - 14);
        layout_.scaleValue = D2D1::RectF(scaleRow.right - 150, scaleRow.top + 14,
                                         scaleRow.right - 62, scaleRow.bottom - 14);
        layout_.scalePlusButton = D2D1::RectF(scaleRow.right - 54, scaleRow.top + 14,
                                              scaleRow.right - 10, scaleRow.bottom - 14);
        DrawSmallButton(layout_.scaleMinusButton, L"−");
        FillRounded(layout_.scaleValue, 12, Color(0x061020, 0.58f));
        DrawGlassHighlight(layout_.scaleValue, 12, 0.72f);
        StrokeRounded(layout_.scaleValue, 12, Color(0xffffff, 0.16f));
        Text(std::to_wstring(static_cast<int>(std::round(data_.uiScale * 100.0f))) + L"%", layout_.scaleValue,
             formatBodyMedium_, Color(0xf0f2f7), DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DrawSmallButton(layout_.scalePlusButton, L"+");
    }

    void DrawHistoryPage() {
        const auto size = CanvasSize();
        const float margin = ContentMargin(size.width);
        Text(L"DRAW ARCHIVE", D2D1::RectF(margin, 92, margin + 260, 113), formatCaption_, Color(0x7d8eff));
        Text(L"Every moment, kept in order.", D2D1::RectF(margin, 114, size.width - margin, 155),
             formatTitle_, Color(0xf5f6fa));
        Text(L"Review complete draw details or manage the two independent data stores.",
             D2D1::RectF(margin, 155, size.width - margin, 180), formatBody_, Color(0x8f95a3));

        const float gap = 14.0f;
        const float cardWidth = (size.width - 2 * margin - 3 * gap) / 4.0f;
        const float statsTop = 194.0f;
        DrawStatCard(D2D1::RectF(margin, statsTop, margin + cardWidth, statsTop + 88),
                     L"TOTAL DRAWS", std::to_wstring(data_.history.size()), Color(0xdfe6f2));
        size_t winnerCount = 0;
        for (const auto& entry : data_.history) winnerCount += entry.numbers.size();
        DrawStatCard(D2D1::RectF(margin + cardWidth + gap, statsTop, margin + 2 * cardWidth + gap, statsTop + 88),
                     L"WINNERS DRAWN", std::to_wstring(winnerCount), Color(0xdfead7));
        DrawStatCard(D2D1::RectF(margin + 2 * (cardWidth + gap), statsTop,
                                 margin + 3 * cardWidth + 2 * gap, statsTop + 88),
                     L"POOL REMAINING", std::to_wstring(RemainingCount()), Color(0xfff0c2));
        DrawStatCard(D2D1::RectF(margin + 3 * (cardWidth + gap), statsTop, size.width - margin, statsTop + 88),
                     L"ACTIVE MODE", data_.noRepeat ? L"NO REPEAT" : L"STANDARD", Color(0xe7d9f8));

        const float actionsTop = 298.0f;
        layout_.exportHistoryButton = D2D1::RectF(size.width - margin - 478, actionsTop,
                                                  size.width - margin - 326, actionsTop + 42);
        layout_.resetPoolButton = D2D1::RectF(size.width - margin - 314, actionsTop,
                                              size.width - margin - 152, actionsTop + 42);
        layout_.clearHistoryButton = D2D1::RectF(size.width - margin - 140, actionsTop,
                                                 size.width - margin, actionsTop + 42);
        Text(L"DRAW HISTORY", D2D1::RectF(margin, actionsTop, margin + 220, actionsTop + 42), formatHeading_,
             Color(0xeff1f6), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        DrawSecondaryButton(layout_.exportHistoryButton, L"EXPORT CSV");
        DrawSecondaryButton(layout_.resetPoolButton, L"RESET POOL");
        DrawSecondaryButton(layout_.clearHistoryButton, L"CLEAR HISTORY", true);

        layout_.historyViewport = D2D1::RectF(margin, 354, size.width - margin, size.height - 24);
        DrawHistoryList();
    }

    void DrawStatCard(const D2D1_RECT_F& rect, const std::wstring& label,
                      const std::wstring& value, D2D1_COLOR_F tint) {
        const bool hovered = Contains(rect, mouseX_, mouseY_);
        const float hover = SmoothedHover(rect, hovered, 0x5354415443415244ULL);
        const D2D1_RECT_F visual = OffsetRectF(rect, 0.0f, -1.5f * hover);
        DrawLiquidGlass(visual, 16, false, true);
        FillRounded(D2D1::RectF(visual.left + 14, visual.top + 14, visual.left + 18, visual.bottom - 14),
                    2.0f, Color(SelectionBlueHex(), 0.66f + 0.24f * hover));
        SetBrush(D2D1::ColorF(tint.r, tint.g, tint.b, 0.72f));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(visual.right - 20, visual.top + 20), 3.0f, 3.0f), brush_);
        Text(label, D2D1::RectF(visual.left + 28, visual.top + 12, visual.right - 32, visual.top + 34),
             formatCaption_, Color(0x8893a7));
        Text(value, D2D1::RectF(visual.left + 28, visual.top + 34, visual.right - 15, visual.bottom - 9),
             formatHeading_, Color(0xf4f7fb), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawSecondaryButton(const D2D1_RECT_F& rect, const std::wstring& label, bool danger = false) {
        const bool hovered = Contains(rect, mouseX_, mouseY_);
        const float hover = SmoothedHover(rect, hovered, 0x5345434f4e444152ULL);
        const D2D1_RECT_F visual = OffsetRectF(rect, 0.0f, -1.5f * hover);
        DrawLiquidGlass(visual, 14, false);
        if (danger && hover > 0.002f) FillRounded(visual, 14, Color(0xff6767, 0.13f * hover));
        StrokeRounded(visual, 12, danger ? Color(0xff7373, 0.26f + 0.22f * hover)
                                        : MixColor(Color(0xffffff, 0.10f), Color(SelectionBlueHex(), 0.52f), hover));
        Text(label, visual, formatCaption_, danger ? Color(0xff9b9b)
                                                   : MixColor(Color(0xc8cdd8), Color(0xffffff), hover),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawHistoryList() {
        renderTarget_->PushAxisAlignedClip(layout_.historyViewport, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (data_.history.empty()) {
            DrawSurface(layout_.historyViewport, 20);
            Text(L"No archived draws yet", D2D1::RectF(layout_.historyViewport.left,
                 layout_.historyViewport.top + 70, layout_.historyViewport.right, layout_.historyViewport.top + 110),
                 formatHeading_, Color(0xe7e9f0), DWRITE_TEXT_ALIGNMENT_CENTER);
            Text(L"Run your first draw and its complete details will appear here.",
                 D2D1::RectF(layout_.historyViewport.left + 30, layout_.historyViewport.top + 115,
                             layout_.historyViewport.right - 30, layout_.historyViewport.top + 145),
                 formatBody_, Color(0x7d8492), DWRITE_TEXT_ALIGNMENT_CENTER);
            historyContentHeight_ = Height(layout_.historyViewport);
            renderTarget_->PopAxisAlignedClip();
            return;
        }

        const float rowHeight = 104.0f;
        const float gap = 12.0f;
        historyContentHeight_ = data_.history.size() * rowHeight + (data_.history.size() - 1) * gap;
        const float maximum = std::max(0.0f, historyContentHeight_ - Height(layout_.historyViewport));
        historyScrollTarget_ = std::clamp(historyScrollTarget_, 0.0f, maximum);
        historyScroll_ = std::clamp(historyScroll_, 0.0f, maximum);

        for (size_t visibleIndex = 0; visibleIndex < data_.history.size(); ++visibleIndex) {
            const size_t dataIndex = data_.history.size() - 1 - visibleIndex;
            const auto& entry = data_.history[dataIndex];
            const float top = layout_.historyViewport.top + visibleIndex * (rowHeight + gap) - historyScroll_;
            const D2D1_RECT_F row = D2D1::RectF(layout_.historyViewport.left, top,
                                                layout_.historyViewport.right, top + rowHeight);
            if (row.bottom < layout_.historyViewport.top || row.top > layout_.historyViewport.bottom) continue;
            DrawSurface(row, 17);

            const D2D1_RECT_F numberBadge = D2D1::RectF(row.left + 18, row.top + 18, row.left + 78, row.bottom - 18);
            FillRounded(numberBadge, 14, Color(0x5b70ff, 0.16f));
            Text(std::to_wstring(dataIndex + 1), numberBadge, formatHeading_, Color(0x9ca8ff),
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            Text(L"DRAW " + std::to_wstring(dataIndex + 1),
                 D2D1::RectF(row.left + 96, row.top + 17, row.left + 190, row.top + 40),
                 formatCaption_, Color(0x8496ff));
            Text(FormatTimestamp(entry.timestamp),
                 D2D1::RectF(row.left + 96, row.top + 40, row.left + 310, row.top + 64),
                 formatCaption_, Color(0x727987));
            std::wstring winnerSummary = JoinNumbers(entry.numbers, 12);
            if (!entry.winners.empty() && !entry.winners.front().name.empty()) {
                winnerSummary = entry.winners.front().name + L"  ·  #" + std::to_wstring(entry.winners.front().ticket);
                if (entry.winners.size() > 1) winnerSummary += L"  ·  +" + std::to_wstring(entry.winners.size() - 1);
            }
            Text(winnerSummary,
                 D2D1::RectF(row.left + 326, row.top + 13, row.right - 190, row.top + 54),
                 formatHeading_, Color(0xf0f2f7), DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(entry.prizeName.empty() ? L"General Doorprize" : entry.prizeName,
                 D2D1::RectF(row.left + 326, row.top + 57, row.right - 190, row.bottom - 12),
                 formatCaption_, Color(0x747b88), DWRITE_TEXT_ALIGNMENT_LEADING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            const std::wstring mode = entry.noRepeat ? L"NO REPEAT" : L"STANDARD";
            const D2D1_RECT_F modeRect = D2D1::RectF(row.right - 174, row.top + 20, row.right - 18, row.top + 49);
            FillRounded(modeRect, 14, entry.noRepeat ? Color(0x5e76ff, 0.14f) : Color(0xffffff, 0.06f));
            Text(mode, modeRect, formatCaption_, entry.noRepeat ? Color(0x91a1ff) : Color(0xa9afbb),
                 DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Text(std::to_wstring(entry.quantity) + L" winners  ·  1—" + std::to_wstring(entry.maxValue),
                 D2D1::RectF(row.right - 190, row.top + 59, row.right - 18, row.bottom - 15),
                 formatCaption_, Color(0x727987), DWRITE_TEXT_ALIGNMENT_TRAILING,
                 DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        renderTarget_->PopAxisAlignedClip();
    }

    void DrawDialog() {
        const auto size = ViewportLogicalSize();
        FillRect(D2D1::RectF(0, 0, size.width, size.height), Color(0x020817, 0.72f));
        if (animatedDialog_ != dialog_) {
            animatedDialog_ = dialog_;
            dialogAnimationStarted_ = std::chrono::steady_clock::now();
        }
        const float dialogEntry = EaseOutBack(MotionProgress(dialogAnimationStarted_, 520.0f));
        const float width = 500.0f;
        const bool textDialog = IsTextDialog();
        const float height = textDialog ? 304.0f : 276.0f;
        const D2D1_RECT_F baseCard = D2D1::RectF((size.width - width) / 2, (size.height - height) / 2,
                                                 (size.width + width) / 2, (size.height + height) / 2);
        const D2D1_RECT_F card = ScaleRectF(baseCard, 0.90f + 0.10f * dialogEntry);
        DrawRadialGlow(D2D1::Point2F((card.left + card.right) * 0.5f, card.bottom), 260.0f,
                       SelectionBlueHex(), 0.11f);
        DrawLiquidGlass(card, 22, false, true);
        const D2D1_RECT_F icon = D2D1::RectF(card.left + 24, card.top + 24, card.left + 70, card.top + 70);
        const bool destructive = dialog_ == DialogType::ClearHistory || dialog_ == DialogType::ClearParticipants ||
                                 dialog_ == DialogType::DeletePrize;
        FillRounded(icon, 12, destructive ? Color(0xff6767, 0.15f) : Color(SelectionBlueHex(), 0.20f));
        Text(destructive ? L"×" : (textDialog ? L"✎" : L"↻"), icon, formatHeading_,
             destructive ? Color(0xff8a8a) : Color(kSelectionBlueLight),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        std::wstring title;
        std::wstring description;
        std::wstring confirm;
        if (dialog_ == DialogType::ClearHistory) {
            title = L"Clear the complete draw history?";
            description = L"This removes every archived draw from this computer. The no-repeat pool will stay unchanged.";
            confirm = L"CLEAR HISTORY";
        } else if (dialog_ == DialogType::ClearParticipants) {
            title = L"Clear the participant directory?";
            description = L"Names, groups, and participant win status will be removed. The app returns to numeric coupon mode.";
            confirm = L"CLEAR LIST";
        } else if (dialog_ == DialogType::DeletePrize) {
            title = L"Remove this prize?";
            description = L"The prize setup is removed, but already confirmed winner history stays intact.";
            confirm = L"REMOVE PRIZE";
        } else if (dialog_ == DialogType::AddPrize) {
            title = L"Create a new prize";
            description = L"Give the prize a clear audience-facing name. You can configure eligibility after creating it.";
            confirm = L"ADD PRIZE";
        } else if (dialog_ == DialogType::EditPrize) {
            title = L"Rename this prize";
            description = L"The new name appears in the operator view, audience display, history, and CSV exports.";
            confirm = L"SAVE NAME";
        } else if (dialog_ == DialogType::EditEventName) {
            title = L"Edit the event title";
            description = L"This title anchors the welcome slideshow and the top of your draw workspace.";
            confirm = L"SAVE TITLE";
        } else if (dialog_ == DialogType::OutOfNumbers) {
            title = L"The pool needs a fresh start";
            description = L"Only " + std::to_wstring(RemainingCount()) + L" unused coupons remain, fewer than the requested " +
                          std::to_wstring(data_.quantity) + L" winners. Reset the pool to continue.";
            confirm = L"RESET POOL";
        } else {
            title = L"Reset the no-repeat pool?";
            description = L"Previously used coupons become available again. Your archived draw history will stay safe.";
            confirm = L"RESET POOL";
        }
        Text(title, D2D1::RectF(card.left + 86, card.top + 23, card.right - 24, card.top + 58),
             formatHeading_, Color(0xf1f3f8), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(description, D2D1::RectF(card.left + 24, card.top + 86, card.right - 24,
                                     textDialog ? card.top + 140 : card.top + 170),
             formatBody_, Color(0x9299a7));

        if (textDialog) {
            layout_.dialogTextField = D2D1::RectF(card.left + 24, card.top + 152, card.right - 24, card.top + 202);
            DrawLiquidGlass(layout_.dialogTextField, 15, true);
            FillRounded(layout_.dialogTextField, 15, Color(0x030b19, 0.42f));
            DrawSelectionBlock(layout_.dialogTextField, 13, 0.24f);
            StrokeRounded(layout_.dialogTextField, 13, Color(SelectionBlueHex(), 0.86f), 1.5f);
            Text(dialogText_.empty() ? L"Type a name…" : dialogText_,
                 D2D1::RectF(layout_.dialogTextField.left + 14, layout_.dialogTextField.top,
                             layout_.dialogTextField.right - 14, layout_.dialogTextField.bottom),
                 formatBodyMedium_, dialogText_.empty() ? Color(0x646b78) : Color(0xf0f2f7),
                 DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        } else {
            layout_.dialogTextField = {};
        }

        layout_.dialogCancel = D2D1::RectF(card.left + 24, card.bottom - 66, card.left + 222, card.bottom - 22);
        layout_.dialogConfirm = D2D1::RectF(card.left + 234, card.bottom - 66, card.right - 24, card.bottom - 22);
        DrawSecondaryButton(layout_.dialogCancel, L"CANCEL");
        const float confirmHover = SmoothedHover(layout_.dialogConfirm,
                                                  Contains(layout_.dialogConfirm, mouseX_, mouseY_),
                                                  0x4449414c4f474f4bULL);
        if (destructive) {
            FillRounded(OffsetRectF(layout_.dialogConfirm, 0.0f, -confirmHover), 13,
                        MixColor(Color(0xb94848), Color(0xdf5a5a), confirmHover));
        } else {
            DrawSelectionBlock(OffsetRectF(layout_.dialogConfirm, 0.0f, -confirmHover), 13,
                               0.82f + 0.18f * confirmHover);
        }
        Text(confirm, layout_.dialogConfirm, formatCaption_, Color(0xffffff),
             DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    void DrawToast() {
        if (toast_.empty()) return;
        const auto size = ViewportLogicalSize();
        const float progress = EaseOutBack(MotionProgress(toastStarted_, 520.0f));
        const float width = std::clamp(180.0f + static_cast<float>(toast_.size()) * 4.0f, 280.0f, 430.0f);
        const float offset = (1.0f - progress) * 36.0f;
        const D2D1_RECT_F rect = D2D1::RectF((size.width - width) / 2, size.height - 69 + offset,
                                             (size.width + width) / 2, size.height - 22 + offset);
        DrawElevation(rect, 15, 12.0f, 0.38f);
        FillRounded(rect, 15, Color(0x111a2a, 0.97f));
        DrawGlassHighlight(rect, 15, 0.82f);
        StrokeRounded(rect, 15, Color(SelectionBlueHex(), 0.54f));
        SetBrush(Color(SelectionBlueHex()));
        renderTarget_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rect.left + 24, rect.top + 23.5f), 5, 5), brush_);
        Text(toast_, D2D1::RectF(rect.left + 38, rect.top, rect.right - 18, rect.bottom), formatBodyMedium_,
             Color(0xf4f7fb), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    JayceeLotteryApp app(instance);
    return app.Run(commandShow);
}
