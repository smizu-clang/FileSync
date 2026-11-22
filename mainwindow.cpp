#include "mainwindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {

bool isOneWay(SyncMode mode)
{
    return mode == SyncMode::SourceToDestination || mode == SyncMode::DestinationToSource;
}

class AppearanceDialog : public QDialog {
public:
    explicit AppearanceDialog(bool dark, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("外観設定"));
        auto* layout = new QVBoxLayout(this);
        auto* heading = new QLabel(QStringLiteral("アプリのテーマ"), this);
        heading->setObjectName(QStringLiteral("dialogHeading"));
        layout->addWidget(heading);
        layout->addWidget(new QLabel(QStringLiteral("ライトまたはダークの配色を選択します。"), this));
        m_theme = new QComboBox(this);
        m_theme->addItem(QStringLiteral("ライト"), false);
        m_theme->addItem(QStringLiteral("ダーク"), true);
        m_theme->setCurrentIndex(dark ? 1 : 0);
        layout->addWidget(m_theme);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    bool darkTheme() const { return m_theme->currentData().toBool(); }

private:
    QComboBox* m_theme = nullptr;
};

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
        m_mode->addItem(QStringLiteral("更新元 → 更新先"), static_cast<int>(SyncMode::SourceToDestination));
        m_mode->addItem(QStringLiteral("更新元 ← 更新先"), static_cast<int>(SyncMode::DestinationToSource));
        m_mode->addItem(QStringLiteral("更新元 ←→ 更新先"), static_cast<int>(SyncMode::Bidirectional));
        m_mode->addItem(QStringLiteral("フォルダー内の重複ファイルを削除"), static_cast<int>(SyncMode::DeleteDuplicates));
        m_sourceLabel = new QLabel(QStringLiteral("更新元フォルダー"), this);
        m_destinationLabel = new QLabel(QStringLiteral("更新先フォルダー"), this);
        m_sourceField = folderField(m_source);
        m_destinationField = folderField(m_destination);
        m_recursive = new QCheckBox(QStringLiteral("下位フォルダーも対象にする"), this);
        m_recursive->setChecked(true);
        m_deleteExtras = new QCheckBox(this);
        m_automatic = new QCheckBox(QStringLiteral("定期的に自動同期する"), this);
        m_interval = new QSpinBox(this);
        m_interval->setRange(1, 1440);
        m_interval->setSuffix(QStringLiteral(" 分"));
        m_interval->setValue(30);
        m_extensions = new QLineEdit(this);
        m_extensions->setPlaceholderText(QStringLiteral("例: jpg, png, pdf（空欄はすべて）"));

        form->addRow(QStringLiteral("設定名"), m_name);
        form->addRow(QStringLiteral("動作"), m_mode);
        form->addRow(m_sourceLabel, m_sourceField);
        form->addRow(m_destinationLabel, m_destinationField);
        form->addRow(QString(), m_recursive);
        form->addRow(QString(), m_deleteExtras);
        form->addRow(QString(), m_automatic);
        form->addRow(QStringLiteral("自動同期間隔"), m_interval);
        form->addRow(QStringLiteral("拡張子フィルター"), m_extensions);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &ProfileDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int) { updateModeControls(); });
        updateModeControls();
    }

    void setProfile(const SyncProfile& profile)
    {
        m_name->setText(profile.name);
        m_source->setText(profile.source);
        m_destination->setText(profile.destination);
        const int modeIndex = m_mode->findData(static_cast<int>(profile.mode));
        m_mode->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
        m_recursive->setChecked(profile.recursive);
        m_deleteExtras->setChecked(profile.deleteExtras);
        m_automatic->setChecked(profile.automatic);
        m_interval->setValue(profile.intervalMinutes);
        m_extensions->setText(profile.extensions);
        updateModeControls();
    }

    SyncProfile profile() const
    {
        SyncProfile result;
        result.name = m_name->text().trimmed();
        result.mode = static_cast<SyncMode>(m_mode->currentData().toInt());
        result.source = QDir::cleanPath(m_source->text().trimmed());
        result.destination = result.mode == SyncMode::DeleteDuplicates
            ? QString() : QDir::cleanPath(m_destination->text().trimmed());
        result.recursive = m_recursive->isChecked();
        result.deleteExtras = isOneWay(result.mode) && m_deleteExtras->isChecked();
        result.automatic = m_automatic->isChecked();
        result.intervalMinutes = m_interval->value();
        result.extensions = m_extensions->text().trimmed();
        return result;
    }

    void accept() override
    {
        const SyncProfile value = profile();
        if (value.name.isEmpty() || value.source.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("入力不足"),
                                 QStringLiteral("設定名と対象フォルダーを入力してください。"));
            return;
        }
        if (!QFileInfo(value.source).isDir()) {
            QMessageBox::warning(this, QStringLiteral("フォルダーを確認"),
                                 QStringLiteral("更新元または対象フォルダーを選択してください。"));
            return;
        }
        if (value.mode != SyncMode::DeleteDuplicates && value.destination.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("入力不足"), QStringLiteral("更新先フォルダーを選択してください。"));
            return;
        }
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
            const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("フォルダーを選択"), edit->text());
            if (!selected.isEmpty())
                edit->setText(selected);
        });
        return row;
    }

    void updateModeControls()
    {
        const SyncMode mode = static_cast<SyncMode>(m_mode->currentData().toInt());
        const bool duplicates = mode == SyncMode::DeleteDuplicates;
        m_sourceLabel->setText(duplicates ? QStringLiteral("対象フォルダー") : QStringLiteral("更新元フォルダー"));
        m_destinationLabel->setVisible(!duplicates);
        m_destinationField->setVisible(!duplicates);
        m_deleteExtras->setEnabled(isOneWay(mode));
        m_deleteExtras->setText(mode == SyncMode::DestinationToSource
            ? QStringLiteral("更新元だけにある対象ファイルを削除する（ミラー）")
            : QStringLiteral("更新先だけにある対象ファイルを削除する（ミラー）"));
        m_recursive->setText(duplicates ? QStringLiteral("下位フォルダーも重複判定する")
                                        : QStringLiteral("下位フォルダーも対象にする"));
    }

    QLineEdit* m_name = nullptr;
    QLineEdit* m_source = nullptr;
    QLineEdit* m_destination = nullptr;
    QComboBox* m_mode = nullptr;
    QLabel* m_sourceLabel = nullptr;
    QLabel* m_destinationLabel = nullptr;
    QWidget* m_sourceField = nullptr;
    QWidget* m_destinationField = nullptr;
    QCheckBox* m_recursive = nullptr;
    QCheckBox* m_deleteExtras = nullptr;
    QCheckBox* m_automatic = nullptr;
    QSpinBox* m_interval = nullptr;
    QLineEdit* m_extensions = nullptr;
};

class PreviewDialog : public QDialog {
public:
    PreviewDialog(const QVector<SyncPlan>& plans, QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("同期内容の確認"));
        resize(1000, 520);
        auto* layout = new QVBoxLayout(this);
        int copies = 0;
        int deletions = 0;
        int issues = 0;
        int rows = 0;
        for (const SyncPlan& plan : plans) {
            rows += plan.actions.size();
            for (const SyncAction& action : plan.actions) {
                copies += action.kind == SyncActionKind::Copy;
                deletions += action.kind == SyncActionKind::Delete;
                issues += action.kind == SyncActionKind::Error || action.kind == SyncActionKind::Conflict;
            }
        }
        auto* summary = new QLabel(QStringLiteral("コピー/更新: %1　削除: %2　競合/エラー: %3")
                                   .arg(copies).arg(deletions).arg(issues), this);
        layout->addWidget(summary);

        auto* table = new QTableWidget(rows, 5, this);
        table->setHorizontalHeaderLabels({QStringLiteral("設定"), QStringLiteral("処理"),
                                          QStringLiteral("更新元 / 残すファイル"),
                                          QStringLiteral("更新先 / 削除対象"), QStringLiteral("詳細")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        int row = 0;
        for (const SyncPlan& plan : plans) {
            for (const SyncAction& action : plan.actions) {
                table->setItem(row, 0, new QTableWidgetItem(plan.profileName));
                table->setItem(row, 1, new QTableWidgetItem(SyncEngine::actionName(action.kind)));
                table->setItem(row, 2, new QTableWidgetItem(action.source.isEmpty() ? QStringLiteral("—") : action.source));
                table->setItem(row, 3, new QTableWidgetItem(action.destination.isEmpty() ? QStringLiteral("—") : action.destination));
                table->setItem(row, 4, new QTableWidgetItem(action.message));
                ++row;
            }
        }
        layout->addWidget(table, 1);

        auto* buttons = new QDialogButtonBox(this);
        auto* cancel = buttons->addButton(QStringLiteral("キャンセル"), QDialogButtonBox::RejectRole);
        auto* apply = buttons->addButton(deletions ? QStringLiteral("削除を含めて実行") : QStringLiteral("実行"),
                                         QDialogButtonBox::AcceptRole);
        apply->setEnabled(copies + deletions > 0);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(apply, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(buttons);
    }
};

QJsonObject toJson(const SyncProfile& profile)
{
    return {{"name", profile.name}, {"source", profile.source}, {"destination", profile.destination},
            {"mode", static_cast<int>(profile.mode)}, {"recursive", profile.recursive},
            {"deleteExtras", profile.deleteExtras}, {"automatic", profile.automatic},
            {"intervalMinutes", profile.intervalMinutes}, {"extensions", profile.extensions}};
}

SyncProfile fromJson(const QJsonObject& json)
{
    SyncProfile profile;
    profile.name = json.value("name").toString();
    profile.source = json.value("source").toString();
    profile.destination = json.value("destination").toString();
    if (json.contains("mode")) {
        const int value = json.value("mode").toInt();
        if (value >= static_cast<int>(SyncMode::SourceToDestination)
            && value <= static_cast<int>(SyncMode::DeleteDuplicates))
            profile.mode = static_cast<SyncMode>(value);
    } else if (json.value("twoWay").toBool()) {
        profile.mode = SyncMode::Bidirectional;
    }
    profile.recursive = json.value("recursive").toBool(true);
    profile.deleteExtras = json.value("deleteExtras").toBool();
    profile.automatic = json.value("automatic").toBool();
    profile.intervalMinutes = json.value("intervalMinutes").toInt(30);
    profile.extensions = json.value("extensions").toString();
    return profile;
}

QString actionTimestamp()
{
    return QStringLiteral("[%1]").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_planWatcher(new QFutureWatcher<QVector<SyncPlan>>(this)),
      m_runWatcher(new QFutureWatcher<QStringList>(this))
{
    setWindowTitle(QStringLiteral("FileSync"));
    resize(1120, 700);
    QSettings settings;
    applyTheme(settings.value("appearance/dark", false).toBool());

    m_toolbar = addToolBar(QStringLiteral("同期"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->addAction(QStringLiteral("追加"), this, &MainWindow::addProfile);
    m_toolbar->addAction(QStringLiteral("編集"), this, &MainWindow::editProfile);
    m_toolbar->addAction(QStringLiteral("削除"), this, &MainWindow::removeProfile);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("選択を同期"), this, &MainWindow::runSelected);
    m_toolbar->addAction(QStringLiteral("すべて同期"), this, &MainWindow::runAll);
    m_toolbar->addSeparator();
    m_toolbar->addAction(QStringLiteral("外観"), this, &MainWindow::openAppearanceSettings);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(14);
    auto* profileHeading = new QLabel(QStringLiteral("同期プロファイル"), central);
    profileHeading->setObjectName(QStringLiteral("sectionHeading"));
    layout->addWidget(profileHeading);
    m_table = new QTableWidget(0, 5, central);
    m_table->setObjectName(QStringLiteral("profileTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("設定名"), QStringLiteral("同期の種類"),
                                        QStringLiteral("更新元"), QStringLiteral("更新先"),
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

    auto* logHeading = new QLabel(QStringLiteral("アクティビティ"), central);
    logHeading->setObjectName(QStringLiteral("sectionHeading"));
    layout->addWidget(logHeading);
    m_log = new QPlainTextEdit(central);
    m_log->setObjectName(QStringLiteral("activityLog"));
    m_log->setReadOnly(true);
    layout->addWidget(m_log, 2);
    setCentralWidget(central);

    loadProfiles();
    refreshTable();
    connect(m_planWatcher, &QFutureWatcher<QVector<SyncPlan>>::finished, this, &MainWindow::plansReady);
    connect(m_runWatcher, &QFutureWatcher<QStringList>::finished, this, &MainWindow::syncFinished);

    m_autoTimer = new QTimer(this);
    m_autoTimer->setInterval(60 * 1000);
    connect(m_autoTimer, &QTimer::timeout, this, &MainWindow::runAutomaticProfiles);
    m_autoTimer->start();
}

void MainWindow::applyTheme(bool dark)
{
    const QString window = dark ? QStringLiteral("#111827") : QStringLiteral("#eaf2fb");
    const QString surface = dark ? QStringLiteral("rgba(31, 42, 61, 225)") : QStringLiteral("rgba(255, 255, 255, 222)");
    const QString raised = dark ? QStringLiteral("rgba(43, 57, 79, 238)") : QStringLiteral("rgba(255, 255, 255, 245)");
    const QString text = dark ? QStringLiteral("#edf4ff") : QStringLiteral("#203047");
    const QString muted = dark ? QStringLiteral("#a9bad1") : QStringLiteral("#6b7d95");
    const QString border = dark ? QStringLiteral("rgba(183, 204, 235, 38)") : QStringLiteral("rgba(78, 112, 158, 38)");
    const QString accent = dark ? QStringLiteral("#72aaff") : QStringLiteral("#3979d8");
    const QString hover = dark ? QStringLiteral("rgba(114, 170, 255, 28)") : QStringLiteral("rgba(57, 121, 216, 18)");
    const QString selection = dark ? QStringLiteral("#345b91") : QStringLiteral("#cfe2ff");

    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor(window));
    palette.setColor(QPalette::WindowText, QColor(text));
    palette.setColor(QPalette::Base, QColor(dark ? QStringLiteral("#182235") : QStringLiteral("#f8fbff")));
    palette.setColor(QPalette::AlternateBase, QColor(dark ? QStringLiteral("#1d2a40") : QStringLiteral("#f0f6ff")));
    palette.setColor(QPalette::Text, QColor(text));
    palette.setColor(QPalette::Button, QColor(dark ? QStringLiteral("#263750") : QStringLiteral("#f7faff")));
    palette.setColor(QPalette::ButtonText, QColor(text));
    palette.setColor(QPalette::Highlight, QColor(selection));
    palette.setColor(QPalette::HighlightedText, QColor(text));
    QApplication::setPalette(palette);

    const QString style = QStringLiteral(
        "QMainWindow { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2); }"
        "QWidget { color:%3; font-family:'Noto Sans'; font-size:10pt; }"
        "QToolBar { background:%4; border:1px solid %5; border-radius:14px; spacing:7px; padding:8px 12px; margin:12px 16px 0 16px; }"
        "QToolButton { background:transparent; border:1px solid transparent; border-radius:9px; padding:8px 12px; }"
        "QToolButton:hover { background:%6; border-color:%5; }"
        "QLabel#sectionHeading, QLabel#dialogHeading { font-size:15pt; font-weight:650; padding:2px 1px; }"
        "QTableWidget, QPlainTextEdit { background:%4; alternate-background-color:%7; border:1px solid %5; border-radius:16px; padding:8px; selection-background-color:%8; selection-color:%3; }"
        "QHeaderView::section { background:%9; color:%10; border:0; border-bottom:1px solid %5; padding:10px 9px; font-weight:600; }"
        "QTableWidget::item { padding:7px; border:0; }"
        "QTableWidget::item:hover { background:%6; }"
        "QLineEdit, QComboBox, QSpinBox { background:%9; border:1px solid %5; border-radius:9px; padding:8px 10px; selection-background-color:%8; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border:1px solid %11; }"
        "QPushButton { background:%9; border:1px solid %5; border-radius:9px; padding:8px 14px; }"
        "QPushButton:hover { background:%6; border-color:%11; }"
        "QPushButton:default { background:%11; color:white; border-color:%11; font-weight:600; }"
        "QCheckBox { spacing:8px; } QCheckBox::indicator { width:17px; height:17px; }"
        "QDialog { background:%1; } QDialogButtonBox { border:0; }"
        "QScrollBar:vertical { background:transparent; width:10px; margin:4px; }"
        "QScrollBar::handle:vertical { background:%5; border-radius:5px; min-height:24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
    ).arg(window)
     .arg(dark ? QStringLiteral("#243653") : QStringLiteral("#d9e9fb"))
     .arg(text).arg(surface).arg(border).arg(hover).arg(raised).arg(selection)
     .arg(raised).arg(muted).arg(accent);
    qApp->setStyleSheet(style);
}

void MainWindow::openAppearanceSettings()
{
    QSettings settings;
    AppearanceDialog dialog(settings.value("appearance/dark", false).toBool(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue("appearance/dark", dialog.darkTheme());
    applyTheme(dialog.darkTheme());
    m_log->appendPlainText(QStringLiteral("%1 外観を%2テーマに変更しました。")
                           .arg(actionTimestamp(), dialog.darkTheme() ? QStringLiteral("ダーク") : QStringLiteral("ライト")));
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
        m_table->setItem(row, 1, new QTableWidgetItem(SyncEngine::modeName(profile.mode)));
        m_table->setItem(row, 2, new QTableWidgetItem(profile.source));
        m_table->setItem(row, 3, new QTableWidgetItem(profile.mode == SyncMode::DeleteDuplicates
                                                        ? QStringLiteral("—") : profile.destination));
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
    if (m_planWatcher->isRunning() || m_runWatcher->isRunning() || !m_table->isEnabled())
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
    if (m_planWatcher->isRunning() || m_runWatcher->isRunning())
        return;
    m_pendingAutomatic = automatic;
    m_log->appendPlainText(QStringLiteral("%1 同期内容を確認中…").arg(actionTimestamp()));
    setBusy(true);
    const QVector<SyncProfile> jobs = profiles;
    m_planWatcher->setFuture(QtConcurrent::run([jobs]() {
        QVector<SyncPlan> plans;
        plans.reserve(jobs.size());
        for (const SyncProfile& profile : jobs)
            plans.push_back(SyncEngine::plan(profile));
        return plans;
    }));
}

void MainWindow::plansReady()
{
    m_pendingPlans = m_planWatcher->result();
    int plannedActions = 0;
    for (const SyncPlan& plan : m_pendingPlans) {
        m_log->appendPlainText(QStringLiteral("%1 %2: %3").arg(actionTimestamp(), plan.profileName,
                                             SyncEngine::modeName(plan.mode)));
        for (const SyncAction& action : plan.actions) {
            if (action.kind == SyncActionKind::Error || action.kind == SyncActionKind::Conflict)
                m_log->appendPlainText(QStringLiteral("%1 %2").arg(actionTimestamp(), SyncEngine::logLine(action)));
            plannedActions += action.kind == SyncActionKind::Copy || action.kind == SyncActionKind::Delete;
        }
        if (plan.actions.isEmpty())
            m_log->appendPlainText(QStringLiteral("%1 変更対象はありません").arg(actionTimestamp()));
    }

    if (plannedActions == 0) {
        setBusy(false);
        return;
    }
    if (m_pendingAutomatic) {
        executePendingPlans();
        return;
    }

    PreviewDialog preview(m_pendingPlans, this);
    if (preview.exec() == QDialog::Accepted) {
        executePendingPlans();
    } else {
        m_log->appendPlainText(QStringLiteral("%1 ユーザーが同期をキャンセルしました").arg(actionTimestamp()));
        setBusy(false);
    }
}

void MainWindow::executePendingPlans()
{
    const QVector<SyncPlan> plans = m_pendingPlans;
    m_runWatcher->setFuture(QtConcurrent::run([plans]() {
        QStringList lines;
        for (const SyncPlan& plan : plans)
            lines.append(SyncEngine::execute(plan));
        return lines;
    }));
}

void MainWindow::syncFinished()
{
    m_log->appendPlainText(m_runWatcher->result().join('\n'));
    setBusy(false);
}

void MainWindow::setBusy(bool busy)
{
    m_table->setEnabled(!busy);
    m_toolbar->setEnabled(!busy);
}
