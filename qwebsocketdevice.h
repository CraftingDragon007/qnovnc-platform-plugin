// Lightweight QIODevice adapter for QWebSocket binary frames
// Bridges stream-style RFB reads/writes onto message-based WebSocket API.
#pragma once

#include <QIODevice>
#include <QWebSocket>

class QWebSocketDevice : public QIODevice {
    Q_OBJECT
public:
    explicit QWebSocketDevice(QWebSocket *socket, QObject *parent = nullptr)
        : QIODevice(parent), m_socket(socket) {
        Q_ASSERT(m_socket);
        QIODevice::open(QIODevice::ReadWrite);
        connect(m_socket, &QWebSocket::binaryMessageReceived,
                this, &QWebSocketDevice::onBinaryMessageReceived);
        connect(m_socket, &QWebSocket::disconnected,
                this, &QWebSocketDevice::onSocketDisconnected);
        connect(m_socket, &QWebSocket::errorOccurred,
                this, &QWebSocketDevice::onSocketError);
    }

    ~QWebSocketDevice() override = default;

    QWebSocket *socket() const { return m_socket; }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override {
        return m_readBuffer.size() + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 n = qMin<qint64>(maxSize, m_readBuffer.size());
        if (n <= 0)
            return 0;
        memcpy(data, m_readBuffer.constData(), size_t(n));
        m_readBuffer.remove(0, int(n));
        return n;
    }

    qint64 writeData(const char *data, qint64 maxSize) override {
        // Send each write as a binary WebSocket frame
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
            return -1;
        m_socket->sendBinaryMessage(QByteArray(data, int(maxSize)));
        return maxSize;
    }

private slots:
    void onBinaryMessageReceived(const QByteArray &message) {
        if (!message.isEmpty()) {
            m_readBuffer.append(message);
            Q_EMIT readyRead();
        }
    }

    void onSocketDisconnected() {
        if (isOpen()) {
            QIODevice::close();
            Q_EMIT aboutToClose();
        }
    }

    void onSocketError(QAbstractSocket::SocketError) {

    }

private:
    QWebSocket *m_socket;
    QByteArray m_readBuffer;
};
