#include "display.h"

#include <Arduino.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>

#include "pins.h"

#ifndef DISPLAY_QSPI_HZ
#define DISPLAY_QSPI_HZ 40000000  // 80 MHz measured only ~6 ms/frame faster, not worth the risk
#endif

namespace display {
namespace {

// Partial draw buffers in internal DMA-capable RAM, 1/12 of the screen each
// (2 x 37.5 KB). Smaller ones free RAM but slow every full redraw, and a
// slide between screens redraws it all each frame: at 1/15 that lagged.
// While DMA sends one, LVGL renders into the other.
constexpr uint32_t kBufLines = LCD_HEIGHT / 12;
constexpr uint32_t kBufPixels = LCD_WIDTH * kBufLines;

// CO5300 QSPI framing: 8-bit opcode, 24-bit address carrying the command.
// 0x02 = write, all single line; 0x32 = write with the data on 4 lines.
constexpr uint32_t kOpWrite = 0x02;
constexpr uint32_t kOpWritePixels = 0x32;

constexpr uint8_t kCmdSleepOut = 0x11;
constexpr uint8_t kCmdDisplayOn = 0x29;
constexpr uint8_t kCmdColumns = 0x2A;
constexpr uint8_t kCmdRows = 0x2B;
constexpr uint8_t kCmdWritePixels = 0x2C;
constexpr uint8_t kCmdMadctl = 0x36;
constexpr uint8_t kCmdPixelFormat = 0x3A;
constexpr uint8_t kCmdBrightness = 0x51;

esp_lcd_panel_io_handle_t io;
TouchDrvCST92xx touch;
volatile bool touchIrq = false;
uint8_t currentBrightness = 0;

// The panel's orientation for each Rotation, and which way that turns the
// picture against the panel: on its left side the picture must turn
// clockwise, on its right anticlockwise. Normal is what Waveshare's examples
// use; the turned ones are MADCTL's other mirror-free combinations a quarter
// turn from it (checked on the radio, both sides). The touch mapping follows
// `clockwise`.
struct Orientation {
    uint8_t madctl;
    bool turned, clockwise;
};
const Orientation kOrientations[] = {
    {0xA0, false, false},  // Normal
    {0x00, true, true},    // Left: the picture turned clockwise
    {0xC0, true, false},   // Right: anticlockwise
};
Rotation currentRotation = Rotation::Normal;
uint32_t fakeTouchUntil = 0;
int16_t fakeX = 0, fakeY = 0;
int fakeReads = 0;  // touch reads left to report the simulated press
// simulateDrag(): from fakeX, fakeY to dragX, dragY over dragMs, timed from the first read
int16_t dragX = 0, dragY = 0;
uint32_t dragMs = 0, dragStart = 0;
bool dragging = false, dragStarted = false;

void IRAM_ATTR onTouchIrq() { touchIrq = true; }

void command(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0) {
    esp_lcd_panel_io_tx_param(io, (kOpWrite << 24) | (cmd << 8), data, len);
}

void setParam(uint8_t cmd, uint8_t value) { command(cmd, &value, 1); }

// Init sequence from the vendor driver (as used by Arduino_GFX's CO5300)
void initPanel() {
    pinMode(PIN_LCD_RST, OUTPUT);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(10);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(10);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(150);

    command(kCmdSleepOut);
    delay(120);
    setParam(0xFE, 0x00);            // command page 0
    setParam(0xC4, 0x80);            // SPI mode control
    setParam(kCmdPixelFormat, 0x55); // RGB565
    setParam(0x53, 0x20);            // brightness control on
    setParam(0x63, 0xFF);            // high brightness mode level
    setParam(kCmdBrightness, 0x00);  // stay dark until the UI fades in
    command(kCmdDisplayOn);
    setParam(0x58, 0x00);            // sunlight contrast enhancement off
    setParam(kCmdMadctl, 0xA0);      // orientation used by Waveshare's examples
    delay(10);
}

bool initBus() {
    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_LCD_SCLK;
    bus.data0_io_num = PIN_LCD_SDIO0;
    bus.data1_io_num = PIN_LCD_SDIO1;
    bus.data2_io_num = PIN_LCD_SDIO2;
    bus.data3_io_num = PIN_LCD_SDIO3;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = kBufPixels * 2 + 16;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    esp_lcd_panel_io_spi_config_t cfg = {};
    cfg.cs_gpio_num = PIN_LCD_CS;
    cfg.dc_gpio_num = -1;
    cfg.spi_mode = 0;
    cfg.pclk_hz = DISPLAY_QSPI_HZ;
    cfg.trans_queue_depth = 4;
    cfg.lcd_cmd_bits = 32;
    cfg.lcd_param_bits = 8;
    cfg.flags.quad_mode = 1;
    return esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &cfg,
                                    &io) == ESP_OK;
}

// Runs in the DMA-done interrupt
bool IRAM_ATTR onPixelsSent(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
                            void *ctx) {
    lv_display_flush_ready(static_cast<lv_display_t *>(ctx));
    return false;
}

// CO5300 only accepts windows that start on an even pixel and have even size
void roundArea(lv_event_t *e) {
    auto *area = static_cast<lv_area_t *>(lv_event_get_param(e));
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

void flush(lv_display_t *, const lv_area_t *a, uint8_t *px) {
    const uint8_t columns[] = {uint8_t(a->x1 >> 8), uint8_t(a->x1), uint8_t(a->x2 >> 8),
                               uint8_t(a->x2)};
    const uint8_t rows[] = {uint8_t(a->y1 >> 8), uint8_t(a->y1), uint8_t(a->y2 >> 8),
                            uint8_t(a->y2)};
    command(kCmdColumns, columns, sizeof(columns));
    command(kCmdRows, rows, sizeof(rows));

    const uint32_t pixels = lv_area_get_size(a);
    lv_draw_sw_rgb565_swap(px, pixels);  // panel wants big-endian RGB565
    // Queued for DMA; onPixelsSent tells LVGL when the buffer is free again
    esp_lcd_panel_io_tx_color(io, (kOpWritePixels << 24) | (kCmdWritePixels << 8), px,
                              pixels * 2);
}

// Read the controller on interrupt, and keep polling while a finger is down
// so a release is never missed.
void readTouch(lv_indev_t *, lv_indev_data_t *data) {
    static bool pressed = false;
    static int16_t lastX = 0, lastY = 0;

    // From simulateTouch(), for testing without a finger. Held for a number of
    // reads, not just a time: a slow frame could otherwise swallow the press.
    // A drag presses where it starts, moves in a line, then lifts at the end
    // (one read pressed there first, so the last stretch counts as a move).
    if (dragging) {
        if (!dragStarted) {
            dragStarted = true;
            dragStart = millis();
        }
        const uint32_t elapsed = millis() - dragStart;
        const uint32_t t = elapsed < dragMs ? elapsed : dragMs;
        const bool down = fakeReads > 0;
        if (t == dragMs && down) fakeReads--;
        data->point.x = fakeX + (int32_t)(dragX - fakeX) * (int32_t)t / (int32_t)dragMs;
        data->point.y = fakeY + (int32_t)(dragY - fakeY) * (int32_t)t / (int32_t)dragMs;
        data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        if (!down) dragging = false;
        return;
    }
    if (fakeReads > 0) {
        const bool down = fakeReads > 1 || (int32_t)(fakeTouchUntil - millis()) > 0;
        if (fakeReads > 1) {
            fakeReads--;
        } else if (!down) {
            fakeReads = 0;  // this read reports the release
        }
        data->point.x = fakeX;
        data->point.y = fakeY;
        data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }

    if (touchIrq || pressed) {
        touchIrq = false;
        const TouchPoints points = touch.getTouchPoints();
        pressed = points.hasPoints();
        if (pressed) {
            lastX = points.getPoint(0).x;
            lastY = points.getPoint(0).y;
        }
    }
    // Where the finger is on the picture: the panel reports it as if unturned
    const Orientation &o = kOrientations[static_cast<int>(currentRotation)];
    int16_t x = lastX, y = lastY;
    if (o.turned && o.clockwise) {
        x = lastY;
        y = LCD_WIDTH - 1 - lastX;
    } else if (o.turned) {
        x = LCD_HEIGHT - 1 - lastY;
        y = lastX;
    }
    // Every real touch in the log, to catch presses nobody made (the panel's own)
    static bool logged = false;
    if (pressed != logged) {
        logged = pressed;
        Serial.printf("touch: %s at %d,%d\n", pressed ? "down" : "up", x, y);
    }
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

bool begin() {
    if (!initBus()) {
        Serial.println("display: QSPI bus init failed");
        return false;
    }
    initPanel();

    touch.setPins(PIN_TP_RST, PIN_TP_INT);
    // -1 pins: reuse the already started shared Wire bus as is
    if (!touch.begin(Wire, CST92XX_SLAVE_ADDRESS, -1, -1)) {
        Serial.println("display: CST9220 touch not found");
        return false;
    }
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(true, false);
    pinMode(PIN_TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TP_INT), onTouchIrq, FALLING);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    void *buf1 = heap_caps_malloc(kBufPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(kBufPixels * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        Serial.println("display: out of memory for draw buffers");
        return false;
    }

    lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(disp, flush);
    lv_display_set_buffers(disp, buf1, buf2, kBufPixels * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);

    const esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = onPixelsSent};
    esp_lcd_panel_io_register_event_callbacks(io, &callbacks, disp);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, readTouch);

    return true;
}

void setBrightness(uint8_t level) {
    if (level == currentBrightness) return;
    currentBrightness = level;
    setParam(kCmdBrightness, level);
}

uint8_t brightness() { return currentBrightness; }

void setRotation(Rotation r) {
    if (r == currentRotation) return;
    currentRotation = r;
    // Queued after any pixels still on their way, so a frame never splits
    setParam(kCmdMadctl, kOrientations[static_cast<int>(r)].madctl);
}

Rotation rotation() { return currentRotation; }

void simulateTouch(int16_t x, int16_t y, uint32_t ms) {
    fakeX = x;
    fakeY = y;
    fakeTouchUntil = millis() + ms;
    fakeReads = 3;
    dragging = false;
}

void simulateDrag(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t ms) {
    fakeX = x1;
    fakeY = y1;
    dragX = x2;
    dragY = y2;
    dragMs = ms ? ms : 1;
    dragStarted = false;
    fakeReads = 1;  // the reads at the end still pressed, before the release
    dragging = true;
}

}  // namespace display
