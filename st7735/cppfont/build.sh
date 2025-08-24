#!/bin/bash
g++ -O0 -g -fsanitize=address -I /usr/include/freetype2 ./cppfont.cpp -lfreetype -o ./cppfont
