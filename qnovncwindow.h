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