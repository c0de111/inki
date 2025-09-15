Custom Fonts Documentation

This directory contains custom font files that are NOT from the upstream Waveshare e-Paper repository.

## Contents

- **Ubuntu Mono fonts**: 42 font files in various sizes (6pt to 36pt)
  - Regular and bold variants
  - Generated specifically for the inki project
  - Formats: `font_ubuntu_mono_{size}pt[_bold].c`

## Font Generation

These fonts were generated from the Ubuntu Mono typeface using custom tooling.
The fonts are in Waveshare ePaper font format compatible with the Paint library.

## Usage

Include the desired font header and use with Paint library functions:
```c
#include "font_ubuntu_mono_20pt_bold.c"
Paint_DrawString_EN(x, y, "text", &font_ubuntu_mono_20pt_bold, WHITE, BLACK);
```

## Maintenance

- These fonts are custom to the inki project
- They should be preserved during Waveshare library updates
- Updates to these fonts require manual regeneration
- No upstream synchronization needed

## Integration

These fonts are compiled separately from the Waveshare fonts and included in the build system via CMakeLists.txt.