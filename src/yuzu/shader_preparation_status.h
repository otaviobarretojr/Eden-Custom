// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include <QString>

#include "common/common_types.h"

namespace EdenCustom {

struct ShaderPreparationStatus {
    bool has_record{};
    bool last_completed{};
    std::size_t known_pipelines{};
    QString last_started;
    QString last_completed_at;
    QString cache_directory;
    quint64 transferable_cache_bytes{};
    quint64 driver_cache_bytes{};
    quint64 opengl_cache_bytes{};
};

void RecordShaderPreparationStart(u64 title_id, std::size_t total);
void RecordShaderPreparationComplete(u64 title_id);
ShaderPreparationStatus ReadShaderPreparationStatus(u64 title_id);
QString FormatBytes(quint64 bytes);

} // namespace EdenCustom
