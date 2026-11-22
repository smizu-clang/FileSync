#pragma once

#include <QString>
#include <QStringList>

struct SyncProfile {
    QString name;
    QString source;
    QString destination;
    bool twoWay = false;
    bool recursive = true;
    bool deleteExtras = false;
    bool automatic = false;
    int intervalMinutes = 30;
    QString extensions;
};

class SyncEngine {
public:
    static QStringList run(const SyncProfile& profile);
};
