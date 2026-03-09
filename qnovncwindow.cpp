// Copyright (C) 2026 Julian Houba
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncwindow.h"

QNoVncWindow::QNoVncWindow(QWindow *window)
    : QFbWindow(window)
{

}

QImage* QNoVncWindow::image()
{
    return &m_image;
}