// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

// Lightweight QIODevice adapter for QWebSocket binary frames
// Bridges stream-style RFB reads/writes onto message-based WebSocket API.

#ifndef QWEBSOCKETDEVICE_H
#define QWEBSOCKETDEVICE_H

#pragma once

#include <QIODevice>
#include <QPointer>
#include <QWebSocket>
#include <QtCore/QtGlobal>

class QWebSocketDevice : public QIODevice {
    Q_OBJECT
public:
    explicit QWebSocketDevice(QWebSocket *socket, QObject *parent = nullptr)
        : QIODevice(parent), m_socket(socket), m_isConnected(false) {
        Q_ASSERT(m_socket);
        QIODevice::open(QIODevice::ReadWrite);
        connect(m_socket, &QWebSocket::connected,
                this, &QWebSocketDevice::onSocketConnected);
        connect(m_socket, &QWebSocket::binaryMessageReceived,
                this, &QWebSocketDevice::onBinaryMessageReceived);
        connect(m_socket, &QWebSocket::disconnected,
                this, &QWebSocketDevice::onSocketDisconnected);
        connect(m_socket, &QObject::destroyed,
                this, &QWebSocketDevice::onSocketDestroyed);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_socket, &QWebSocket::errorOccurred,
                this, &QWebSocketDevice::onSocketError);
#else
        connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                this, &QWebSocketDevice::onSocketError);
#endif
        m_isConnected = (m_socket->state() == QAbstractSocket::ConnectedState);
    }

    ~QWebSocketDevice() override = default;

    [[nodiscard]] QWebSocket *socket() const { return m_socket; }

    [[nodiscard]] bool isSequential() const override { return true; }

    [[nodiscard]] qint64 bytesAvailable() const override {
        return m_readBuffer.size() + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, const qint64 maxSize) override {
        const qint64 n = qMin<qint64>(maxSize, m_readBuffer.size());
        if (n <= 0)
            return 0;
        memcpy(data, m_readBuffer.constData(), static_cast<size_t>(n));
        m_readBuffer.remove(0, static_cast<int>(n));
        return n;
    }

    qint64 writeData(const char *data, const qint64 maxSize) override {
        // Send each write as a binary WebSocket frame
        if (!isOpen() || m_socket.isNull() || !m_isConnected)
            return -1;
        if (m_socket->state() != QAbstractSocket::ConnectedState) {
            m_isConnected = false;
            return -1;
        }
        const qint64 written = m_socket->sendBinaryMessage(QByteArray(data, static_cast<int>(maxSize)));
        if (written < 0)
            return -1;
        return written;
    }

private slots:
    void onSocketConnected() {
        m_isConnected = true;
    }

    void onBinaryMessageReceived(const QByteArray &message) {
        if (!message.isEmpty()) {
            m_readBuffer.append(message);
            Q_EMIT readyRead();
        }
    }

    void onSocketDisconnected() {
        m_isConnected = false;
        if (isOpen()) {
            Q_EMIT aboutToClose();
            QIODevice::close();
        }
    }

    void onSocketDestroyed() {
        m_isConnected = false;
    }

    void onSocketError(QAbstractSocket::SocketError) {
        m_isConnected = false;
    }

private:
    QPointer<QWebSocket> m_socket;
    bool m_isConnected;
    QByteArray m_readBuffer;
};

#endif //QWEBSOCKETDEVICE_H
