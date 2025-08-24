#define FT_ERROR_DEFINITIONS
#include <freetype/freetype.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <limits>
#include <ctype.h>
#include <iomanip>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

using namespace std;

template <typename Dest, typename T>
void rpt(Dest& dest, int rpt, T what)
{
    for (int i = 0; i < rpt; i++) {
        dest << what;
    }
}
template <typename T>
string& rpt(string& str, int rpt, T what)
{
    for (int i = 0; i < rpt; i++) {
        str += what;
    }
    return str;
}
static inline void checkError(FT_Error err, const char* opName)
{
    if (err) {
        auto str = FT_Error_String(err);
        string errName = str ? string(str) + "(" + to_string(err) + ")" : to_string(err);
        throw runtime_error(string("FreeType error ") + errName + " while " + opName);
    }
}
struct TextBox: public vector<string>
{
    iterator currLine;
    int width = 0;
    TextBox(int w, int h): vector(h) {
        for (auto& line: *this) {
            line.reserve(w + 16);
        }
        currLine = begin();
    }
    void clear() {
        for (auto& line: *this) {
            line.clear();
        }
        currLine = begin();
    }
    TextBox& newLine() {
        if (++currLine == end()) {
            throw runtime_error("nextLine: Attempted to go past last line");
        }
        return *this;
    }
    template<typename T>
    TextBox& append(const T& str) {
        currLine->append(str);
        return *this;
    }
    template<typename T>
    TextBox& operator<<(const T& str) {
        currLine->append(str);
        return *this;
    }
    TextBox& operator<<(char ch) {
        if (ch == '\n') {
            newLine();
        }
        else {
            (*currLine) += ch;
        }
        return *this;
    }
};

struct FreetypeLib {
    FT_Library mLib;
    FreetypeLib() {
        checkError(FT_Init_FreeType(&mLib), "initializing FreeType");
    }
    ~FreetypeLib() {
        if (mLib) {
            FT_Done_FreeType(mLib);
        }
    }
    operator FT_Library() { return mLib; }
};
struct Font {
    struct OutputRange {
        uint32_t first;
        uint32_t last;
        uint32_t startIdx;
        int numHexDigits() const {
            return last <= 0xff ? 2 : (last <= 0xfff ? 3 : 4);
        }
    };
    FT_Library mLib;
    FT_Face mFace = nullptr;
    int mHeight = 0;
    int mWidth = 0;
    string mFontFileName;
    // bounding box coords relative to pen origin, which is (x=start-of-char=0, y=baseline=0)
    // y increases top to bottom, x increases left to right
    int mBmpLeftMin; // min offset from cursor to left of glyph: min(bitmap_left)
    int mBmpRightMax; // max span of glyphs to the right of cursor: max(bitmap_left + width)
    int mBmpTopMax; // max distance from baseline to top of glyph: max(bitmap_top)
    int mBmpBottomMax; // max distance from baseline to bottom of glyph: max(bmp.rows - bitmap_top)
    uint32_t mCharCode = 0;
    static const map<string, vector<pair<uint32_t, uint32_t>>> sNamedRanges;
Font(FT_Library ft): mLib(ft) {}
void load(const char* fontPath, long fontSize)
{
    if (mFace) {
        FT_Done_Face(mFace);
        mFace = nullptr;
    }
    checkError(FT_New_Face(mLib, fontPath, 0, &mFace), "loading font");
    FT_Set_Pixel_Sizes(mFace, 0, fontSize);
    mHeight = mFace->size->metrics.height >> 6;
    mWidth = mFace->size->metrics.max_advance >> 6;
    resetBoundingBox();
    auto slash = strrchr(fontPath, '/');
    mFontFileName.assign(slash ? slash + 1 : fontPath);
}
void resetBoundingBox()
{
    mBmpLeftMin = 0xffffff;
    mBmpRightMax = -1;
    mBmpTopMax = -1;
    mBmpBottomMax = -1;
}
FT_Error loadCharMono(uint32_t code)
{
    auto err = FT_Load_Char(mFace, code, FT_LOAD_TARGET_MONO);
    if (err) {
        return err;
    }
    mCharCode = code;
    return err;
}
void loadCharMonoMustExist(uint32_t code)
{
    checkError(FT_Load_Char(mFace, code, FT_LOAD_TARGET_MONO), "loading char");
    mCharCode = code;
}
void currGlyphRender()
{
    checkError(FT_Render_Glyph(mFace->glyph, FT_RENDER_MODE_MONO), "rendering glyph");
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    mBmpLeftMin = min<int>(mBmpLeftMin, glyph->bitmap_left);
    mBmpRightMax = max<int>(mBmpRightMax, glyph->bitmap_left + bmp.width);
    mBmpTopMax = max<int>(mBmpTopMax, glyph->bitmap_top);
    mBmpBottomMax = max<int>(mBmpBottomMax, bmp.rows - glyph->bitmap_top);
}
int fontWidth() { return mBmpRightMax - mBmpLeftMin; }
int fontHeight() { return mBmpTopMax + mBmpBottomMax; }

template <typename T=unsigned long, T(*Func)(const char*, char**, int) = strtoul>
static T parseInt(const char* str, const char* opName)
{
    char* endptr = nullptr;
    T n = strtol(str, &endptr, 0);
    if (endptr == str) {
        throw runtime_error(string(opName) + ": no numbers in '" + string(str) + '\'');
    }
    while(isspace(*endptr)) {
        endptr++;
    }
    if (*endptr) {
        throw runtime_error(string(opName) + ": non-numeric char(s) after number '" + string(str) + '\'');
    }
    if ((n == std::numeric_limits<T>::min() || n == numeric_limits<T>::max()) && errno == ERANGE) {
        throw runtime_error(string(opName) + ": value " + string(str) + " out of range");
    }
    return n;
}
void parseRanges(char* str, vector<pair<uint32_t, uint32_t>>& ranges)
{
    // format is: first1-last1;first2-last2;.....
    ranges.clear();
    char* state = nullptr;
    for (auto token = strtok_r(str, ",", &state); token; token = strtok_r(nullptr, ",", &state)) {
        while (isspace(*token)) {
            token++;
        }
        if (!token) {
            continue;
        }
        char* delim = strchr(token, '-');
        if (!delim) {
            auto it = sNamedRanges.find(token);
            if (it == sNamedRanges.end()) {
                throw runtime_error(string("Unknown range name ") + token);
            }
            for (auto& range: it->second) {
                ranges.push_back(range);
            }
        }
        else {
            *delim = 0;
            auto start = parseInt(token, "Parsing range start");
            auto end = parseInt(delim + 1, "Parsing range end");
            ranges.push_back(make_pair(start, end));
        }
    }
}
void groupIntoRanges(vector<uint32_t>& codes, vector<OutputRange>& ranges)
{
    ranges.clear();
    if (codes.empty()) {
        return;
    }
    sort(codes.begin(), codes.end());
    OutputRange currRange = {.first = codes[0], .last = codes[0], .startIdx = 0};
    for (int i = 1; i < codes.size(); i++) {
        auto code = codes[i];
        auto delta = code - currRange.last;
        if (!delta) {
            continue; // duplicate
        }
        else if (delta == 1) {
            currRange.last = code;
            continue;
        }
        // start new range
        ranges.push_back(currRange);
        currRange.first = currRange.last = code;
        currRange.startIdx = i;
    }
    ranges.push_back(currRange);
}
uint32_t cmdExportToCpp(int argc, char** argv)
{
    vector<pair<uint32_t, uint32_t>> inRanges;
    parseRanges(argv[0], inRanges);
    int missing = 0;
    vector<uint32_t> allCodes;
    for (auto& range: inRanges) {
        for (uint32_t code = range.first; code <= range.second; code++) {
            auto idx = FT_Get_Char_Index(mFace, code);
            if (!idx) {
                missing++;
                printf("Warning: missing char for code 0x%x(%u)\n", code, code);
                continue;
            }
            allCodes.push_back(code);
        }
    }
    inRanges.clear(); // free some memory, we don't need this anymore
    vector<OutputRange> outRanges;
    groupIntoRanges(allCodes, outRanges);
    for (auto code: allCodes) {
        loadCharMonoMustExist(code);
        currGlyphRender();
    }
    auto fontName = argv[1];
    string outFileName = "./" + string(fontName) + ".cpp";
    ofstream ofs(outFileName);
    if (!ofs.is_open()) {
        fprintf(stderr, "Can't open %s for writing\n", outFileName.c_str());
        return 2;
    }
    int fntWidth = fontWidth();
    int fntHeight = fontHeight();
    ofs << "/*\n"
        << "Vertical-scan bitmap data for " << dec << fntWidth << "x" << fntHeight << " font '" << mFontFileName << "'\n"
        << "Defines char code ranges ";
    ofs << hex;
    for (int i = 0; i < outRanges.size(); i++) {
        auto& range = outRanges[i];
        int nDigits = range.numHexDigits();
        ofs << "0x" << setw(nDigits) << setfill('0') << range.first << "-0x" << setw(nDigits) << range.last;
        ofs << ((i < outRanges.size() - 1) ? ", ": "\n");
    }
    ofs << dec << setw(0);
    int glyphSize = fntWidth * ((fntHeight + 7) / 8);
    ofs << "Total " << allCodes.size() << " chars * " << glyphSize << " = " << allCodes.size() * glyphSize << " bytes\n";
    ofs << "Generated with cppfont utility by Alexander Vassilev\n"
        << "*/\n"
        << "// fontdesc: " << fntWidth << 'x' << fntHeight << "; vh-scan\n"
        << "#include <font.hpp>\n"
        << "static const unsigned char " << fontName << "_data[] = {\n";
    string text;
    for (auto code: allCodes) {
        loadCharMonoMustExist(code);
        currGlyphRender();
        text.clear();
        currGlyphToCpp(text);
        ofs << text;
    }
    ofs << "};\n";
    if (outRanges.size() > 1) {
        ofs << dec;
        ofs << "_FONT_FORCE_ROM static const Font::RangeList " << fontName << "_extranges(" << outRanges.size() - 1 <<", (const Font::Range[]){";
        ofs << hex;
        for (size_t i = 1; i < outRanges.size(); i++) {
            auto& range = outRanges[i];
            ofs << setw(range.numHexDigits());
            ofs << "{0x" << range.first << ",0x" << range.last << ",0x" << range.startIdx << "},";
        }
        ofs << "});\n";
    }
    ofs << dec;
    ofs << "// (flags, width, height, firstCode, lastCode, data, ranges, offsets, charSpacing, lineSpacing)\n"
        << "_FONT_FORCE_ROM extern const Font " << fontName << "(Font::kVertScan, " << fntWidth << ", " << fntHeight << ", "
        << outRanges[0].first << ", " << outRanges[0].last << ", " << fontName << "_data, &" << fontName
        << "_extranges, nullptr, 1, 1);\n";
    printf("Generated font of %zu chars, with size %d x %d\nSaved to %s\n", allCodes.size(), fntWidth, fntHeight, outFileName.c_str());
    return missing;
}
void cmdShowChars(const char* chars)
{
    auto len = strlen(chars) + 1;
    wchar_t* wstr = (wchar_t*)alloca(len * sizeof(wchar_t));
    std::setlocale(LC_ALL, "");
    if (mbstowcs(wstr, chars, len) == size_t(-1)) {
        printf("Invalid utf-8 input string for chars\n");
        return;
    }
    for(auto ch = wstr; *ch; ch++) {
        loadCharMono(*ch);
        currGlyphRender();
    }
    vector<TextBox> boxes;
    int boxWidth = fontWidth() + 1;
    int boxesPerLine = (consoleGetWidth() - 1) / boxWidth; // leave one column for right border of last box
    boxes.reserve(boxesPerLine);
    for (auto i = 0; i < boxesPerLine; i++) {
        boxes.emplace_back(boxWidth, fontHeight() + 10);
    }
    int charIdx = 0;
    for(auto ch = wstr; *ch;) {
        if (loadCharMono(*ch) == 0) {
            currGlyphRender();
            int boxCol = charIdx % boxesPerLine;
            auto& box = boxes[boxCol];
            box.clear();
            ch++;
            bool isLineLast = (boxCol == boxesPerLine - 1) || !*ch;
            currGlyphPrint(box, isLineLast);
            if (isLineLast) {
                renderTextBoxesInLine(boxes, boxCol+1);
            }
        }
        else {
            printf("Char %x missing in font face\n", *ch);
        }
        charIdx++;
    }
    printf("Font size of complete font: %d x %d\n", mWidth, mHeight);
    printf("Font size of selected chars: %d x %d\n", fontWidth(), fontHeight());
}
int consoleGetWidth()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns = 80;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hStdout, &csbi)) {
        columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return columns;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    } else {
        return 80; // fallback
    }
#endif
}
int utf8strlen(const char* str)
{
    int len = 0;
    while(*str) {
        uint8_t ch = *(uint8_t*)str;
        int n;
        if (ch < 0x80) {
            n = 1;
        }
        else if ((ch & 0b1110'0000) == 0b1100'0000) { // two-byte wchar
            n = 2;
        }
        else if ((ch & 0b1111'0000) == 0b1110'0000) { // three-byte wchar
            n = 3;
        }
        else if ((ch & 0b1111'1000) == 0b1111'0000) { // 4-byte wchar
            n = 4;
        }
        else {
            throw runtime_error("Invalid utf-8 sequence");
        }
        len++;
        str += n;
    }
    return len;
}
void renderTextBoxesInLine(const vector<TextBox>& boxes, int n)
{
    auto numLines = boxes[0].size();
    std::string line;
    line.reserve(boxes[0].width * boxes.size() + 1);
    for (int l = 0; l < numLines; l++) {
        line.clear();
        for (int b = 0; b < n; b++) {
            auto& box = boxes[b];
            const auto& boxLine = box[l];
            line.append(boxLine);
            int padding = box.width - (int)utf8strlen(boxLine.c_str());
            if (padding > 0) {
                line.append(padding, ' ');
            }
        }
        puts(line.c_str());
    }
}
void currGlyphToCpp(string& dest)
{
    char hex[32];
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    int offsTop = mBmpTopMax - glyph->bitmap_top; // Number of empty rows above our bitmap. Can't be negative
    int yEnd = mBmpTopMax + mBmpBottomMax - offsTop; // End row index, rows between bmp.rows and this are empty space at bottom
    int offsLeft = glyph->bitmap_left - mBmpLeftMin; // Number of empty rows to the left of our bitmap. Can't be negative
    int xEnd = mBmpRightMax - mBmpLeftMin - offsLeft; // End col index, cols between bmp.width and this are empty space at right
    for (int rowBase = -offsTop; rowBase < yEnd; rowBase += 8) {
        for (int col = -offsLeft; col < xEnd; col++) {
            uint8_t bits = 0;
            if (col >= 0 && col < bmp.width) {
                for (int bit = 0; bit < 8; bit++) {
                    int row = rowBase + bit;
                    if (row >= 0 && row < bmp.rows) {
                        uint8_t mask = 7 - (col % 8); // MSB first
                        if (bmp.buffer[row * bmp.pitch + col / 8] & (1 << mask)) {
                            bits |= 1 << bit;
                        }
                    }
                }
            }
            snprintf(hex, sizeof(hex), "0x%02x,", bits);
            dest += hex;
        }
    }
    dest += " //";
    dest += (mCharCode > 31 && mCharCode < 127) ? mCharCode : ' ';
    snprintf(hex, sizeof(hex), " (0x%02x)\n", mCharCode);
    dest += hex;
}
void currGlyphPrint(TextBox& os, bool rightBorder)
{
    os.clear();
    string topBottomLine = "┬";
    rpt(topBottomLine, fontWidth(), "─");
    if (rightBorder) {
        topBottomLine.append("┬");
        os.width = fontWidth() + 2;
    }
    else {
        os.width = fontWidth() + 1;
    }
    os << topBottomLine;
    os.newLine();
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    int offsTop = mBmpTopMax - glyph->bitmap_top; // Number of empty rows above our bitmap. Can't be negative
    int yEnd = mBmpTopMax + mBmpBottomMax - offsTop;
    int offsLeft = glyph->bitmap_left - mBmpLeftMin; // Number of empty rows to the left of our bitmap. Can't be negative
    int xEnd = mBmpRightMax - mBmpLeftMin - offsLeft;
    for (int row = -offsTop; row < yEnd; ++row) {
        os << "│";
        if (row < 0 || row >= bmp.rows) {
            rpt(os, fontWidth(), ' ');
        }
        else {
            for (int col = -offsLeft; col < xEnd; ++col) {
                if (col < 0 || col >= bmp.width) {
                    os << ' ';
                }
                else {
                    int byte = bmp.buffer[row * bmp.pitch + col / 8];
                    int bit = 7 - (col % 8); // MSB first
                    int pixel = byte & (1 << bit);
                    os << (pixel ? "\xE2\x96\x88" : " ");
                }
            }
        }
        if (rightBorder) {
            os << "│";
        }
        os.newLine();
    }
    topBottomLine = "┴";
    rpt(topBottomLine, fontWidth(), "─");
    if (rightBorder) {
        topBottomLine.append("┴");
    }
    os << topBottomLine;
    os.newLine();

    char utf8buf[MB_CUR_MAX + 1];  // max bytes needed for one multibyte char
    mbstate_t state = mbstate_t();
    size_t bytes = wcrtomb(utf8buf, mCharCode, &state);
    if (bytes != (size_t)-1) {
        utf8buf[bytes] = 0;
    }
    else {
        utf8buf[0] = '?';
        utf8buf[1] = 0;
    }
    char buf[128];
    snprintf(buf, sizeof(buf)-1, " ch:'%s'", utf8buf);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " U+%04X", mCharCode);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " spcT:%d", offsTop);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " spcB:%d", yEnd - bmp.rows);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " spcL:%d", offsLeft);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " spcR:%d", xEnd - bmp.width);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " wdth:%d", bmp.width);
    os.append(buf).newLine();
    snprintf(buf, sizeof(buf)-1, " hght:%d", bmp.rows);
    os.append(buf);
}
};
const map<string, vector<pair<uint32_t, uint32_t>>> Font::sNamedRanges = {
    { "ascii", {{0x20, 0x7e}}},
    { "latin", {{0xa1, 0x17f}}},
    { "cyr", {{0x400, 0x45e}}}
};

int main(int argc, char* argv[])
{
    if (argc < 5) {
        cout << "Font generation utility by Alexander Vassilev\n"
             << "Usage:\n"
             << "Show selected chars of specified font and vertical font size:\n"
             << "  " << argv[0] << " view <font-file> <font-vsize> <chars>\n"
             << "Create a C++ v-scan font:\n"
             << "  " << argv[0] << " tocpp <font-path> <font-size> <ranges> <cppfont-name>\n"
             << "  <ranges> is in the form (startCode1-endCode1|named1),(startCode2-endCode2|named2);....\n"
             << "  Named ranges:";
        for (auto& range: Font::sNamedRanges) {
            if (&range != &(*Font::sNamedRanges.begin())) {
                cout << ", ";
            }
            cout << range.first;
        }
        cout << "\n  Range examples: '0x20-0x7e,cyr', '0x20-126,latin,cyr\n";
        return 1;
    }
    auto command = argv[1];
    const char* fontPath = argv[2];
    auto fontSize = strtol(argv[3], nullptr, 10);
    if (!fontSize) {
        printf("Bad font size %s specified\n", argv[3]);
        return 1;
    }
    argc -= 4;
    argv += 4;
    try {
        FreetypeLib ft;
        Font font(ft);
        font.load(fontPath, fontSize);
        if (!strcmp(command, "tocpp")) {
            font.cmdExportToCpp(argc, argv);
        }
        else if (strcmp(command, "show")) {
            font.cmdShowChars(argv[0]);
        }
        else {
            printf("Unknown command %s\n", command);
            return 1;
        }
    }
    catch(exception& ex) {
        printf("%s\n", ex.what());
        return 2;
    }
    return 0;
}
