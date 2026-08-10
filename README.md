# QNoVNC QPA Platform Plugin for Qt 5 and 6

A Qt 6 Platform Abstraction (QPA) plugin enabling noVNC client support via WebSockets,
based on the original Qt VNC QPA plugin.

## Additional changes
- Allows you to listen on a custom host (not only 0.0.0.0) (example: `QT_QPA_PLATFORM="novnc:size=1078x1106:depth=16:port=5911:host=127.0.0.1"`)
- Prevents segfaults when the user is destroying and recreating a lot of windows.
- Zlib compression support
- Optional client update timing diagnostics via `QNOVNC_DEBUG_REFRESH`
- Added windows support (only qt6, currenctly readonly)
- Optional OpenGL context support via EGL/Mesa on Linux (initial integration adapted from `https://github.com/duerkopp-adler/qtvncplugin` by `https://github.com/poker-phase` and `https://github.com/KoopA-DA`)
- Popup mouse/keyboard grab support for repeated Qt menu and combo-box popups.

## Debugging

To inspect the refresh cadence seen by connected noVNC clients, enable the client
update timing logger:

```bash
QNOVNC_DEBUG_REFRESH=1 QT_QPA_PLATFORM=novnc ...
```

While enabled the plugin emits a log line per connected client every time an encoded
framebuffer update is sent. Each line looks like `Client[<id>] updates: ...` and reports
the average/last interval between the updates along with the corresponding time spent in
the encoder. The statistics are aggregated over a one‑second window; you can change that
interval through `QNOVNC_DEBUG_REFRESH_WINDOW_MS` (milliseconds).

For popup/menu input routing diagnostics, enable:

```bash
QT_LOGGING_RULES="qt.qpa.novnc.popup.debug=true" QT_QPA_PLATFORM=novnc ...
```

Other focused debug categories are `qt.qpa.novnc.protocol`, `qt.qpa.novnc.encoding`,
and `qt.qpa.novnc.input`.

## OpenGL Notes

- The EGL/Mesa path is intended for Linux and now supports both `QtWebEngine` and `Qt3D` (including `Qt3DWindow` embedded via `QWidget::createWindowContainer(...)`).
- For software rendering you can force Mesa/llvmpipe via environment variables:

```bash
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
export QT_QPA_PLATFORM=novnc
```

- If OpenGL content is missing while widgets are visible, verify that the application is actually using this platform plugin (`QT_QPA_PLATFORM=novnc`) and that the plugin was built with EGL support enabled.

## Building

```bash
# Qt 6 build (default) 
# (Windows: When using Ninja make sure to set -DCMAKE_BUILD_TYPE=Release when generating the build files)
cmake -S . -B build
cmake --build build

# Qt 5 build (if Qt 6 is unavailable or explicitly desired)
# Not supported on windows!
cmake -S . -B build-qt5 -DQT_DEFAULT_MAJOR_VERSION=5
cmake --build build-qt5
```

## Test utility

An optional Qt 6 popup test utility can be built with:

```bash
cmake -S . -B build -DQT_DEFAULT_MAJOR_VERSION=6 -DQNOVNC_BUILD_TESTS=ON
cmake --build build --target qnovnc_popupgrab_test
QT_QPA_PLATFORM_PLUGIN_PATH=build QT_QPA_PLATFORM="novnc:size=400x300:depth=32:port=5911:host=127.0.0.1" build/qnovnc_popupgrab_test
```

## Building RPMs in Docker

Use the helper script to produce SRPMs/Binary RPMs for both Rocky Linux 9 (Qt 5) and AlmaLinux 10 (Qt 6). The source tarball is rebuilt from the current workspace (including uncommitted changes) each time you run it, honoring the patterns listed in `.gitignore` for what to exclude:

```bash
scripts/build-rpm-in-docker.sh
```

Requirements:
- Docker installed locally
- Internet access for the images to install build dependencies (dnf builddep)

Artifacts are copied into `rpm-dist/el9` and `rpm-dist/el10`, each containing `RPMS` and `SRPMS` folders.

## Licensing

This project is licensed under the following licenses:

SPDX-License-Identifier: LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

For the full license text, please refer to the `LICENSE` files in this repository (`LICENSE.GPL-2.0-only`, `LICENSE.GPL-3.0-only` and `LICENSE.LGPL-3.0-only`).

### Original Work Attribution

This software is a derivative work of the Qt 6 VNC QPA plugin,
which is part of the QtBase module. The original Qt VNC QPA plugin code is
Copyright (C) 2016/2017 The Qt Company Ltd., and is available under the LGPLv3 / GPLv2 / GPLv3
or commercial license from The Qt Company.
Original source code available at [https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/vnc](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/vnc).

### Copyright
Copyright (C) 2026, CraftingDragon007 <info@craftingdragon.ch> <br>
Copyright (C) 2016/2017 The Qt Company Ltd for the original Qt VNC QPA plugin
