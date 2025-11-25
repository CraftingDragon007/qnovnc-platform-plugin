// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncintegration.h"
#include "qnovncscreen.h"
#include "qnovnc_p.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/private/qgenericunixfontdatabase_p.h>
#include <QtGui/private/qdesktopunixservices_p.h>
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#else
#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtServiceSupport/private/qgenericunixservices_p.h>
#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#endif

#include <QtCore/QStringLiteral>
#include <QtFbSupport/private/qfbbackingstore_p.h>
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtFbSupport/private/qfbcursor_p.h>

#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatforminputcontextfactory_p.h>
#include <private/qinputdevicemanager_p_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <QtCore/QRegularExpression>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using QUnixPlatformServices = QDesktopUnixServices;
#else
using QUnixPlatformServices = QGenericUnixServices;
#endif

QT_BEGIN_NAMESPACE

QNoVncIntegration::QNoVncIntegration(const QStringList &paramList)
    : m_fontDb(new QGenericUnixFontDatabase)
{
    const QRegularExpression portRx(QStringLiteral("port=(\\d+)"));
    quint16 port = 5900;
    for (const QString &arg : paramList) {
        QRegularExpressionMatch match;
        if (arg.contains(portRx, &match))
            port = match.captured(1).toInt();
    }
    QString host = QStringLiteral("0.0.0.0");
    const QRegularExpression hostRx(QStringLiteral("host=([^\\s]+)"));
    for (const QString &arg : paramList) {
        QRegularExpressionMatch match;
        if (arg.contains(hostRx, &match))
            host = match.captured(1);;
    }

    m_primaryScreen = new QNoVncScreen(paramList);
    m_server = new QNoVncServer(m_primaryScreen, port, host);
    m_primaryScreen->vncServer = m_server;
}

QNoVncIntegration::~QNoVncIntegration()
{
    delete m_server;
    QWindowSystemInterface::handleScreenRemoved(m_primaryScreen);
}

void QNoVncIntegration::initialize()
{
    if (m_primaryScreen->initialize())
        QWindowSystemInterface::handleScreenAdded(m_primaryScreen);
    else
        qWarning("vnc: Failed to initialize screen");

    m_inputContext = QPlatformInputContextFactory::create();

    m_nativeInterface.reset(new QPlatformNativeInterface);

    // we always have exactly one mouse and keyboard
    QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
        QInputDeviceManager::DeviceTypePointer, 1);
    QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())->setDeviceCount(
        QInputDeviceManager::DeviceTypeKeyboard, 1);

}

bool QNoVncIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps: return true;
    case WindowManagement: return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    case RhiBasedRendering: return false;
#endif
    default: return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformBackingStore *QNoVncIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QFbBackingStore(window);
}

QPlatformWindow *QNoVncIntegration::createPlatformWindow(QWindow *window) const
{
    return new QFbWindow(window);
}

QAbstractEventDispatcher *QNoVncIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
}

QList<QPlatformScreen *> QNoVncIntegration::screens() const
{
    QList<QPlatformScreen *> list;
    list.append(m_primaryScreen);
    return list;
}

QPlatformFontDatabase *QNoVncIntegration::fontDatabase() const
{
    return m_fontDb.data();
}

QPlatformServices *QNoVncIntegration::services() const
{
    if (m_services.isNull())
        m_services.reset(new QUnixPlatformServices);

    return m_services.data();
}

QPlatformNativeInterface *QNoVncIntegration::nativeInterface() const
{
    return m_nativeInterface.data();
}

QT_END_NAMESPACE
