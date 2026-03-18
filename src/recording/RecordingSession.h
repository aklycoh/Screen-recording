#pragma once

#include "common/OperationResult.h"
#include "core/RecordingTypes.h"

#include <QObject>
#include <memory>

class IRecordingBackend;

class RecordingSession : public QObject
{
    Q_OBJECT

public:
    explicit RecordingSession(QObject* parent = nullptr);
    ~RecordingSession() override;

    OperationResult start(const RecordingOptions& options);
    OperationResult stop();

    RecordingState state() const;
    RecordingOptions lastOptions() const;

signals:
    void stateChanged(RecordingState state, const QString& detail);

private:
    std::unique_ptr<IRecordingBackend> createBackend();
    void setState(RecordingState state, const QString& detail);
    void resetBackend();

    RecordingState state_ {RecordingState::Idle};
    RecordingOptions lastOptions_;
    std::unique_ptr<IRecordingBackend> backend_;
};
