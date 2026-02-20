// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNoVncScreen_H
#define QNoVncScreen_H

#include <QtFbSupport/private/qfbscreen_p.h>

QT_BEGIN_NAMESPACE

class QPainter;
class QFbCursor;
class QNoVncServer;
class QNoVncDirtyMap;
class QNoVncClientCursor;
class QNoVncClient;

class QNoVncScreen : public QFbScreen
{
    Q_OBJECT
public:
    QNoVncScreen(const QStringList &args);
    ~QNoVncScreen();

    bool initialize() override;

    QPixmap grabWindow(WId wid, int x, int y, int width, int height) const override;

    QRegion doRedraw() override;
    QImage *image() { return &mScreenImage; }

    void enableClientCursor(QNoVncClient *client);
    void disableClientCursor(QNoVncClient *client);
    QPlatformCursor *cursor() const override;

    Flags flags() const override;

    void clearDirty() { dirtyRegion = QRegion(); }

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    bool swapBytes() const;
#endif

    QStringList mArgs;

    [[nodiscard]] QDpi logicalDpi() const override {
        return QDpi(dpiX, dpiY);
    }

    [[nodiscard]] QSizeF physicalSize() const override {
        constexpr qreal kDpi = 96.0;
        const QSize pixelSize = geometry().size();
        return QSizeF(pixelSize.width() / kDpi * 25.4,
                      pixelSize.height() / kDpi * 25.4);
    }


    qreal dpiX = 96;
    qreal dpiY = 96;
    QNoVncDirtyMap *dirty = nullptr;
    QRegion dirtyRegion;
    int refreshRate = 30;
    bool m_readonly = false;
    QNoVncServer *vncServer = nullptr;
#if QT_CONFIG(cursor)
    QNoVncClientCursor *clientCursor = nullptr;
#endif

private:
    QImage m_prevScreenImage; // Shadow buffer for previous frame
};

QT_END_NAMESPACE

#endif // QNoVncScreen_H
