#include <stdlib.h>
#include <stdio.h>
#include <lcd.hpp>
#include <framebufMono.hpp>
#include <framebuf.hpp>
#include <stdfonts.hpp>
#include <coroutine>
#include <iostream>

#ifdef EMSCRIPTEN
    #include <emscripten.h>
    __attribute__((import_module("env")))
    __attribute__((import_name("lcdInit")))
    extern void jsLcdInit(int bpp, int w, int h, const void* buf);

    __attribute__((import_module("env")))
    __attribute__((import_name("lcdUpdate")))
    extern void jsLcdUpdate(const void* buf);
    __attribute__((import_module("env")))
    __attribute__((import_name("scheduleResume")))
    extern void jsScheduleResume(int ms, void* func);
#else
    void jsLcdInit(int bpp, int w, int h, const void* buf) {}
    void jsLcdUpdate(const void* buf) {}
    void jsScheduleResume(int ms, void* func) {}
#endif

template<class FrameBuffer>
class FrameBufferWasm: public FrameBuffer {
public:
using FrameBuffer::FrameBuffer;
void init() {
    jsLcdInit(std::is_same_v<typename FrameBuffer::Color, bool> ? 1 : sizeof(typename FrameBuffer::Color)*8,
        this->width(), this->height(), this->frameBuf());
    jsLcdUpdate((void*)this->frameBuf());
}
void update() {
    jsLcdUpdate(this->frameBuf());
}
};

// Custom awaiter that suspends and stores the continuation
struct JsResumeAwaiter {
    int msDelay;
    JsResumeAwaiter(int aMsDelay): msDelay(aMsDelay) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const {
        jsScheduleResume(msDelay, h.address());
    }
    void await_resume() const noexcept {}
};
struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};
extern "C" __attribute__((used)) void coroutineResume(void* ptr) {
    printf("Resuming coroutine %p\n", ptr);
    std::coroutine_handle<>::from_address(ptr)();
}
Task asyncWait(int ms)
{
    co_await JsResumeAwaiter(ms);
}

Lcd<FrameBufferWasm<FrameBufferMono<192, 96>>> lcd;
//Lcd<FrameBufferWasm<FrameBufferColor<uint16_t>>> lcd(320, 170);
extern Font font_Camingo22;
extern Font Font_7x10;

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
void appMain()
{
    lcd.init();
    lcd.setFgColor(0xffff);// 0x07E0);
    lcd.setFont(Font_7x10, 1);
//  lcd.setFont(font_Camingo22);
    lcd.cursorY = 0;
    lcd.cursorX = 0;
    lcd.puts("C:20 L:25 Хуй 3.75v 100%");
#if 0
    lcd.hLine(0, lcd.width(), lcd.fontHeight() + 1);
#else
    for (int x = 0; x < lcd.width(); x += 2) {
        lcd.setPixel(x, lcd.fontHeight() + 1, true);
    }
#endif
    lcd.newLine();
    lcd.cursorY += 1;
    lcd.puts("2\n3\n4\n5\n6\n7");
    lcd.cursorY = lcd.lineHeight() + 2;
    int t1 = 50;
    int t2 = 50;
#if 1
    lcd.vLine(lcd.fontHeight() + 2, lcd.height() - lcd.fontHeight() -1 , lcd.width() / 2);
    if (t1 == t2) {
        lcd.hLine(lcd.width() / 2 - 2, lcd.width() / 2 + 2, t1);
    }
    else {
        lcd.setPixel(lcd.width() / 2 - 1, t1, true);
        lcd.vLine(t1 - 1, t1 + 1, lcd.width() / 2 - 2);
        lcd.setPixel(lcd.width() / 2 + 1, t2, true);
        lcd.vLine(t2 - 1, t2 + 1, lcd.width() / 2 + 2);
    }
#else
    lcd.vLine(lcd.fontHeight() + 2, lcd.height() - lcd.fontHeight() -1 , lcd.width() / 2 - 2);
    lcd.vLine(lcd.fontHeight() + 2, lcd.height() - lcd.fontHeight() -1 , lcd.width() / 2 + 1);
    lcd.vLine(t1, lcd.height() - lcd.fontHeight() -1 , lcd.width() / 2 - 1);
    lcd.vLine(t2, lcd.height() - lcd.fontHeight() -1 , lcd.width() / 2);
#endif
    lcd.gotoXY(1, lcd.height() - lcd.lineHeight() + 1);
    lcd.puts("Cruise ");
    lcd.cursorX += 2;
    lcd.puts("Limits ");
    lcd.cursorX += 2;
    lcd.puts("View ");
    lcd.cursorX += 2;
    lcd.puts("Menu");
    lcd.cursorX = 1;
    lcd.cursorY = lcd.lineHeight() + 1;
    lcd.setFont(font_Camingo22);
    lcd.puts("20.5");
    lcd.setFont(Font_7x11);
    lcd.cursorY += (font_Camingo22.height - lcd.fontHeight() - 7);
    lcd.cursorX += 2;
    lcd.puts("km/h");
#if 0
    lcd.invertRect(0, lcd.height() - lcd.lineHeight(), 128, lcd.lineHeight());
#else
  #if 0
    lcd.hLine(0, lcd.width(), lcd.height() - lcd.lineHeight());
  #else
    for (int x = 0; x < lcd.width(); x += 2) {
        lcd.setPixel(x, lcd.height() - lcd.lineHeight(), true);
    }
  #endif
#endif
    //lcd.puts("\nTest message");
    lcd.update();
}
Task animate()
{
    for (int y = 0; y < lcd.height() - lcd.lineHeight(); y++) {
        if (y > 0) {
            lcd.clear(0, y-1, lcd.width(), lcd.lineHeight());
        }
        lcd.cursorY = y;
        lcd.cursorX = 0;
        lcd.puts("C:20 L:25  3.75v 100%");
        lcd.update();
        co_await JsResumeAwaiter(200);
    }
}
int main()
{
    appMain();
}