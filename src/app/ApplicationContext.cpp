#include "app/ApplicationContext.h"

ApplicationContext::ApplicationContext()
    : windowEnumerator_()
    , recordingController_(windowEnumerator_)
{
}

RecordingController& ApplicationContext::recordingController()
{
    return recordingController_;
}

