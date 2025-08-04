#include <stdlib.h>
#include <stdio.h>
#include <lcd.hpp>
#include <framebufMono.hpp>
#include <framebuf.hpp>
#include <stdfonts.hpp>

#ifdef EMSCRIPTEN
    #include <emscripten.h>
    __attribute__((import_module("env")))
    __attribute__((import_name("lcdInit")))
    extern void lcdInit(int bpp, int w, int h, const void* buf);

    __attribute__((import_module("env")))
    __attribute__((import_name("lcdUpdate")))
    extern void lcdUpdate(const void* buf);
    void delayMs(int milliseconds) {
        double start = emscripten_get_now();
        while (emscripten_get_now() - start < milliseconds) {
            // Busy wait
        }
    }
    __attribute__((import_module("env")))
    __attribute__((import_name("jsPrint")))
    extern void jsPrint(int x);
#else
    void lcdInit(int bpp, int w, int h, const void* buf) {}
    void lcdUpdate(const void* buf) {}
    void delayMs(int milliseconds) {}
#endif

template<class FrameBuffer>
class FrameBufferWasm: public FrameBuffer {
public:
using FrameBuffer::FrameBuffer;
void init() {
    lcdInit(std::is_same_v<typename FrameBuffer::Color, bool> ? 1 : sizeof(typename FrameBuffer::Color)*8,
        this->width(), this->height(), this->frameBuf());
    lcdUpdate((void*)this->frameBuf());
}
void update() {
    lcdUpdate(this->frameBuf());
}
};
Lcd<FrameBufferWasm<FrameBufferMono<128, 64>>> lcd;
//Lcd<FrameBufferWasm<FrameBufferColor<uint16_t>>> lcd(320, 170);
extern Font font_Camingo22;

extern "C" __attribute__((used)) void test()
{
    printf("test called\n");
    lcd.clear(0, lcd.cursorY, 128, lcd.fontHeight());
    if (++lcd.cursorY > 63) {
        lcd.cursorY = 0;
    }
    lcd.cursorX = 0;
    lcd.puts("C:20 L:25  3.75v 100%", lcd.kFlagNoAutoNewline);
//  lcd.invertRect(0, 0, 6, 7);
    lcd.update();
}
//Test t;
int main()
{
    printf("main called\n");
    lcd.init();
    lcd.setFgColor(0xffff);// 0x07E0);
    lcd.setFont(Font_5x7, 1);
    lcd.cursorY = 0;
    lcd.cursorX = 0;
    lcd.puts("C:20 L:25  3.75v 100%");
    //  lcd.puts("Test!");
    lcd.update();
#if 1
    lcd.hLine(0, lcd.width(), lcd.fontHeight() + 1);
#else
    for (int x = 0; x < lcd.width(); x += 2) {
        lcd.setPixel(x, lcd.fontHeight(), true);
    }
#endif
    lcd.newLine();
    lcd.cursorY += 1;
    printf("height: %d, fontHeight: %d\n", lcd.height(), lcd.fontHeight());
    lcd.vLine(lcd.fontHeight() + 2, lcd.height() - lcd.fontHeight() -1 , 127);
    lcd.vLine(lcd.fontHeight() + 20, lcd.height() - lcd.fontHeight() -1 , 126);
//  lcd.puts("2\n3\n4\n5\n6\n7");
    lcd.gotoXY(1, lcd.height() - lcd.lineHeight() + 1);
    lcd.puts("Cruise ");
    lcd.cursorX -= 2;
    lcd.puts("SpLim ");
    lcd.cursorX -= 2;
    lcd.puts("View ");
    lcd.cursorX -= 2;
    lcd.puts("Menu");
    lcd.setFont(font_Camingo22);
    lcd.cursorX = 0;
    lcd.cursorY = 8;
    lcd.puts("20.5");
    lcd.setFont(Font_5x7);
    lcd.cursorY += 9; //(font_Camingo22.height - Font_5x7.height);
    lcd.puts("kph");
#if 0
    lcd.invertRect(0, lcd.height() - lcd.lineHeight(), 128, lcd.lineHeight());
#else
    lcd.hLine(0, lcd.width(), lcd.height() - lcd.lineHeight());
//for (int x = 0; x < lcd.width(); x += 2) {
//    lcd.setPixel(x, lcd.height() - lcd.lineHeight(), true);
//}
#endif
    //lcd.puts("\nTest message");
    lcd.update();
}
