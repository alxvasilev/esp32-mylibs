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

using namespace std;

template <typename T>
void rpt(std::ostream& os, int rpt, T what)
{
    for (int i = 0; i < rpt; i++) {
        os << what;
    }
}
static inline void checkError(FT_Error err, const char* opName) {
    if (err) {
        throw runtime_error(string(opName) + ": Error " + FT_Error_String(err));
    }
}

struct FreetypeLib {
    FT_Library mLib;
    FreetypeLib() {
        checkError(FT_Init_FreeType(&mLib), "init Freetype");
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
    };
    FT_Library mLib;
    FT_Face mFace = nullptr;
    int mHeight = 0;
    int mWidth = 0;
    // bounding box coords relative to pen origin, which is (x=start-of-char=0, y=baseline=0)
    // y increases top to bottom, x increases left to right
    int mBmpLeftMin;
    int mBmpTopMax;
    int mBmpWidthMax;
    int mBmpHeightMax;
    uint32_t mCharCode = 0;
    static const map<string, pair<uint32_t, uint32_t>> sNamedRanges;
Font(FT_Library ft): mLib(ft) {}
void load(const char* fontPath, long fontSize)
{
    if (mFace) {
        FT_Done_Face(mFace);
        mFace = nullptr;
    }
    auto err = FT_New_Face(mLib, fontPath, 0, &mFace);
    if (err) {
        throw runtime_error(string("Failed to load font: ") + FT_Error_String(err));
    }
    FT_Set_Pixel_Sizes(mFace, 0, fontSize);
    mHeight = mFace->size->metrics.height >> 6;
    mWidth = mFace->size->metrics.max_advance >> 6;
    resetBoundingBox();
}
void resetBoundingBox()
{
    mBmpLeftMin = 0xffffff;
    mBmpTopMax = -0xffffff;
    mBmpWidthMax = -1;
    mBmpHeightMax = -1;
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
    checkError(FT_Load_Char(mFace, code, FT_LOAD_TARGET_MONO), "loadChar");
    mCharCode = code;
}
void currGlyphRender()
{
    checkError(FT_Render_Glyph(mFace->glyph, FT_RENDER_MODE_MONO), "Render glyph");
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    mBmpLeftMin = min<int>(mBmpLeftMin, glyph->bitmap_left);
    mBmpWidthMax = max<int>(mBmpWidthMax, glyph->bitmap_left + bmp.width - mBmpLeftMin);
    mBmpTopMax = max<int>(mBmpTopMax, glyph->bitmap_top);
    mBmpHeightMax = max<int>(mBmpHeightMax, bmp.rows + mBmpTopMax - glyph->bitmap_top);
}
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
    // format is: first1:last1;first2:last2;.....
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
            ranges.push_back(it->second);
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
    ofs << "/*\n"
        << "Vertical-scan bitmap data for " << dec << mBmpWidthMax << "x" << mBmpHeightMax << " font " << fontName << "\n"
        << "Defines char code ranges ";
    ofs << hex;
    for (int i = 0; i < outRanges.size(); i++) {
        auto& range = outRanges[i];
        int nDigits = range.last <= 0xff ? 2 : (range.last <= 0xfff ? 3 : 4);
        ofs << "0x" << setw(nDigits) << setfill('0') << range.first << "-0x" << setw(nDigits) << range.last;
        ofs << ((i < outRanges.size() - 1) ? ", ": "\n");
    }
    ofs << dec << setw(0);
    int glyphSize = mBmpWidthMax * ((mBmpHeightMax + 7) / 8);
    ofs << "Total " << dec << allCodes.size() << " chars * " << glyphSize << " = " << allCodes.size() * glyphSize << " bytes\n";
    ofs << "Generated with cppfont utility by Alexander Vassilev\n"
        << "*/\n"
        << "// fontdesc: " << dec << mBmpWidthMax << 'x' << mBmpHeightMax << "; vh-scan\n"
        << "#include <font.hpp>\n"
        << "static const unsigned char " << fontName << "_data[] ={\n";
    string text;
    for (auto code: allCodes) {
        loadCharMonoMustExist(code);
        currGlyphRender();
        text.clear();
        currGlyphToCpp(text);
        ofs << text;
    }
    ofs << "};\n"
        << "// (isVertical, width, height, count, charSpacing, lineSpacing, data, offsets)\n"
        << "__attribute__((section(\".rodata\")))\n"
        << "extern Font const " << fontName << "(true, " << mBmpWidthMax << ", " << mBmpHeightMax << ", "
        << outRanges[0].first << ", 1, 1, " << fontName << "_data, nullptr);\n";
    printf("Font saved to %s\n", outFileName.c_str());
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
    for(auto ch = wstr; *ch; ch++) {
        if (loadCharMono(*ch) == 0) {
            currGlyphRender();
            currGlyphPrint(cout);
        }
        else {
            printf("Char %x missing in font face\n", *ch);
        }
    }
    printf("Font size of complete font: %d x %d\n", mWidth, mHeight);
    printf("Font size of selected chars: %d x %d\n", mBmpWidthMax, mBmpHeightMax);

}
void currGlyphToCpp(string& dest)
{
    char hex[32];
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    int offsTop = mBmpTopMax - glyph->bitmap_top; // Number of empty rows above our bitmap. Can't be negative
    int yEnd = mBmpHeightMax - offsTop;
    int offsLeft = glyph->bitmap_left - mBmpLeftMin; // Number of empty rows to the left of our bitmap. Can't be negative
    int xEnd = mBmpWidthMax - offsLeft;
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
void currGlyphPrint(ostream& os)
{
    string topBottomLine = "+";
    topBottomLine.append(mBmpWidthMax, '-');
    topBottomLine.append("+\n");
    os << topBottomLine;
//    rpt(os, mSpaceTop, emptyLine);
    auto glyph = mFace->glyph;
    auto bmp = glyph->bitmap;
    int offsTop = mBmpTopMax - glyph->bitmap_top; // Number of empty rows above our bitmap. Can't be negative
    int yEnd = mBmpHeightMax - offsTop;
    int offsLeft = glyph->bitmap_left - mBmpLeftMin; // Number of empty rows to the left of our bitmap. Can't be negative
    int xEnd = mBmpWidthMax - offsLeft;
    for (int row = -offsTop; row < yEnd; ++row) {
        os << '|';
        if (row < 0 || row >= bmp.rows) {
            rpt(os, mBmpWidthMax, ' ');
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
        os << "|    ";
        if (row == 0) {
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
            printf("char   : '%s' (0x%04x)", utf8buf, mCharCode);
        }
        else if (row == 1) {
            printf("spcTop : %2d, spcBottm: %2d", offsTop, yEnd - bmp.rows);
        }
        else if (row == 2) {
            printf("spcLeft: %2d, spcRight: %2d", offsLeft, xEnd - bmp.width);
        }
        else if (row == 3) {
            printf("width  : %2d", bmp.width);
        }
        else if (row == 4) {
            printf("height : %2d", bmp.rows);
        }
        os << endl;
    }
    os << topBottomLine;
}
};
const map<string, pair<uint32_t, uint32_t>> Font::sNamedRanges = {
    { "ascii", {0x20, 0x7e}},
    { "latin", {0xa1, 0x17f}},
    { "cyr", { 0x400, 0x45e}}
};

int main(int argc, char* argv[])
{
    if (argc < 5) {
        cout << "Font generation utility by Alexander Vassilev\n"
             << "Usage:\n"
             << "Show selected chars of specified font and vertical font size:\n"
             << "  " << argv[0] << " show  <font-file> <font-vsize> <chars>\n"
             << "Create a C++ font:\n"
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
