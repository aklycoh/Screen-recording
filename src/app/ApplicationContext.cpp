#include "app/ApplicationContext.h"

ApplicationContext::ApplicationContext()
    : windowEnumerator_()
    , monitorEnumerator_()
    , recordingController_(windowEnumerator_, monitorEnumerator_)
{
}

RecordingController& ApplicationContext::recordingController()
{
    return recordingController_;
}
