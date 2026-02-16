// Copyright (C) 2026 CraftingDragon007
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNCFRAMECACHE_H
#define QNOVNCFRAMECACHE_H

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QMutex>
#include <QtCore/QRect>

#include "qnovnc_p.h"

QT_BEGIN_NAMESPACE

struct QNoVncEncodingConfig
{
    QRfbPixelFormat pixelFormat;

    bool operator==(const QNoVncEncodingConfig &other) const;
};

size_t qHash(const QNoVncEncodingConfig &config, size_t seed = 0);

/**
 * @brief Holds pre-converted (but NOT compressed) pixel data
 */
struct QNoVncCachedTile
{
    QByteArray rawData;
    quint64 frameId;

    QNoVncCachedTile() : frameId(0) {}
};

class QNoVncFrameCache : public QObject
{
    Q_OBJECT

public:
    explicit QNoVncFrameCache(QObject *parent = nullptr);
    
    // Get converted pixels for a specific rect and format
    QByteArray getConvertedPixels(
        const QImage &screenImage,
        const QRect &rect,
        const QRfbPixelFormat &format);

    void invalidate();

private:
    void convertPixels(char *dst, const char *src, int count, int screendepth, const QRfbPixelFormat &pixelFormat) const;

    mutable QMutex m_mutex;
    quint64 m_currentFrameId = 0;
    // Map: EncodingConfig -> (Rect -> Tile)
    QHash<QNoVncEncodingConfig, QHash<QRect, QNoVncCachedTile>> m_cache;
};

QT_END_NAMESPACE

#endif // QNOVNCFRAMECACHE_H
