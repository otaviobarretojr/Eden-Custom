// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QString>

enum class EdenPerformanceProfile {
    Auto,
    Stable,
    Performance,
    Quality,
    Custom,
};

class QLabel;
class QPushButton;

class PerformanceProfileDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PerformanceProfileDialog(const QString& detected_gpu, QWidget* parent = nullptr);
    ~PerformanceProfileDialog() override;

signals:
    void ProfileSelected(EdenPerformanceProfile profile);
    void CustomRequested();

private:
    QWidget* CreateProfileCard(const QString& title, const QString& subtitle,
                               const QString& description, EdenPerformanceProfile profile,
                               bool recommended = false);

    QString detected_gpu;
};
