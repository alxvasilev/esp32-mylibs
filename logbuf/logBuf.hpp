#ifndef _LOGBUF_H_INCLUDED
#define _LOGBUF_H_INCLUDED

#include <stddef.h>
#include <vector>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "mutex.hpp"

class LogBuffer {
public:
    struct Line {
        typedef uint32_t Size;
        char* data;
        Size size;
        uint8_t type = 0;
        Line(const char* aData, Size aSize, uint8_t aType)
        {
            assign(aData, aSize, aType);
        }
        void assign(const char* aData, Size aSize, uint8_t aType)
        {
            if (aData) {
                assert(aSize);
                this->data = (char*)malloc(aSize);
                assert(this->data);
                memcpy(this->data, aData, aSize);
            }
            else {
                this->data = nullptr;
            }
            size = aSize;
            type = aType;
        }
        void clear() {
            if (!data) {
                return;
            }
            free(data);
            data = nullptr;
            size = 0;
        }
        // No destructor to free the buffer because the vector may copy&delete items when expanding,
        // which will result in 2 items pointing to the same buffer temporarily. When the old one is destroyed
        // it will free the buffer, making the copy point to an invalid buffer
    };
protected:
    std::vector<Line> mLines;
    size_t mMaxDataSize;
    size_t mMaxLineCount;
    size_t mDataSize = 0;
    /** Index of first line, -1 if empty */
    int mStart = -1;
    /** One past the index of the last line, 0 if empty */
    int mEnd = 0;
    /* mStart == mEnd -> wrapped, list is full
     * mStart < mEnd -> not wrapped, may be empty if mStart < 0
     * mStart > mEnd -> wrapped, not full
     */
    bool deleteFirstLine();
public:
    Mutex mutex;
    const std::vector<Line>& lines() const { return mLines; }
    LogBuffer(size_t maxDataSize, size_t maxLines, size_t numReserveLines)
    :mMaxDataSize(maxDataSize), mMaxLineCount(maxLines)
    {
        mLines.reserve(numReserveLines);
    }
    void addLine(const char* data, Line::Size size, uint8_t type);
    template<class Cb>
    void iterate(Cb&& cb) const
    {
        if (mStart < 0) {
            return;
        }
        if (mStart < mEnd) {
            for (int i = mStart; i<mEnd; i++) {
                cb(mLines[i]);
            }
        } else {
            for (int i = mStart; i < mLines.size(); i++) {
                cb(mLines[i]);
            }
            for (int i = 0; i < mEnd; i++) {
                cb(mLines[i]);
            }
        }
    }
    void clear() {
        for (auto& line: mLines) {
            line.clear();
        }
        mLines.clear();
        mStart = -1;
        mEnd = 0;
    }
};
#endif
