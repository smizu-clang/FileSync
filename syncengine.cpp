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
    QFileInfo info(path);
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

bool matchesFilter(const QString& relativePath, const QStringList& filters)
{
    if (filters.isEmpty())
        return true;
    const QString suffix = QFileInfo(relativePath).suffix().toLower();
    return filters.contains(suffix);
}

FileMap scan(const QString& root, bool recursive)
{
    FileMap files;
    const QDir::Filters filters = QDir::Files | QDir::NoDotAndDotDot;
    const QDirIterator::IteratorFlags flags = recursive
        ? QDirIterator::Subdirectories
        : QDirIterator::NoIteratorFlags;
    QDirIterator it(root, filters, flags);
    const QDir rootDir(root);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink() || !info.isFile())
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

bool copyFile(const QString& source, const QString& destination, QStringList& log)
{
    const QFileInfo sourceInfo(source);
    const QFileInfo destinationInfo(destination);
    if (destinationInfo.isSymLink() || (destinationInfo.exists() && !destinationInfo.isFile())) {
        log << QStringLiteral("競合: 同期先のパスが通常ファイルではありません: %1").arg(destination);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        log << QStringLiteral("エラー: フォルダを作成できません: %1").arg(destination);
        return false;
    }

    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        log << QStringLiteral("エラー: 読み込めません: %1").arg(source);
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        log << QStringLiteral("エラー: 書き込めません: %1").arg(destination);
        return false;
    }
    if (output.write(input.readAll()) < 0 || !output.commit()) {
        log << QStringLiteral("エラー: コピーに失敗しました: %1").arg(destination);
        return false;
    }

    QFile copied(destination);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(sourceInfo.lastModified(), QFileDevice::FileModificationTime);
        copied.close();
    }
    log << QStringLiteral("コピー: %1").arg(destination);
    return true;
}

void copyIfDifferent(const QString& source, const QString& destination,
                     const QFileInfo& sourceInfo, const QFileInfo* destinationInfo,
                     QStringList& log)
{
    if (destinationInfo && (destinationInfo->isSymLink()
        || (destinationInfo->exists() && !destinationInfo->isFile()))) {
        copyFile(source, destination, log);
        return;
    }
    if (!destinationInfo || !destinationInfo->exists() || !sameContents(sourceInfo, *destinationInfo))
        copyFile(source, destination, log);
}

} // namespace

QStringList SyncEngine::run(const SyncProfile& profile)
{
    QStringList log;
    const QString sourceRoot = normalizedPath(profile.source);
    const QString destinationRoot = normalizedPath(profile.destination);
    const QFileInfo sourceInfo(sourceRoot);
    if (!sourceInfo.exists() || !sourceInfo.isDir()) {
        return {QStringLiteral("エラー: 同期元フォルダがありません: %1").arg(sourceRoot)};
    }
    if (sourceRoot == destinationRoot || isWithin(sourceRoot, destinationRoot)
        || isWithin(destinationRoot, sourceRoot)) {
        return {QStringLiteral("エラー: 同期元と同期先は別のフォルダを指定してください: %1").arg(profile.name)};
    }

    log << QStringLiteral("開始: %1").arg(profile.name);
    const QStringList extensions = extensionFilters(profile.extensions);
    const FileMap sourceFiles = scan(sourceRoot, profile.recursive);
    const FileMap destinationFiles = scan(destinationRoot, profile.recursive);

    if (profile.twoWay) {
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
            const auto sourceIt = sourceFiles.find(relative);
            const auto destinationIt = destinationFiles.find(relative);
            const QString sourcePath = QDir(sourceRoot).filePath(relative);
            const QString destinationPath = QDir(destinationRoot).filePath(relative);
            if (sourceIt == sourceFiles.cend()) {
                copyFile(destinationPath, sourcePath, log);
            } else if (destinationIt == destinationFiles.cend()) {
                copyFile(sourcePath, destinationPath, log);
            } else if (!sameContents(sourceIt->second, destinationIt->second)) {
                const auto sourceTime = sourceIt->second.lastModified();
                const auto destinationTime = destinationIt->second.lastModified();
                if (sourceTime > destinationTime)
                    copyFile(sourcePath, destinationPath, log);
                else if (destinationTime > sourceTime)
                    copyFile(destinationPath, sourcePath, log);
                else
                    log << QStringLiteral("競合: 更新日時が同じで内容が異なります: %1").arg(relative);
            }
        }
    } else {
        for (auto it = sourceFiles.cbegin(); it != sourceFiles.cend(); ++it) {
            const QString relative = it->first;
            if (!matchesFilter(relative, extensions))
                continue;
            const QString destinationPath = QDir(destinationRoot).filePath(relative);
            const auto destinationIt = destinationFiles.find(relative);
            copyIfDifferent(it->second.absoluteFilePath(), destinationPath, it->second,
                            destinationIt == destinationFiles.cend() ? nullptr : &destinationIt->second, log);
        }

        if (profile.deleteExtras) {
            for (auto it = destinationFiles.cbegin(); it != destinationFiles.cend(); ++it) {
                if (sourceFiles.contains(it->first) || !matchesFilter(it->first, extensions))
                    continue;
                if (QFile::remove(it->second.absoluteFilePath()))
                    log << QStringLiteral("削除: %1").arg(it->second.absoluteFilePath());
                else
                    log << QStringLiteral("エラー: 削除できません: %1").arg(it->second.absoluteFilePath());
            }
        }
    }

    log << QStringLiteral("完了: %1").arg(profile.name);
    return log;
}
