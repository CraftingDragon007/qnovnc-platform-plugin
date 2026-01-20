// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QVNC_P_H
#define QVNC_P_H


#include "qnovncscreen.h"

#include <QtCore/QLoggingCategory>
#include <QtCore/qbytearray.h>
#include <QtCore/qvarlengtharray.h>
#include <qpa/qplatformcursor.h>

#include <QTimer>
#include <zlib.h>

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(lcVnc)

class QIODevice;
class QWebSocketServer;

class QNoVncScreen;
class QNoVncServer;
class QNoVncClientCursor;
class QNoVncClient;

// This fits with the VNC hextile messages
#define MAP_TILE_SIZE 16

class QNoVncDirtyMap
{
public:
    QNoVncDirtyMap(QNoVncScreen *screen);
    virtual ~QNoVncDirtyMap();

    void reset();
    bool dirty(int x, int y) const;
    virtual void setDirty(int x, int y, bool force = false) = 0;
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
    QNoVncDirtyMapOptimized(QNoVncScreen *screen) : QNoVncDirtyMap(screen) {}
    ~QNoVncDirtyMapOptimized() {}

    void setDirty(int x, int y, bool force = false) override;
};


class QRfbRect
{
public:
    QRfbRect() {}
    QRfbRect(quint16 _x, quint16 _y, quint16 _w, quint16 _h) {
        x = _x; y = _y; w = _w; h = _h;
    }

    void read(QIODevice *s);
    void write(QIODevice *s) const;

    quint16 x;
    quint16 y;
    quint16 w;
    quint16 h;
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
    void write(QIODevice *s);

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
    QRfbServerInit() { name = nullptr; }
    ~QRfbServerInit() { delete[] name; }

    int size() const { return QRfbPixelFormat::size() + 8 + strlen(name); }
    void setName(const char *n);

    void read(QIODevice *s);
    void write(QIODevice *s);

    quint16 width;
    quint16 height;
    QRfbPixelFormat format;
    char *name;
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

    char incremental;
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
    quint16 x;
    quint16 y;
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
    QRfbEncoder(QNoVncClient *s) : client(s) {}
    virtual ~QRfbEncoder() {}

    virtual void write() = 0;

protected:
    QNoVncClient *client;
};

class QRfbRawEncoder : public QRfbEncoder
{
public:
    QRfbRawEncoder(QNoVncClient *s) : QRfbEncoder(s) {}

    void write() override;

private:
    QByteArray buffer;
};

class QRfbZlibEncoder : public QRfbEncoder
{
public:
    QRfbZlibEncoder(QNoVncClient *s);
    ~QRfbZlibEncoder() override;

    void write() override;

private:
    bool compressCurrentBuffer(qsizetype rawSize, qsizetype *compressedSize);
    void ensurePixelBuffer(qsizetype size);
    void ensureCompressedBuffer(qsizetype minimumSize);

    QByteArray m_pixelBuffer;
    QByteArray m_compressBuffer;
    z_stream m_stream;
    bool m_streamInitialized = false;
};

/*
template <class SRC> class QRfbHextileEncoder;

template <class SRC>
class QRfbSingleColorHextile
{
public:
    QRfbSingleColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QIODevice *socket) const;

private:
    QRfbHextileEncoder<SRC> *encoder;
};

template <class SRC>
class QRfbDualColorHextile
{
public:
    QRfbDualColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QIODevice *socket) const;

private:
    struct Rect {
        quint8 xy;
        quint8 wh;
    } Q_PACKED rects[8 * 16];

    quint8 numRects;
    QRfbHextileEncoder<SRC> *encoder;

private:
    inline int lastx() const { return rectx(numRects); }
    inline int lasty() const { return recty(numRects); }
    inline int rectx(int r) const { return rects[r].xy >> 4; }
    inline int recty(int r) const { return rects[r].xy & 0x0f; }
    inline int width(int r) const { return (rects[r].wh >> 4) + 1; }
    inline int height(int r) const { return (rects[r].wh & 0x0f) + 1; }

    inline void setX(int r, int x) {
        rects[r].xy = (x << 4) | (rects[r].xy & 0x0f);
    }
    inline void setY(int r, int y) {
        rects[r].xy = (rects[r].xy & 0xf0) | y;
    }
    inline void setWidth(int r, int width) {
        rects[r].wh = ((width - 1) << 4) | (rects[r].wh & 0x0f);
    }
    inline void setHeight(int r, int height) {
        rects[r].wh = (rects[r].wh & 0xf0) | (height - 1);
    }

    inline void setWidth(int width) { setWidth(numRects, width); }
    inline void setHeight(int height) { setHeight(numRects, height); }
    inline void setX(int x) { setX(numRects, x); }
    inline void setY(int y) { setY(numRects, y); }
    void next();
};

template <class SRC>
class QRfbMultiColorHextile
{
public:
    QRfbMultiColorHextile(QRfbHextileEncoder<SRC> *e) : encoder(e) {}
    bool read(const uchar *data, int width, int height, int stride);
    void write(QIODevice *socket) const;

private:
    inline quint8* rect(int r) {
        return rects.data() + r * (bpp + 2);
    }
    inline const quint8* rect(int r) const {
        return rects.constData() + r * (bpp + 2);
    }
    inline void setX(int r, int x) {
        quint8 *ptr = rect(r) + bpp;
        *ptr = (x << 4) | (*ptr & 0x0f);
    }
    inline void setY(int r, int y) {
        quint8 *ptr = rect(r) + bpp;
        *ptr = (*ptr & 0xf0) | y;
    }
    void setColor(SRC color);
    inline int rectx(int r) const {
        const quint8 *ptr = rect(r) + bpp;
        return *ptr >> 4;
    }
    inline int recty(int r) const {
        const quint8 *ptr = rect(r) + bpp;
        return *ptr & 0x0f;
    }
    inline void setWidth(int r, int width) {
        quint8 *ptr = rect(r) + bpp + 1;
        *ptr = ((width - 1) << 4) | (*ptr & 0x0f);
    }
    inline void setHeight(int r, int height) {
        quint8 *ptr = rect(r) + bpp + 1;
        *ptr = (*ptr & 0xf0) | (height - 1);
    }

    bool beginRect();
    void endRect();

    static const int maxRectsSize = 16 * 16;
    QVarLengthArray<quint8, maxRectsSize> rects;

    quint8 bpp;
    quint8 numRects;
    QRfbHextileEncoder<SRC> *encoder;
};

template <class SRC>
class QRfbHextileEncoder : public QRfbEncoder
{
public:
    QRfbHextileEncoder(QNoVncServer *s);
    void write();

private:
    enum SubEncoding {
        Raw = 1,
        BackgroundSpecified = 2,
        ForegroundSpecified = 4,
        AnySubrects = 8,
        SubrectsColoured = 16
    };

    QByteArray buffer;
    QRfbSingleColorHextile<SRC> singleColorHextile;
    QRfbDualColorHextile<SRC> dualColorHextile;
    QRfbMultiColorHextile<SRC> multiColorHextile;

    SRC bg;
    SRC fg;
    bool newBg;
    bool newFg;

    friend class QRfbSingleColorHextile<SRC>;
    friend class QRfbDualColorHextile<SRC>;
    friend class QRfbMultiColorHextile<SRC>;
};*/

#if QT_CONFIG(cursor)
class QNoVncClientCursor : public QPlatformCursor
{
public:
    QNoVncClientCursor();
    ~QNoVncClientCursor();

    void write(QNoVncClient *client) const;

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
    ~QNoVncServer();

    enum ServerMsg { FramebufferUpdate = 0,
                     SetColourMapEntries = 1 };

    void setDirty();


    inline QNoVncScreen* screen() const { return QNoVnc_screen; }
    inline QNoVncDirtyMap* dirtyMap() const { return QNoVnc_screen->dirty; }
    QImage screenImage() const;
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

    QTimer* m_visualizeUpdateTimer;
};

QT_END_NAMESPACE

#endif
