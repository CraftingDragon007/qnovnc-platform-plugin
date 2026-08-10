// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <QtTest/QTest>

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

class PopupGrabTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void persistentButtonMenuCanOpenRepeatedly();
    void transientContextMenuCanOpenRepeatedly();
    void comboPopupCanOpenRepeatedly();
    void popupGeometryIsNormalizedToScreen();
};

void PopupGrabTest::initTestCase()
{
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("novnc"));
}

void PopupGrabTest::persistentButtonMenuCanOpenRepeatedly()
{
    QWidget topLevel;
    auto *layout = new QVBoxLayout(&topLevel);
    auto *button = new QPushButton(QStringLiteral("Menu"), &topLevel);
    auto *menu = new QMenu(button);
    menu->addAction(QStringLiteral("First"));
    menu->addAction(QStringLiteral("Second"));
    button->setMenu(menu);
    layout->addWidget(button);

    topLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    for (int i = 0; i < 3; ++i) {
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        QTRY_VERIFY(menu->isVisible());
        menu->hide();
        QTRY_VERIFY(!menu->isVisible());
    }
}

void PopupGrabTest::transientContextMenuCanOpenRepeatedly()
{
    QWidget topLevel;
    topLevel.resize(200, 100);
    topLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    QMenu menu(&topLevel);
    menu.addAction(QStringLiteral("First"));
    menu.addAction(QStringLiteral("Second"));

    for (int i = 0; i < 3; ++i) {
        menu.popup(topLevel.mapToGlobal(QPoint(20, 20)));
        QTRY_VERIFY(menu.isVisible());
        menu.hide();
        QTRY_VERIFY(!menu.isVisible());
    }
}

void PopupGrabTest::comboPopupCanOpenRepeatedly()
{
    QWidget topLevel;
    auto *layout = new QVBoxLayout(&topLevel);
    auto *combo = new QComboBox(&topLevel);
    combo->addItems({QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")});
    layout->addWidget(combo);

    topLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    for (int i = 0; i < 3; ++i) {
        combo->showPopup();
        QTRY_VERIFY(combo->view()->isVisible());
        combo->hidePopup();
        QTRY_VERIFY(!combo->view()->isVisible());
    }
}

void PopupGrabTest::popupGeometryIsNormalizedToScreen()
{
    QWidget topLevel;
    topLevel.setGeometry(3000, 1500, 200, 100);
    topLevel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topLevel));

    QMenu menu(&topLevel);
    menu.addAction(QStringLiteral("First"));
    menu.addAction(QStringLiteral("Second"));

    menu.popup(topLevel.mapToGlobal(QPoint(60, 40)));
    QTRY_VERIFY(menu.isVisible());

    const QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    QVERIFY2(screenGeometry.intersects(menu.windowHandle()->geometry()),
             qPrintable(QStringLiteral("popup geometry %1 is outside screen %2")
                        .arg(QString::fromLatin1(QByteArray::number(menu.windowHandle()->geometry().x()) + "," +
                                                 QByteArray::number(menu.windowHandle()->geometry().y()) + " " +
                                                 QByteArray::number(menu.windowHandle()->geometry().width()) + "x" +
                                                 QByteArray::number(menu.windowHandle()->geometry().height())))
                        .arg(QString::fromLatin1(QByteArray::number(screenGeometry.x()) + "," +
                                                 QByteArray::number(screenGeometry.y()) + " " +
                                                 QByteArray::number(screenGeometry.width()) + "x" +
                                                 QByteArray::number(screenGeometry.height())))));

    menu.hide();
    QTRY_VERIFY(!menu.isVisible());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    PopupGrabTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "popupgrabtest.moc"
