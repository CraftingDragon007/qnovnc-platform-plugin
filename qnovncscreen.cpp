// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncscreen.h"

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

    const QRegularExpression sizeRx(QStringLiteral("size=(\\d+)x(\\d+)"));
    const QRegularExpression mmSizeRx(QStringLiteral("mmsize=(?<width>(\\d*\\.)?\\d+)x(?<height>(\\d*\\.)?\\d+)"));
    const QRegularExpression depthRx(QStringLiteral("depth=(\\d+)"));

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
        qWarning("QVNCScreen::initDevice: No support for screen depth %d",
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
            qWarning("QVNCScreen::doRedraw: QFbWindow is null");
            mWindowStack.removeAt(i);
            i--;
            continue;
        }
        if (window->window() == nullptr) {
            qWarning("QVNCScreen::doRedraw: QFbWindow->window() is null");
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

        for (qsizetype layerIndex = mWindowStack.size() - 1; layerIndex != -1; layerIndex--) {
            if (!mWindowStack[layerIndex]->window()->isVisible())
                continue;

            const QRect windowRect = mWindowStack[layerIndex]->geometry().translated(-screenOffset);
            const QRect windowIntersect = rect.translated(-windowRect.left(), -windowRect.top());
            if (QFbBackingStore *backingStore = mWindowStack[layerIndex]->backingStore()) {
                backingStore->lock();
                painter.drawImage(rect, backingStore->image(), windowIntersect);
                backingStore->unlock();
            }
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

        const int TILE_SIZE = 64;

        for (const QRect &largeRect : touchedRegion) {

            // Subdivide the large rect into small tiles
            for (int y = largeRect.y(); y <= largeRect.bottom(); y += TILE_SIZE) {
                for (int x = largeRect.x(); x <= largeRect.right(); x += TILE_SIZE) {

                    const int w = qMin(TILE_SIZE, largeRect.right() - x + 1);
                    const int h = qMin(TILE_SIZE, largeRect.bottom() - y + 1);

                    bool tileChanged = false;
                    for (int row = 0; row < h; ++row) {
                        const int lineOffset = ((y + row) * bytesPerLine) + (x * depth);
                        // Compare one scanline of the tile
                        if (memcmp(currBase + lineOffset, prevBase + lineOffset, w * depth) != 0) {
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

    uint clientCount = clientCursor->removeClient(client);
    if (clientCount == 0) {
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
QPixmap QNoVncScreen::grabWindow(WId wid, int x, int y, int width, int height) const
{
    if (!wid) {
        if (width < 0)
            width = mScreenImage.width() - x;
        if (height < 0)
            height = mScreenImage.height() - y;
        return QPixmap::fromImage(mScreenImage).copy(x, y, width, height);
    }

    QFbWindow *window = windowForId(wid);
    if (window) {
        const QRect geom = window->geometry();
        if (width < 0)
            width = geom.width() - x;
        if (height < 0)
            height = geom.height() - y;
        QRect rect(geom.topLeft() + QPoint(x, y), QSize(width, height));
        rect &= window->geometry();
        return QPixmap::fromImage(mScreenImage).copy(rect);
    }

    return QPixmap();
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

QT_END_NAMESPACE

//#include "moc_qvncscreen.cpp"
