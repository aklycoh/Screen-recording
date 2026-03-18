#include "platform/windows/WindowEnumerator.h"

#include <Windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <string>

namespace
{
struct EnumContext
{
    QList<WindowInfo> windows;
};

bool isWindowCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    const HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

bool isTopLevelCandidate(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) {
        return false;
    }

    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }

    if (isWindowCloaked(hwnd)) {
        return false;
    }

    return true;
}

QString windowTitle(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }

    std::wstring buffer(static_cast<std::size_t>(length) + 1U, L'\0');
    GetWindowTextW(hwnd, buffer.data(), length + 1);
    return QString::fromWCharArray(buffer.c_str()).trimmed();
}

QString className(HWND hwnd)
{
    constexpr int kBufferSize = 256;
    wchar_t buffer[kBufferSize] {};
    const int written = GetClassNameW(hwnd, buffer, kBufferSize);
    if (written <= 0) {
        return {};
    }
    return QString::fromWCharArray(buffer, written);
}

QSize windowSize(HWND hwnd)
{
    RECT rect {};
    if (!GetWindowRect(hwnd, &rect)) {
        return {};
    }

    return {rect.right - rect.left, rect.bottom - rect.top};
}

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* context = reinterpret_cast<EnumContext*>(lParam);
    if (!context || !isTopLevelCandidate(hwnd)) {
        return TRUE;
    }

    const QString title = windowTitle(hwnd);
    if (title.isEmpty()) {
        return TRUE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    WindowInfo info;
    info.nativeHandle = reinterpret_cast<quintptr>(hwnd);
    info.title = title;
    info.className = className(hwnd);
    info.processId = processId;
    info.windowSize = windowSize(hwnd);
    info.minimized = IsIconic(hwnd) != FALSE;

    context->windows.push_back(info);
    return TRUE;
}
}

QList<WindowInfo> WindowEnumerator::enumerateWindows() const
{
    EnumContext context;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&context));
    const DWORD currentProcessId = GetCurrentProcessId();

    context.windows.erase(std::remove_if(context.windows.begin(), context.windows.end(), [currentProcessId](const WindowInfo& info) {
        return info.processId == currentProcessId;
    }), context.windows.end());

    std::sort(context.windows.begin(), context.windows.end(), [](const WindowInfo& left, const WindowInfo& right) {
        return left.title.localeAwareCompare(right.title) < 0;
    });

    return context.windows;
}
