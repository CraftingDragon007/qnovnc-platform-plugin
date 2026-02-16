// Copyright (C) 2026 CraftingDragon007
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncframecache.h"
#include <QtCore/QSysInfo>
#include <QtCore/QDebug>
#include <QtCore/QMutexLocker>
#include <QtCore/QtEndian>
#include <QtCore/QtGlobal>

QT_BEGIN_NAMESPACE

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
uint qHash(const QRect &rect, uint seed)
{
    return qHash(rect.x(), seed) ^ qHash(rect.y(), seed) ^ qHash(rect.width(), seed) ^ qHash(rect.height(), seed);
}

uint qHash(const QNoVncEncodingConfig &config, uint seed)
{
    const auto &pf = config.pixelFormat;
    // Simple and fast hash combination for the pixel format fields
    unsigned int h = pf.bitsPerPixel;
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.depth);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.bigEndian);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.redShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.greenShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.blueShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.redBits);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.greenBits);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.blueBits);
    return h ^ seed;
}
#else
size_t qHash(const QNoVncEncodingConfig &config, size_t seed)
{
    const auto &pf = config.pixelFormat;
    // Simple and fast hash combination for the pixel format fields
    unsigned int h = pf.bitsPerPixel;
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.depth);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.bigEndian);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.redShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.greenShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.blueShift);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.redBits);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.greenBits);
    h = (h << 5) + h ^ static_cast<unsigned int>(pf.blueBits);
    return static_cast<size_t>(h) ^ seed;
}
#endif

bool QNoVncEncodingConfig::operator==(const QNoVncEncodingConfig &other) const
{
    return pixelFormat.bitsPerPixel == other.pixelFormat.bitsPerPixel &&
           pixelFormat.depth == other.pixelFormat.depth &&
           pixelFormat.bigEndian == other.pixelFormat.bigEndian &&
           pixelFormat.redShift == other.pixelFormat.redShift &&
           pixelFormat.greenShift == other.pixelFormat.greenShift &&
           pixelFormat.blueShift == other.pixelFormat.blueShift &&
           pixelFormat.redBits == other.pixelFormat.redBits &&
           pixelFormat.greenBits == other.pixelFormat.greenBits &&
           pixelFormat.blueBits == other.pixelFormat.blueBits;
}

QNoVncFrameCache::QNoVncFrameCache(QObject *parent) : QObject(parent) {}

void QNoVncFrameCache::invalidate()
{
    QMutexLocker locker(&m_mutex);
    m_currentFrameId++;
}

QByteArray QNoVncFrameCache::getConvertedPixels(
    const QImage &screenImage,
    const QRect &rect,
    const QRfbPixelFormat &format)
{
    QMutexLocker locker(&m_mutex);
    
    QNoVncEncodingConfig config { format };
    auto &formatCache = m_cache[config];
    auto &cachedTile = formatCache[rect];
    
    if (cachedTile.frameId == m_currentFrameId && !cachedTile.rawData.isEmpty()) {
        return cachedTile.rawData;
    }
    
    const int bytesPerPixel = (format.bitsPerPixel + 7) / 8;
    const int totalSize = rect.width() * rect.height() * bytesPerPixel;
    cachedTile.rawData.resize(totalSize);
    
    char *destination = cachedTile.rawData.data();
    const int screenDepth = screenImage.depth();
    const int screenStride = screenImage.bytesPerLine();
    const uchar *sourceLine = screenImage.scanLine(rect.y()) + rect.x() * screenDepth / 8;
    
    for (int i = 0; i < rect.height(); ++i) {
        convertPixels(destination, reinterpret_cast<const char*>(sourceLine), rect.width(), screenDepth, format);
        sourceLine += screenStride;
        destination += rect.width() * bytesPerPixel;
    }
    
    cachedTile.frameId = m_currentFrameId;
    return cachedTile.rawData;
}

void QNoVncFrameCache::convertPixels(char *dst, const char *src, const int count, const int screendepth, const QRfbPixelFormat &pixelFormat) const
{
    const int bytesPerPixel = (pixelFormat.bitsPerPixel + 7) / 8;
    const bool sameEndian = QSysInfo::ByteOrder == QSysInfo::BigEndian == !!pixelFormat.bigEndian;

    for (int i = 0; i < count; ++i) {
        int r, g, b;
        switch (screendepth) {
        case 32: {
            const quint32 p = *reinterpret_cast<const quint32*>(src);
            r = p >> 16 & 0xff; g = p >> 8 & 0xff; b = p & 0xff;
            src += 4; break;
        }
        case 16: {
            const quint16 p = *reinterpret_cast<const quint16*>(src);
            r = (p >> 11 & 0x1f) << 3; g = (p >> 5 & 0x3f) << 2; b = (p & 0x1f) << 3;
            src += 2; break;
        }
        default: r = g = b = 0; break;
        }

        r >>= 8 - pixelFormat.redBits;
        g >>= 8 - pixelFormat.greenBits;
        b >>= 8 - pixelFormat.blueBits;

        int pixel = r << pixelFormat.redShift | g << pixelFormat.greenShift | b << pixelFormat.blueShift;

        if (sameEndian || pixelFormat.bitsPerPixel == 8) {
            memcpy(dst, &pixel, bytesPerPixel);
        } else {
            if (pixelFormat.bitsPerPixel == 16) {
                pixel = (pixel & 0xff) << 8 | (pixel & 0xff00) >> 8;
            } else if (pixelFormat.bitsPerPixel == 32) {
                const quint32 p = static_cast<quint32>(pixel);
                pixel = static_cast<int>((p & 0xff000000) >> 24 |
                                         (p & 0x00ff0000) >> 8  |
                                         (p & 0x0000ff00) << 8  |
                                         (p & 0x000000ff) << 24);
            }
            memcpy(dst, &pixel, bytesPerPixel);
        }
        dst += bytesPerPixel;
    }
}

QT_END_NAMESPACE
