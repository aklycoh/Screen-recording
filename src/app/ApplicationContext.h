#pragma once

#include "platform/windows/MonitorEnumerator.h"
#include "platform/windows/WindowEnumerator.h"
#include "recording/RecordingController.h"

class ApplicationContext
{
public:
    ApplicationContext();

    RecordingController& recordingController();

private:
    WindowEnumerator windowEnumerator_;
    MonitorEnumerator monitorEnumerator_;
    RecordingController recordingController_;
};
