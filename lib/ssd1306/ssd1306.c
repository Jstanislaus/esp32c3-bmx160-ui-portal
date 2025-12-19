#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_idf_version.h" // Needed for version check

#include "ssd1306.h"
#include "font8x8_basic.h"

// Include the New Driver Header if on ESP-IDF v5.2+
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0))
#include "driver/i2c_master.h"
#endif

#define TAG "SSD1306"

#define PACK8 __attribute__((aligned( __alignof__( uint8_t ) ), packed ))

typedef union out_column_t {
    uint32_t u32;
    uint8_t  u8[4];
} PACK8 out_column_t;

// Forward declaration if i2c.c is removed
void i2c_display_image(SSD1306_t * dev, int page, int seg, const uint8_t * images, int width);
void i2c_contrast(SSD1306_t * dev, int contrast);
void i2c_hardware_scroll(SSD1306_t * dev, ssd1306_scroll_type_t scroll);

void ssd1306_init(SSD1306_t * dev, int width, int height)
{
    dev->_width = width;
    dev->_height = height;
    dev->_pages = height / 8;

    if (dev->_address == SPI_ADDRESS) {
        spi_init(dev, width, height);
    } else {
        // FIX: Bypass legacy i2c_init if using New Driver (Handle is already set in ui_manager)
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0))
        ESP_LOGI(TAG, "New Driver: Legacy i2c_init skipped. Using existing handle.");
#else
        i2c_init(dev, width, height);
#endif
    }
    // Initialize internal buffer
    for (int i=0;i<dev->_pages;i++) {
        memset(dev->_page[i]._segs, 0, 128);
    }
}

int ssd1306_get_width(SSD1306_t * dev)
{
    return dev->_width;
}

int ssd1306_get_height(SSD1306_t * dev)
{
    return dev->_height;
}

int ssd1306_get_pages(SSD1306_t * dev)
{
    return dev->_pages;
}

void ssd1306_show_buffer(SSD1306_t * dev)
{
    if (dev->_address == SPI_ADDRESS) {
        for (int page=0; page<dev->_pages;page++) {
            spi_display_image(dev, page, 0, dev->_page[page]._segs, dev->_width);
        }
    } else {
        for (int page=0; page<dev->_pages;page++) {
            i2c_display_image(dev, page, 0, dev->_page[page]._segs, dev->_width);
        }
    }
}

void ssd1306_set_buffer(SSD1306_t * dev, const uint8_t * buffer)
{
    int index = 0;
    for (int page=0; page<dev->_pages;page++) {
        memcpy(&dev->_page[page]._segs, &buffer[index], 128);
        index = index + 128;
    }
}

void ssd1306_get_buffer(SSD1306_t * dev, uint8_t * buffer)
{
    int index = 0;
    for (int page=0; page<dev->_pages;page++) {
        memcpy(&buffer[index], &dev->_page[page]._segs, 128);
        index = index + 128;
    }
}

void ssd1306_set_page(SSD1306_t * dev, int page, const uint8_t * buffer)
{
    memcpy(&dev->_page[page]._segs, buffer, 128);
}

void ssd1306_get_page(SSD1306_t * dev, int page, uint8_t * buffer)
{
    memcpy(buffer, &dev->_page[page]._segs, 128);
}

void ssd1306_display_image(SSD1306_t * dev, int page, int seg, const uint8_t * images, int width)
{
    if (dev->_address == SPI_ADDRESS) {
        spi_display_image(dev, page, seg, images, width);
    } else {
        i2c_display_image(dev, page, seg, images, width);
    }
    // Set to internal buffer
    memcpy(&dev->_page[page]._segs[seg], images, width);
}

void ssd1306_display_text(SSD1306_t * dev, int page, const char * text, int text_len, bool invert)
{
    if (page >= dev->_pages) return;
    int _text_len = text_len;
    if (_text_len > 16) _text_len = 16;

    int seg = 0;
    uint8_t image[8];
    for (int i = 0; i < _text_len; i++) {
        memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        ssd1306_display_image(dev, page, seg, image, 8);
        seg = seg + 8;
    }
}

void ssd1306_display_text_box1(SSD1306_t * dev, int page, int seg, const char * text, int box_width, int text_len, bool invert, int delay)
{
    if (page >= dev->_pages) return;
    int text_box_pixel = box_width * 8;
    if (seg + text_box_pixel > dev->_width) return;

    int _seg = seg;
    uint8_t image[8];
    for (int i = 0; i < box_width; i++) {
        memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        ssd1306_display_image(dev, page, _seg, image, 8);
        _seg = _seg + 8;
    }
    vTaskDelay(delay);

    // Horizontally scroll inside the box
    for (int _text=box_width;_text<text_len;_text++) {
        memcpy(image, font8x8_basic_tr[(uint8_t)text[_text]], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        for (int _bit=0;_bit<8;_bit++) {
            for (int _pixel=0;_pixel<text_box_pixel;_pixel++) {
                dev->_page[page]._segs[_pixel+seg] = dev->_page[page]._segs[_pixel+seg+1];
            }
            dev->_page[page]._segs[seg+text_box_pixel-1] = image[_bit];
            ssd1306_display_image(dev, page, seg, &dev->_page[page]._segs[seg], text_box_pixel);
            vTaskDelay(delay);
        }
    }
}

void ssd1306_display_text_box2(SSD1306_t * dev, int page, int seg, const char * text, int box_width, int text_len, bool invert, int delay)
{
    if (page >= dev->_pages) return;
    int text_box_pixel = box_width * 8;
    if (seg + text_box_pixel > dev->_width) return;

    int _seg = seg;
    uint8_t image[8];

    // Fill the text box with blanks
    for (int i = 0; i < box_width; i++) {
        memcpy(image, font8x8_basic_tr[0x20], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        ssd1306_display_image(dev, page, _seg, image, 8);
        _seg = _seg + 8;
    }
    vTaskDelay(delay);

    // Horizontally scroll inside the box
    for (int _text=0;_text<text_len;_text++) {
        memcpy(image, font8x8_basic_tr[(uint8_t)text[_text]], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        for (int _bit=0;_bit<8;_bit++) {
            for (int _pixel=0;_pixel<text_box_pixel;_pixel++) {
                dev->_page[page]._segs[_pixel+seg] = dev->_page[page]._segs[_pixel+seg+1];
            }
            dev->_page[page]._segs[seg+text_box_pixel-1] = image[_bit];
            ssd1306_display_image(dev, page, seg, &dev->_page[page]._segs[seg], text_box_pixel);
            vTaskDelay(delay);
        }
    }

    // Horizontally scroll inside the box
    for (int _text=0;_text<box_width;_text++) {
        memcpy(image, font8x8_basic_tr[0x20], 8);
        if (invert) ssd1306_invert(image, 8);
        if (dev->_flip) ssd1306_flip(image, 8);
        for (int _bit=0;_bit<8;_bit++) {
            for (int _pixel=0;_pixel<text_box_pixel;_pixel++) {
                dev->_page[page]._segs[_pixel+seg] = dev->_page[page]._segs[_pixel+seg+1];
            }
            dev->_page[page]._segs[seg+text_box_pixel-1] = image[_bit];
            ssd1306_display_image(dev, page, seg, &dev->_page[page]._segs[seg], text_box_pixel);
            vTaskDelay(delay);
        }
    }
}

void ssd1306_display_text_x3(SSD1306_t * dev, int page, const char * text, int text_len, bool invert)
{
    if (page >= dev->_pages) return;
    int _text_len = text_len;
    if (_text_len > 5) _text_len = 5;

    int seg = 0;

    for (int nn = 0; nn < _text_len; nn++) {
        uint8_t const * const in_columns = font8x8_basic_tr[(uint8_t)text[nn]];
        out_column_t out_columns[8];
        memset(out_columns, 0, sizeof(out_columns));

        for (int xx = 0; xx < 8; xx++) {
            uint32_t in_bitmask = 0b1;
            uint32_t out_bitmask = 0b111;
            for (int yy = 0; yy < 8; yy++) {
                if (in_columns[xx] & in_bitmask) {
                    out_columns[xx].u32 |= out_bitmask;
                }
                in_bitmask <<= 1;
                out_bitmask <<= 3;
            }
        }

        for (int yy = 0; yy < 3; yy++)  {
            uint8_t image[24];
            for (int xx = 0; xx < 8; xx++) {
                image[xx*3+0] = 
                image[xx*3+1] = 
                image[xx*3+2] = out_columns[xx].u8[yy];
            }
            if (invert) ssd1306_invert(image, 24);
            if (dev->_flip) ssd1306_flip(image, 24);
            if (dev->_address == SPI_ADDRESS) {
                spi_display_image(dev, page+yy, seg, image, 24);
            } else {
                i2c_display_image(dev, page+yy, seg, image, 24);
            }
            memcpy(&dev->_page[page+yy]._segs[seg], image, 24);
        }
        seg = seg + 24;
    }
}

void ssd1306_clear_screen(SSD1306_t * dev, bool invert)
{
    char space[16];
    memset(space, 0x00, sizeof(space));
    for (int page = 0; page < dev->_pages; page++) {
        ssd1306_display_text(dev, page, space, sizeof(space), invert);
    }
}

void ssd1306_clear_line(SSD1306_t * dev, int page, bool invert)
{
    char space[16];
    memset(space, 0x00, sizeof(space));
    ssd1306_display_text(dev, page, space, sizeof(space), invert);
}

void ssd1306_contrast(SSD1306_t * dev, int contrast)
{
    if (dev->_address == SPI_ADDRESS) {
        spi_contrast(dev, contrast);
    } else {
        i2c_contrast(dev, contrast);
    }
}

void ssd1306_software_scroll(SSD1306_t * dev, int start, int end)
{
    if (start < 0 || end < 0) {
        dev->_scEnable = false;
    } else if (start >= dev->_pages || end >= dev->_pages) {
        dev->_scEnable = false;
    } else {
        dev->_scEnable = true;
        dev->_scStart = start;
        dev->_scEnd = end;
        dev->_scDirection = 1;
        if (start > end ) dev->_scDirection = -1;
    }
}

void ssd1306_scroll_text(SSD1306_t * dev, const char * text, int text_len, bool invert)
{
    if (dev->_scEnable == false) return;

    void (*func)(SSD1306_t * dev, int page, int seg, const uint8_t * images, int width);
    if (dev->_address == SPI_ADDRESS) {
        func = spi_display_image;
    } else {
        func = i2c_display_image;
    }

    int srcIndex = dev->_scEnd - dev->_scDirection;
    while(1) {
        int dstIndex = srcIndex + dev->_scDirection;
        for(int seg = 0; seg < dev->_width; seg++) {
            dev->_page[dstIndex]._segs[seg] = dev->_page[srcIndex]._segs[seg];
        }
        (*func)(dev, dstIndex, 0, dev->_page[dstIndex]._segs, sizeof(dev->_page[dstIndex]._segs));
        if (srcIndex == dev->_scStart) break;
        srcIndex = srcIndex - dev->_scDirection;
    }
    
    int _text_len = text_len;
    if (_text_len > 16) _text_len = 16;
    ssd1306_display_text(dev, srcIndex, text, text_len, invert);
}

void ssd1306_scroll_clear(SSD1306_t * dev)
{
    if (dev->_scEnable == false) return;
    int srcIndex = dev->_scEnd - dev->_scDirection;
    while(1) {
        int dstIndex = srcIndex + dev->_scDirection;
        ssd1306_clear_line(dev, dstIndex, false);
        if (dstIndex == dev->_scStart) break;
        srcIndex = srcIndex - dev->_scDirection;
    }
}

void ssd1306_hardware_scroll(SSD1306_t * dev, ssd1306_scroll_type_t scroll)
{
    if (dev->_address == SPI_ADDRESS) {
        spi_hardware_scroll(dev, scroll);
    } else {
        i2c_hardware_scroll(dev, scroll);
    }
}

void ssd1306_wrap_arround(SSD1306_t * dev, ssd1306_scroll_type_t scroll, int start, int end, int8_t delay)
{
    if (scroll == SCROLL_RIGHT) {
        int _start = start; 
        int _end = end; 
        if (_end >= dev->_pages) _end = dev->_pages - 1;
        uint8_t wk;
        for (int page=_start;page<=_end;page++) {
            wk = dev->_page[page]._segs[127];
            for (int seg=127;seg>0;seg--) {
                dev->_page[page]._segs[seg] = dev->_page[page]._segs[seg-1];
            }
            dev->_page[page]._segs[0] = wk;
        }
    } else if (scroll == SCROLL_LEFT) {
        int _start = start; 
        int _end = end; 
        if (_end >= dev->_pages) _end = dev->_pages - 1;
        uint8_t wk;
        for (int page=_start;page<=_end;page++) {
            wk = dev->_page[page]._segs[0];
            for (int seg=0;seg<127;seg++) {
                dev->_page[page]._segs[seg] = dev->_page[page]._segs[seg+1];
            }
            dev->_page[page]._segs[127] = wk;
        }
    } else if (scroll == SCROLL_UP) {
        int _start = start; 
        int _end = end; 
        if (_end >= dev->_width) _end = dev->_width - 1;
        uint8_t wk0, wk1, wk2;
        uint8_t save[128];
        for (int seg=0;seg<128;seg++) save[seg] = dev->_page[0]._segs[seg];
        for (int page=0;page<dev->_pages-1;page++) {
            for (int seg=_start;seg<=_end;seg++) {
                wk0 = dev->_page[page]._segs[seg];
                wk1 = dev->_page[page+1]._segs[seg];
                if (dev->_flip) { wk0 = ssd1306_rotate_byte(wk0); wk1 = ssd1306_rotate_byte(wk1); }
                wk0 = wk0 >> 1;
                wk1 = wk1 & 0x01;
                wk1 = wk1 << 7;
                wk2 = wk0 | wk1;
                if (dev->_flip) wk2 = ssd1306_rotate_byte(wk2);
                dev->_page[page]._segs[seg] = wk2;
            }
        }
        int pages = dev->_pages-1;
        for (int seg=_start;seg<=_end;seg++) {
            wk0 = dev->_page[pages]._segs[seg];
            wk1 = save[seg];
            if (dev->_flip) { wk0 = ssd1306_rotate_byte(wk0); wk1 = ssd1306_rotate_byte(wk1); }
            wk0 = wk0 >> 1;
            wk1 = wk1 & 0x01;
            wk1 = wk1 << 7;
            wk2 = wk0 | wk1;
            if (dev->_flip) wk2 = ssd1306_rotate_byte(wk2);
            dev->_page[pages]._segs[seg] = wk2;
        }
    } else if (scroll == SCROLL_DOWN) {
        int _start = start; 
        int _end = end; 
        if (_end >= dev->_width) _end = dev->_width - 1;
        uint8_t wk0, wk1, wk2;
        uint8_t save[128];
        int pages = dev->_pages-1;
        for (int seg=0;seg<128;seg++) save[seg] = dev->_page[pages]._segs[seg];
        for (int page=pages;page>0;page--) {
            for (int seg=_start;seg<=_end;seg++) {
                wk0 = dev->_page[page]._segs[seg];
                wk1 = dev->_page[page-1]._segs[seg];
                if (dev->_flip) { wk0 = ssd1306_rotate_byte(wk0); wk1 = ssd1306_rotate_byte(wk1); }
                wk0 = wk0 << 1;
                wk1 = wk1 & 0x80;
                wk1 = wk1 >> 7;
                wk2 = wk0 | wk1;
                if (dev->_flip) wk2 = ssd1306_rotate_byte(wk2);
                dev->_page[page]._segs[seg] = wk2;
            }
        }
        for (int seg=_start;seg<=_end;seg++) {
            wk0 = dev->_page[0]._segs[seg];
            wk1 = save[seg];
            if (dev->_flip) { wk0 = ssd1306_rotate_byte(wk0); wk1 = ssd1306_rotate_byte(wk1); }
            wk0 = wk0 << 1;
            wk1 = wk1 & 0x80;
            wk1 = wk1 >> 7;
            wk2 = wk0 | wk1;
            if (dev->_flip) wk2 = ssd1306_rotate_byte(wk2);
            dev->_page[0]._segs[seg] = wk2;
        }
    } else if (scroll == PAGE_SCROLL_DOWN) {
        uint8_t save[128];
        for (int seg=0;seg<128;seg++) save[seg] = dev->_page[dev->_pages-1]._segs[seg];
        for (int page=dev->_pages-1;page>0;page--) {
            for (int seg=0;seg<128;seg++) dev->_page[page]._segs[seg] = dev->_page[page-1]._segs[seg];
        }
        for (int seg=0;seg<128;seg++) dev->_page[0]._segs[seg] = save[seg];
    } else if (scroll == PAGE_SCROLL_UP) {
        uint8_t save[128];
        for (int seg=0;seg<128;seg++) save[seg] = dev->_page[0]._segs[seg];
        for (int page=0;page<dev->_pages-1;page++) {
            for (int seg=0;seg<128;seg++) dev->_page[page]._segs[seg] = dev->_page[page+1]._segs[seg];
        }
        for (int seg=0;seg<128;seg++) dev->_page[dev->_pages-1]._segs[seg] = save[seg];
    }

    if (delay >= 0) {
        for (int page=0;page<dev->_pages;page++) {
            if (dev->_address == SPI_ADDRESS) {
                spi_display_image(dev, page, 0, dev->_page[page]._segs, 128);
            } else {
                i2c_display_image(dev, page, 0, dev->_page[page]._segs, 128);
            }
            if (delay) vTaskDelay(delay);
        }
    }
}

void _ssd1306_bitmaps(SSD1306_t * dev, int xpos, int ypos, const uint8_t * bitmap, int width, int height, bool invert)
{
    if ( (width % 8) != 0) {
        ESP_LOGE(TAG, "width must be a multiple of 8");
        return;
    }
    int _width = width / 8;
    uint8_t wk0;
    uint8_t wk1;
    uint8_t wk2;
    uint8_t page = (ypos / 8);
    uint8_t _seg = xpos;
    uint8_t dstBits = (ypos % 8);
    int offset = 0;
    for(int _height=0;_height<height;_height++) {
        for (int index=0;index<_width;index++) {
            for (int srcBits=7; srcBits>=0; srcBits--) {
                wk0 = dev->_page[page]._segs[_seg];
                if (dev->_flip) wk0 = ssd1306_rotate_byte(wk0);

                wk1 = bitmap[index+offset];
                if (invert) wk1 = ~wk1;

                wk2 = ssd1306_copy_bit(wk1, srcBits, wk0, dstBits);
                if (dev->_flip) wk2 = ssd1306_rotate_byte(wk2);

                if (_seg >= 128) break;
                if (page >= dev->_pages) break;
                dev->_page[page]._segs[_seg] = wk2;
                _seg++;
            }
        }
        offset = offset + _width;
        dstBits++;
        _seg = xpos;
        if (dstBits == 8) {
            page++;
            dstBits=0;
        }
    }
}

void ssd1306_bitmaps(SSD1306_t * dev, int xpos, int ypos, const uint8_t * bitmap, int width, int height, bool invert)
{
    _ssd1306_bitmaps(dev, xpos, ypos, bitmap, width, height, invert);
    int start_page = ypos / 8;
    int end_page = (ypos + height - 1) / 8;
    int start_seg = xpos;
    int end_seg = xpos + width - 1;

    for (int page = start_page; page <= end_page; page++) {
        int seg_start = (page == start_page) ? start_seg : 0;
        int seg_end = (page == end_page) ? end_seg : 127;
        int seg_width = seg_end - seg_start + 1;
        ssd1306_display_image(dev, page, seg_start, &dev->_page[page]._segs[seg_start], seg_width);
    }
}

void _ssd1306_pixel(SSD1306_t * dev, int xpos, int ypos, bool invert)
{
    uint8_t _page = (ypos / 8);
    uint8_t _bits = (ypos % 8);
    uint8_t _seg = xpos;
    uint8_t wk0 = dev->_page[_page]._segs[_seg];
    uint8_t wk1 = 1 << _bits;
    if (invert) {
        wk0 = wk0 & ~wk1;
    } else {
        wk0 = wk0 | wk1;
    }
    if (dev->_flip) wk0 = ssd1306_rotate_byte(wk0);
    dev->_page[_page]._segs[_seg] = wk0;
}

void _ssd1306_line(SSD1306_t * dev, int x1, int y1, int x2, int y2,  bool invert)
{
    int i;
    int dx,dy;
    int sx,sy;
    int E;
    dx = ( x2 > x1 ) ? x2 - x1 : x1 - x2;
    dy = ( y2 > y1 ) ? y2 - y1 : y1 - y2;
    sx = ( x2 > x1 ) ? 1 : -1;
    sy = ( y2 > y1 ) ? 1 : -1;
    if ( dx > dy ) {
        E = -dx;
        for ( i = 0 ; i <= dx ; i++ ) {
            _ssd1306_pixel(dev, x1, y1, invert);
            x1 += sx;
            E += 2 * dy;
            if ( E >= 0 ) {
            y1 += sy;
            E -= 2 * dx;
        }
    }
    } else {
        E = -dy;
        for ( i = 0 ; i <= dy ; i++ ) {
            _ssd1306_pixel(dev, x1, y1, invert);
            y1 += sy;
            E += 2 * dx;
            if ( E >= 0 ) {
                x1 += sx;
                E -= 2 * dy;
            }
        }
    }
}

void _ssd1306_circle(SSD1306_t * dev, int x0, int y0, int r, unsigned int opt, bool invert)
{
    int x;
    int y;
    int err;
    int old_err;
    x=0;
    y=-r;
    err=2-2*r;
    do{
        if ((opt & OLED_DRAW_UPPER_LEFT) == OLED_DRAW_UPPER_LEFT)
            _ssd1306_pixel(dev, x0-x, y0+y, invert); 
        if ((opt & OLED_DRAW_UPPER_RIGHT) == OLED_DRAW_UPPER_RIGHT)
            _ssd1306_pixel(dev, x0-y, y0-x, invert); 
        if ((opt & OLED_DRAW_LOWER_RIGHT) == OLED_DRAW_LOWER_RIGHT)
            _ssd1306_pixel(dev, x0+x, y0-y, invert); 
        if ((opt & OLED_DRAW_LOWER_LEFT) == OLED_DRAW_LOWER_LEFT)
            _ssd1306_pixel(dev, x0+y, y0+x, invert); 
        if ((old_err=err)<=x) err+=++x*2+1;
        if (old_err>y || err>x) err+=++y*2+1;    
    } while(y<0);
}

void _ssd1306_disc(SSD1306_t * dev, int x0, int y0, int r, unsigned int opt, bool invert)
{
    int x;
    int y;
    int err;
    int old_err;
    int ChangeX;
    x=0;
    y=-r;
    err=2-2*r;
    ChangeX=1;
    do{
        if(ChangeX) {
            if ((opt & OLED_DRAW_LOWER_LEFT) == OLED_DRAW_LOWER_LEFT)
                _ssd1306_line(dev, x0-x, y0-y, x0-x, y0, invert);
            if ((opt & OLED_DRAW_UPPER_LEFT) == OLED_DRAW_UPPER_LEFT)
                _ssd1306_line(dev, x0-x, y0, x0-x, y0+y, invert);
            if ((opt & OLED_DRAW_LOWER_RIGHT) == OLED_DRAW_LOWER_RIGHT)
                _ssd1306_line(dev, x0+x, y0-y, x0+x, y0, invert);
            if ((opt & OLED_DRAW_UPPER_RIGHT) == OLED_DRAW_UPPER_RIGHT)
                _ssd1306_line(dev, x0+x, y0, x0+x, y0+y, invert);
        }
        ChangeX=(old_err=err)<=x;
        if (ChangeX) err+=++x*2+1;
        if (old_err>y || err>x) err+=++y*2+1;
    } while(y<=0);
}

void _ssd1306_cursor(SSD1306_t * dev, int x0, int y0, int r, bool invert)
{
    _ssd1306_line(dev, x0-r, y0, x0+r, y0, invert);
    _ssd1306_line(dev, x0, y0-r, x0, y0+r, invert);
}

void ssd1306_invert(uint8_t *buf, size_t blen)
{
    uint8_t wk;
    for(int i=0; i<blen; i++){
        wk = buf[i];
        buf[i] = ~wk;
    }
}

void ssd1306_flip(uint8_t *buf, size_t blen)
{
    for(int i=0; i<blen; i++){
        buf[i] = ssd1306_rotate_byte(buf[i]);
    }
}

uint8_t ssd1306_copy_bit(uint8_t src, int srcBits, uint8_t dst, int dstBits)
{
    uint8_t smask = 0x01 << srcBits;
    uint8_t dmask = 0x01 << dstBits;
    uint8_t _src = src & smask;
    uint8_t _dst;
    if (_src != 0) {
        _dst = dst | dmask;
    } else {
        _dst = dst & ~(dmask);
    }
    return _dst;
}

uint8_t ssd1306_rotate_byte(uint8_t ch1) {
    uint8_t ch2 = 0;
    for (int j=0;j<8;j++) {
        ch2 = (ch2 << 1) + (ch1 & 0x01);
        ch1 = ch1 >> 1;
    }
    return ch2;
}

void ssd1306_fadeout(SSD1306_t * dev)
{
    void (*func)(SSD1306_t * dev, int page, int seg, const uint8_t * images, int width);
    if (dev->_address == SPI_ADDRESS) {
        func = spi_display_image;
    } else {
        func = i2c_display_image;
    }

    uint8_t image[1];
    for(int page=0; page<dev->_pages; page++) {
        image[0] = 0xFF;
        for(int line=0; line<8; line++) {
            if (dev->_flip) {
                image[0] = image[0] >> 1;
            } else {
                image[0] = image[0] << 1;
            }
            for(int seg=0; seg<128; seg++) {
                (*func)(dev, page, seg, image, 1);
                dev->_page[page]._segs[seg] = image[0];
            }
        }
    }
}

void ssd1306_rotate_image(uint8_t *image, bool flip) {
    uint8_t _image[8];
    uint8_t _smask = 0x01;
    for (int i=0;i<8;i++) {
        uint8_t _dmask = 0x80;
        _image[i] = 0;
        for (int j=0;j<8;j++) {
            uint8_t _wk = image[j] & _smask;
            if (_wk != 0) {
                _image[i] = _image[i] + _dmask;
            }
            _dmask = _dmask >> 1;
        }
        _smask = _smask << 1;
    }
    for (int i=0;i<8;i++) {
        image[i] = _image[i];
    }
    if (flip) ssd1306_flip(image, 8);
}

void ssd1306_display_rotate_text(SSD1306_t * dev, int seg, const char * text, int text_len, bool invert) {
    int _text_len = text_len;
    if (_text_len > 8) _text_len = 8;
    uint8_t image[8];
    int _page = dev->_pages-1;
    for (uint8_t i = 0; i < _text_len; i++) {
        memcpy(image, font8x8_basic_tr[(uint8_t)text[i]], 8);
        ssd1306_rotate_image(image, dev->_flip);
        if (invert) ssd1306_invert(image, 8);
        ssd1306_display_image(dev, _page, seg, image, 8);
        _page--;
        if (_page < 0) return;
    }
}

void ssd1306_dump(SSD1306_t dev)
{
    printf("_address=%x\n",dev._address);
    printf("_width=%x\n",dev._width);
    printf("_height=%x\n",dev._height);
    printf("_pages=%x\n",dev._pages);
}

void ssd1306_dump_page(SSD1306_t * dev, int page, int seg)
{
    ESP_LOGI(TAG, "dev->_page[%d]._segs[%d]=%02x", page, seg, dev->_page[page]._segs[seg]);
}

/* ================================================================ */
/* NEW I2C DRIVER IMPLEMENTATION (Added to fix CONFLICT error)      */
/* ================================================================ */

#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0))

// 1. Implementation of i2c_display_image using i2c_master_transmit
void i2c_display_image(SSD1306_t * dev, int page, int seg, const uint8_t * images, int width) {
    if (dev->_i2c_dev_handle == NULL) return;

    // Set Page and Column (Window)
    uint8_t cmd[] = {
        0x00,               // CMD Stream
        0xB0 | page,        // Page Start
        0x00 | (seg & 0xF), // Lower Col
        0x10 | ((seg >> 4) & 0xF) // Upper Col
    };
    i2c_master_transmit(dev->_i2c_dev_handle, cmd, sizeof(cmd), -1);

    // Send Data using VLA (Variable Length Array)
    // Note: Max width is 128, so stack usage is minimal (~129 bytes)
    uint8_t data_buf[width + 1];
    data_buf[0] = 0x40; // DATA Stream
    memcpy(&data_buf[1], images, width);
    i2c_master_transmit(dev->_i2c_dev_handle, data_buf, width + 1, -1);
}

// 2. Implementation of i2c_contrast
void i2c_contrast(SSD1306_t * dev, int contrast) {
    if (dev->_i2c_dev_handle == NULL) return;
    uint8_t cmd[] = { 0x00, 0x81, (uint8_t)contrast };
    i2c_master_transmit(dev->_i2c_dev_handle, cmd, sizeof(cmd), -1);
}

// 3. Hardware Scroll stub/implementation
void i2c_hardware_scroll(SSD1306_t * dev, ssd1306_scroll_type_t scroll) {
    if (dev->_i2c_dev_handle == NULL) return;
    // Minimal implementation to stop scrolling (safe default)
    uint8_t cmd[] = { 0x00, 0x2E };
    i2c_master_transmit(dev->_i2c_dev_handle, cmd, sizeof(cmd), -1);
}

#endif