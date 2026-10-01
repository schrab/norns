# Memory

## Project Overview
This is a norns fork for a custom Fates shield with SSD1325 OLED (128x128, 4-bit grayscale). The screen uses direct SPI mode via a custom C driver (`matron/src/hardware/screen/ssd1325.c`) instead of the old fbtft kernel driver. No device tree overlay for the display — all SPI control is done from userspace via GPIO (D/C pin), spidev (data), and libgpiod.

## Architecture Notes
- **SSD1325 protocol**: All bytes in a command sequence (command byte + parameter bytes) must be sent with D/C=LOW. Pixel data is sent separately with D/C=HIGH. See `ssd1325_write_command` in `ssd1325.c` — the SPI transfer combines command + params into a single `spidev_buf` write with a single `gpiod_line_set_value(gpio_dc, 0)`.
- **Pixel data packing**: Remap 0x56 sets vertical address increment. Data is sent column-by-column (x outer loop, y inner loop), packing two 4-bit pixels per byte.
- **Init values**: From fbtft driver (proven working with this exact panel) — remap 0x56, oscillator 0xF1, etc. These must not be changed.
- **libmonome**: `monome_led_ring_intensity` doesn't exist in released versions. Use `monome_led_intensity` instead.
- **Build system**: waf (`./waf clean && ./waf configure --release && ./waf build --release`)

## Development Workflow
1. Edit code on WSL2 machine
2. Commit and push to `https://github.com/schrab/norns`
3. On Fates RPi: `cd ~/norns && git pull && ./waf build --release && sudo reboot`
4. The RPi is at 192.168.8.163

## Common Issues
- **update.sh overwrites repo**: The Fates update script replaces `/home/we/norns` with the packaged copy, losing local changes. After running update.sh, re-clone from origin and rebuild.
- **Under-voltage detected**: The Pi's 5V supply is marginal. Can cause SPI glitches, USB drops, crashes.
- **Missing libmonome**: Build from source: `git clone https://github.com/monome/libmonome && cd libmonome && ./waf configure --prefix=/usr && sudo ./waf install`
