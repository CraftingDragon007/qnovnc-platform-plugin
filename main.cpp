// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <qpa/qplatformintegrationplugin.h>
#include <QtCore/QStringLiteral>
#include "qnovncintegration.h"
#include "qnovnc_p.h"

QT_BEGIN_NAMESPACE

class QNoVncIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "novnc.json")
public:
    QPlatformIntegration *create(const QString&, const QStringList&) override;
};

QPlatformIntegration* QNoVncIntegrationPlugin::create(const QString& system, const QStringList& paramList)
{
    if (!system.compare(QStringLiteral("novnc"), Qt::CaseInsensitive))
        return new QNoVncIntegration(paramList);

    return nullptr;
}

QT_END_NAMESPACE

#include "main.moc"
