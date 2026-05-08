// Copyright (C) 2026 Julian Houba <info@craftingdragon.ch>
// Initial OpenGL context integration was adapted from
// https://github.com/duerkopp-adler/qtvncplugin (qvncopenglcontext.{h,cpp})
// by https://github.com/poker-phase and https://github.com/KoopA-DA
// SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qnovncopenglcontext.h"

#include "qnovncscreen.h"
#include "qnovncwindow.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QByteArray>
#include <QtCore/QMutexLocker>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/qopengl.h>
#include <qpa/qplatformsurface.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstring>
#include <qloggingcategory.h>

#include "qnovnc_p.h"

QT_BEGIN_NAMESPACE

class QNoVncOpenGLContextData
{
public:
    QSurfaceFormat format;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    QSize surfaceSize;
    bool holdsDisplayRef = false;

    GLuint fbo = 0;
    GLuint colorTex = 0;
    GLuint depthStencilRbo = 0;
    QSize fboSize;
};

static QMutex sEglDisplayMutex;
static EGLDisplay sSharedDisplay = EGL_NO_DISPLAY;
static int sSharedDisplayRefCount = 0;

static EGLDisplay acquireSharedEglDisplay()
{
    QMutexLocker locker(&sEglDisplayMutex);

    if (sSharedDisplay != EGL_NO_DISPLAY) {
        ++sSharedDisplayRefCount;
        return sSharedDisplay;
    }

    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (getPlatformDisplay)
        sSharedDisplay = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);

    if (sSharedDisplay == EGL_NO_DISPLAY)
        sSharedDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (sSharedDisplay == EGL_NO_DISPLAY)
        return EGL_NO_DISPLAY;

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(sSharedDisplay, &major, &minor)) {
        sSharedDisplay = EGL_NO_DISPLAY;
        return EGL_NO_DISPLAY;
    }

    sSharedDisplayRefCount = 1;
    return sSharedDisplay;
}

static void releaseSharedEglDisplay()
{
    QMutexLocker locker(&sEglDisplayMutex);

    if (sSharedDisplay == EGL_NO_DISPLAY || sSharedDisplayRefCount <= 0)
        return;

    --sSharedDisplayRefCount;
    if (sSharedDisplayRefCount == 0) {
        eglTerminate(sSharedDisplay);
        sSharedDisplay = EGL_NO_DISPLAY;
    }
}

static QImage::Format toImageFormat(const QSurfaceFormat &format)
{
    if (format.redBufferSize() == 5 && format.greenBufferSize() == 6 && format.blueBufferSize() == 5)
        return QImage::Format_RGB16;
    return QImage::Format_RGBX8888;
}

using BindFramebufferProc = void (*)(GLenum, GLuint);

static bool bindFramebufferRaw(QOpenGLContext *ctx, GLenum target, GLuint framebuffer)
{
    if (!ctx)
        return false;

    auto proc = reinterpret_cast<BindFramebufferProc>(ctx->getProcAddress("glBindFramebuffer"));
    if (!proc)
        return false;

    proc(target, framebuffer);
    return true;
}

static bool frameLooksFlatWhite(const QImage &image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return true;

    const QPoint samples[] = {
        {0, 0},
        {image.width() / 2, image.height() / 2},
        {image.width() - 1, image.height() - 1},
        {image.width() / 4, image.height() / 4},
        {(image.width() * 3) / 4, (image.height() * 3) / 4}
    };

    return std::all_of(std::begin(samples), std::end(samples), [&](const QPoint &sample) {
        const QColor color = image.pixel(sample);
        return color.red() == 255 && color.green() == 255 && color.blue() == 255;
    });
}

static bool createEglDisplayAndContext(QNoVncOpenGLContextData *d, EGLContext shareContext)
{
    if (!d)
        return false;

    d->display = acquireSharedEglDisplay();
    if (d->display != EGL_NO_DISPLAY)
        d->holdsDisplayRef = true;

    if (d->display == EGL_NO_DISPLAY)
        return false;

    const auto tryCreate = [d, shareContext](bool wantsOpenGles) -> bool {
        if (!eglBindAPI(wantsOpenGles ? EGL_OPENGL_ES_API : EGL_OPENGL_API))
            return false;

        EGLint renderableType = EGL_OPENGL_BIT;
        if (wantsOpenGles) {
            renderableType = EGL_OPENGL_ES2_BIT;
#if defined(EGL_OPENGL_ES3_BIT)
            if (d->format.majorVersion() >= 3)
                renderableType = EGL_OPENGL_ES3_BIT;
#endif
        }

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, renderableType,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, qMax(0, d->format.depthBufferSize()),
            EGL_STENCIL_SIZE, qMax(0, d->format.stencilBufferSize()),
            EGL_NONE
        };

        EGLint numConfigs = 0;
        if (!eglChooseConfig(d->display, configAttribs, &d->config, 1, &numConfigs) || numConfigs < 1)
            return false;

        const EGLint contextAttribsOpenGl[] = {
            EGL_CONTEXT_MAJOR_VERSION, qMax(2, d->format.majorVersion()),
            EGL_CONTEXT_MINOR_VERSION, qMax(0, d->format.minorVersion()),
            EGL_NONE
        };

        const EGLint contextAttribsGles[] = {
            EGL_CONTEXT_CLIENT_VERSION, qMax(2, d->format.majorVersion()),
            EGL_NONE
        };

        d->context = eglCreateContext(d->display,
                                      d->config,
                                      shareContext,
                                      wantsOpenGles ? contextAttribsGles : contextAttribsOpenGl);
        return d->context != EGL_NO_CONTEXT;
    };

    if (d->format.renderableType() == QSurfaceFormat::OpenGLES) {
        if (tryCreate(true))
            return true;
    } else if (d->format.renderableType() == QSurfaceFormat::OpenGL) {
        if (tryCreate(false))
            return true;
    } else {
        // For default formats, prefer desktop OpenGL first and only fallback to GLES.
        if (tryCreate(false) || tryCreate(true))
            return true;
    }

    if (d->holdsDisplayRef) {
        releaseSharedEglDisplay();
        d->holdsDisplayRef = false;
    }
    d->display = EGL_NO_DISPLAY;
    return false;
}

QNoVncOpenGLContext::QNoVncOpenGLContext(const QSurfaceFormat &format,
                                         QPlatformOpenGLContext *shareContext)
    : d(new QNoVncOpenGLContextData)
{
    d->format = format;

    if (d->format.redBufferSize() <= 0)
        d->format.setRedBufferSize(8);
    if (d->format.greenBufferSize() <= 0)
        d->format.setGreenBufferSize(8);
    if (d->format.blueBufferSize() <= 0)
        d->format.setBlueBufferSize(8);
    if (d->format.alphaBufferSize() < 0)
        d->format.setAlphaBufferSize(8);
    if (d->format.depthBufferSize() < 0)
        d->format.setDepthBufferSize(24);
    if (d->format.stencilBufferSize() < 0)
        d->format.setStencilBufferSize(8);

    if (d->format.renderableType() == QSurfaceFormat::DefaultRenderableType)
        d->format.setRenderableType(QSurfaceFormat::OpenGL);

    d->format.setVersion(qMax(2, d->format.majorVersion()), qMax(0, d->format.minorVersion()));
    if (d->format.renderableType() == QSurfaceFormat::OpenGL)
        d->format.setProfile(QSurfaceFormat::NoProfile);

    EGLContext shareEglContext = EGL_NO_CONTEXT;
    if (auto *shareNoVncContext = dynamic_cast<QNoVncOpenGLContext *>(shareContext))
        shareEglContext = shareNoVncContext->d->context;

    if (!createEglDisplayAndContext(d.data(), shareEglContext))
        qCWarning(lcVnc) << "novnc: Failed to initialize EGL context";
}

QNoVncOpenGLContext::~QNoVncOpenGLContext()
{
    if (d->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(d->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d->surface != EGL_NO_SURFACE)
            eglDestroySurface(d->display, d->surface);
        if (d->context != EGL_NO_CONTEXT)
            eglDestroyContext(d->display, d->context);
    }

    if (d->holdsDisplayRef) {
        releaseSharedEglDisplay();
        d->holdsDisplayRef = false;
    }
}

bool QNoVncOpenGLContext::ensureSurfaceForWindow(QNoVncWindow *window, const QSize &size)
{
    if (!window || d->display == EGL_NO_DISPLAY || d->context == EGL_NO_CONTEXT)
        return false;

    if (size.isEmpty())
        return false;

    if (d->surface != EGL_NO_SURFACE && d->surfaceSize == size)
        return true;

    if (d->surface != EGL_NO_SURFACE) {
        eglDestroySurface(d->display, d->surface);
        d->surface = EGL_NO_SURFACE;
        d->surfaceSize = QSize();
    }

    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, size.width(),
        EGL_HEIGHT, size.height(),
        EGL_NONE
    };

    d->surface = eglCreatePbufferSurface(d->display, d->config, pbufferAttribs);
    if (d->surface == EGL_NO_SURFACE)
        return false;

    d->surfaceSize = size;
    return true;
}

bool QNoVncOpenGLContext::ensureFramebuffer(const QSize &size)
{
    if (size.isEmpty())
        return false;

    if (d->fbo && d->fboSize == size)
        return true;

    if (d->depthStencilRbo) {
        context()->functions()->glDeleteRenderbuffers(1, &d->depthStencilRbo);
        d->depthStencilRbo = 0;
    }
    if (d->colorTex) {
        context()->functions()->glDeleteTextures(1, &d->colorTex);
        d->colorTex = 0;
    }
    if (d->fbo) {
        context()->functions()->glDeleteFramebuffers(1, &d->fbo);
        d->fbo = 0;
    }

    GLint previousFbo = 0;
    context()->functions()->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);

    context()->functions()->glGenFramebuffers(1, &d->fbo);
    if (!bindFramebufferRaw(context(), GL_FRAMEBUFFER, d->fbo))
        return false;

    context()->functions()->glGenTextures(1, &d->colorTex);
    context()->functions()->glBindTexture(GL_TEXTURE_2D, d->colorTex);
    context()->functions()->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    context()->functions()->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    context()->functions()->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    context()->functions()->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    context()->functions()->glTexImage2D(GL_TEXTURE_2D,
                                         0,
                                         GL_RGBA8,
                                         size.width(),
                                         size.height(),
                                         0,
                                         GL_RGBA,
                                         GL_UNSIGNED_BYTE,
                                         nullptr);
    context()->functions()->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                                   GL_COLOR_ATTACHMENT0,
                                                   GL_TEXTURE_2D,
                                                   d->colorTex,
                                                   0);

    context()->functions()->glGenRenderbuffers(1, &d->depthStencilRbo);
    context()->functions()->glBindRenderbuffer(GL_RENDERBUFFER, d->depthStencilRbo);
    context()->functions()->glRenderbufferStorage(GL_RENDERBUFFER,
                                                  GL_DEPTH24_STENCIL8,
                                                  size.width(),
                                                  size.height());
    context()->functions()->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                                      GL_DEPTH_ATTACHMENT,
                                                      GL_RENDERBUFFER,
                                                      d->depthStencilRbo);
    context()->functions()->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                                      GL_STENCIL_ATTACHMENT,
                                                      GL_RENDERBUFFER,
                                                      d->depthStencilRbo);

    const GLenum status = context()->functions()->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    bindFramebufferRaw(context(), GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qCWarning(lcVnc) << "novnc-gl: FBO incomplete, status=0x" << Qt::hex << int(status);
        return false;
    }

    d->fboSize = size;
    return true;
}

bool QNoVncOpenGLContext::readBackToWindow(QNoVncWindow *window, const QSize &size)
{
    if (!window)
        return false;

    if (size.isEmpty())
        return false;

    QImage *image = window->image();
    const QImage::Format imageFormat = toImageFormat(format());
    QImage newImage(size, imageFormat);
    if (newImage.isNull())
        return false;

    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    context()->functions()->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    context()->functions()->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    const bool isOpenGles = format().renderableType() == QSurfaceFormat::OpenGLES;
    GLenum chosenReadBuffer = GL_BACK;
    if (!isOpenGles)
        context()->functions()->glGetIntegerv(GL_READ_BUFFER, &readBuffer);

    if (d->fbo)
        bindFramebufferRaw(context(), GL_FRAMEBUFFER, d->fbo);

    context()->functions()->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    context()->functions()->glReadPixels(0,
                                         0,
                                         size.width(),
                                         size.height(),
                                         GL_RGBA,
                                         GL_UNSIGNED_BYTE,
                                         newImage.bits());

    if (!isOpenGles)
        chosenReadBuffer = static_cast<GLenum>(readBuffer);

    if (d->fbo)
        bindFramebufferRaw(context(), GL_FRAMEBUFFER, static_cast<GLuint>(readFbo));

    Q_UNUSED(readFbo);
    Q_UNUSED(drawFbo);
    Q_UNUSED(readBuffer);
    Q_UNUSED(chosenReadBuffer);

    // OpenGL readback is bottom-up, Qt expects top-down scanlines.
    const int bytesPerLine = newImage.bytesPerLine();
    QByteArray temp(bytesPerLine, Qt::Uninitialized);
    for (int y = 0, half = newImage.height() / 2; y < half; ++y) {
        uchar *top = newImage.scanLine(y);
        uchar *bottom = newImage.scanLine(newImage.height() - 1 - y);
        memcpy(temp.data(), top, bytesPerLine);
        memcpy(top, bottom, bytesPerLine);
        memcpy(bottom, temp.constData(), bytesPerLine);
    }

    *image = std::move(newImage);
    return true;
}

bool QNoVncOpenGLContext::makeCurrent(QPlatformSurface *surface)
{
    if (!surface || d->display == EGL_NO_DISPLAY || d->context == EGL_NO_CONTEXT)
        return false;

    if (auto *window = dynamic_cast<QNoVncWindow *>(surface)) {
        QSize size = surface->surface() ? surface->surface()->size() : QSize();
        if (size.isEmpty())
            size = window->window() ? window->window()->size() : QSize();
        if (!ensureSurfaceForWindow(window, size))
            return false;
    } else if (d->surface == EGL_NO_SURFACE) {
        // Fallback surfaces (for example Qt RHI probe surfaces) may not be QNoVncWindow.
        const EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        d->surface = eglCreatePbufferSurface(d->display, d->config, pbufferAttribs);
        if (d->surface == EGL_NO_SURFACE)
            return false;
        d->surfaceSize = QSize(1, 1);
    }

    if (eglMakeCurrent(d->display, d->surface, d->surface, d->context) != EGL_TRUE)
        return false;

    return true;
}

void QNoVncOpenGLContext::doneCurrent()
{
    if (d->display != EGL_NO_DISPLAY)
        eglMakeCurrent(d->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void QNoVncOpenGLContext::swapBuffers(QPlatformSurface *surface)
{
    context()->functions()->glFinish();

    auto *window = dynamic_cast<QNoVncWindow *>(surface);
    QSize size = surface && surface->surface() ? surface->surface()->size() : QSize();
    if (window && size.isEmpty())
        size = window->window() ? window->window()->size() : QSize();

    if (window)
        ensureFramebuffer(size);

    bool firstReadOk = true;
    if (window)
        firstReadOk = readBackToWindow(window, size);

    if (d->display != EGL_NO_DISPLAY && d->surface != EGL_NO_SURFACE)
        eglSwapBuffers(d->display, d->surface);

    // Some drivers expose the freshly rendered image only after swap on pbuffer surfaces.
    if (window && (!firstReadOk || frameLooksFlatWhite(*window->image())))
        readBackToWindow(window, size);

    if (!surface || !surface->screen())
        return;

    auto *screen = dynamic_cast<QNoVncScreen *>(surface->screen());
    if (window)
        screen->setDirty(window->geometry());
    else
        screen->setDirty(screen->geometry());

    QEvent request(QEvent::UpdateRequest);
    QCoreApplication::sendEvent(screen, &request);
}

QFunctionPointer QNoVncOpenGLContext::getProcAddress(const char *procName)
{
    return eglGetProcAddress(procName);
}

GLuint QNoVncOpenGLContext::defaultFramebufferObject(QPlatformSurface *surface) const
{
    if (!surface)
        return d->fbo;

    QSize size = surface->surface() ? surface->surface()->size() : QSize();
    if (size.isEmpty()) {
        if (auto *window = dynamic_cast<QNoVncWindow *>(surface))
            size = window->window() ? window->window()->size() : QSize();
    }

    // During platform makeCurrent(), Qt has not yet set currentContext(); avoid using QOpenGLFunctions there.
    if (QOpenGLContext::currentContext() != context())
        return 0;

    if (size.isEmpty())
        return 0;

    auto *self = const_cast<QNoVncOpenGLContext *>(this);
    if (!self->ensureFramebuffer(size))
        return 0;

    return d->fbo;
}

QSurfaceFormat QNoVncOpenGLContext::format() const
{
    return d->format;
}

bool QNoVncOpenGLContext::isSharing() const
{
    return false;
}

bool QNoVncOpenGLContext::isValid() const
{
    return d->display != EGL_NO_DISPLAY && d->context != EGL_NO_CONTEXT;
}

QT_END_NAMESPACE

