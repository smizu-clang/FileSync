#pragma once

#include "syncengine.h"

#include <QDateTime>
#include <QMainWindow>
#include <QVector>

template <typename T>
class QFutureWatcher;
class QTableWidget;
class QPlainTextEdit;
class QToolBar;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void addProfile();
    void editProfile();
    void removeProfile();
    void runAll();
    void runSelected();
    void runAutomaticProfiles();
    void syncFinished();

private:
    void loadProfiles();
    void saveProfiles() const;
    void refreshTable();
    void runProfiles(const QVector<SyncProfile>& profiles, bool automatic);
    QVector<SyncProfile> selectedProfiles() const;

    QVector<SyncProfile> m_profiles;
    QVector<QDateTime> m_lastAutomaticRun;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QToolBar* m_toolbar = nullptr;
    QTimer* m_autoTimer = nullptr;
    QFutureWatcher<QStringList>* m_watcher = nullptr;
};
