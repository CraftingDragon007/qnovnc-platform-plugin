// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncclient.h"
#include "qnovncclient.h"

#include <QWebSocket>

#include <qpa/qwindowsysteminterface.h>
#include <QtGui/qguiapplication.h>
#include <QtCore/QElapsedTimer>
#include <atomic>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

QT_BEGIN_NAMESPACE

namespace {
std::atomic<int> s_nextClientId{0};
}

QNoVncClient::QNoVncClient(QWebSocket *clientSocket, QNoVncServer *server)
    : QObject(server)
    , m_server(server)
    , m_clientSocket(new QWebSocketDevice(clientSocket, this))
    , m_encoder(nullptr)
    , m_msgType(0)
    , m_handleMsg(false)
    , m_sameEndian(true)
    , m_needConversion(true)
    , m_encodingsPending(0)
    , m_cutTextPending(0)
    , m_supportHextile(false)
    , m_wantUpdate(false)
    , m_dirtyCursor(false)
    , m_updatePending(false)
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    , m_swapBytes(false)
#endif
    , m_protocolVersion(V3_3)
    , m_clientId(++s_nextClientId)
{
    connect(m_clientSocket,SIGNAL(readyRead()),this,SLOT(readClient()));
    connect(m_clientSocket->socket(),SIGNAL(disconnected()),this,SLOT(discardClient()));

    m_debugTimingEnabled = qEnvironmentVariableIntValue("QNOVNC_DEBUG_REFRESH") == 1;
    const int requestedWindow = qEnvironmentVariableIntValue("QNOVNC_DEBUG_REFRESH_WINDOW_MS");
    if (requestedWindow > 0)
        m_debugWindowMs = requestedWindow;

    // send protocol version
    const char *proto = "RFB 003.003\n";
    m_clientSocket->write(proto, 12);
    m_state = Protocol;
}

QNoVncClient::~QNoVncClient()
{
    delete m_encoder;
}

QWebSocketDevice* QNoVncClient::clientSocket() const
{
    return m_clientSocket;
}

void QNoVncClient::setDirty(const QRegion &region)
{
    m_dirtyRegion += region;
    if (m_state == Connected &&
        ((m_server->dirtyMap()->numDirty > 0) || m_dirtyCursor)) {
        scheduleUpdate();
    }
}

void QNoVncClient::convertPixels(char *dst, const char *src, int count, int screendepth) const
{
    // cutoffs
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    if (!m_swapBytes)
#endif
    if (m_sameEndian) {
        if (screendepth == m_pixelFormat.bitsPerPixel) { // memcpy cutoffs

            switch (screendepth) {
            case 32:
                memcpy(dst, src, count * sizeof(quint32));
                return;
            case 16:
                if (m_pixelFormat.redBits == 5
                    && m_pixelFormat.greenBits == 6
                    && m_pixelFormat.blueBits == 5)
                {
                    memcpy(dst, src, count * sizeof(quint16));
                    return;
                }
            }
        }
    }

    const int bytesPerPixel = (m_pixelFormat.bitsPerPixel + 7) / 8;

    for (int i = 0; i < count; ++i) {
        int r, g, b;

        switch (screendepth) {
        case 8: {
            QRgb rgb = m_server->screen()->image()->colorTable()[int(*src)];
            r = qRed(rgb);
            g = qGreen(rgb);
            b = qBlue(rgb);
            src++;
            break;
        }
        case 16: {
            quint16 p = *reinterpret_cast<const quint16*>(src);
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
            if (m_swapBytes)
                p = ((p & 0xff) << 8) | ((p & 0xff00) >> 8);
#endif
            r = (p >> 11) & 0x1f;
            g = (p >> 5) & 0x3f;
            b = p & 0x1f;
            r <<= 3;
            g <<= 2;
            b <<= 3;
            src += sizeof(quint16);
            break;
        }
        case 32: {
            quint32 p = *reinterpret_cast<const quint32*>(src);
            r = (p >> 16) & 0xff;
            g = (p >> 8) & 0xff;
            b = p & 0xff;
            src += sizeof(quint32);
            break;
        }
        default: {
            r = g = b = 0;
            qWarning("QVNCServer: don't support %dbpp display", screendepth);
            return;
        }
        }

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        if (m_swapBytes)
            qSwap(r, b);
#endif

        r >>= (8 - m_pixelFormat.redBits);
        g >>= (8 - m_pixelFormat.greenBits);
        b >>= (8 - m_pixelFormat.blueBits);

        int pixel = (r << m_pixelFormat.redShift) |
                    (g << m_pixelFormat.greenShift) |
                    (b << m_pixelFormat.blueShift);

        if (m_sameEndian || m_pixelFormat.bitsPerPixel == 8) {
            memcpy(dst, &pixel, bytesPerPixel);
            dst += bytesPerPixel;
            continue;
        }


        if (QSysInfo::ByteOrder == QSysInfo::BigEndian) {
            switch (m_pixelFormat.bitsPerPixel) {
            case 16:
                pixel = (((pixel & 0x0000ff00) << 8)  |
                         ((pixel & 0x000000ff) << 24));
                break;
            case 32:
                pixel = (((pixel & 0xff000000) >> 24) |
                         ((pixel & 0x00ff0000) >> 8)  |
                         ((pixel & 0x0000ff00) << 8)  |
                         ((pixel & 0x000000ff) << 24));
                break;
            default:
                qWarning("Cannot handle %d bpp client", m_pixelFormat.bitsPerPixel);
            }
        } else { // QSysInfo::ByteOrder == QSysInfo::LittleEndian
            switch (m_pixelFormat.bitsPerPixel) {
            case 16:
                pixel = (((pixel & 0xff000000) >> 8) |
                         ((pixel & 0x00ff0000) << 8));
                break;
            case 32:
                pixel = (((pixel & 0xff000000) >> 24) |
                         ((pixel & 0x00ff0000) >> 8)  |
                         ((pixel & 0x0000ff00) << 8)  |
                         ((pixel & 0x000000ff) << 24));
                break;
            default:
                qWarning("Cannot handle %d bpp client",
                       m_pixelFormat.bitsPerPixel);
                break;
            }
        }
        memcpy(dst, &pixel, bytesPerPixel);
        dst += bytesPerPixel;
    }
}

void QNoVncClient::readClient()
{
    qCDebug(lcVnc) << "readClient" << m_state;
    switch (m_state) {
        case Disconnected:

            break;
        case Protocol:
            if (m_clientSocket->bytesAvailable() >= 12) {
                char proto[13];
                m_clientSocket->read(proto, 12);
                proto[12] = '\0';
                qCDebug(lcVnc, "Client protocol version %s", proto);
                if (!strcmp(proto, "RFB 003.008\n")) {
                    m_protocolVersion = V3_8;
                } else if (!strcmp(proto, "RFB 003.007\n")) {
                    m_protocolVersion = V3_7;
                } else {
                    m_protocolVersion = V3_3;
                }

                if (m_protocolVersion == V3_3) {
                    // No authentication
                    quint32 auth = htonl(1);
                    m_clientSocket->write((char *) &auth, sizeof(auth));
                    m_state = Init;
                }
            }
            break;
        case Authentication:

            break;
        case Init:
            if (m_clientSocket->bytesAvailable() >= 1) {
                quint8 shared;
                m_clientSocket->read((char *) &shared, 1);

                // Server Init msg
                QRfbServerInit sim;
                QRfbPixelFormat &format = sim.format;
                switch (m_server->screen()->depth()) {
                case 32:
                    format.bitsPerPixel = 32;
                    format.depth = 32;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 8;
                    format.greenBits = 8;
                    format.blueBits = 8;
                    format.redShift = 16;
                    format.greenShift = 8;
                    format.blueShift = 0;
                    break;

                case 24:
                    format.bitsPerPixel = 24;
                    format.depth = 24;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 8;
                    format.greenBits = 8;
                    format.blueBits = 8;
                    format.redShift = 16;
                    format.greenShift = 8;
                    format.blueShift = 0;
                    break;

                case 18:
                    format.bitsPerPixel = 24;
                    format.depth = 18;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 6;
                    format.greenBits = 6;
                    format.blueBits = 6;
                    format.redShift = 12;
                    format.greenShift = 6;
                    format.blueShift = 0;
                    break;

                case 16:
                    format.bitsPerPixel = 16;
                    format.depth = 16;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 5;
                    format.greenBits = 6;
                    format.blueBits = 5;
                    format.redShift = 11;
                    format.greenShift = 5;
                    format.blueShift = 0;
                    break;

                case 15:
                    format.bitsPerPixel = 16;
                    format.depth = 15;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 5;
                    format.greenBits = 5;
                    format.blueBits = 5;
                    format.redShift = 10;
                    format.greenShift = 5;
                    format.blueShift = 0;
                    break;

                case 12:
                    format.bitsPerPixel = 16;
                    format.depth = 12;
                    format.bigEndian = 0;
                    format.trueColor = true;
                    format.redBits = 4;
                    format.greenBits = 4;
                    format.blueBits = 4;
                    format.redShift = 8;
                    format.greenShift = 4;
                    format.blueShift = 0;
                    break;

                case 8:
                case 4:
                    format.bitsPerPixel = 8;
                    format.depth = 8;
                    format.bigEndian = 0;
                    format.trueColor = false;
                    format.redBits = 0;
                    format.greenBits = 0;
                    format.blueBits = 0;
                    format.redShift = 0;
                    format.greenShift = 0;
                    format.blueShift = 0;
                    break;

                default:
                    qWarning("QVNC cannot drive depth %d", m_server->screen()->depth());
                    discardClient();
                    return;
                }
                sim.width = m_server->screen()->geometry().width();
                sim.height = m_server->screen()->geometry().height();
                sim.setName("Qt for Embedded Linux VNC Server");
                sim.write(m_clientSocket);
                m_pixelFormat = format;
                m_sameEndian = (QSysInfo::ByteOrder == QSysInfo::BigEndian) == !!m_pixelFormat.bigEndian;
                m_needConversion = pixelConversionNeeded();
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
                m_swapBytes = m_server->screen()->swapBytes();
#endif
                m_state = Connected;
            }
            break;

        case Connected:
            do {
                if (!m_handleMsg) {
                    m_clientSocket->read((char *)&m_msgType, 1);
                    m_handleMsg = true;
                }
                if (m_handleMsg) {
                    switch (m_msgType ) {
                    case SetPixelFormat:
                        setPixelFormat();
                        break;
                    case FixColourMapEntries:
                        qWarning("Not supported: FixColourMapEntries");
                        m_handleMsg = false;
                        break;
                    case SetEncodings:
                        setEncodings();
                        break;
                    case FramebufferUpdateRequest:
                        frameBufferUpdateRequest();
                        break;
                    case KeyEvent:
                        keyEvent();
                        break;
                    case PointerEvent:
                        pointerEvent();
                        break;
                    case ClientCutText:
                        clientCutText();
                        break;
                    default:
                        qWarning("Unknown message type: %d", (int)m_msgType);
                        m_handleMsg = false;
                    }
                }
            } while (!m_handleMsg && m_clientSocket->bytesAvailable());
            break;
    default:
        break;
    }
}

void QNoVncClient::discardClient()
{
    m_state = Disconnected;
    m_server->discardClient(this);
}

void QNoVncClient::checkUpdate()
{
    if (!m_wantUpdate)
        return;
#if QT_CONFIG(cursor)
    if (m_dirtyCursor) {
        m_server->screen()->clientCursor->write(this);
        m_dirtyCursor = false;
        m_wantUpdate = false;
        return;
    }
#endif
    if (!m_dirtyRegion.isEmpty()) {
        qint64 encodeDurationNs = 0;
        QElapsedTimer encodeTimer;
        if (m_debugTimingEnabled)
            encodeTimer.start();
        if (m_encoder)
            m_encoder->write();
        if (m_debugTimingEnabled)
            encodeDurationNs = encodeTimer.nsecsElapsed();
        recordClientStats(encodeDurationNs);
        m_wantUpdate = false;
        m_dirtyRegion = QRegion();
    }
}

void QNoVncClient::scheduleUpdate()
{
    if (!m_updatePending) {
        m_updatePending = true;
        QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
    }
}

bool QNoVncClient::event(QEvent *event)
{
    if (event->type() == QEvent::UpdateRequest) {
        m_updatePending = false;
        checkUpdate();
        return true;
    }
    return QObject::event(event);
}

void QNoVncClient::recordClientStats(qint64 encodeDurationNs)
{
    if (!m_debugTimingEnabled)
        return;

    if (!m_updateTimersPrimed) {
        m_updateIntervalTimer.start();
        m_updateWindowTimer.start();
        m_updateFrames = 0;
        m_updateAccumIntervalNs = 0;
        m_updateAccumEncodeNs = 0;
        m_updateLastIntervalNs = 0;
        m_updateLastEncodeNs = encodeDurationNs;
        m_updateTimersPrimed = true;
        return;
    }

    const qint64 intervalNs = m_updateIntervalTimer.nsecsElapsed();
    m_updateIntervalTimer.restart();
    m_updateLastIntervalNs = intervalNs;
    m_updateLastEncodeNs = encodeDurationNs;

    ++m_updateFrames;
    m_updateAccumIntervalNs += intervalNs;
    m_updateAccumEncodeNs += encodeDurationNs;

    if (m_updateWindowTimer.elapsed() < m_debugWindowMs)
        return;

    m_updateWindowTimer.restart();
    const qreal avgIntervalMs = m_updateFrames > 0
            ?  m_updateAccumIntervalNs / (1'000'000.0 * qreal(m_updateFrames))
            : 0.0;
    const qreal avgFps = avgIntervalMs > 0.0 ? 1000.0 / avgIntervalMs : 0.0;
    const qreal lastIntervalMs = m_updateLastIntervalNs / 1'000'000.0;
    const qreal avgEncodeMs = m_updateFrames > 0
            ?  m_updateAccumEncodeNs / (1'000'000.0 * qreal(m_updateFrames))
            : 0.0;
    const qreal lastEncodeMs = m_updateLastEncodeNs / 1'000'000.0;

   qWarning().nospace()
        << "Client[" << m_clientId << "] updates: avg interval "
        << QString::number(avgIntervalMs, 'f', 2)
        << " ms (" << QString::number(avgFps, 'f', 2) << " fps)"
        << ", last interval " << QString::number(lastIntervalMs, 'f', 2) << " ms"
        << ", avg encode " << QString::number(avgEncodeMs, 'f', 2) << " ms"
        << ", last encode " << QString::number(lastEncodeMs, 'f', 2) << " ms"
        << ", frames=" << m_updateFrames;

    m_updateFrames = 0;
    m_updateAccumIntervalNs = 0;
    m_updateAccumEncodeNs = 0;
}

void QNoVncClient::setPixelFormat()
{
    if (m_clientSocket->bytesAvailable() >= 19) {
        char buf[3];
        m_clientSocket->read(buf, 3); // just padding
        m_pixelFormat.read(m_clientSocket);
        qCDebug(lcVnc, "Want format: %d %d %d %d %d %d %d %d %d %d",
            int(m_pixelFormat.bitsPerPixel),
            int(m_pixelFormat.depth),
            int(m_pixelFormat.bigEndian),
            int(m_pixelFormat.trueColor),
            int(m_pixelFormat.redBits),
            int(m_pixelFormat.greenBits),
            int(m_pixelFormat.blueBits),
            int(m_pixelFormat.redShift),
            int(m_pixelFormat.greenShift),
            int(m_pixelFormat.blueShift));
        if (!m_pixelFormat.trueColor) {
            qWarning("Can only handle true color clients");
            discardClient();
        }
        m_handleMsg = false;
        m_sameEndian = (QSysInfo::ByteOrder == QSysInfo::BigEndian) == !!m_pixelFormat.bigEndian;
        m_needConversion = pixelConversionNeeded();
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        m_swapBytes = server()->screen()->swapBytes();
#endif
    }
}

void QNoVncClient::setEncodings()
{
    QRfbSetEncodings enc;

    if (!m_encodingsPending && enc.read(m_clientSocket)) {
        m_encodingsPending = enc.count;
        if (!m_encodingsPending)
            m_handleMsg = false;
    }

    if (m_encoder) {
        delete m_encoder;
        m_encoder = nullptr;
    }

    enum Encodings {
        Raw = 0,
        CopyRect = 1,
        RRE = 2,
        CoRRE = 4,
        Hextile = 5,
        Zlib = 6,
        ZRLE = 16,
        Cursor = -239,
        DesktopSize = -223
    };

    if (m_encodingsPending && (unsigned)m_clientSocket->bytesAvailable() >=
                                m_encodingsPending * sizeof(quint32)) {
        for (int i = 0; i < m_encodingsPending; ++i) {
            qint32 enc;
            m_clientSocket->read((char *)&enc, sizeof(qint32));
            enc = ntohl(enc);
            qCDebug(lcVnc, "QNoVncServer::setEncodings: %d", enc);
            switch (enc) {
            case Raw:
                if (!m_encoder) {
                    m_encoder = new QRfbRawEncoder(this);
                    qCDebug(lcVnc, "QNoVncServer::setEncodings: using raw");
                }
               break;
            case CopyRect:
                m_supportCopyRect = true;
                break;
            case RRE:
                m_supportRRE = true;
                break;
            case CoRRE:
                m_supportCoRRE = true;
                break;
            case Hextile:
                m_supportHextile = true;
                if (m_encoder)
                    break;
                break;
            case Zlib:
                if (!m_encoder) {
                    m_encoder = new QRfbZlibEncoder(this);
                    qCDebug(lcVnc, "QNoVncServer::setEncodings: using zlib");
                }
                break;
            case ZRLE:
                m_supportZRLE = true;
                break;
            case Cursor:
                m_supportCursor = true;
                m_server->screen()->enableClientCursor(this);
                break;
            case DesktopSize:
                m_supportDesktopSize = true;
                break;
            default:
                break;
            }
        }
        m_handleMsg = false;
        m_encodingsPending = 0;
    }

    if (!m_encoder) {
        m_encoder = new QRfbRawEncoder(this);
        qCDebug(lcVnc, "QNoVncServer::setEncodings: fallback using raw");
    }
}

void QNoVncClient::frameBufferUpdateRequest()
{
    qCDebug(lcVnc) << "FramebufferUpdateRequest";
    QRfbFrameBufferUpdateRequest ev;

    if (ev.read(m_clientSocket)) {
        if (!ev.incremental) {
            QRect r(ev.rect.x, ev.rect.y, ev.rect.w, ev.rect.h);
            r.translate(m_server->screen()->geometry().topLeft());
            setDirty(r);
        }
        m_wantUpdate = true;
        checkUpdate();
        m_handleMsg = false;
    }
}

void QNoVncClient::pointerEvent()
{
    QRfbPointerEvent ev;
    static int buttonState = Qt::NoButton;
    if (ev.read(m_clientSocket)) {
        const QPointF pos = m_server->screen()->geometry().topLeft() + QPoint(ev.x, ev.y);
        int buttonStateChange = buttonState ^ int(ev.buttons);
        QEvent::Type type = QEvent::MouseMove;
        if (int(ev.buttons) > buttonState)
            type = QEvent::MouseButtonPress;
        else if (int(ev.buttons) < buttonState)
            type = QEvent::MouseButtonRelease;
        QWindowSystemInterface::handleMouseEvent(nullptr, pos, pos, ev.buttons, Qt::MouseButton(buttonStateChange),
                                                 type, QGuiApplication::keyboardModifiers());
        buttonState = int(ev.buttons);
        m_handleMsg = false;
    }
}

void QNoVncClient::keyEvent()
{
    QRfbKeyEvent ev;

    if (ev.read(m_clientSocket)) {
        if (ev.keycode == Qt::Key_Shift)
            m_keymod = ev.down ? m_keymod | Qt::ShiftModifier :
                                 m_keymod & ~Qt::ShiftModifier;
        else if (ev.keycode == Qt::Key_Control)
            m_keymod = ev.down ? m_keymod | Qt::ControlModifier :
                                 m_keymod & ~Qt::ControlModifier;
        else if (ev.keycode == Qt::Key_Alt)
            m_keymod = ev.down ? m_keymod | Qt::AltModifier :
                                 m_keymod & ~Qt::AltModifier;
        if (ev.unicode || ev.keycode) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QChar unicodeChar = QChar::fromUcs2(ev.unicode);
#else
            const QChar unicodeChar = QChar(ushort(ev.unicode));
#endif
            QWindowSystemInterface::handleKeyEvent(nullptr,
                                                   ev.down ? QEvent::KeyPress : QEvent::KeyRelease,
                                                   ev.keycode, m_keymod, QString(unicodeChar));
        }
        m_handleMsg = false;
    }
}

void QNoVncClient::clientCutText()
{
    QRfbClientCutText ev;

    if (m_cutTextPending == 0 && ev.read(m_clientSocket)) {
        m_cutTextPending = ev.length;
        if (!m_cutTextPending)
            m_handleMsg = false;
    }

    if (m_cutTextPending && m_clientSocket->bytesAvailable() >= m_cutTextPending) {
        char *text = new char [m_cutTextPending+1];
        m_clientSocket->read(text, m_cutTextPending);
        delete [] text;
        m_cutTextPending = 0;
        m_handleMsg = false;
    }
}

bool QNoVncClient::pixelConversionNeeded() const
{
    if (!m_sameEndian)
        return true;

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    if (server()->screen()->swapBytes())
        return true;
#endif

    const int screendepth = m_server->screen()->depth();
    if (screendepth != m_pixelFormat.bitsPerPixel)
        return true;

    switch (screendepth) {
    case 32:
    case 24:
        return false;
    case 16:
        return (m_pixelFormat.redBits == 5
                && m_pixelFormat.greenBits == 6
                && m_pixelFormat.blueBits == 5);
    }
    return true;
}

QT_END_NAMESPACE

//#include "moc_qvncclient.cpp"
