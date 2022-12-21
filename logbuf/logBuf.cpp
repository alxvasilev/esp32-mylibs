#include "logBuf.hpp"
#include "logBufLog.hpp"

void LogBuffer::addLine(const char* data, Line::Size size, uint8_t type)
{
    size_t newSize;
    while ((newSize = mDataSize + size) > mMaxDataSize) {
        LOGBUF_LOG_DEBUG("addLine: Total size %zu is more than %zu, deleting first line", newSize, mMaxDataSize);
        deleteFirstLine();
    }
    if (mStart == 0 && mEnd == 0 && mLines.size() < mMaxLineCount) { // starting at zero and ending at vector end
        LOGBUF_LOG_DEBUG("addLine: fits vector: pushing new vector element");
        // data starts at start of mLines vector and ends at its end - we can
        // easily expand the vector.
        mLines.emplace_back(data, size, type);
        mDataSize = newSize;
        return;
    }
    if (mStart < 0) { // empty, bootstrap mStart
        mStart = 0;
        if (mLines.empty()) {
            LOGBUF_LOG_DEBUG("addLine: vector is empty: pushing new element");
            mLines.emplace_back(data, size, type);
        } else {
            LOGBUF_LOG_DEBUG("addLine: log is empty, but vector not: using first element");
            mLines[0].assign(data, size, type);
        }
        mDataSize = newSize;
        return;
    }
    if (mEnd == mStart) { // full, and can't grow the vector, must free an element
        LOGBUF_LOG_DEBUG("addLine: Full, deleting first line");
        assert(mStart > -1 && !mLines.empty());
        deleteFirstLine();
        assert(mEnd != mStart);
    }
    // mLines is not empty and mStart is not at start of vector
    if (mEnd > mStart) { // no wrapping
        assert(mEnd < mLines.size()); // if mEnd was at the end of the vector, it must have wrapped to zero
        mLines[mEnd++].assign(data, size, type);
        if (mEnd >= mLines.size()) {
            mEnd = 0; // wrap
        }
    } else { // mEnd < mStart: buffer is wrapped
        LOGBUF_LOG_DEBUG("addLine: wrapped, mEnd < mStart, using next element");
        assert(mEnd < mStart);
        mLines[mEnd++].assign(data, size, type);
    }
    mDataSize += size;
}

bool LogBuffer::deleteFirstLine()
{
    if (mStart < 0) {
        return false;
    }
    assert(!mLines.empty());
    auto& delLine = mLines[mStart++];
    mDataSize -= delLine.size;
    delLine.clear();
    if (mStart >= mLines.size()) {
        mStart = 0;
    }
    if (mStart == mEnd) { // empty
        mStart = -1;
        mEnd = 0;
    }
    return true;
}
