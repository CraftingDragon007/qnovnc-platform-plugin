// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#include "qnovnc_p.h"
#include "qnovncscreen.h"
#include "qnovncclient.h"
#include "qnovncframecache.h"
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include <qendian.h>
#include <qthread.h>

#include <QtGui/qguiapplication.h>
#include <QtGui/QWindow>
#include <QtGui/QPainter>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <QtCore/QDebug>
#include <utility>
#include <limits>

#ifdef max
#undef max
#endif

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcVnc, "qt.qpa.novnc");

QNoVncDirtyMap::QNoVncDirtyMap(QNoVncScreen *screen)
    : screen(screen), bytesPerPixel(0), numDirty(0)
{
    bytesPerPixel = (screen->depth() + 7) / 8;
    bufferWidth = screen->geometry().width();
    bufferHeight = screen->geometry().height();
    bufferStride = bufferWidth * bytesPerPixel;
    buffer = new uchar[bufferHeight * bufferStride];

    mapWidth = (bufferWidth + MAP_TILE_SIZE - 1) / MAP_TILE_SIZE;
    mapHeight = (bufferHeight + MAP_TILE_SIZE - 1) / MAP_TILE_SIZE;
    numTiles = mapWidth * mapHeight;
    map = new uchar[numTiles];
}

QNoVncDirtyMap::~QNoVncDirtyMap()
{
    delete[] map;
    delete[] buffer;
}

void QNoVncDirtyMap::reset()
{
    memset(map, 1, numTiles);
    memset(buffer, 0, bufferHeight * bufferStride);
    numDirty = numTiles;
}

inline bool QNoVncDirtyMap::dirty(int x, int y) const
{
    return map[y * mapWidth + x];
}

inline void QNoVncDirtyMap::setClean(int x, int y)
{
    map[y * mapWidth + x] = 0;
    --numDirty;
}

template <class T>
void QNoVncDirtyMapOptimized<T>::setDirty(int tileX, int tileY, bool force)
{
    static bool alwaysForce = qEnvironmentVariableIsSet("QT_VNC_NO_COMPAREBUFFER");
    if (alwaysForce)
        force = true;

    bool changed = false;

    if (!force) {
        const int lstep = bufferStride;
        const int startX = tileX * MAP_TILE_SIZE;
        const int startY = tileY * MAP_TILE_SIZE;
        const uchar *scrn = screen->image()->constBits()
                            + startY * lstep + startX * bytesPerPixel;
        uchar *old = buffer + startY * bufferStride + startX * sizeof(T);

        const int tileHeight = (startY + MAP_TILE_SIZE > bufferHeight ?
                                bufferHeight - startY : MAP_TILE_SIZE);
        const int tileWidth = (startX + MAP_TILE_SIZE > bufferWidth ?
                               bufferWidth - startX : MAP_TILE_SIZE);
        const bool doInlines = (tileWidth == MAP_TILE_SIZE);

        int y = tileHeight;

        if (doInlines) { // hw: memcmp/memcpy is inlined when using constants
            while (y) {
                if (memcmp(old, scrn, sizeof(T) * MAP_TILE_SIZE)) {
                    changed = true;
                    break;
                }
                scrn += lstep;
                old += bufferStride;
                --y;
            }

            while (y) {
                memcpy(old, scrn, sizeof(T) * MAP_TILE_SIZE);
                scrn += lstep;
                old += bufferStride;
                --y;
            }
        } else {
            while (y) {
                if (memcmp(old, scrn, sizeof(T) * tileWidth)) {
                    changed = true;
                    break;
                }
                scrn += lstep;
                old += bufferStride;
                --y;
            }

            while (y) {
                memcpy(old, scrn, sizeof(T) * tileWidth);
                scrn += lstep;
                old += bufferStride;
                --y;
            }
        }
    }

    const int mapIndex = tileY * mapWidth + tileX;
    if ((force || changed) && !map[mapIndex]) {
        map[mapIndex] = 1;
        ++numDirty;
    }
}

template class QNoVncDirtyMapOptimized<unsigned char>;
template class QNoVncDirtyMapOptimized<unsigned short>;
template class QNoVncDirtyMapOptimized<unsigned int>;

static const struct {
    int keysym;
    int keycode;
} keyMap[] = {
    { 0xff08, Qt::Key_Backspace },
    { 0xff09, Qt::Key_Tab       },
    { 0xff0d, Qt::Key_Return    },
    { 0xff1b, Qt::Key_Escape    },
    { 0xff63, Qt::Key_Insert    },
    { 0xffff, Qt::Key_Delete    },
    { 0xff50, Qt::Key_Home      },
    { 0xff57, Qt::Key_End       },
    { 0xff55, Qt::Key_PageUp    },
    { 0xff56, Qt::Key_PageDown  },
    { 0xff51, Qt::Key_Left      },
    { 0xff52, Qt::Key_Up        },
    { 0xff53, Qt::Key_Right     },
    { 0xff54, Qt::Key_Down      },
    { 0xffbe, Qt::Key_F1        },
    { 0xffbf, Qt::Key_F2        },
    { 0xffc0, Qt::Key_F3        },
    { 0xffc1, Qt::Key_F4        },
    { 0xffc2, Qt::Key_F5        },
    { 0xffc3, Qt::Key_F6        },
    { 0xffc4, Qt::Key_F7        },
    { 0xffc5, Qt::Key_F8        },
    { 0xffc6, Qt::Key_F9        },
    { 0xffc7, Qt::Key_F10       },
    { 0xffc8, Qt::Key_F11       },
    { 0xffc9, Qt::Key_F12       },
    { 0xffe1, Qt::Key_Shift     },
    { 0xffe2, Qt::Key_Shift     },
    { 0xffe3, Qt::Key_Control   },
    { 0xffe4, Qt::Key_Control   },
    { 0xffe7, Qt::Key_Meta      },
    { 0xffe8, Qt::Key_Meta      },
    { 0xffe9, Qt::Key_Alt       },
    { 0xffea, Qt::Key_Alt       },

    { 0xffb0, Qt::Key_0         },
    { 0xffb1, Qt::Key_1         },
    { 0xffb2, Qt::Key_2         },
    { 0xffb3, Qt::Key_3         },
    { 0xffb4, Qt::Key_4         },
    { 0xffb5, Qt::Key_5         },
    { 0xffb6, Qt::Key_6         },
    { 0xffb7, Qt::Key_7         },
    { 0xffb8, Qt::Key_8         },
    { 0xffb9, Qt::Key_9         },

    { 0xff8d, Qt::Key_Return    },
    { 0xffaa, Qt::Key_Asterisk  },
    { 0xffab, Qt::Key_Plus      },
    { 0xffad, Qt::Key_Minus     },
    { 0xffae, Qt::Key_Period    },
    { 0xffaf, Qt::Key_Slash     },

    { 0xff95, Qt::Key_Home      },
    { 0xff96, Qt::Key_Left      },
    { 0xff97, Qt::Key_Up        },
    { 0xff98, Qt::Key_Right     },
    { 0xff99, Qt::Key_Down      },
    { 0xff9a, Qt::Key_PageUp    },
    { 0xff9b, Qt::Key_PageDown  },
    { 0xff9c, Qt::Key_End       },
    { 0xff9e, Qt::Key_Insert    },
    { 0xff9f, Qt::Key_Delete    },

    { 0, 0 }
};

void QRfbRect::read(QIODevice *s)
{
    quint16 buf[4];
    s->read(reinterpret_cast<char*>(buf), 8);
    x = ntohs(buf[0]);
    y = ntohs(buf[1]);
    w = ntohs(buf[2]);
    h = ntohs(buf[3]);
}

void QRfbRect::write(QIODevice *s) const
{
    quint16 buf[4];
    buf[0] = htons(x);
    buf[1] = htons(y);
    buf[2] = htons(w);
    buf[3] = htons(h);
    s->write(reinterpret_cast<char*>(buf) , 8);
}

void QRfbPixelFormat::read(QIODevice *s)
{
    char buf[16];
    s->read(buf, 16);
    bitsPerPixel = buf[0];
    depth = buf[1];
    bigEndian = buf[2];
    trueColor = buf[3];

    quint16 a = ntohs(*reinterpret_cast<quint16 *>(buf + 4));
    redBits = 0;
    while (a) { a >>= 1; redBits++; }

    a = ntohs(*reinterpret_cast<quint16 *>(buf + 6));
    greenBits = 0;
    while (a) { a >>= 1; greenBits++; }

    a = ntohs(*reinterpret_cast<quint16 *>(buf + 8));
    blueBits = 0;
    while (a) { a >>= 1; blueBits++; }

    redShift = buf[10];
    greenShift = buf[11];
    blueShift = buf[12];
}

void QRfbPixelFormat::write(QIODevice *s)
{
    char buf[16];
    buf[0] = bitsPerPixel;
    buf[1] = depth;
    buf[2] = bigEndian;
    buf[3] = trueColor;

    quint16 a = 0;
    for (int i = 0; i < redBits; i++) a = (a << 1) | 1;
    *reinterpret_cast<quint16 *>(buf + 4) = htons(a);

    a = 0;
    for (int i = 0; i < greenBits; i++) a = (a << 1) | 1;
    *reinterpret_cast<quint16 *>(buf + 6) = htons(a);

    a = 0;
    for (int i = 0; i < blueBits; i++) a = (a << 1) | 1;
    *reinterpret_cast<quint16 *>(buf + 8) = htons(a);

    buf[10] = redShift;
    buf[11] = greenShift;
    buf[12] = blueShift;
    s->write(buf, 16);
}


void QRfbServerInit::setName(const char *n)
{
    delete[] name;
    name = new char [strlen(n) + 1];
    strcpy(name, n);
}

void QRfbServerInit::read(QIODevice *s)
{
    s->read(reinterpret_cast<char *>(&width), 2);
    width = ntohs(width);
    s->read(reinterpret_cast<char *>(&height), 2);
    height = ntohs(height);
    format.read(s);

    quint32 len;
    s->read(reinterpret_cast<char *>(&len), 4);
    len = ntohl(len);

    name = new char [len + 1];
    s->read(name, len);
    name[len] = '\0';
}

void QRfbServerInit::write(QIODevice *s)
{
    quint16 t = htons(width);
    s->write(reinterpret_cast<char *>(&t), 2);
    t = htons(height);
    s->write(reinterpret_cast<char *>(&t), 2);
    format.write(s);
    quint32 len = static_cast<quint32>(strlen(name));
    len = htonl(len);
    s->write(reinterpret_cast<char *>(&len), 4);
    s->write(name, static_cast<qint64>(strlen(name)));
}

bool QRfbSetEncodings::read(QIODevice *s)
{
    if (s->bytesAvailable() < 3)
        return false;

    char tmp;
    s->read(&tmp, 1);        // padding
    s->read(reinterpret_cast<char *>(&count), 2);
    count = ntohs(count);

    return true;
}

bool QRfbFrameBufferUpdateRequest::read(QIODevice *s)
{
    if (s->bytesAvailable() < 9)
        return false;

    s->read(&incremental, 1);
    rect.read(s);

    return true;
}

bool QRfbKeyEvent::read(QIODevice *s)
{
    if (s->bytesAvailable() < 7)
        return false;

    s->read(&down, 1);
    quint16 tmp;
    s->read(reinterpret_cast<char *>(&tmp), 2);  // padding

    quint32 key;
    s->read(reinterpret_cast<char *>(&key), 4);
    key = ntohl(key);

    unicode = 0;
    keycode = 0;
    int i = 0;
    while (keyMap[i].keysym && !keycode) {
        if (keyMap[i].keysym == static_cast<int>(key))
            keycode = keyMap[i].keycode;
        i++;
    }

    if (keycode >= ' ' && keycode <= '~')
        unicode = keycode;

    if (!keycode) {
        if (key <= 0xff) {
            unicode = key;
            if (key >= 'a' && key <= 'z')
                keycode = Qt::Key_A + key - 'a';
            else if (key >= ' ' && key <= '~')
                keycode = Qt::Key_Space + key - ' ';
        }
    }

    return true;
}

bool QRfbPointerEvent::read(QIODevice *s)
{
    if (s->bytesAvailable() < 5)
        return false;

    char buttonMask;
    s->read(&buttonMask, 1);
    buttons = Qt::NoButton;
    if (buttonMask & 1)
        buttons |= Qt::LeftButton;
    if (buttonMask & 2)
        buttons |= Qt::MiddleButton;
    if (buttonMask & 4)
        buttons |= Qt::RightButton;

    quint16 tmp;
    s->read(reinterpret_cast<char *>(&tmp), 2);
    x = ntohs(tmp);
    s->read(reinterpret_cast<char *>(&tmp), 2);
    y = ntohs(tmp);

    return true;
}

bool QRfbClientCutText::read(QIODevice *s)
{
    if (s->bytesAvailable() < 7)
        return false;

    char tmp[3];
    s->read(tmp, 3);        // padding
    s->read(reinterpret_cast<char *>(&length), 4);
    length = ntohl(length);

    return true;
}

void QRfbRawEncoder::write()
{
    QIODevice *socket = client->clientSocket();

    const int bytesPerPixel = client->clientBytesPerPixel();
    QRegion rgn = client->dirtyRegion();
    qCDebug(lcVnc) << "QRfbRawEncoder::write()" << rgn;

    QImage screenImage = client->server()->screenImage();

    if (qEnvironmentVariableIntValue("QNOVNC_VISUALIZE_UPDATE") == 1 && !rgn.isEmpty()) {
        QPainter p(&screenImage);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.fillRect(rgn.boundingRect(), QColor(0, 0, 255, 64));
        p.end();
    }

    rgn &= screenImage.rect();

    const auto rectsInRegion = rgn.rectCount();

    {
        const char tmp[2] = { 0, 0 }; // msg type, padding
        socket->write(tmp, sizeof(tmp));
    }

    {
        const quint16 count = htons(rectsInRegion);
        socket->write(reinterpret_cast<const char *>(&count), sizeof(count));
    }

    if (rectsInRegion <= 0)
        return;

    for (const QRect &tileRect: rgn) {
        const QRfbRect rect(tileRect.x(), tileRect.y(),
                            tileRect.width(), tileRect.height());
        rect.write(socket);

        const quint32 encoding = htonl(0); // raw encoding
        socket->write(reinterpret_cast<const char *>(&encoding), sizeof(encoding));

        if (client->doPixelConversion()) {
            QByteArray pixels = client->server()->frameCache()->getConvertedPixels(
                screenImage, tileRect, client->pixelFormat());
            socket->write(pixels.constData(), pixels.size());
        } else {
            qsizetype linestep = screenImage.bytesPerLine();
            const uchar *screendata = screenImage.scanLine(rect.y)
                                      + rect.x * screenImage.depth() / 8;
            for (int i = 0; i < rect.h; ++i) {
                socket->write(reinterpret_cast<const char*>(screendata), rect.w * bytesPerPixel);
                screendata += linestep;
            }
        }
    }
}

QRfbZlibEncoder::QRfbZlibEncoder(QNoVncClient *s)
    : QRfbEncoder(s)
{
    memset(&m_stream, 0, sizeof(m_stream));
}

QRfbZlibEncoder::~QRfbZlibEncoder()
{
    if (m_streamInitialized)
        deflateEnd(&m_stream);
}

void QRfbZlibEncoder::ensurePixelBuffer(qsizetype size)
{
    if (m_pixelBuffer.size() < size)
        m_pixelBuffer.resize(size);
}

void QRfbZlibEncoder::ensureCompressedBuffer(qsizetype minimumSize)
{
    if (m_compressBuffer.size() < minimumSize)
        m_compressBuffer.resize(minimumSize);
}

bool QRfbZlibEncoder::compressCurrentBuffer(qsizetype rawSize, qsizetype *compressedSize)
{
    if (!m_streamInitialized) {
        m_stream.zalloc = Z_NULL;
        m_stream.zfree = Z_NULL;
        m_stream.opaque = Z_NULL;
        if (deflateInit(&m_stream, 2) != Z_OK) {
            qWarning(lcVnc) << "Failed to initialize zlib stream";
            return false;
        }
        m_streamInitialized = true;
    }

    if (rawSize <= 0 || rawSize > std::numeric_limits<uLong>::max()) {
        qWarning(lcVnc) << "Rectangle too large for zlib compression" << rawSize;
        return false;
    }

    uLong bound = deflateBound(&m_stream, static_cast<uLong>(rawSize));
    bound += 6; // extra headroom for Z_SYNC_FLUSH trailer
    if (bound > std::numeric_limits<uInt>::max()) {
        qWarning(lcVnc) << "zlib bound exceeds supported size" << bound;
        return false;
    }
    ensureCompressedBuffer(static_cast<qsizetype>(bound));

    m_stream.next_in = reinterpret_cast<Bytef *>(m_pixelBuffer.data());
    m_stream.avail_in = static_cast<uInt>(rawSize);
    m_stream.next_out = reinterpret_cast<Bytef *>(m_compressBuffer.data());
    m_stream.avail_out = static_cast<uInt>(m_compressBuffer.size());

    while (m_stream.avail_in > 0) {
        const int ret = deflate(&m_stream, Z_SYNC_FLUSH);
        if (ret != Z_OK) {
            qWarning(lcVnc) << "zlib compression failed" << ret;
            deflateEnd(&m_stream);
            memset(&m_stream, 0, sizeof(m_stream));
            m_streamInitialized = false;
            return false;
        }
    }

    *compressedSize = m_compressBuffer.size() - m_stream.avail_out;

    return true;
}

void QRfbZlibEncoder::write()
{
    QIODevice *socket = client->clientSocket();
    const int bytesPerPixel = client->clientBytesPerPixel();
    QRegion rgn = client->dirtyRegion();
    qCDebug(lcVnc) << "QRfbZlibEncoder::write()" << rgn;

    QImage screenImage = client->server()->screenImage();

    if (qEnvironmentVariableIntValue("QNOVNC_VISUALIZE_UPDATE") == 1 && !rgn.isEmpty()) {
        QPainter p(&screenImage);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.fillRect(rgn.boundingRect(), QColor(0, 0, 255, 64));
        p.end();
    }

    rgn &= screenImage.rect();

    const int rectsInRegion = rgn.rectCount();

    {
        const char tmp[2] = { 0, 0 };
        socket->write(tmp, sizeof(tmp));
    }

    {
        const quint16 count = htons(static_cast<quint16>(rectsInRegion));
        socket->write(reinterpret_cast<const char *>(&count), sizeof(count));
    }

    if (rectsInRegion <= 0)
        return;

    const bool needConversion = client->doPixelConversion();
    const int screenDepth = screenImage.depth();

    for (const QRect &tileRect : rgn) {
        const QRfbRect rect(tileRect.x(), tileRect.y(),
                            tileRect.width(), tileRect.height());
        rect.write(socket);

        const qsizetype rowBytes = qsizetype(rect.w) * bytesPerPixel;
        const qsizetype rawSize = rowBytes * rect.h;
        
        QByteArray rawData;
        if (needConversion) {
            rawData = client->server()->frameCache()->getConvertedPixels(
                screenImage, tileRect, client->pixelFormat());
        } else {
            ensurePixelBuffer(rawSize);
            char *dst = m_pixelBuffer.data();
            const qsizetype linestep = screenImage.bytesPerLine();
            const uchar *screendata = screenImage.scanLine(rect.y)
                                      + rect.x * screenImage.depth() / 8;
            for (int i = 0; i < rect.h; ++i) {
                memcpy(dst, screendata, rowBytes);
                screendata += linestep;
                dst += rowBytes;
            }
            rawData = m_pixelBuffer.left(rawSize); // Copy for deflate
        }

        // We MUST use a per-client buffer for compression because deflate is stateful
        m_pixelBuffer = rawData; 
        
        qsizetype compressedSize = 0;
        bool useZlib = compressCurrentBuffer(rawSize, &compressedSize);
        if (useZlib) {
            const quint32 encoding = htonl(6);
            socket->write(reinterpret_cast<const char *>(&encoding), sizeof(encoding));
            const quint32 length = htonl(static_cast<quint32>(compressedSize));
            socket->write(reinterpret_cast<const char *>(&length), sizeof(length));
            socket->write(m_compressBuffer.constData(), compressedSize);
        } else {
            const quint32 encoding = htonl(0);
            socket->write(reinterpret_cast<const char *>(&encoding), sizeof(encoding));
            socket->write(m_pixelBuffer.constData(), rawSize);
        }
    }
}

#if QT_CONFIG(cursor)
QNoVncClientCursor::QNoVncClientCursor()
{
    QWindow *w = QGuiApplication::focusWindow();
    QCursor c = w ? w->cursor() : QCursor(Qt::ArrowCursor);
    changeCursor(&c, nullptr);
}

QNoVncClientCursor::~QNoVncClientCursor()
{
}

void QNoVncClientCursor::write(QNoVncClient *client) const
{
    QIODevice *socket = client->clientSocket();

    // FramebufferUpdate header
    {
        const quint16 tmp[6] = { htons(0),
                                 htons(1),
                                 htons(static_cast<uint16_t>(hotspot.x())), htons(static_cast<uint16_t>(hotspot.y())),
                                 htons(static_cast<uint16_t>(cursor.width())),
                                 htons(static_cast<uint16_t>(cursor.height())) };
        socket->write(reinterpret_cast<const char*>(tmp), sizeof(tmp));

        const qint32 encoding = qToBigEndian(-239);
        socket->write(reinterpret_cast<const char*>(&encoding), sizeof(encoding));
    }

    if (cursor.isNull())
        return;

    // write pixels
    Q_ASSERT(cursor.hasAlphaChannel());
    const QImage img = cursor.convertToFormat(client->server()->screen()->format());
    const int n = client->clientBytesPerPixel() * img.width();
    const int depth = img.depth();
    char *buffer = new char[n];
    for (int i = 0; i < img.height(); ++i) {
        client->convertPixels(buffer, (const char*)img.scanLine(i), img.width(), depth);
        socket->write(buffer, n);
    }
    delete[] buffer;

    // write mask
    const QImage bitmap = cursor.createAlphaMask().convertToFormat(QImage::Format_Mono);
    Q_ASSERT(bitmap.depth() == 1);
    Q_ASSERT(bitmap.size() == img.size());
    const int width = (bitmap.width() + 7) / 8;
    for (int i = 0; i < bitmap.height(); ++i)
        socket->write(reinterpret_cast<const char*>(bitmap.scanLine(i)), width);
}

void QNoVncClientCursor::changeCursor(QCursor *widgetCursor, QWindow *window)
{
    Q_UNUSED(window);
    const Qt::CursorShape shape = widgetCursor ? widgetCursor->shape() : Qt::ArrowCursor;

    if (shape == Qt::BitmapCursor) {
        // application supplied cursor
        hotspot = widgetCursor->hotSpot();
        cursor = widgetCursor->pixmap().toImage();
    } else {
        // system cursor
        QPlatformCursorImage platformImage(nullptr, nullptr, 0, 0, 0, 0);
        platformImage.set(shape);
        cursor = *platformImage.image();
        hotspot = platformImage.hotspot();
    }
    for (auto client : std::as_const(clients))
        client->setDirtyCursor();
}

void QNoVncClientCursor::addClient(QNoVncClient *client)
{
    if (!clients.contains(client)) {
        clients.append(client);
        // Force a cursor update when the client connects.
        client->setDirtyCursor();
    }
}

uint QNoVncClientCursor::removeClient(QNoVncClient *client)
{
    clients.removeOne(client);
    return clients.size();
}
#endif // QT_CONFIG(cursor)

QNoVncServer::QNoVncServer(QNoVncScreen *screen, quint16 port, QString host)
    : QNoVnc_screen(screen)
    , m_port(port)
    , m_host(std::move(host))
    , m_frameCache(new QNoVncFrameCache(this))
{
    QMetaObject::invokeMethod(this, "init", Qt::QueuedConnection);
}

void QNoVncServer::init()
{
    serverSocket = new QWebSocketServer(QStringLiteral("QNoVNC Server"),
                                        QWebSocketServer::NonSecureMode, this);
    if (!serverSocket->listen(QHostAddress(m_host), m_port))
        qWarning() << "QNoVncServer could not connect:" << serverSocket->errorString();
    else
        qWarning("QNoVncServer created on port %d on host %s", m_port, m_host.toStdString().c_str());

    connect(serverSocket, SIGNAL(newConnection()), this, SLOT(newConnection()));

    m_visualizeUpdateTimer = new QTimer(this);
    m_visualizeUpdateTimer->setInterval(1000 * 20);

    connect(m_visualizeUpdateTimer, &QTimer::timeout, [] {
        if (qEnvironmentVariableIntValue("QNOVNC_VISUALIZE_UPDATE") == 1)
        {
            qputenv("QNOVNC_VISUALIZE_UPDATE", "0");
            qWarning("QNOVNC_VISUALIZE_UPDATE is now disabled");
        } else {
            qputenv("QNOVNC_VISUALIZE_UPDATE", "1");
            qWarning("QNOVNC_VISUALIZE_UPDATE is now enabled for 20 seconds");
        }
    });

    if (qEnvironmentVariableIntValue("QNOVNC_VISUALIZE_UPDATE") == 1)
        m_visualizeUpdateTimer->start();
}

QNoVncServer::~QNoVncServer()
{
    for (const auto client : std::as_const(clients)) {
        disconnect(client, nullptr, this, nullptr);
        disconnect(this, nullptr, client, nullptr);
    }

    qDeleteAll(clients);
    clients.clear();
}

void QNoVncServer::setDirty()
{
    m_frameCache->invalidate();
    for (auto client : std::as_const(clients))
        client->setDirty(QNoVnc_screen->dirtyRegion);

    QNoVnc_screen->clearDirty();
}


void QNoVncServer::newConnection()
{
    auto clientSocket = serverSocket->nextPendingConnection();
    clients.append(new QNoVncClient(clientSocket, this));

    dirtyMap()->reset();

    qCDebug(lcVnc) << "new Connection from: " << clientSocket->localAddress();

    QNoVnc_screen->setPowerState(QPlatformScreen::PowerStateOn);
}

void QNoVncServer::discardClient(QNoVncClient *client)
{
    clients.removeOne(client);
    QNoVnc_screen->disableClientCursor(client);
    client->deleteLater();
    if (clients.isEmpty())
        QNoVnc_screen->setPowerState(QPlatformScreen::PowerStateOff);
}

inline QImage QNoVncServer::screenImage() const
{
    return *QNoVnc_screen->image();
}

QT_END_NAMESPACE

//#include "moc_qvnc_p.cpp"
