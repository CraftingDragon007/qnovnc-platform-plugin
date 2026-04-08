// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// Initial OpenGL context integration was adapted from
// https://github.com/duerkopp-adler/qtvncplugin (qvncopenglcontext.{h,cpp})
// by https://github.com/poker-phase and https://github.com/KoopA-DA
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QNOVNCOPENGLCONTEXT_H
#define QNOVNCOPENGLCONTEXT_H

#include <qpa/qplatformopenglcontext.h>
#include <QtCore/QScopedPointer>

QT_BEGIN_NAMESPACE

class QNoVncOpenGLContextData;
class QNoVncWindow;

class QNoVncOpenGLContext final : public QPlatformOpenGLContext
{
public:
    explicit QNoVncOpenGLContext(const QSurfaceFormat &format,
                                 QPlatformOpenGLContext *shareContext = nullptr);
    ~QNoVncOpenGLContext() override;

    bool makeCurrent(QPlatformSurface *surface) override;
    void doneCurrent() override;
    void swapBuffers(QPlatformSurface *surface) override;
    QFunctionPointer getProcAddress(const char *procName) override;
    GLuint defaultFramebufferObject(QPlatformSurface *surface) const override;

    [[nodiscard]] QSurfaceFormat format() const override;
    [[nodiscard]] bool isSharing() const override;
    [[nodiscard]] bool isValid() const override;

private:
    bool ensureSurfaceForWindow(QNoVncWindow *window, const QSize &size);
    bool ensureFramebuffer(const QSize &size);
    bool readBackToWindow(QNoVncWindow *window, const QSize &size);

    QScopedPointer<QNoVncOpenGLContextData> d;
};

QT_END_NAMESPACE

#endif // QNOVNCOPENGLCONTEXT_H

