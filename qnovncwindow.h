// Copyright (C) 2026 Julian Houba
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNC_QNOVNCWINDOW_H
#define QNOVNC_QNOVNCWINDOW_H
#include <private/qfbwindow_p.h>


class QNoVncWindow : public QFbWindow
{
public:
    QNoVncWindow(QWindow *window);

    QImage* image();

private:
    QImage m_image;
};


#endif //QNOVNC_QNOVNCWINDOW_H