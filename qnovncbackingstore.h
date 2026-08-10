// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNCBACKINGSTORE_H
#define QNOVNCBACKINGSTORE_H

#include <QtFbSupport/private/qfbbackingstore_p.h>

QT_BEGIN_NAMESPACE

class QWindow;

class QNoVncBackingStore : public QFbBackingStore
{
public:
    explicit QNoVncBackingStore(QWindow *window);

    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;

private:
    void attachToWindow(QWindow *window);
};

QT_END_NAMESPACE

#endif // QNOVNCBACKINGSTORE_H
