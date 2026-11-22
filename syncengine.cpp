import std;

#include "syncengine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

using FileMap = std::map<QString, QFileInfo>;

QString normalizedPath(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

bool isWithin(const QString& child, const QString& parent)
{
    const QString cleanParent = QDir::cleanPath(parent);
    const QString cleanChild = QDir::cleanPath(child);
    return cleanChild == cleanParent || cleanChild.startsWith(cleanParent + QDir::separator());
}

QStringList extensionFilters(const QString& text)
{
    QStringList filters = text.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    for (QString& filter : filters) {
        filter = filter.trimmed().toLower();
        if (filter.startsWith("*."))
            filter.remove(0, 2);
        else if (filter.startsWith('.'))
            filter.remove(0, 1);
    }
    return filters;
}

bool matchesFilter(const QString& path, const QStringList& filters)
{
    return filters.isEmpty() || filters.contains(QFileInfo(path).suffix().toLower());
}

FileMap scan(const QString& root, bool recursive, QStringList& errors)
{
    FileMap files;
    const QDir::Filters fileFilters = QDir::AllEntries | QDir::NoDotAndDotDot;
    const auto flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator iterator(root, fileFilters, flags);
    const QDir rootDir(root);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink())
            continue;
        if (info.isDir()) {
            if (recursive && !info.isReadable())
                errors << QStringLiteral("フォルダーを読み取れません: %1").arg(path);
            continue;
        }
        if (!info.isFile())
            continue;
        files.emplace(rootDir.relativeFilePath(path), info);
    }
    return files;
}

QByteArray digest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return {};
        hash.addData(chunk);
    }
    return hash.result();
}

bool sameContents(const QFileInfo& left, const QFileInfo& right)
{
    if (left.size() != right.size())
        return false;
    const QByteArray leftDigest = digest(left.absoluteFilePath());
    const QByteArray rightDigest = digest(right.absoluteFilePath());
    return !leftDigest.isEmpty() && leftDigest == rightDigest;
}

SyncAction errorAction(const QString& message, const QString& source = {}, const QString& destination = {})
{
    return {SyncActionKind::Error, source, destination, message, {}, {}};
}

SyncAction copyAction(const QString& source, const QString& destination)
{
    const QByteArray sourceDigest = digest(source);
    if (sourceDigest.isEmpty())
        return errorAction(QStringLiteral("更新元ファイルを読み込めません"), source, destination);

    const QFileInfo destinationInfo(destination);
    if (destinationInfo.isSymLink() || (destinationInfo.exists() && !destinationInfo.isFile()))
        return errorAction(QStringLiteral("更新先が通常ファイルではありません"), source, destination);

    QByteArray destinationDigest;
    if (destinationInfo.exists()) {
        destinationDigest = digest(destination);
        if (destinationDigest.isEmpty())
            return errorAction(QStringLiteral("更新先ファイルを読み込めません"), source, destination);
    }
    return {SyncActionKind::Copy, source, destination, QStringLiteral("ファイルをコピーまたは更新"),
            sourceDigest, destinationDigest};
}

void addOneWayActions(SyncPlan& result, const QString& sourceRoot, const QString& destinationRoot,
                      const SyncProfile& profile, const QStringList& extensions)
{
    QStringList scanErrors;
    const FileMap sourceFiles = scan(sourceRoot, profile.recursive, scanErrors);
    const FileMap destinationFiles = scan(destinationRoot, profile.recursive, scanErrors);
    if (!scanErrors.isEmpty()) {
        for (const QString& error : scanErrors)
            result.actions.push_back(errorAction(error, sourceRoot, destinationRoot));
        return;
    }
    for (const auto& [relative, info] : sourceFiles) {
        if (!matchesFilter(relative, extensions))
            continue;
        const QString destinationPath = QDir(destinationRoot).filePath(relative);
        const auto destination = destinationFiles.find(relative);
        if (destination == destinationFiles.cend() || !sameContents(info, destination->second))
            result.actions.push_back(copyAction(info.absoluteFilePath(), destinationPath));
    }

    if (!profile.deleteExtras)
        return;
    for (const auto& [relative, info] : destinationFiles) {
        if (sourceFiles.contains(relative) || !matchesFilter(relative, extensions))
            continue;
        const QByteArray expected = digest(info.absoluteFilePath());
        if (expected.isEmpty()) {
            result.actions.push_back(errorAction(QStringLiteral("削除対象を読み込めません"), {}, info.absoluteFilePath()));
        } else {
            result.actions.push_back({SyncActionKind::Delete, {}, info.absoluteFilePath(),
                                     QStringLiteral("ミラー設定により余分なファイルを削除"), {}, expected});
        }
    }
}

void addBidirectionalActions(SyncPlan& result, const QString& sourceRoot, const QString& destinationRoot,
                             const SyncProfile& profile, const QStringList& extensions)
{
    QStringList scanErrors;
    const FileMap sourceFiles = scan(sourceRoot, profile.recursive, scanErrors);
    const FileMap destinationFiles = scan(destinationRoot, profile.recursive, scanErrors);
    if (!scanErrors.isEmpty()) {
        for (const QString& error : scanErrors)
            result.actions.push_back(errorAction(error, sourceRoot, destinationRoot));
        return;
    }
    std::set<QString> paths;
    for (const auto& [relative, info] : sourceFiles) {
        Q_UNUSED(info);
        paths.insert(relative);
    }
    for (const auto& [relative, info] : destinationFiles) {
        Q_UNUSED(info);
        paths.insert(relative);
    }

    for (const QString& relative : paths) {
        if (!matchesFilter(relative, extensions))
            continue;
        const auto source = sourceFiles.find(relative);
        const auto destination = destinationFiles.find(relative);
        const QString sourcePath = QDir(sourceRoot).filePath(relative);
        const QString destinationPath = QDir(destinationRoot).filePath(relative);
        if (source == sourceFiles.cend()) {
            result.actions.push_back(copyAction(destinationPath, sourcePath));
        } else if (destination == destinationFiles.cend()) {
            result.actions.push_back(copyAction(sourcePath, destinationPath));
        } else if (!sameContents(source->second, destination->second)) {
            const QDateTime sourceTime = source->second.lastModified();
            const QDateTime destinationTime = destination->second.lastModified();
            if (sourceTime > destinationTime)
                result.actions.push_back(copyAction(sourcePath, destinationPath));
            else if (destinationTime > sourceTime)
                result.actions.push_back(copyAction(destinationPath, sourcePath));
            else
                result.actions.push_back({SyncActionKind::Conflict, sourcePath, destinationPath,
                                          QStringLiteral("更新日時が同じで内容が異なります"), {}, {}});
        }
    }
}

void addDuplicateDeletionActions(SyncPlan& result, const QString& root, const SyncProfile& profile,
                                 const QStringList& extensions)
{
    QStringList scanErrors;
    const FileMap files = scan(root, profile.recursive, scanErrors);
    if (!scanErrors.isEmpty()) {
        for (const QString& error : scanErrors)
            result.actions.push_back(errorAction(error, root));
        return;
    }
    std::map<QByteArray, QStringList> groups;
    bool digestError = false;
    for (const auto& [relative, info] : files) {
        if (!matchesFilter(relative, extensions))
            continue;
        const QByteArray fileDigest = digest(info.absoluteFilePath());
        if (fileDigest.isEmpty()) {
            result.actions.push_back(errorAction(QStringLiteral("重複判定のためにファイルを読み込めません"),
                                                 info.absoluteFilePath()));
            digestError = true;
            continue;
        }
        groups[fileDigest].push_back(info.absoluteFilePath());
    }

    if (digestError)
        return;

    for (const auto& [fileDigest, paths] : groups) {
        Q_UNUSED(fileDigest);
        if (paths.size() < 2)
            continue;
        const QString retainedPath = paths.front();
        for (qsizetype index = 1; index < paths.size(); ++index) {
            result.actions.push_back({SyncActionKind::Delete, retainedPath, paths[index],
                                      QStringLiteral("内容が一致する重複ファイルを削除"),
                                      fileDigest, digest(paths[index])});
        }
    }
}

QString errorText(const QString& message, const QString& source, const QString& destination)
{
    QStringList fields;
    if (!source.isEmpty())
        fields << QStringLiteral("更新元: %1").arg(source);
    if (!destination.isEmpty())
        fields << QStringLiteral("更新先: %1").arg(destination);
    fields << message;
    return fields.join(QStringLiteral(" | "));
}

QString executeCopy(const SyncAction& action)
{
    if (digest(action.source) != action.expectedSourceDigest)
        return QStringLiteral("実行前に更新元が変わったためコピーを中止");

    const QFileInfo destinationInfo(action.destination);
    if (destinationInfo.isSymLink() || (destinationInfo.exists() && !destinationInfo.isFile()))
        return QStringLiteral("更新先が通常ファイルではないためコピーを中止");
    if (destinationInfo.exists() && digest(action.destination) != action.expectedDestinationDigest)
        return QStringLiteral("プレビュー後に更新先が変わったためコピーを中止");

    if (!QDir().mkpath(QFileInfo(action.destination).absolutePath()))
        return QStringLiteral("更新先フォルダーを作成できません");

    QFile input(action.source);
    if (!input.open(QIODevice::ReadOnly))
        return QStringLiteral("更新元ファイルを開けません");
    QSaveFile output(action.destination);
    if (!output.open(QIODevice::WriteOnly))
        return QStringLiteral("更新先ファイルを開けません");
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if ((chunk.isEmpty() && input.error() != QFileDevice::NoError)
            || output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            return QStringLiteral("ファイルのコピーに失敗しました");
        }
    }
    if (!output.commit())
        return QStringLiteral("更新先への保存に失敗しました");

    QFile copied(action.destination);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(QFileInfo(action.source).lastModified(), QFileDevice::FileModificationTime);
        copied.close();
    }
    return QStringLiteral("コピーしました");
}

QString executeDelete(const SyncAction& action)
{
    if (!action.source.isEmpty() && digest(action.source) != action.expectedSourceDigest)
        return QStringLiteral("残すファイルがプレビュー後に変わったため削除を中止");
    if (digest(action.destination) != action.expectedDestinationDigest)
        return QStringLiteral("プレビュー後に削除対象が変わったため削除を中止");
    if (!QFile::remove(action.destination))
        return QStringLiteral("ファイルを削除できません");
    return QStringLiteral("削除しました");
}

} // namespace

QString SyncEngine::modeName(SyncMode mode)
{
    switch (mode) {
    case SyncMode::SourceToDestination: return QStringLiteral("更新元 → 更新先");
    case SyncMode::DestinationToSource: return QStringLiteral("更新元 ← 更新先");
    case SyncMode::Bidirectional: return QStringLiteral("更新元 ←→ 更新先");
    case SyncMode::DeleteDuplicates: return QStringLiteral("フォルダー内重複削除");
    }
    return QStringLiteral("不明");
}

QString SyncEngine::actionName(SyncActionKind kind)
{
    switch (kind) {
    case SyncActionKind::Copy: return QStringLiteral("コピー/更新");
    case SyncActionKind::Delete: return QStringLiteral("削除");
    case SyncActionKind::Conflict: return QStringLiteral("競合");
    case SyncActionKind::Error: return QStringLiteral("エラー");
    }
    return QStringLiteral("不明");
}

QString SyncEngine::logLine(const SyncAction& action)
{
    return QStringLiteral("%1 | %2")
        .arg(actionName(action.kind), errorText(action.message, action.source, action.destination));
}

SyncPlan SyncEngine::plan(const SyncProfile& profile)
{
    SyncPlan result;
    result.profileName = profile.name;
    result.mode = profile.mode;

    const QString sourceRoot = normalizedPath(profile.source);
    const QStringList extensions = extensionFilters(profile.extensions);
    const QFileInfo sourceInfo(sourceRoot);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        result.actions.push_back(errorAction(QStringLiteral("更新元フォルダーがありません"), sourceRoot));
        return result;
    }
    if (!sourceInfo.isReadable()) {
        result.actions.push_back(errorAction(QStringLiteral("更新元フォルダーを読み取れません"), sourceRoot));
        return result;
    }

    if (profile.mode == SyncMode::DeleteDuplicates) {
        addDuplicateDeletionActions(result, sourceRoot, profile, extensions);
        return result;
    }

    const QString destinationRoot = normalizedPath(profile.destination);
    const QFileInfo destinationInfo(destinationRoot);
    if (profile.destination.trimmed().isEmpty() || (destinationInfo.exists() && !destinationInfo.isDir())) {
        result.actions.push_back(errorAction(QStringLiteral("更新先フォルダーが正しくありません"), sourceRoot, destinationRoot));
        return result;
    }
    if (destinationInfo.exists() && !destinationInfo.isReadable()) {
        result.actions.push_back(errorAction(QStringLiteral("更新先フォルダーを読み取れません"), sourceRoot, destinationRoot));
        return result;
    }
    if (sourceRoot == destinationRoot || isWithin(sourceRoot, destinationRoot)
        || isWithin(destinationRoot, sourceRoot)) {
        result.actions.push_back(errorAction(QStringLiteral("更新元と更新先は別々のフォルダーにしてください"),
                                             sourceRoot, destinationRoot));
        return result;
    }

    switch (profile.mode) {
    case SyncMode::SourceToDestination:
        addOneWayActions(result, sourceRoot, destinationRoot, profile, extensions);
        break;
    case SyncMode::DestinationToSource:
        addOneWayActions(result, destinationRoot, sourceRoot, profile, extensions);
        break;
    case SyncMode::Bidirectional:
        addBidirectionalActions(result, sourceRoot, destinationRoot, profile, extensions);
        break;
    case SyncMode::DeleteDuplicates:
        break;
    }
    return result;
}

QStringList SyncEngine::execute(const SyncPlan& plan)
{
    QStringList log;
    log << QStringLiteral("[%1] %2: 同期開始 (%3)")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), plan.profileName, modeName(plan.mode));
    for (const SyncAction& action : plan.actions) {
        if (action.kind == SyncActionKind::Copy) {
            const QString outcome = executeCopy(action);
            const QString type = outcome == QStringLiteral("コピーしました")
                ? actionName(action.kind) : QStringLiteral("エラー");
            log << QStringLiteral("%1 | %2").arg(type, errorText(outcome, action.source, action.destination));
        } else if (action.kind == SyncActionKind::Delete) {
            const QString outcome = executeDelete(action);
            const QString type = outcome == QStringLiteral("削除しました")
                ? actionName(action.kind) : QStringLiteral("エラー");
            log << QStringLiteral("%1 | %2").arg(type, errorText(outcome, action.source, action.destination));
        } else {
            log << logLine(action);
        }
    }
    log << QStringLiteral("[%1] %2: 同期終了")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), plan.profileName);
    return log;
}
