// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qt_common/util/archive_extract.h"

#include "qt_common/util/compress.h"

namespace QtCommon::Archive {

QStringList ExtractZip(const QString& archive_path, const QString& destination,
                       ProgressCallback callback) {
    return QtCommon::Compress::extractDir(archive_path, destination, callback);
}

} // namespace QtCommon::Archive
