#include "platform/windows/MonitorEnumerator.h"

#include <Windows.h>
#include <ShellScalingApi.h>

#include <algorithm>

namespace
{
struct EnumContext
{
    QList<DisplayInfo> displays;
};

QString formatDisplayName(int index, bool primary)
{
    if (primary) {
        return QStringLiteral("Display %1 (Primary)").arg(index);
    }
    return QStringLiteral("Display %1").arg(index);
}

double queryMonitorScale(HMONITOR monitor)
{
    UINT dpiX = 96;
    UINT dpiY = 96;
    const HRESULT hr = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (FAILED(hr) || dpiX == 0) {
        return 1.0;
    }

    return static_cast<double>(dpiX) / 96.0;
}

BOOL CALLBACK enumMonitorsProc(HMONITOR monitor, HDC, LPRECT, LPARAM lParam)
{
    auto* context = reinterpret_cast<EnumContext*>(lParam);
    if (!context) {
        return FALSE;
    }

    MONITORINFOEXW monitorInfo {};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return TRUE;
    }

    const int width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    const int height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    const bool primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;

    DisplayInfo info;
    info.nativeHandle = reinterpret_cast<quintptr>(monitor);
    info.deviceName = QString::fromWCharArray(monitorInfo.szDevice);
    info.pixelSize = QSize(width, height);
    info.primary = primary;
    info.dpiScale = queryMonitorScale(monitor);
    context->displays.push_back(std::move(info));
    return TRUE;
}
}

QList<DisplayInfo> MonitorEnumerator::enumerateMonitors() const
{
    EnumContext context;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&context));

    std::sort(context.displays.begin(), context.displays.end(), [](const DisplayInfo& left, const DisplayInfo& right) {
        if (left.primary != right.primary) {
            return left.primary;
        }
        return left.deviceName.localeAwareCompare(right.deviceName) < 0;
    });

    for (int index = 0; index < context.displays.size(); ++index) {
        auto& display = context.displays[index];
        display.name = formatDisplayName(index + 1, display.primary);
    }

    return context.displays;
}
