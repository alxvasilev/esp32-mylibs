#include "utils-parse.hpp"
#include <string>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sstream>

const char* _utils_hexDigits = "0123456789abcdef";

char* binToHex(const uint8_t* data, size_t len, char* str, char delim)
{
    auto end = data + len;
    if (delim) {
        while (data < end) {
            *(str++) = _utils_hexDigits[*data >> 4];
            *(str++) = _utils_hexDigits[*data & 0x0f];
            *(str++) = delim;
            data++;
        }
    } else {
        while (data < end) {
            *(str++) = _utils_hexDigits[*data >> 4];
            *(str++) = _utils_hexDigits[*data & 0x0f];
            data++;
        }
    }
    *str = 0;
    return str;
}
bool hexToBin(const char* hex, size_t hexLen, uint8_t* bin, size_t binLen) {
    if (hexLen != binLen * 2) {
        return false;
    }
    const char* psym = hex;
    for (int i = 0; i < binLen; i++) {
        uint8_t hi = hexDigitVal(*(psym++));
        uint8_t low = hexDigitVal(*(psym++));
        if (hi > 0x0f || low > 0x0f) {
            return false;
        }
        bin[i] = hi << 4 | low;
    }
    return true;
}
uint8_t hexDigitVal(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    } else if (digit >= 'a' && digit <= 'f') {
        return 10 + (digit - 'a');
    } else if (digit >= 'A' && digit <= 'F') {
        return 10 + (digit - 'A');
    } else {
        return 0xff;
    }
}
std::string binToAscii(char* buf, int len, int lineLen)
{
    std::ostringstream result;
    for (int i = 0; i < len; i++) {
        if (i % lineLen == 0) {
            result << '\n' << i << '\t' << ':';
        }
        char ch = buf[i];
        if (ch >= 32 && ch <= 126) {
            result << ch;
        } else {
            result << '.';
        }
    }
    return result.str();
}

bool unescapeUrlParam(char* str, size_t len)
{
    const char* rptr = str;
    char* wptr = str;
    const char* end = str + len;
    bool ok = true;
    for (; rptr < end; rptr++, wptr++) {
        char ch = *rptr;
        if (ch != '%') {
            if (rptr != wptr) {
                *wptr = ch;
            }
        } else {
            rptr++;
            auto highNibble = hexDigitVal(*(rptr++));
            auto lowNibble = hexDigitVal(*rptr);
            if (highNibble > 15 || lowNibble > 15) {
                *wptr = '?';
                ok = false;
            }
            *wptr = (highNibble << 4) | lowNibble;
        }
    }
    if (wptr < rptr) {
        *wptr = 0;
    }
    return ok;
}

long strToInt(const char* str, size_t len, long defVal, int base)
{
    char* end;
    long val = strtol(str, &end, base);
    return (end == str + len) ? val : defVal;
}

float strToFloat(const char* str, size_t len, float defVal)
{
    char* end;
    float val = strtod(str, &end);
    return (end == str + len) ? val : defVal;
}

void Substring::trimSpaces()
{
    auto end = str + len;
    assert(*end == 0);
    while (str < end && (*str == ' ' || *str == '\t')) {
        str++;
    }
    if (str == end) {
        str = nullptr;
        len = 0;
        return;
    }
    auto newEnd = end - 1;
    while (newEnd > str && (*newEnd == ' ' || *newEnd == '\t')) {
        newEnd--;
    }
    if (newEnd == str) {
        str = nullptr;
        len = 0;
        return;
    }
    newEnd++;
    if (newEnd != end) {
        *newEnd = 0;
    }
    len = newEnd - str;
}

bool KeyValParser::parse(char pairDelim, char keyValDelim, int flags)
{
    auto end = mBuf + mSize - 1;
    assert(*end == 0);
    char* pch = mBuf;
    for (;;) {
        auto start = pch;
        for (;; pch++) {
            if (pch >= end) {
                return true; // input ends with whitespace
            }
            if (!::isspace(*pch)) {
                break;
            }
        }
        if (flags & kKeysToLower) { //same loops, only difference is tolower() call
            for (; (pch < end) && (*pch != keyValDelim); pch++) {
                *pch = ::tolower(*pch);
            }
        }
        else {
            for (; (pch < end) && (*pch != keyValDelim); pch++);
        }
        if (pch >= end) { // unexpected end of key
            return false;
        }
        *pch = 0; // null-terminate the key
        mKeyVals.emplace_back();
        KeyVal& keyval = mKeyVals.back();
        auto& key = keyval.key;
        key.str = start;
        key.len = pch - start;

        start = ++pch;
        for (; (pch < end) && (*pch != pairDelim); pch++);
        auto& val = keyval.val;
        auto len = pch - start;
        if (flags & kUrlUnescape) {
            // we are not null terminated yet, but unescapeUrlParam doesn't care
            if (!unescapeUrlParam(start, len)) {
                return false;
            }
        }
        val.str = start;
        val.len = len;
        if (pch >= end) {
            assert(pch == end);
            assert(*pch == 0);
            return true;
        }
        *(pch++) = 0; // null-terminate the value
        if (flags & kTrimSpaces) {
            keyval.key.trimSpaces();
            keyval.val.trimSpaces();
        }
    }
}

Substring KeyValParser::strVal(const char* name)
{
    for (auto& keyval: mKeyVals) {
        if (strcmp(name, keyval.key.str) == 0) {
            return keyval.val;
        }
    }
    return Substring(nullptr, 0);
}
long KeyValParser::intVal(const char* name, long defVal)
{
    auto sval = strVal(name);
    auto str = sval.str;
    if (!str) {
        return defVal;
    }
    return sval.toInt(defVal);
}

float KeyValParser::floatVal(const char* name, float defVal)
{
    auto sval = strVal(name);
    auto str = sval.str;
    if (!str) {
        return defVal;
    }
    return sval.toFloat(defVal);
}
KeyValParser::~KeyValParser()
{
    if (mBuf && mOwn) {
        free(mBuf);
    }
}

Substring urlGetFile(const char* url)
{
    const char* start = nullptr;
    while(*url) {
        if (*url == '/') {
            start = url;
        }
        url++;
    }
    if (!start) {
        return Substring();
    }
    start++;
    auto end = start;
    for(;;) {
        char ch = *(end++);
        if (ch == 0 || ch == '?') {
            return Substring((char*)start, end - start);
        }
    }
}
Substring urlGetHost(const char* url)
{
    const char* start = strstr(url, "://");
    if (start) {
        start += 3;
    }
    else {
        start = url;
    }
    auto end = start + 1;
    while (*end && *end != '/') {
        end++;
    }
    return Substring((char*)start, end - start);
}
std::string jsonStringEscape(const char* str)
{
    std::string buf;
    for (const char* ptr = str; *ptr; ptr++) {
        char ch = *ptr;
        switch (ch) {
            case '\b': buf.append("\b", 2); break;
            case '\f': buf.append("\f", 2); break;
            case '\r': buf.append("\r", 2); break;
            case '\n': buf.append("\n", 2); break;
            case '\t': buf.append("\t", 2); break;
            case '\"': buf.append("\"", 2); break;
            case '\\': buf.append("\\", 2); break;
            default: buf += ch; break;
        }
    }
    return buf;
}
long parseInt(const char* str, long defltVal, int base)
{
    char* endptr;
    errno = 0;
    long val = strtol(str, &endptr, base);
    if (val == 0 && (errno || (endptr == str))) {
        return defltVal;
    }
    return val;
}
