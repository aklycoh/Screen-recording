#pragma once

#include "core/RecordingTypes.h"

#include <QList>

class MonitorEnumerator
{
public:
    QList<DisplayInfo> enumerateMonitors() const;
};
