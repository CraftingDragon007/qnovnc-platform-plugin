// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QVNCCLIENT_H
#define QVNCCLIENT_H

#include <QtCore/QElapsedTimer>

#include "qnovnc_p.h"
#include "qwebsocketdevice.h"

QT_BEGIN_NAMESPACE

class QNoVncServer;

class QNoVncClient : public QObject
{
    Q_OBJECT
public:
    enum ClientMsg {
        SetPixelFormat = 0,
        FixColourMapEntries = 1,
        SetEncodings = 2,
        FramebufferUpdateRequest = 3,
        KeyEvent = 4,
        PointerEvent = 5,
        ClientCutText = 6
    };

    explicit QNoVncClient(QWebSocket *clientSocket, QNoVncServer *server);
    ~QNoVncClient();
    QWebSocketDevice* clientSocket() const;
    QNoVncServer *server() const { return m_server; }

    void setDirty(const QRegion &region);
    void setDirtyCursor() { m_dirtyCursor = true; scheduleUpdate(); }
    QRegion dirtyRegion() const { return m_dirtyRegion; }
    inline bool isConnected() const { return m_state == Connected; }

    inline int clientBytesPerPixel() const {
        return m_pixelFormat.bitsPerPixel > 0
                ? (m_pixelFormat.bitsPerPixel + 7) / 8
                : 0;
    }

    void convertPixels(char *dst, const char *src, int count, int depth) const;
    inline bool doPixelConversion() const { return m_needConversion; }

signals:

private slots:
    void readClient();
    void discardClient();
    void checkUpdate();
    void scheduleUpdate();

protected:
    bool event(QEvent *event) override;

private:
    enum ClientState {
        Disconnected,
        Protocol,
        Authentication,
        Init,
        Connected
    };
    enum ProtocolVersion {
        V3_3,
        V3_7,
        V3_8
    };

    void setPixelFormat();
    void setEncodings();
    void frameBufferUpdateRequest();
    void pointerEvent();
    void keyEvent();
    void clientCutText();
    bool pixelConversionNeeded() const;
    void recordClientStats(qint64 encodeDurationNs);

    QNoVncServer *m_server;
    QWebSocketDevice *m_clientSocket;
    QRfbEncoder *m_encoder;

    // Client State
    ClientState m_state;
    quint8 m_msgType;
    bool m_handleMsg;
    QRfbPixelFormat m_pixelFormat;
    bool m_sameEndian;
    bool m_needConversion;
    int m_encodingsPending;
    int m_cutTextPending;
    uint m_supportCopyRect : 1;
    uint m_supportRRE : 1;
    uint m_supportCoRRE : 1;
    uint m_supportHextile : 1;
    uint m_supportZRLE : 1;
    uint m_supportCursor : 1;
    uint m_supportDesktopSize : 1;
    bool m_wantUpdate;
    Qt::KeyboardModifiers m_keymod;
    bool m_dirtyCursor;
    bool m_updatePending;
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    bool m_swapBytes;
#endif
    QRegion m_dirtyRegion;
    ProtocolVersion m_protocolVersion;
    const int m_clientId;

    bool m_debugTimingEnabled = false;
    int m_debugWindowMs = 1000;
    bool m_updateTimersPrimed = false;
    QElapsedTimer m_updateIntervalTimer;
    QElapsedTimer m_updateWindowTimer;
    qint64 m_updateFrames = 0;
    qint64 m_updateAccumIntervalNs = 0;
    qint64 m_updateAccumEncodeNs = 0;
    qint64 m_updateLastIntervalNs = 0;
    qint64 m_updateLastEncodeNs = 0;
};

QT_END_NAMESPACE

#endif // QVNCCLIENT_H
