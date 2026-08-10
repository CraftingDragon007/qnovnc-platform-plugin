// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// Copyright (C) 2016 The Qt Company Ltd.
// This file is a derivative of the qvnc platform plugin from Qt Base
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNC_P_H
#define QNOVNC_P_H


#include "qnovncscreen.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/qbytearray.h>
#include <qpa/qplatformcursor.h>

#include <QTimer>
#include <zlib.h>

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(lcVnc)
Q_DECLARE_LOGGING_CATEGORY(lcVncProtocol)
Q_DECLARE_LOGGING_CATEGORY(lcVncEncoding)
Q_DECLARE_LOGGING_CATEGORY(lcVncInput)
Q_DECLARE_LOGGING_CATEGORY(lcVncPopup)

class QIODevice;
class QWebSocketServer;

class QNoVncScreen;
class QNoVncServer;
class QNoVncClientCursor;
class QNoVncClient;
class QNoVncFrameCache;

// This fits with the VNC hextile messages
#define MAP_TILE_SIZE 16

class QNoVncDirtyMap
{
public:
    explicit QNoVncDirtyMap(QNoVncScreen *screen);
    virtual ~QNoVncDirtyMap();

    void reset();
    [[nodiscard]] bool dirty(int x, int y) const;
    virtual void setDirty(int x, int y, bool force) = 0;
    void setClean(int x, int y);

    QNoVncScreen *screen;
    int bytesPerPixel;
    int numDirty;
    int mapWidth;
    int mapHeight;

protected:
    uchar *map;
    uchar *buffer;
    int bufferWidth;
    int bufferHeight;
    int bufferStride;
    int numTiles;
};

template <class T>
class QNoVncDirtyMapOptimized : public QNoVncDirtyMap
{
public:
    explicit QNoVncDirtyMapOptimized(QNoVncScreen *screen) : QNoVncDirtyMap(screen) {}
    ~QNoVncDirtyMapOptimized() override = default;

    void setDirty(int x, int y, bool force) override;
};


class QRfbRect
{
public:
    QRfbRect() = default;
    QRfbRect(const quint16 _x, const quint16 _y, const quint16 _w, const quint16 _h) {
        x = _x; y = _y; w = _w; h = _h;
    }

    void read(QIODevice *s);
    void write(QIODevice *s) const;

    quint16 x{};
    quint16 y{};
    quint16 w{};
    quint16 h{};
};

class QRfbPixelFormat
{
public:
    QRfbPixelFormat()
        : bitsPerPixel(0),
          depth(0),
          bigEndian(false),
          trueColor(false),
          redBits(0),
          greenBits(0),
          blueBits(0),
          redShift(0),
          greenShift(0),
          blueShift(0)
    {}

    static int size() { return 16; }

    void read(QIODevice *s);
    void write(QIODevice *s) const;

    int bitsPerPixel;
    int depth;
    bool bigEndian;
    bool trueColor;
    int redBits;
    int greenBits;
    int blueBits;
    int redShift;
    int greenShift;
    int blueShift;
};

class QRfbServerInit
{
public:

    [[nodiscard]] qsizetype size() const { return QRfbPixelFormat::size() + 8 + name.toUtf8().size(); }
    void setName(QString name);

    void read(QIODevice *s);
    void write(QIODevice *s) const;

    quint16 width;
    quint16 height;
    QRfbPixelFormat format;
    QString name;
};

class QRfbSetEncodings
{
public:
    bool read(QIODevice *s);

    quint16 count;
};

class QRfbFrameBufferUpdateRequest
{
public:
    bool read(QIODevice *s);

    char incremental{};
    QRfbRect rect;
};

class QRfbKeyEvent
{
public:
    bool read(QIODevice *s);

    char down;
    int  keycode;
    int  unicode;
};

class QRfbPointerEvent
{
public:
    bool read(QIODevice *s);

    Qt::MouseButtons buttons;
    quint16 x{};
    quint16 y{};
};

class QRfbClientCutText
{
public:
    bool read(QIODevice *s);

    quint32 length;
};

class QRfbEncoder
{
public:
    explicit QRfbEncoder(QNoVncClient *s) : client(s) {}
    virtual ~QRfbEncoder() = default;

    virtual void write() = 0;

protected:
    QNoVncClient *client;
};

class QRfbRawEncoder : public QRfbEncoder
{
public:
    explicit QRfbRawEncoder(QNoVncClient *s) : QRfbEncoder(s) {}

    void write() override;

private:
    QByteArray buffer;
};

class QRfbZlibEncoder : public QRfbEncoder
{
public:
    explicit QRfbZlibEncoder(QNoVncClient *s);
    ~QRfbZlibEncoder() override;

    void write() override;

private:
    bool compressCurrentBuffer(qsizetype rawSize, qsizetype *compressedSize);
    void ensurePixelBuffer(qsizetype size);
    void ensureCompressedBuffer(qsizetype minimumSize);

    QByteArray m_pixelBuffer;
    QByteArray m_compressBuffer;
    z_stream m_stream{};
    bool m_streamInitialized = false;
};

#if QT_CONFIG(cursor)
class QNoVncClientCursor : public QPlatformCursor
{
public:
    QNoVncClientCursor();
    ~QNoVncClientCursor() override;

    void write(const QNoVncClient *client) const;

    void changeCursor(QCursor *widgetCursor, QWindow *window) override;

    void addClient(QNoVncClient *client);
    uint removeClient(QNoVncClient *client);

    QImage cursor;
    QPoint hotspot;
    QList<QNoVncClient *> clients;
};
#endif // QT_CONFIG(cursor)

class QNoVncServer : public QObject
{
    Q_OBJECT
public:
    explicit QNoVncServer(QNoVncScreen *screen, quint16 port = 5900, QString host = "0.0.0.0");
    ~QNoVncServer() override;

    enum ServerMsg { FramebufferUpdate = 0,
                     SetColourMapEntries = 1 };

    void setDirty();


    [[nodiscard]] inline QNoVncScreen* screen() const { return QNoVnc_screen; }
    [[nodiscard]] inline QNoVncDirtyMap* dirtyMap() const { return QNoVnc_screen->dirty; }
    [[nodiscard]] QImage screenImage() const;
    [[nodiscard]] QNoVncFrameCache *frameCache() const { return m_frameCache; }
    void discardClient(QNoVncClient *client);

private slots:
    void newConnection();
    void init();

private:
    QWebSocketServer *serverSocket{};
    QList<QNoVncClient*> clients;
    QNoVncScreen *QNoVnc_screen;
    quint16 m_port;
    QString m_host;
    QNoVncFrameCache *m_frameCache;

    QTimer* m_visualizeUpdateTimer{};
};

QT_END_NAMESPACE

#endif
