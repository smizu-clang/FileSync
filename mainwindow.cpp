#include "mainwindow.h"

#include "syncengine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {

class ProfileDialog : public QDialog {
public:
    explicit ProfileDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("同期設定"));
        auto* form = new QFormLayout(this);
        m_name = new QLineEdit(this);
        m_source = new QLineEdit(this);
        m_destination = new QLineEdit(this);
        m_mode = new QComboBox(this);
        m_mode->addItem(QStringLiteral("片方向: 同期元 → 同期先"), false);
        m_mode->addItem(QStringLiteral("双方向: 新しいファイルを両側へ同期"), true);
        m_recursive = new QCheckBox(QStringLiteral("下位フォルダも対象にする"), this);
        m_recursive->setChecked(true);
        m_deleteExtras = new QCheckBox(QStringLiteral("同期先だけにある対象ファイルを削除する（ミラー）"), this);
        m_automatic = new QCheckBox(QStringLiteral("定期的に自動同期する"), this);
        m_interval = new QSpinBox(this);
        m_interval->setRange(1, 1440);
        m_interval->setSuffix(QStringLiteral(" 分"));
        m_interval->setValue(30);
        m_extensions = new QLineEdit(this);
        m_extensions->setPlaceholderText(QStringLiteral("例: jpg, png, pdf（空欄はすべて）"));

        form->addRow(QStringLiteral("設定名"), m_name);
        form->addRow(QStringLiteral("同期元"), folderField(m_source));
        form->addRow(QStringLiteral("同期先"), folderField(m_destination));
        form->addRow(QStringLiteral("動作"), m_mode);
        form->addRow(QString(), m_recursive);
        form->addRow(QString(), m_deleteExtras);
        form->addRow(QString(), m_automatic);
        form->addRow(QStringLiteral("自動同期間隔"), m_interval);
        form->addRow(QStringLiteral("拡張子フィルター"), m_extensions);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            m_deleteExtras->setEnabled(index == 0);
        });
    }

    void setProfile(const SyncProfile& profile)
    {
        m_name->setText(profile.name);
        m_source->setText(profile.source);
        m_destination->setText(profile.destination);
        m_mode->setCurrentIndex(profile.twoWay ? 1 : 0);
        m_recursive->setChecked(profile.recursive);
        m_deleteExtras->setChecked(profile.deleteExtras);
        m_deleteExtras->setEnabled(!profile.twoWay);
        m_automatic->setChecked(profile.automatic);
        m_interval->setValue(profile.intervalMinutes);
        m_extensions->setText(profile.extensions);
    }

    SyncProfile profile() const
    {
        SyncProfile result;
        result.name = m_name->text().trimmed();
        result.source = QDir::cleanPath(m_source->text().trimmed());
        result.destination = QDir::cleanPath(m_destination->text().trimmed());
        result.twoWay = m_mode->currentData().toBool();
        result.recursive = m_recursive->isChecked();
        result.deleteExtras = !result.twoWay && m_deleteExtras->isChecked();
        result.automatic = m_automatic->isChecked();
        result.intervalMinutes = m_interval->value();
        result.extensions = m_extensions->text().trimmed();
        return result;
    }

    bool validate()
    {
        const SyncProfile value = profile();
        if (value.name.isEmpty() || value.source.isEmpty() || value.destination.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("入力不足"),
                                 QStringLiteral("設定名、同期元、同期先を入力してください。"));
            return false;
        }
        if (!QFileInfo(value.source).isDir()) {
            QMessageBox::warning(this, QStringLiteral("同期元を確認"),
                                 QStringLiteral("同期元フォルダを選択してください。"));
            return false;
        }
        return true;
    }

    void accept() override
    {
        if (validate())
            QDialog::accept();
    }

private:
    QWidget* folderField(QLineEdit* edit)
    {
        auto* row = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* browse = new QPushButton(QStringLiteral("参照…"), row);
        layout->addWidget(edit, 1);
        layout->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit]() {
            const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("フォルダを選択"), edit->text());
            if (!selected.isEmpty())
                edit->setText(selected);
        });
        return row;
    }

    QLineEdit* m_name = nullptr;
    QLineEdit* m_source = nullptr;
    QLineEdit* m_destination = nullptr;
    QComboBox* m_mode = nullptr;
    QCheckBox* m_recursive = nullptr;
    QCheckBox* m_deleteExtras = nullptr;
    QCheckBox* m_automatic = nullptr;
    QSpinBox* m_interval = nullptr;
    QLineEdit* m_extensions = nullptr;
};

QJsonObject toJson(const SyncProfile& profile)
{
    return {{"name", profile.name}, {"source", profile.source}, {"destination", profile.destination},
            {"twoWay", profile.twoWay}, {"recursive", profile.recursive},
            {"deleteExtras", profile.deleteExtras}, {"automatic", profile.automatic},
            {"intervalMinutes", profile.intervalMinutes}, {"extensions", profile.extensions}};
}

SyncProfile fromJson(const QJsonObject& json)
{
    SyncProfile profile;
    profile.name = json.value("name").toString();
    profile.source = json.value("source").toString();
    profile.destination = json.value("destination").toString();
    profile.twoWay = json.value("twoWay").toBool();
    profile.recursive = json.value("recursive").toBool(true);
    profile.deleteExtras = json.value("deleteExtras").toBool();
    profile.automatic = json.value("automatic").toBool();
    profile.intervalMinutes = json.value("intervalMinutes").toInt(30);
    profile.extensions = json.value("extensions").toString();
    return profile;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_watcher(new QFutureWatcher<QStringList>(this))
{
    setWindowTitle(QStringLiteral("FileSync"));
    resize(1050, 650);

    m_toolbar = addToolBar(QStringLiteral("同期"));
    m_toolbar->addAction(QStringLiteral("追加"), this, &MainWindow::addProfile);
    m_toolbar->addAction(QStringLiteral("編集"), this, &MainWindow::editProfile);
    m_toolbar->addAction(QStringLiteral("削除"), this, &MainWindow::removeProfile);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("選択を同期"), this, &MainWindow::runSelected);
    m_toolbar->addAction(QStringLiteral("すべて同期"), this, &MainWindow::runAll);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    m_table = new QTableWidget(0, 5, central);
    m_table->setHorizontalHeaderLabels({QStringLiteral("設定名"), QStringLiteral("動作"),
                                        QStringLiteral("同期元"), QStringLiteral("同期先"),
                                        QStringLiteral("自動")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &MainWindow::editProfile);
    layout->addWidget(m_table, 3);

    layout->addWidget(new QLabel(QStringLiteral("実行ログ"), central));
    m_log = new QPlainTextEdit(central);
    m_log->setReadOnly(true);
    layout->addWidget(m_log, 2);
    setCentralWidget(central);

    loadProfiles();
    refreshTable();
    connect(m_watcher, &QFutureWatcher<QStringList>::finished, this, &MainWindow::syncFinished);

    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(60 * 1000);
    connect(m_autoTimer, &QTimer::timeout, this, &MainWindow::runAutomaticProfiles);
    m_autoTimer->start();
}

void MainWindow::loadProfiles()
{
    QSettings settings;
    const QJsonArray array = QJsonDocument::fromJson(settings.value("profiles").toByteArray()).array();
    for (const QJsonValue& value : array)
        m_profiles.push_back(fromJson(value.toObject()));
    m_lastAutomaticRun.resize(m_profiles.size());
}

void MainWindow::saveProfiles() const
{
    QJsonArray array;
    for (const SyncProfile& profile : m_profiles)
        array.append(toJson(profile));
    QSettings settings;
    settings.setValue("profiles", QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void MainWindow::refreshTable()
{
    m_table->setRowCount(m_profiles.size());
    for (int row = 0; row < m_profiles.size(); ++row) {
        const SyncProfile& profile = m_profiles[row];
        m_table->setItem(row, 0, new QTableWidgetItem(profile.name));
        m_table->setItem(row, 1, new QTableWidgetItem(profile.twoWay ? QStringLiteral("双方向") : QStringLiteral("片方向")));
        m_table->setItem(row, 2, new QTableWidgetItem(profile.source));
        m_table->setItem(row, 3, new QTableWidgetItem(profile.destination));
        m_table->setItem(row, 4, new QTableWidgetItem(profile.automatic ? QStringLiteral("有効") : QStringLiteral("無効")));
    }
}

void MainWindow::addProfile()
{
    ProfileDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_profiles.push_back(dialog.profile());
    m_lastAutomaticRun.push_back(QDateTime());
    saveProfiles();
    refreshTable();
}

void MainWindow::editProfile()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_profiles.size())
        return;
    ProfileDialog dialog(this);
    dialog.setProfile(m_profiles[row]);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_profiles[row] = dialog.profile();
    saveProfiles();
    refreshTable();
}

void MainWindow::removeProfile()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_profiles.size())
        return;
    if (QMessageBox::question(this, QStringLiteral("設定を削除"),
                              QStringLiteral("選択した同期設定を削除しますか？")) != QMessageBox::Yes)
        return;
    m_profiles.removeAt(row);
    m_lastAutomaticRun.removeAt(row);
    saveProfiles();
    refreshTable();
}

QVector<SyncProfile> MainWindow::selectedProfiles() const
{
    QVector<SyncProfile> selected;
    QSet<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows())
        rows.insert(index.row());
    for (int row : rows)
        if (row >= 0 && row < m_profiles.size())
            selected.push_back(m_profiles[row]);
    return selected;
}

void MainWindow::runAll()
{
    runProfiles(m_profiles, false);
}

void MainWindow::runSelected()
{
    const QVector<SyncProfile> selected = selectedProfiles();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("同期設定"), QStringLiteral("同期する設定を選択してください。"));
        return;
    }
    runProfiles(selected, false);
}

void MainWindow::runAutomaticProfiles()
{
    if (m_watcher->isRunning())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    QVector<SyncProfile> due;
    for (int i = 0; i < m_profiles.size(); ++i) {
        const SyncProfile& profile = m_profiles[i];
        if (!profile.automatic)
            continue;
        if (!m_lastAutomaticRun[i].isValid()
            || m_lastAutomaticRun[i].secsTo(now) >= profile.intervalMinutes * 60) {
            due.push_back(profile);
            m_lastAutomaticRun[i] = now;
        }
    }
    if (!due.isEmpty())
        runProfiles(due, true);
}

void MainWindow::runProfiles(const QVector<SyncProfile>& profiles, bool automatic)
{
    if (m_watcher->isRunning())
        return;
    if (!automatic) {
        for (const SyncProfile& profile : profiles) {
            if (profile.deleteExtras
                && QMessageBox::warning(this, QStringLiteral("削除を含む同期"),
                    QStringLiteral("「%1」は同期先だけにある対象ファイルを削除します。続行しますか？")
                        .arg(profile.name), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
                    != QMessageBox::Yes)
                return;
        }
    }
    m_log->appendPlainText(QStringLiteral("--- %1 ---").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    m_table->setEnabled(false);
    m_toolbar->setEnabled(false);
    const QVector<SyncProfile> jobs = profiles;
    m_watcher->setFuture(QtConcurrent::run([jobs]() {
        QStringList lines;
        for (const SyncProfile& profile : jobs)
            lines.append(SyncEngine::run(profile));
        return lines;
    }));
}

void MainWindow::syncFinished()
{
    m_log->appendPlainText(m_watcher->result().join('\n'));
    m_table->setEnabled(true);
    m_toolbar->setEnabled(true);
}
