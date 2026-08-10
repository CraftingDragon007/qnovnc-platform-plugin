// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncbackingstore.h"
#include "qnovnc_p.h"
#include "qnovncwindow.h"

#include <QtGui/QWindow>

QT_BEGIN_NAMESPACE

QNoVncBackingStore::QNoVncBackingStore(QWindow *window)
    : QFbBackingStore(window)
{
    attachToWindow(window);
}

void QNoVncBackingStore::flush(QWindow *window, const QRegion &region, const QPoint &offset)
{
    attachToWindow(window);
    QFbBackingStore::flush(window, region, offset);
}

void QNoVncBackingStore::attachToWindow(QWindow *window)
{
    if (!window || !window->handle())
        return;

    auto *platformWindow = static_cast<QNoVncWindow *>(window->handle());
    if (platformWindow->backingStore() == this)
        return;

    platformWindow->setBackingStore(this);
    qCDebug(lcVncPopup) << "attached backingstore" << this
                        << "to" << window
                        << "type" << window->type()
                        << "geometry" << window->geometry();
}

QT_END_NAMESPACE
