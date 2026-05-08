// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// Copyright (C) 2016 The Qt Company Ltd.
// This file is a derivative of the qvnc platform plugin from Qt Base
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncscreen.h"

#if defined(QNOVNC_HAS_EGL_MESA)
#include "qnovncopenglcontext.h"
#endif

#include <qpa/qplatformopenglcontext.h>

#include <QtFbSupport/private/qfbbackingstore_p.h>

#include "qnovnc_p.h"
#include "qnovncwindow.h"
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtFbSupport/private/qfbcursor_p.h>

#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtCore/QRegularExpression>
#include <QtCore/QStringLiteral>


QT_BEGIN_NAMESPACE

QNoVncScreen::QNoVncScreen(const QStringList &args)
    : mArgs(args)
{
    // Initialization is performed by QNoVncIntegration::initialize().
}

QNoVncScreen::~QNoVncScreen()
{
    delete m_platformContext;
    m_platformContext = nullptr;
    delete dirty;
    dirty = nullptr;
#if QT_CONFIG(cursor)
    if (clientCursor)
        delete clientCursor;
#endif
}

bool QNoVncScreen::initialize()
{
    if (dirty) {
        delete dirty;
        dirty = nullptr;
    }

    static QRegularExpression sizeRx(QStringLiteral("size=(\\d+)x(\\d+)"));
    static QRegularExpression mmSizeRx(QStringLiteral("mmsize=(?<width>(\\d*\\.)?\\d+)x(?<height>(\\d*\\.)?\\d+)"));
    static QRegularExpression depthRx(QStringLiteral("depth=(\\d+)"));

    mGeometry = QRect(0, 0, 1024, 768);
    mFormat = QImage::Format_RGBA8888;
    mDepth = 32;
    mPhysicalSize = QSizeF(mGeometry.width()/96.*25.4, mGeometry.height()/96.*25.4);

    for (const QString &arg : std::as_const(mArgs)) {
        QRegularExpressionMatch match;
        if (arg.contains(mmSizeRx, &match)) {
            mPhysicalSize = QSizeF(match.captured("width").toDouble(), match.captured("height").toDouble());
        } else if (arg.contains(sizeRx, &match)) {
            mGeometry.setSize(QSize(match.captured(1).toInt(), match.captured(2).toInt()));
        } else if (arg.contains(depthRx, &match)) {
            mDepth = match.captured(1).toInt();
        } else if (arg.contains(QStringLiteral("readonly"))) {
            m_readonly = true;
        }
    }

    switch (depth()) {
    case 32:
        dirty = new QNoVncDirtyMapOptimized<quint32>(this);
        break;
    case 16:
        dirty = new QNoVncDirtyMapOptimized<quint16>(this);
        mFormat = QImage::Format_RGB16;
        break;
    case 8:
        dirty = new QNoVncDirtyMapOptimized<quint8>(this);
        break;
    default:
        qCWarning(lcVnc, "QNoVNCScreen::initDevice: No support for screen depth %d",
                 depth());
        dirty = nullptr;
        return false;
    }

    QFbScreen::initializeCompositor();

    setPowerState(PowerStateOff);

    return true;
}

QRegion QNoVncScreen::doRedraw()
{
    for (int i = 0; i < mWindowStack.size(); ++i)
    {
        const QFbWindow *window = mWindowStack[i];
        if (window == nullptr)
        {
            qCWarning(lcVnc) << "QNoVNCScreen::doRedraw: QFbWindow is null";
            mWindowStack.removeAt(i);
            i--;
            continue;
        }
        if (window->window() == nullptr) {
            qCWarning(lcVnc) << "QNoVNCScreen::doRedraw: QFbWindow->window() is null";
            mWindowStack.removeAt(i);
            i--;
            continue;
        }

        if (window->window()->windowState() == Qt::WindowMinimized)
        {
            window->window()->setVisible(false);
        }
    }

    const QPoint screenOffset = mGeometry.topLeft();
    QRegion touchedRegion;

    if (mCursor && mCursor->isDirty() && mCursor->isOnScreen()) {
        const QRect lastCursor = mCursor->dirtyRect();
        mRepaintRegion += lastCursor;
    }
    if (mRepaintRegion.isEmpty() && (!mCursor || !mCursor->isDirty()))
        return touchedRegion;

    QPainter painter(&mScreenImage);

    const QRect screenRect = mGeometry.translated(-screenOffset);
    for (QRect rect : mRepaintRegion) {
        rect = rect.intersected(screenRect);
        if (rect.isEmpty())
            continue;

        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect, mScreenImage.hasAlphaChannel() ? Qt::transparent : Qt::black);

        // Pass 1: paint regular raster/backingstore windows.
        for (qsizetype layerIndex = mWindowStack.size() - 1; layerIndex != -1; layerIndex--) {
            QFbWindow *fbWindow = mWindowStack[layerIndex];
            if (!fbWindow->window()->isVisible())
                continue;

            const QRect windowRect = fbWindow->geometry().translated(-screenOffset);
            const QRect windowIntersect = rect.translated(-windowRect.left(), -windowRect.top());

            auto *window = static_cast<QNoVncWindow *>(fbWindow);
            const bool hasWindowImage = window->image() && !window->image()->isNull();

            if (hasWindowImage)
                continue;

            if (QFbBackingStore *backingStore = fbWindow->backingStore()) {
                backingStore->lock();
                painter.drawImage(rect, backingStore->image(), windowIntersect);
                backingStore->unlock();
            }
        }

        // Pass 2: paint OpenGL/readback windows on top so they are not covered by parent backingstores.
        for (qsizetype layerIndex = mWindowStack.size() - 1; layerIndex != -1; layerIndex--) {
            QFbWindow *fbWindow = mWindowStack[layerIndex];
            if (!fbWindow->window()->isVisible())
                continue;

            auto *window = static_cast<QNoVncWindow *>(fbWindow);
            if (!window->image() || window->image()->isNull())
                continue;

            const QRect windowRect = fbWindow->geometry().translated(-screenOffset);
            const QRect windowIntersect = rect.translated(-windowRect.left(), -windowRect.top());
            painter.drawImage(rect, *window->image(), windowIntersect);
        }
    }

    if (mCursor && (mCursor->isDirty() || mRepaintRegion.intersects(mCursor->lastPainted()))) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        touchedRegion += mCursor->drawCursor(painter);
    }
    touchedRegion += mRepaintRegion;

    QRegion realChanges;

    if (m_prevScreenImage.size() != mScreenImage.size() ||
        m_prevScreenImage.format() != mScreenImage.format())
    {
        m_prevScreenImage = mScreenImage.copy();
        realChanges = touchedRegion;
    }
    else
    {
        const int depth = mScreenImage.depth() / 8;
        const qsizetype bytesPerLine = mScreenImage.bytesPerLine();
        const uchar *currBase = mScreenImage.bits();
        const uchar *prevBase = m_prevScreenImage.bits();

        for (const QRect &largeRect : touchedRegion)
        {
            constexpr int TILE_SIZE = 64;

            // Subdivide the large rect into small tiles
            for (int y = largeRect.y(); y <= largeRect.bottom(); y += TILE_SIZE) {
                for (int x = largeRect.x(); x <= largeRect.right(); x += TILE_SIZE) {

                    const int w = qMin(TILE_SIZE, largeRect.right() - x + 1);
                    const int h = qMin(TILE_SIZE, largeRect.bottom() - y + 1);

                    bool tileChanged = false;
                    for (int row = 0; row < h; ++row) {
                        // Compare one scanline of the tile
                        if (const int lineOffset = ((y + row) * static_cast<int>(bytesPerLine)) + (x * depth); memcmp(currBase + lineOffset, prevBase + lineOffset, w * depth) != 0) {
                            tileChanged = true;
                            break; // Stop checking this tile, it's dirty
                        }
                    }

                    if (tileChanged) {
                        realChanges += QRect(x, y, w, h);
                    }
                }
            }
        }

        if (!touchedRegion.isEmpty()) {
            QPainter shadowPainter(&m_prevScreenImage);
            shadowPainter.setCompositionMode(QPainter::CompositionMode_Source);
            for (const QRect &r : touchedRegion) {
                shadowPainter.drawImage(r, mScreenImage, r);
            }
        }
    }

    touchedRegion = realChanges;

    mRepaintRegion = QRegion();

    if (touchedRegion.isEmpty())
        return touchedRegion;
    dirtyRegion += touchedRegion;

    vncServer->setDirty();
    return touchedRegion;
}


void QNoVncScreen::enableClientCursor(QNoVncClient *client)
{
#if QT_CONFIG(cursor)
    delete mCursor;
    mCursor = nullptr;
    if (!clientCursor)
        clientCursor = new QNoVncClientCursor();
    clientCursor->addClient(client);
#else
    Q_UNUSED(client);
#endif
}

void QNoVncScreen::disableClientCursor(QNoVncClient *client)
{
#if QT_CONFIG(cursor)
    if (!clientCursor)
        return;

    if (const uint clientCount = clientCursor->removeClient(client); clientCount == 0) {
        delete clientCursor;
        clientCursor = nullptr;

        if (mCursor == nullptr)
            mCursor = new QFbCursor(this);
    }
#else
    Q_UNUSED(client);
#endif
}

QPlatformCursor *QNoVncScreen::cursor() const
{
#if QT_CONFIG(cursor)
    return mCursor ? static_cast<QPlatformCursor *>(mCursor) : static_cast<QPlatformCursor *>(clientCursor);
#else
    return nullptr;
#endif
}

// grabWindow() grabs "from the screen" not from the backingstores.
// In linuxfb's case it will also include the mouse cursor.
QPixmap QNoVncScreen::grabWindow(const WId wid, const int x, const int y, int width, int height) const
{
    if (!wid) {
        if (width < 0)
            width = mScreenImage.width() - x;
        if (height < 0)
            height = mScreenImage.height() - y;
        return QPixmap::fromImage(mScreenImage).copy(x, y, width, height);
    }

    if (const QFbWindow *window = windowForId(wid)) {
        const QRect geom = window->geometry();
        if (width < 0)
            width = geom.width() - x;
        if (height < 0)
            height = geom.height() - y;
        QRect rect(geom.topLeft() + QPoint(x, y), QSize(width, height));
        rect &= window->geometry();
        return QPixmap::fromImage(mScreenImage).copy(rect);
    }

    return {};
}

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
bool QNoVncScreen::swapBytes() const
{
    return false;

    /* TODO
    if (depth() != 16)
        return false;

    if (screen())
        return screen()->frameBufferLittleEndian();
    return frameBufferLittleEndian();
    */
}
#endif

QFbScreen::Flags QNoVncScreen::flags() const
{
    return QFbScreen::DontForceFirstWindowToFullScreen;
}

QPlatformOpenGLContext *QNoVncScreen::platformContext() const
{
    return m_platformContext;
}

void QNoVncScreen::createAndSetPlatformContext(const QSurfaceFormat &format) const
{
    const_cast<QNoVncScreen *>(this)->createAndSetPlatformContext(format);
}

void QNoVncScreen::createAndSetPlatformContext(const QSurfaceFormat &format)
{
#if defined(QNOVNC_HAS_EGL_MESA)
    if (m_platformContext && !m_platformContext->isValid()) {
        delete m_platformContext;
        m_platformContext = nullptr;
    }

    if (!m_platformContext)
        m_platformContext = new QNoVncOpenGLContext(format);
#else
    Q_UNUSED(format);
#endif
}

QT_END_NAMESPACE

//#include "mocqnovncscreen.cpp"
