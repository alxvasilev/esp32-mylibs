#ifndef UTILS_HPP
#define UTILS_HPP
/* Import a binary file */
#define EMBED_TEXTFILE(file, sym) asm (\
    ".section \".rodata\"\n"                  /* Change section */\
    ".balign 4\n"                           /* Word alignment */\
    #sym ":\n"                              /* Define the object label */\
    ".incbin \"" file "\"\n"                /* Import the file */\
    #sym "_end:\n"                        \
    ".byte 0\n"                           \
    ".balign 4\n"                           /* Word alignment */\
    #sym "_size:\n"                       \
    ".int " #sym "_end - " #sym "\n"       \
    ".section \".text\"\n")                 /* Restore section */

#endif
