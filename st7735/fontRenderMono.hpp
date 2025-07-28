#ifndef FONT_RENDER_MONO_HPP
#define FONT_RENDER_MONO_HPP
template <class GfxDisplay>
class FontRenderMono: public GfxDisplay
{
public:
    enum DrawFlags
    {
        kFlagNoAutoNewline = 1,
        kFlagAllowPartial = 2
    };
    using GfxDisplay::GfxDisplay;
protected:
};
#endif
