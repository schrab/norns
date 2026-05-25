#include "ssd1325.h"

#include <string.h>

#include "event_types.h"
#include "events.h"

static int spidev_fd = 0;
static bool display_dirty = false;
static bool should_translate_color = false;
static bool should_turn_on = true;
static uint8_t *spidev_buffer = NULL;
static uint32_t *surface_buffer = NULL;
static struct gpiod_chip *gpio_0;
static struct gpiod_line *gpio_dc;
static struct gpiod_line *gpio_reset;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ssd1325_pthread_t;

// SSD1325: 2 pixels per byte (4-bit grayscale)
#define SPIDEV_BUFFER_LEN (SSD1325_PIXEL_WIDTH * SSD1325_PIXEL_HEIGHT / 2)
#define SURFACE_BUFFER_LEN (SSD1325_PIXEL_WIDTH * SSD1325_PIXEL_HEIGHT * sizeof(uint32_t))

int open_spi() {
    uint8_t mode = SPI_MODE_0;
    uint8_t bits_per_word = SPI0_BUS_WIDTH;
    uint8_t little_endian = 0;
    uint32_t speed_hz = 1200000000 / 64;

    int fd = open(SPIDEV_0_0_PATH, O_RDWR | O_SYNC);

    if (fd < 0) {
        fprintf(stderr, "(screen) couldn't open %s\n", SPIDEV_0_0_PATH);
        return -1;
    }

    int outcome = 0 || (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) || (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits_per_word) < 0) || (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz) < 0) || (ioctl(fd, SPI_IOC_WR_LSB_FIRST, &little_endian) < 0);
    if (outcome != 0) {
        fprintf(stderr, "could not set SPI WR settings via IOC\n");
        close(fd);
        return -1;
    }

    return fd;
}

int ssd1325_write_command(uint8_t command, uint8_t data_len, ...) {
    va_list args;
    uint8_t cmd_buf[1];
    uint8_t data_buf[256];
    struct spi_ioc_transfer cmd_transfer = {0};
    struct spi_ioc_transfer data_transfer = {0};

    pthread_mutex_lock(&lock);

    if (spidev_fd <= 0) {
        fprintf(stderr, "%s: spidev not yet opened\n", __func__);
        goto fail;
    }

    gpiod_line_set_value(gpio_dc, 0);

    cmd_buf[0] = command;
    cmd_transfer.tx_buf = (unsigned long)cmd_buf;
    cmd_transfer.len = (uint32_t)sizeof(cmd_buf);

    if (ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &cmd_transfer) < 0) {
        fprintf(stderr, "%s: could not send command-message.\n", __func__);
        goto fail;
    }

    if (data_len > 0) {
        gpiod_line_set_value(gpio_dc, 1);

        va_start(args, data_len);

        for (uint8_t i = 0; i < data_len; i++) {
            data_buf[i] = va_arg(args, int);
        }

        va_end(args);

        data_transfer.tx_buf = (unsigned long)data_buf;
        data_transfer.len = (uint32_t)data_len;

        if (ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &data_transfer) < 0) {
            fprintf(stderr, "%s: could not send data-message.\n", __func__);
            goto fail;
        }
    }

    pthread_mutex_unlock(&lock);
    return 0;
fail:
    pthread_mutex_unlock(&lock);
    return -1;
}

#ifndef NUMARGS
#define NUMARGS(...) (sizeof((int[]){__VA_ARGS__}) / sizeof(int))
#endif
#define write_command_with_data(x, ...) \
    (ssd1325_write_command(x, NUMARGS(__VA_ARGS__), __VA_ARGS__))
#define write_command(x) \
    (ssd1325_write_command(x, 0, 0))

static void *ssd1325_thread_run(void *p) {
    (void)p;

    static struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = 16666666, // 60Hz
    };

    while (spidev_buffer) {
        if (display_dirty) {
            ssd1325_refresh();
            display_dirty = false;
        }

        event_post(event_data_new(EVENT_SCREEN_REFRESH));

        clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }

    return NULL;
}

void ssd1325_init() {

    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(stderr, "%s: pthread_mutex_init failed\n", __func__);
        return;
    }

    surface_buffer = calloc(SURFACE_BUFFER_LEN, 1);
    if (surface_buffer == NULL) {
        fprintf(stderr, "%s: couldn't allocate surface_buffer\n", __func__);
        return;
    }

    spidev_buffer = calloc(SPIDEV_BUFFER_LEN, 1);
    if (spidev_buffer == NULL) {
        fprintf(stderr, "%s: couldn't allocate spidev_buffer\n", __func__);
        return;
    }

    spidev_fd = open_spi(SPIDEV_0_0_PATH);
    if (spidev_fd < 0) {
        fprintf(stderr, "%s: couldn't open %s.\n", __func__, SPIDEV_0_0_PATH);
        return;
    }

    gpio_0 = gpiod_chip_open_by_name(SSD1325_DC_AND_RESET_GPIO_CHIP);
    gpio_dc = gpiod_chip_get_line(gpio_0, SSD1325_DC_GPIO_LINE);
    gpio_reset = gpiod_chip_get_line(gpio_0, SSD1325_RESET_GPIO_LINE);

    gpiod_line_request_output(gpio_dc, "D/C", 0);
    gpiod_line_request_output(gpio_reset, "RST", 0);

    // Hardware reset pulse - datasheet Section 8.5: min 1us RST low, then 1us high
    gpiod_line_set_value(gpio_reset, 0);
    usleep(10);
    gpiod_line_set_value(gpio_reset, 1);
    usleep(10);

    // Init sequence from your fbtft driver
    write_command(SSD1325_SET_DISPLAY_OFF);
    write_command_with_data(SSD1325_SET_OSCILLATOR_FREQUENCY, 0xF1);
    write_command_with_data(SSD1325_SET_MULTIPLEX_RATIO, 0x3f);
    write_command_with_data(SSD1325_SET_DISPLAY_OFFSET, 0x4C);
    write_command_with_data(SSD1325_SET_DISPLAY_START_LINE, 0x00);
    write_command_with_data(SSD1325_SET_MASTER_CONFIG, 0x02);
    write_command_with_data(SSD1325_SET_REMAP, 0x56);
    write_command(SSD1325_SET_FULL_CURRENT_RANGE);
    
    write_command(0x01);
    write_command(0x11);
    write_command(0x22);
    write_command(0x32);
    write_command(0x43);
    write_command(0x54);
    write_command(0x65);
    write_command(0x76);
    
    write_command_with_data(SSD1325_SET_CONTRAST_CURRENT, 0x7f);
    write_command_with_data(SSD1325_SET_ROW_PERIOD, 0x51);
    write_command_with_data(SSD1325_SET_PHASE_LENGTH, 0x55);
    write_command_with_data(SSD1325_SET_PRECHARGE_COMPENSATION_LEVEL, 0x02);
    write_command_with_data(SSD1325_ENABLE_PRECHARGE_COMPENSATION, 0x28);
    write_command_with_data(SSD1325_SET_VCOMH_VOLTAGE, 0x55);
    write_command_with_data(SSD1325_SET_SEGMENT_LOW_VOLTAGE, 0x02);
    write_command(SSD1325_SET_DISPLAY_MODE_NORMAL);

    static struct sched_param param;
    param.sched_priority = sched_get_priority_max(SCHED_OTHER);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedparam(&attr, &param);
    pthread_create(&ssd1325_pthread_t, &attr, &ssd1325_thread_run, NULL);
    pthread_attr_destroy(&attr);
}

void ssd1325_deinit() {
    if (spidev_fd > 0) {
        gpiod_line_set_value(gpio_reset, 0);
        pthread_mutex_destroy(&lock);
        gpiod_line_release(gpio_reset);
        gpiod_line_release(gpio_dc);
        gpiod_chip_close(gpio_0);
        close(spidev_fd);
        free(spidev_buffer);
        spidev_buffer = NULL;
    }
}

void ssd1325_update(cairo_surface_t *surface_pointer, bool surface_may_have_color) {
    pthread_mutex_lock(&lock);

    should_translate_color = surface_may_have_color;

    if (surface_buffer != NULL && surface_pointer != NULL) {
        const uint32_t surface_w = cairo_image_surface_get_width(surface_pointer);
        const uint32_t surface_h = cairo_image_surface_get_height(surface_pointer);
        cairo_format_t surface_f = cairo_image_surface_get_format(surface_pointer);

        if (surface_w != 128 || surface_h != 64 || surface_f != CAIRO_FORMAT_ARGB32) {
            fprintf(stderr, "%s: %ux%u = invalid surface size\n", __func__, surface_w, surface_h);
            goto early_return;
        }

        memcpy(
            (uint8_t *)surface_buffer,
            (uint8_t *)cairo_image_surface_get_data(surface_pointer),
            SURFACE_BUFFER_LEN);
    } else {
        fprintf(stderr, "%s: surface_buffer (%p) surface_pointer (%p)\n", __func__, surface_buffer, surface_pointer);
    }

    display_dirty = true;

early_return:
    pthread_mutex_unlock(&lock);
}

void ssd1325_refresh() {
    struct spi_ioc_transfer transfer = {0};

    if (spidev_fd <= 0) {
        fprintf(stderr, "%s: spidev not yet opened.\n", __func__);
        return;
    }

    if (surface_buffer == NULL) {
        fprintf(stderr, "%s: surface_buffer not allocated yet.\n", __func__);
        return;
    }

    write_command_with_data(SSD1325_SET_COLUMN_ADDRESS, 0x00, 0x7f);
    write_command_with_data(SSD1325_SET_ROW_ADDRESS, 0x00, 0x3f);
    write_command(SSD1325_WRITE_RAM_COMMAND);

    if (should_turn_on) {
        write_command(SSD1325_SET_DISPLAY_ON);
        should_turn_on = 0;
    }

    pthread_mutex_lock(&lock);

    // Convert ARGB32 to 4-bit grayscale nibble-packed
    if (should_translate_color) {
        for (uint32_t y = 0; y < SSD1325_PIXEL_HEIGHT; y++) {
            for (uint32_t x = 0; x < SSD1325_PIXEL_WIDTH; x += 2) {
                const uint32_t idx = (y * SSD1325_PIXEL_WIDTH + x);
                const uint8_t *pixel1 = (uint8_t *)(surface_buffer + idx);
                const uint8_t *pixel2 = (uint8_t *)(surface_buffer + idx + 1);

                uint8_t b1 = pixel1[0], g1 = pixel1[1], r1 = pixel1[2];
                uint8_t b2 = pixel2[0], g2 = pixel2[1], r2 = pixel2[2];

                uint16_t gray1 = (64 * r1 + 160 * g1 + 32 * b1) >> 8;
                uint16_t gray2 = (64 * r2 + 160 * g2 + 32 * b2) >> 8;

                uint8_t g4_1 = (gray1 >> 4) & 0x0F;
                uint8_t g4_2 = (gray2 >> 4) & 0x0F;

                spidev_buffer[(y * SSD1325_PIXEL_WIDTH + x) / 2] = (g4_1 << 4) | g4_2;
            }
        }
    } else {
        for (uint32_t y = 0; y < SSD1325_PIXEL_HEIGHT; y++) {
            for (uint32_t x = 0; x < SSD1325_PIXEL_WIDTH; x += 2) {
                const uint32_t idx = (y * SSD1325_PIXEL_WIDTH + x);
                const uint8_t *pixel1 = (uint8_t *)(surface_buffer + idx);
                const uint8_t *pixel2 = (uint8_t *)(surface_buffer + idx + 1);

                uint8_t g1 = pixel1[1];
                uint8_t g2 = pixel2[1];

                uint8_t g4_1 = (g1 >> 4) & 0x0F;
                uint8_t g4_2 = (g2 >> 4) & 0x0F;

                spidev_buffer[(y * SSD1325_PIXEL_WIDTH + x) / 2] = (g4_1 << 4) | g4_2;
            }
        }
    }

    gpiod_line_set_value(gpio_dc, 1);

    // Send pixel data - buffer is 4096 bytes, well within spidev bufsiz (8192)
    transfer.tx_buf = (unsigned long)spidev_buffer;
    transfer.len = SPIDEV_BUFFER_LEN;
    if (ioctl(spidev_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        fprintf(stderr, "%s: SPI data transfer failed.\n", __func__);
        goto early_return;
    }

early_return:
    pthread_mutex_unlock(&lock);
    return;
}

void ssd1325_set_brightness(uint8_t b) {
    write_command_with_data(SSD1325_SET_PRECHARGE_VOLTAGE, b);
}

void ssd1325_set_contrast(uint8_t c) {
    write_command_with_data(SSD1325_SET_CONTRAST_CURRENT, c);
}

void ssd1325_set_display_mode(ssd1325_display_mode_t mode) {
    write_command((uint8_t)mode);
}

void ssd1325_set_gamma(double g) {
    uint8_t gs[16] = {0};
    double max_grayscale = SSD1325_GRAYSCALE_MAX_VALUE;
    
    for (int level = 0; level <= 14; level++) {
        double pre_gamma = level / 14.0;
        double grayscale = round(pow(pre_gamma, g) * max_grayscale);
        double limit = (grayscale > max_grayscale) ? max_grayscale : grayscale;
        gs[level + 1] = (uint8_t)limit;
    }

    write_command_with_data(
        SSD1325_SET_GRAYSCALE_TABLE,
        gs[0x1], gs[0x2], gs[0x3], gs[0x4], gs[0x5],
        gs[0x6], gs[0x7], gs[0x8], gs[0x9], gs[0xA],
        gs[0xB], gs[0xC], gs[0xD], gs[0xE], gs[0xF]);
    write_command(SSD1325_SET_DEFAULT_LINEAR_GRAY_SCALE);
}

void ssd1325_set_refresh_rate(uint8_t hz) {
    (void)hz;
    write_command_with_data(SSD1325_SET_OSCILLATOR_FREQUENCY, 0xF1);
}

uint8_t *ssd1325_resize_buffer(size_t size) {
    spidev_buffer = realloc(spidev_buffer, size);
    return spidev_buffer;
}

#undef NUMARGS
#undef write_command
#undef write_command_with_data