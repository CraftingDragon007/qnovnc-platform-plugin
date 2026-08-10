// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// Copyright (C) 2016 The Qt Company Ltd.
// This file is a derivative of the qvnc platform plugin from Qt Base
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
class QNoVncWindow;
class QPlatformOpenGLContext;
class QSurfaceFormat;

class QNoVncScreen : public QFbScreen
{
    Q_OBJECT
public:
    explicit QNoVncScreen(const QStringList &args);
    ~QNoVncScreen() override;

    bool initialize() override;

    [[nodiscard]] QPixmap grabWindow(WId wid, int x, int y, int width, int height) const override;
    void removeWindow(QFbWindow *window) override;

    QRegion doRedraw() override;
    QImage *image() { return &mScreenImage; }

    void setMouseGrabber(QNoVncWindow *window);
    void clearMouseGrabber(QNoVncWindow *window);
    [[nodiscard]] QNoVncWindow *mouseGrabber() const;

    void setKeyboardGrabber(QNoVncWindow *window);
    void clearKeyboardGrabber(QNoVncWindow *window);
    [[nodiscard]] QNoVncWindow *keyboardGrabber() const;

    void clearWindowGrabs(QNoVncWindow *window);
    [[nodiscard]] QNoVncWindow *topMostPopup() const;

    void enableClientCursor(QNoVncClient *client);
    void disableClientCursor(QNoVncClient *client);
    [[nodiscard]] QPlatformCursor *cursor() const override;

    [[nodiscard]] Flags flags() const override;

    [[nodiscard]] QPlatformOpenGLContext *platformContext() const;
    void createAndSetPlatformContext(const QSurfaceFormat &format) const;
    void createAndSetPlatformContext(const QSurfaceFormat &format);

    void clearDirty() { dirtyRegion = QRegion(); }

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    bool swapBytes() const;
#endif

    QStringList mArgs;

    [[nodiscard]] QDpi logicalDpi() const override {
        return {dpiX, dpiY};
    }

    [[nodiscard]] QSizeF physicalSize() const override {
        constexpr qreal kDpi = 96.0;
        const QSize pixelSize = geometry().size();
        return {pixelSize.width() / kDpi * 25.4,
                      pixelSize.height() / kDpi * 25.4};
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
    QPlatformOpenGLContext *m_platformContext = nullptr;

private:
    [[nodiscard]] QNoVncWindow *validGrabber(QNoVncWindow *window) const;

    QNoVncWindow *m_mouseGrabber = nullptr;
    QNoVncWindow *m_keyboardGrabber = nullptr;
    QImage m_prevScreenImage; // Shadow buffer for previous frame
};

QT_END_NAMESPACE

#endif // QNoVncScreen_H
