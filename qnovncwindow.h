// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNCWINDOW_H
#define QNOVNCWINDOW_H
#include <private/qfbwindow_p.h>


class QNoVncWindow : public QFbWindow
{
public:
    explicit QNoVncWindow(QWindow *window);

    QImage* image();

private:
    QImage m_image;
};


#endif //QNOVNCWINDOW_H