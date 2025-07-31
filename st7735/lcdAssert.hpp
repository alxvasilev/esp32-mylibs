#pragma once
#define lcdassert(cond) while (!(cond)) { \
    printf("Assertion failed:\n%s at %s:%d\n", #cond, __FILE__, __LINE__); fflush(stdout); abort(); }
