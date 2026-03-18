#pragma once

#include "common/OperationResult.h"
#include "core/RecordingTypes.h"

#include <functional>

class IRecordingBackend
{
public:
    using LogCallback = std::function<void(const QString&)>;
    using StartedCallback = std::function<void()>;
    using FinishedCallback = std::function<void(const OperationResult&)>;

    virtual ~IRecordingBackend() = default;

    virtual void setLogCallback(LogCallback callback) = 0;
    virtual void setStartedCallback(StartedCallback callback) = 0;
    virtual void setFinishedCallback(FinishedCallback callback) = 0;

    virtual OperationResult start(const RecordingOptions& options) = 0;
    virtual OperationResult requestStop() = 0;
};
