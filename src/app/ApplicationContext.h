#pragma once

#include "platform/windows/WindowEnumerator.h"
#include "recording/RecordingController.h"

class ApplicationContext
{
public:
    ApplicationContext();

    RecordingController& recordingController();

private:
    WindowEnumerator windowEnumerator_;
    RecordingController recordingController_;
};

