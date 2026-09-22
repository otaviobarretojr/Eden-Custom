// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/util/mod_library.h"

#include <algorithm>

#include <JlCompress.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "common/fs/path_util.h"

namespace QtCommon::ModLibrary {
namespace {

QString IndexPath() {
    return QDir{LibraryRootPath()}.filePath(QStringLiteral("index.json"));
}

QString SafeBaseName(QString name) {
    name = QFileInfo{name}.completeBaseName();
    name.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")),
                 QStringLiteral("_"));
    while (name.startsWith(QLatin1Char('_'))) {
        name.remove(0, 1);
    }
    while (name.endsWith(QLatin1Char('_'))) {
        name.chop(1);
    }
    return name.isEmpty() ? QStringLiteral("mod-package") : name.left(80);
}

QString HashFile(const QString& path, QString* error) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Unable to read mod package: %1").arg(file.errorString());
        }
        return {};
    }

    QCryptographicHash hash{QCryptographicHash::Sha256};
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            if (error) {
                *error = QObject::tr("Unable to hash mod package: %1").arg(file.errorString());
            }
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

QStringList NormalizeEntries(const QStringList& raw_entries) {
    QStringList files;
    files.reserve(raw_entries.size());

    for (const QString& raw : raw_entries) {
        QString entry = NormalizeEntryPath(raw);
        if (entry.isEmpty() || entry.endsWith(QLatin1Char('/'))) {
            continue;
        }
        files.push_back(entry);
    }

    QString common_root;
    bool has_common_root = !files.isEmpty();
    for (const QString& entry : files) {
        const qsizetype slash = entry.indexOf(QLatin1Char('/'));
        if (slash <= 0) {
            has_common_root = false;
            break;
        }

        const QString root = entry.left(slash);
        if (common_root.isEmpty()) {
            common_root = root;
        } else if (QString::compare(common_root, root, Qt::CaseInsensitive) != 0) {
            has_common_root = false;
            break;
        }
    }

    if (has_common_root && !common_root.isEmpty()) {
        const qsizetype prefix_size = common_root.size() + 1;
        for (QString& entry : files) {
            entry.remove(0, prefix_size);
        }
    }

    files.removeDuplicates();
    return files;
}

QSet<QString> FindTitleIds(const QStringList& entries) {
    static const QRegularExpression title_id_re{
        QStringLiteral("(?i)(?<![0-9A-F])010[0-9A-F]{13}(?![0-9A-F])")};

    QSet<QString> result;
    for (const QString& entry : entries) {
        auto matches = title_id_re.globalMatch(entry);
        while (matches.hasNext()) {
            result.insert(matches.next().captured(0).toUpper());
        }
    }
    return result;
}

QJsonObject PackageToJson(const PackageInfo& package) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), package.id);
    object.insert(QStringLiteral("name"), package.name);
    object.insert(QStringLiteral("file_name"), package.file_name);
    object.insert(QStringLiteral("imported_at"), package.imported_at);
    object.insert(QStringLiteral("size_bytes"), static_cast<qint64>(package.size_bytes));
    object.insert(QStringLiteral("entry_count"), package.entry_count);

    QJsonArray titles;
    QStringList sorted_titles{package.title_ids.begin(), package.title_ids.end()};
    std::sort(sorted_titles.begin(), sorted_titles.end());
    for (const QString& title : sorted_titles) {
        titles.append(title);
    }
    object.insert(QStringLiteral("title_ids"), titles);

    QJsonArray entries;
    QStringList sorted_entries{package.entries.begin(), package.entries.end()};
    std::sort(sorted_entries.begin(), sorted_entries.end());
    for (const QString& entry : sorted_entries) {
        entries.append(entry);
    }
    object.insert(QStringLiteral("entries"), entries);
    return object;
}

PackageInfo PackageFromJson(const QJsonObject& object) {
    PackageInfo package;
    package.id = object.value(QStringLiteral("id")).toString();
    package.name = object.value(QStringLiteral("name")).toString();
    package.file_name = object.value(QStringLiteral("file_name")).toString();
    package.imported_at = object.value(QStringLiteral("imported_at")).toString();
    package.size_bytes = static_cast<quint64>(
        object.value(QStringLiteral("size_bytes")).toInteger());
    package.entry_count = object.value(QStringLiteral("entry_count")).toInt();

    for (const auto& value : object.value(QStringLiteral("title_ids")).toArray()) {
        const QString title = value.toString().toUpper();
        if (!title.isEmpty()) {
            package.title_ids.insert(title);
        }
    }
    for (const auto& value : object.value(QStringLiteral("entries")).toArray()) {
        const QString entry = NormalizeEntryPath(value.toString());
        if (!entry.isEmpty()) {
            package.entries.insert(entry);
        }
    }
    return package;
}

QVector<PackageInfo> ReadIndex(QString* error) {
    QFile file{IndexPath()};
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Unable to read the mod library index: %1")
                         .arg(file.errorString());
        }
        return {};
    }

    QJsonParseError parse_error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QObject::tr("The mod library index is invalid: %1")
                         .arg(parse_error.errorString());
        }
        return {};
    }

    QVector<PackageInfo> packages;
    for (const auto& value : document.object().value(QStringLiteral("packages")).toArray()) {
        const auto object = value.toObject();
        if (!object.isEmpty()) {
            packages.push_back(PackageFromJson(object));
        }
    }
    return packages;
}

bool WriteIndex(const QVector<PackageInfo>& packages, QString* error) {
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);

    QJsonArray array;
    for (const auto& package : packages) {
        array.append(PackageToJson(package));
    }
    root.insert(QStringLiteral("packages"), array);

    QDir().mkpath(LibraryRootPath());
    QSaveFile file{IndexPath()};
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QObject::tr("Unable to write the mod library index: %1")
                         .arg(file.errorString());
        }
        return false;
    }

    file.write(QJsonDocument{root}.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) {
            *error = QObject::tr("Unable to commit the mod library index.");
        }
        return false;
    }
    return true;
}

} // namespace

QString LibraryRootPath() {
    const auto root = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "mod_library";
    return QString::fromStdString(root.string());
}

QString PackagesPath() {
    return QDir{LibraryRootPath()}.filePath(QStringLiteral("packs"));
}

QString NormalizeEntryPath(QString path) {
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    path = QDir::cleanPath(path);
    while (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    if (path == QStringLiteral(".")) {
        return {};
    }
    return path;
}

bool ImportPackage(const QString& source_zip, PackageInfo& package, QString* error) {
    const QFileInfo source_info{source_zip};
    if (!source_info.exists() || !source_info.isFile()) {
        if (error) {
            *error = QObject::tr("Mod package does not exist.");
        }
        return false;
    }
    if (source_info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = QObject::tr("Only ZIP mod packages are supported in this version.");
        }
        return false;
    }

    const QString hash = HashFile(source_zip, error);
    if (hash.isEmpty()) {
        return false;
    }

    QVector<PackageInfo> packages = ReadIndex(error);
    if (error && !error->isEmpty()) {
        return false;
    }

    for (const auto& existing : packages) {
        if (QString::compare(existing.id, hash, Qt::CaseInsensitive) == 0) {
            package = existing;
            package.already_imported = true;
            return true;
        }
    }

    const QStringList raw_entries = JlCompress::getFileList(source_zip);
    if (raw_entries.isEmpty()) {
        if (error) {
            *error = QObject::tr("The ZIP is empty or could not be read.");
        }
        return false;
    }

    const QStringList normalized_entries = NormalizeEntries(raw_entries);
    if (normalized_entries.isEmpty()) {
        if (error) {
            *error = QObject::tr("No files were found inside the ZIP.");
        }
        return false;
    }

    const QSet<QString> title_ids = FindTitleIds(normalized_entries);
    if (title_ids.isEmpty()) {
        if (error) {
            *error = QObject::tr(
                "No Nintendo Switch Title IDs were detected in this package.");
        }
        return false;
    }

    QDir().mkpath(PackagesPath());
    const QString safe_name = SafeBaseName(source_info.fileName());
    const QString target_name =
        QStringLiteral("%1-%2.zip").arg(hash.left(16), safe_name);
    const QString target_path = QDir{PackagesPath()}.filePath(target_name);

    if (!QFileInfo::exists(target_path) && !QFile::copy(source_zip, target_path)) {
        if (error) {
            *error = QObject::tr("Unable to copy the mod package into Eden's library.");
        }
        return false;
    }

    package.id = hash;
    package.name = source_info.fileName();
    package.file_name = target_name;
    package.imported_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    package.size_bytes = static_cast<quint64>(source_info.size());
    package.entry_count = normalized_entries.size();
    package.title_ids = title_ids;
    package.entries = QSet<QString>{normalized_entries.begin(), normalized_entries.end()};
    package.already_imported = false;

    packages.push_back(package);
    if (!WriteIndex(packages, error)) {
        QFile::remove(target_path);
        return false;
    }
    return true;
}

QVector<PackageInfo> ListPackages(QString* error) {
    return ReadIndex(error);
}

QVector<PackageInfo> PackagesForTitle(u64 title_id, QString* error) {
    const QString title_key =
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char('0')).toUpper();

    QVector<PackageInfo> matches;
    for (const auto& package : ReadIndex(error)) {
        if (package.title_ids.contains(title_key)) {
            matches.push_back(package);
        }
    }
    return matches;
}

bool PackageContainsSource(const PackageInfo& package, const QString& source_path,
                           const QString& container_path) {
    const QString candidate =
        NormalizeEntryPath(container_path.isEmpty() ? source_path : container_path);
    if (candidate.isEmpty()) {
        return false;
    }

    if (package.entries.contains(candidate)) {
        return true;
    }

    // Some packages keep a single root folder. Import normalizes that root away, but
    // older indexes may still contain it. Accept a suffix match with a directory boundary.
    const QString suffix = QLatin1Char('/') + candidate;
    return std::any_of(package.entries.cbegin(), package.entries.cend(),
                       [&suffix](const QString& entry) {
                           return entry.endsWith(suffix, Qt::CaseInsensitive);
                       });
}

} // namespace QtCommon::ModLibrary
