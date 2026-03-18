#pragma once

#include <QString>
#include <utility>

struct OperationResult
{
    bool ok {false};
    QString message;

    static OperationResult success(QString message = {})
    {
        return {true, std::move(message)};
    }

    static OperationResult failure(QString message)
    {
        return {false, std::move(message)};
    }
};
