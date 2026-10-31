# Boot artwork

The NeoPod boot screen loads `/.echo/boot.rgb565` from the SD card. Copy
`release/sd-card/.echo/boot.rgb565` into the SD card's `.echo` folder after
flashing the updated firmware. Both themes use the same boot artwork.

The image is 240 × 320 pixels, little-endian RGB565 (153,600 bytes). The boot
renderer reads it into the existing strip buffer and fades it in from black.
Missing, unreadable or incorrectly sized artwork falls back to the NeoPod text.

The packaging script regenerates it from `assets/boot.png` with
`tools/build_boot_image.py`. Keep replacement source images at a 3:4 aspect ratio.
