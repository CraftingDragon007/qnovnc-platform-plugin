# QNoVNC QPA Platform Plugin for Qt 5 and 6

A Qt 6 Platform Abstraction (QPA) plugin enabling noVNC client support via WebSockets,
based on the original Qt VNC QPA plugin.

## Additional changes
- Allows you to listen on a custom host (not only 0.0.0.0) (example: `QT_QPA_PLATFORM="novnc:size=1078x1106:depth=16:port=5911:host=127.0.0.1"`)
- Prevents segfaults when the user is destroying and recreating a lot of windows.
- Zlib compression support
- Optional client update timing diagnostics via `QNOVNC_DEBUG_REFRESH`

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

## Building

```bash
# Qt 6 build (default)
cmake -S . -B build
cmake --build build

# Qt 5 build (if Qt 6 is unavailable or explicitly desired)
cmake -S . -B build-qt5 -DQT_DEFAULT_MAJOR_VERSION=5
cmake --build build-qt5
```

## Licensing

This project is licensed under the **GNU Lesser General Public License v3.0 (LGPLv3)**.

For the full license text, please refer to the `LICENSE` file in this repository.

### Original Work Attribution

This software is a derivative work of the Qt 6 VNC QPA plugin,
which is part of the QtBase module. The original Qt VNC QPA plugin code is
Copyright (C) 2025 The Qt Company Ltd., and is available under the LGPLv3 / GPLv2 / GPLv3
or commercial license from The Qt Company.
Original source code available at [https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/vnc](https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/vnc).

### Copyright

Copyright (c) 2025, CraftingDragon007 <info@craftingdragon.ch>
