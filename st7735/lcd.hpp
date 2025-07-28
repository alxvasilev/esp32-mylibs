#ifndef LCD_DISPLAY_HPP_INCLUDED
#define LCD_DISPLAY_HPP_INCLUDED
#include <gfx.hpp>
#include <textRender.hpp>
#include <fontRenderColor.hpp>
#include <fontRenderMono.hpp>
#include <type_traits>
/*
 * A complete display class has several layers.
 * The bottom one is the Driver, which understands the display protocol and may provide
 * frame buffering and DMA transactions. The driver class itself may take additional template params,
 * i.e. hardware port, pin config, etc. It may also be a virtual framebuffer, which doesn't have
 * a physically connected display but only operates on a RAM framebuffer.
 * The Driver exposes the basic set of drawing function that are the base of all other: setting a pixel,
 * drawing a horizontal and a vertical line, filling a rectangle.
 * The second layer - the Gfx class it provides more advanced drawing functions and cursor positioning.
 * The third layer - the FontRenderXXX class provides font character rendering. For performance, it is
 * dependent on whether pixels are represented as bits is video memory, or a words, and their layout in
 * memory. Hence, there are several FontRender classes depending on the underlying Display. The driver
 * reports the type required via the Driver::isMono member.
 * The fourth layer - the TextRender class provides text rendering, handling things like text centering,
 * newlines, text size estimation, etc.
 */
template <class Base>
struct FontRenderSelector {
    using type = std::conditional_t<Base::isMono, FontRenderMono<Base>, FontRenderColor<Base>>;
};
/* Defining this as a class rather than a template alias abstracts the actual base, and facilitates
 * forward declarations, which can't otherwise be done on a template typedef:
 * [user class definition]:
 * class MyDisplay: public Lcd<MyDriver> {public: using Lcd<MyDriver>::Lcd; };
 * (Lcd<MyDriver>::Lcd won't be accepted if Lcd is a template typedef, and not an actual class)
 * [user class forward usage]:
 * template<class Driver> class MyDisplay;
 */
template <class Driver>
class Lcd: public TextRender<typename FontRenderSelector<Gfx<Driver>>::type> {
    using TextRender<typename FontRenderSelector<Gfx<Driver>>::type>::TextRender;
};
#endif
