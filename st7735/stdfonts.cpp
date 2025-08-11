#include "font.hpp"
#include "font5x7.hpp"
#include "font7x11.hpp"

extern const Font Font_5x7(Font::kVertScan, 5, 7, 32, 127, Font5x7_data, nullptr, nullptr, 1, 1);
extern const Font Font_7x11(Font::kVertScan, 7, 11, 32, 223, Font7x11_data, nullptr, nullptr, 1, 1);
