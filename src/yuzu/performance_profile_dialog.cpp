// SPDX-FileCopyrightText: Copyright 2026 Eden Custom Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/performance_profile_dialog.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

PerformanceProfileDialog::PerformanceProfileDialog(const QString& detected_gpu_,
                                                   QWidget* parent)
    : QDialog(parent), detected_gpu(detected_gpu_) {
    setWindowTitle(tr("Eden Custom Performance Profiles"));
    setModal(true);
    resize(680, 620);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        tr("Choose a baseline for the next game launch. Profiles avoid unsafe CPU/GPU accuracy "
           "hacks and can always be changed later."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* gpu = new QLabel(
        detected_gpu.isEmpty() ? tr("Detected Vulkan GPU: unavailable")
                               : tr("Detected Vulkan GPU: %1").arg(detected_gpu),
        this);
    gpu->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(gpu);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget(scroll);
    auto* cards = new QVBoxLayout(container);
    cards->setContentsMargins(0, 8, 0, 8);

    cards->addWidget(CreateProfileCard(
        tr("Auto"), tr("Recommended"),
        tr("Uses safe Vulkan defaults and adapts VRAM policy for recognized modern desktop GPUs. "
           "Keeps native 1x resolution and medium GPU accuracy."),
        EdenPerformanceProfile::Auto, true));

    cards->addWidget(CreateProfileCard(
        tr("Stable"), tr("Compatibility first"),
        tr("Closest to Eden's conservative defaults: native resolution, conservative VRAM, "
           "medium GPU accuracy and persistent shader caches."),
        EdenPerformanceProfile::Stable));

    cards->addWidget(CreateProfileCard(
        tr("Performance"), tr("Favor frametime and throughput"),
        tr("Native resolution with aggressive VRAM use and maximum renderer clock request. "
           "Shader caches stay enabled; unsafe CPU accuracy and asynchronous shader hacks remain off."),
        EdenPerformanceProfile::Performance));

    cards->addWidget(CreateProfileCard(
        tr("Quality"), tr("Sharper image"),
        tr("2x resolution, 16x anisotropic filtering and SMAA while retaining medium GPU accuracy "
           "and the same compatibility-oriented shader settings."),
        EdenPerformanceProfile::Quality));

    cards->addWidget(CreateProfileCard(
        tr("Custom"), tr("Full manual control"),
        tr("Opens Eden's normal configuration window without changing settings automatically."),
        EdenPerformanceProfile::Custom));

    cards->addStretch();
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    auto* note = new QLabel(
        tr("Profile changes are saved globally and take effect on the next game launch. "
           "Per-game configuration can still override global settings."),
        this);
    note->setWordWrap(true);
    root->addWidget(note);
}

PerformanceProfileDialog::~PerformanceProfileDialog() = default;

QWidget* PerformanceProfileDialog::CreateProfileCard(const QString& title, const QString& subtitle,
                                                     const QString& description,
                                                     EdenPerformanceProfile profile,
                                                     bool recommended) {
    auto* card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(card);

    auto* heading = new QLabel(
        recommended ? tr("%1  •  %2").arg(title, subtitle)
                    : QStringLiteral("%1  —  %2").arg(title, subtitle),
        card);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setPointSize(heading_font.pointSize() + 1);
    heading->setFont(heading_font);
    layout->addWidget(heading);

    auto* body = new QLabel(description, card);
    body->setWordWrap(true);
    layout->addWidget(body);

    auto* apply = new QPushButton(profile == EdenPerformanceProfile::Custom
                                      ? tr("Open custom settings")
                                      : tr("Apply %1").arg(title),
                                  card);
    layout->addWidget(apply, 0, Qt::AlignRight);

    connect(apply, &QPushButton::clicked, this, [this, profile] {
        if (profile == EdenPerformanceProfile::Custom) {
            accept();
            emit CustomRequested();
            return;
        }
        accept();
        emit ProfileSelected(profile);
    });

    return card;
}
