#pragma once

#include "core/RecordingTypes.h"

#include <QList>

class WindowEnumerator
{
public:
    QList<WindowInfo> enumerateWindows() const;
};

