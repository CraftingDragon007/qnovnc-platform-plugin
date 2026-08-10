// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncwindow.h"
#include "qnovnc_p.h"
#include "qnovncscreen.h"

#include <QtCore/qglobal.h>
#include <QtGui/QRegion>
#include <QtGui/QWindow>

QNoVncWindow::QNoVncWindow(QWindow *window)
    : QFbWindow(window)
{

}

QNoVncWindow::~QNoVncWindow()
{
    if (auto *screen = static_cast<QNoVncScreen *>(platformScreen()))
        screen->clearWindowGrabs(this);
}

QImage* QNoVncWindow::image()
{
    return &m_image;
}

void QNoVncWindow::setGeometry(const QRect &rect)
{
    QRect adjusted = rect;

    if (window() && window()->type() == Qt::Popup) {
        if (auto *screen = static_cast<QNoVncScreen *>(platformScreen())) {
            const QRect screenRect = screen->geometry();

            if (!screenRect.intersects(adjusted)) {
                if (QWindow *parent = window()->transientParent()) {
                    const QPoint relativeToParent = adjusted.topLeft() - parent->geometry().topLeft();
                    adjusted.moveTopLeft(screenRect.topLeft() + relativeToParent);
                }
            }

            if (!screenRect.contains(adjusted)) {
                const int maxLeft = screenRect.right() - adjusted.width() + 1;
                const int maxTop = screenRect.bottom() - adjusted.height() + 1;
                adjusted.moveLeft(maxLeft >= screenRect.left()
                                  ? qBound(screenRect.left(), adjusted.left(), maxLeft)
                                  : screenRect.left());
                adjusted.moveTop(maxTop >= screenRect.top()
                                 ? qBound(screenRect.top(), adjusted.top(), maxTop)
                                 : screenRect.top());
            }

            qCDebug(lcVncPopup) << "popup geometry" << rect << "adjusted" << adjusted
                                << "screen" << screenRect
                                << "transientParent" << window()->transientParent()
                                << "parentGeometry" << (window()->transientParent() ? window()->transientParent()->geometry() : QRect());
        }
    }

    QFbWindow::setGeometry(adjusted);
}

void QNoVncWindow::setVisible(bool visible)
{
    if (!visible) {
        if (auto *screen = static_cast<QNoVncScreen *>(platformScreen()))
            screen->clearWindowGrabs(this);
    }

    QFbWindow::setVisible(visible);

    if (visible && window() && window()->type() == Qt::Popup) {
        QFbWindow::raise();
        repaint(QRegion(geometry()));
        qCDebug(lcVncPopup) << "popup raised on show" << window() << window()->geometry();
    }
}

bool QNoVncWindow::setKeyboardGrabEnabled(bool enabled)
{
    auto *screen = static_cast<QNoVncScreen *>(platformScreen());
    if (!screen)
        return false;

    if (enabled)
        screen->setKeyboardGrabber(this);
    else
        screen->clearKeyboardGrabber(this);

    qCDebug(lcVncPopup) << "keyboard grab" << enabled << window() << window()->type() << window()->geometry();

    return true;
}

bool QNoVncWindow::setMouseGrabEnabled(bool enabled)
{
    auto *screen = static_cast<QNoVncScreen *>(platformScreen());
    if (!screen)
        return false;

    if (enabled)
        screen->setMouseGrabber(this);
    else
        screen->clearMouseGrabber(this);

    qCDebug(lcVncPopup) << "mouse grab" << enabled << window() << window()->type() << window()->geometry();

    return true;
}
