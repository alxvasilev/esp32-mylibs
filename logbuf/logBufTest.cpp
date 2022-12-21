#include "logBuf.hpp"
#include <string>
#include <string.h>

std::string genLine() {
    static uint32_t ctr = 0;
    return std::to_string(ctr++) + ": log line";
}

int main()
{
    LogBuffer buf(10000, 50, 1);
    for (auto i = 0; i<100; i++) {
        auto str = genLine();
        buf.addLine(strdup(str.c_str()), str.size());
        printf("start: %d, end: %d\n", buf.mStart, buf.mEnd);
        for (auto& line: buf.mLines) {
            printf("%s\n", line.data ? line.data : "null");
        }
    }
}
