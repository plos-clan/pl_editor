# pleditor - VT100-based Text Editor

pleditor is a simple text editor that works with terminal devices supporting VT100 escape sequences. The core editor functionality is platform-independent, while platform-specific functionality is abstracted through a clean interface.

## Features

- VT100 terminal interface with status bar
- Platform-independent core that works with any VT100-compatible terminal
- Basic text editing operations
- Reference Linux implementation included
- Simple to port to other platforms by implementing the platform interface
- Filters control characters to prevent editing issues

## Keyboard Shortcuts

- `Ctrl-S`: Save file
- `Ctrl-Q`: Quit
- Arrow keys: Move cursor
- Page Up/Down: Scroll by page
- Home/End: Move to start/end of line

## Building

The project uses xmake as its build system. To build:

```
xmake
```

To run:

```
xmake run [filename]
```

## Architecture

The editor is split into platform-independent and platform-dependent code:

- `pleditor.h/c`: Core editor functionality (platform-independent)
- `terminal.h`: VT100 terminal control codes (platform-independent)
- `platform.h`: Platform abstraction interface
- `platform/linux.c`: Linux implementation of the platform interface

## Porting to Other Platforms

To port the editor to another platform:

1. Create a new file in the `src/platform` directory (e.g., `src/platform/your_platform.c`)
2. Implement all functions declared in `platform.h`
3. Update the build system to use your platform file

## License

See LICENSE file for details.