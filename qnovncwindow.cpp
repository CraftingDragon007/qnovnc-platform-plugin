#include "qnovncwindow.h"

QNoVncWindow::QNoVncWindow(QWindow *window)
    : QFbWindow(window)
{

}

QImage* QNoVncWindow::image()
{
    return &m_image;
}