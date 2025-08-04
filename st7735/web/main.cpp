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
//Lcd<FrameBufferWasm<FrameBufferMono<128, 64>>> lcd;
Lcd<FrameBufferWasm<FrameBufferColor<uint16_t>>> lcd(320, 170);
extern Font font_Camingo32;

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
//    lcd.fillRect(0, 0, lcd.width(), 10);
    lcd.setFont(font_Camingo32, 2);
    lcd.cursorY = 0;
//    lcd.puts("C:20 L:25  3.75v 100%");
    lcd.puts("Test!");
    lcd.update();
    return 2;
    lcd.hLine(0, 127, 8);
    lcd.newLine();
    lcd.cursorY += 1;
    lcd.puts("2\n3\n4\n5\n6\n7");
    lcd.vLine(lcd.cursorY, lcd.height() - lcd.fontHeight(), 126);
    lcd.gotoXY(1, lcd.height() - lcd.lineHeight() + 1);
    lcd.puts("Cruise ");
    lcd.cursorX -= 2;
    lcd.puts("SpLim ");
    lcd.cursorX -= 2;
    lcd.puts("View ");
    lcd.cursorX -= 2;
    lcd.puts("Menu");
    //lcd.invertRect(0, lcd.height() - lcd.charHeight() + 1, 128, lcd.charHeight() -1);
    //lcd.puts("\nTest message");
    lcd.update();
}
