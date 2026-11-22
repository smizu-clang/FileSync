#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>

enum class SyncMode {
    SourceToDestination,
    DestinationToSource,
    Bidirectional,
    DeleteDuplicates
};

enum class SyncActionKind {
    Copy,
    Delete,
    Conflict,
    Error
};

struct SyncProfile {
    QString name;
    QString source;
    QString destination;
    SyncMode mode = SyncMode::SourceToDestination;
    bool recursive = true;
    bool deleteExtras = false;
    bool automatic = false;
    int intervalMinutes = 30;
    QString extensions;
};

struct SyncAction {
    SyncActionKind kind = SyncActionKind::Error;
    QString source;
    QString destination;
    QString message;
    QByteArray expectedSourceDigest;
    QByteArray expectedDestinationDigest;
};

struct SyncPlan {
    QString profileName;
    SyncMode mode = SyncMode::SourceToDestination;
    QVector<SyncAction> actions;
};

class SyncEngine {
public:
    static SyncPlan plan(const SyncProfile& profile);
    static QStringList execute(const SyncPlan& plan);
    static QString modeName(SyncMode mode);
    static QString actionName(SyncActionKind kind);
    static QString logLine(const SyncAction& action);
};
