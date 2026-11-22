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
    void openAppearanceSettings();
    void plansReady();
    void syncFinished();

private:
    void loadProfiles();
    void saveProfiles() const;
    void refreshTable();
    void runProfiles(const QVector<SyncProfile>& profiles, bool automatic);
    void executePendingPlans();
    void setBusy(bool busy);
    void applyTheme(bool dark);
    QVector<SyncProfile> selectedProfiles() const;

    QVector<SyncProfile> m_profiles;
    QVector<QDateTime> m_lastAutomaticRun;
    QVector<SyncPlan> m_pendingPlans;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QToolBar* m_toolbar = nullptr;
    QTimer* m_autoTimer = nullptr;
    QFutureWatcher<QVector<SyncPlan>>* m_planWatcher = nullptr;
    QFutureWatcher<QStringList>* m_runWatcher = nullptr;
    bool m_pendingAutomatic = false;
};
