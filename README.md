# QNoVNC QPA Platform Plugin for Qt 6

A Qt 6 Platform Abstraction (QPA) plugin enabling noVNC client support via WebSockets,
based on the original Qt VNC QPA plugin.

## Additional changes
Prevents segfaults when the user is destroying and recreating a lot of windows.

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