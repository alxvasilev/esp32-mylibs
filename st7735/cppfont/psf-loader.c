#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04
#define PSF2_MAGIC0 0x72
#define PSF2_MAGIC1 0xb5
#define PSF2_MAGIC2 0x4a
#define PSF2_MAGIC3 0x86

struct psf_font {
    int version; // 1 or 2
    uint32_t glyph_count;
    uint32_t bytes_per_glyph;
    uint32_t width;
    uint32_t height;
    unsigned char *glyph_buffer;
};

static uint32_t le32(const unsigned char *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int load_psf1(FILE *f, struct psf_font *font) {
    unsigned char header[4];
    if (fread(header, 1, 4, f) != 4) return -1;
    if (header[0] != PSF1_MAGIC0 || header[1] != PSF1_MAGIC1) return -1;

    font->version = 1;
    int mode = header[2];
    font->glyph_count = (mode & 0x01) ? 512 : 256;
    font->bytes_per_glyph = header[3];
    font->width = 8;
    font->height = font->bytes_per_glyph;

    font->glyph_buffer = malloc(font->glyph_count * font->bytes_per_glyph);
    if (!font->glyph_buffer) return -1;

    size_t read_bytes = fread(font->glyph_buffer, 1, font->glyph_count * font->bytes_per_glyph, f);
    if (read_bytes != font->glyph_count * font->bytes_per_glyph) {
        free(font->glyph_buffer);
        return -1;
    }
    return 0;
}

int load_psf2(FILE *f, struct psf_font *font) {
    unsigned char header[32];
    if (fread(header, 1, 32, f) != 32) return -1;
    if (!(header[0] == PSF2_MAGIC0 && header[1] == PSF2_MAGIC1 && header[2] == PSF2_MAGIC2 && header[3] == PSF2_MAGIC3))
        return -1;

    font->version = 2;
    font->glyph_count = le32(&header[4]);
    font->bytes_per_glyph = le32(&header[8]);
    font->width = le32(&header[12]);
    font->height = le32(&header[16]);

    font->glyph_buffer = malloc(font->glyph_count * font->bytes_per_glyph);
    if (!font->glyph_buffer) return -1;

    size_t read_bytes = fread(font->glyph_buffer, 1, font->glyph_count * font->bytes_per_glyph, f);
    if (read_bytes != font->glyph_count * font->bytes_per_glyph) {
        free(font->glyph_buffer);
        return -1;
    }
    return 0;
}

int load_psf(const char *filename, struct psf_font *font) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

    unsigned char magic[4];
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return -1; }
    rewind(f);

    if (magic[0] == PSF1_MAGIC0 && magic[1] == PSF1_MAGIC1) {
        int ret = load_psf1(f, font);
        fclose(f);
        return ret;
    } else if (magic[0] == PSF2_MAGIC0 && magic[1] == PSF2_MAGIC1 &&
               magic[2] == PSF2_MAGIC2 && magic[3] == PSF2_MAGIC3) {
        int ret = load_psf2(f, font);
        fclose(f);
        return ret;
    }

    fclose(f);
    return -1;
}

void print_codepoint_range(struct psf_font *font) {
    if (font->version == 1) {
        if (font->glyph_count == 256)
            printf("Codepoints range: 0x20 - 0x7F (ASCII printable)\n");
        else if (font->glyph_count == 512)
            printf("Codepoints range: 0x00 - 0x1FF\n");
        else
            printf("Codepoints count: %u (unknown range)\n", font->glyph_count);
    } else if (font->version == 2) {
        printf("Codepoints count: %u (assumed sequential)\n", font->glyph_count);
    }
}

void print_string_glyphs_thin(struct psf_font *font, const char *str, int print_all) {
    int glyph_width = font->width;
    int glyph_height = font->height;
    int bytes_per_row = (glyph_width + 7) / 8;

    int len = print_all ? font->glyph_count : (int)strlen(str);
    if (len == 0) return;

    // Top frames
    for (int i = 0; i < len; i++) {
        printf("┌");
        for (int x = 0; x < glyph_width; x++)
            printf("─");
        printf("┐ ");
    }
    printf("\n");

    for (int y = 0; y < glyph_height; y++) {
        for (int c = 0; c < len; c++) {
            printf("│");
            unsigned int glyph_index;
            if (print_all) {
                glyph_index = c;
            } else {
                unsigned char ch = (unsigned char)str[c];
                glyph_index = ch;
            }
            if (glyph_index >= font->glyph_count) glyph_index = 0;

            unsigned char *glyph = font->glyph_buffer + glyph_index * font->bytes_per_glyph;
            int byte_idx = y * bytes_per_row;

            for (int x = 0; x < glyph_width; x++) {
                int bit = 7 - (x % 8);
                if (((glyph[byte_idx] >> bit) & 1))
                    printf("\xE2\x96\x88"); // █
                else
                    printf(" ");
            }
            printf("│ ");
        }
        printf("\n");
    }

    // Bottom frames
    for (int i = 0; i < len; i++) {
        printf("└");
        for (int x = 0; x < glyph_width; x++)
            printf("─");
        printf("┘ ");
    }
    printf("\n");

    printf("Font size: %ux%u\n", font->width, font->height);
}

void print_all_glyphs_wrapped(struct psf_font *font, int glyphs_per_row) {
    int glyph_width = font->width;
    int glyph_height = font->height;
    int bytes_per_row = (glyph_width + 7) / 8;

    int rows = (font->glyph_count + glyphs_per_row - 1) / glyphs_per_row;

    for (int row = 0; row < rows; row++) {
        int start_glyph = row * glyphs_per_row;
        int end_glyph = start_glyph + glyphs_per_row;
        if (end_glyph > font->glyph_count) end_glyph = font->glyph_count;
        int count = end_glyph - start_glyph;

        // Print top frame for this row
        for (int i = 0; i < count; i++) {
            printf("┌");
            for (int x = 0; x < glyph_width; x++) printf("─");
            printf("┐ ");
        }
        printf("\n");

        // Print glyph rows
        for (int y = 0; y < glyph_height; y++) {
            for (int g = start_glyph; g < end_glyph; g++) {
                printf("│");
                unsigned char *glyph = font->glyph_buffer + g * font->bytes_per_glyph;
                int byte_idx = y * bytes_per_row;

                for (int x = 0; x < glyph_width; x++) {
                    int bit = 7 - (x % 8);
                    if ((glyph[byte_idx] >> bit) & 1)
                        printf("\xE2\x96\x88"); // █
                    else
                        printf(" ");
                }
                printf("│ ");
            }
            printf("\n");
        }

        // Print bottom frame for this row
        for (int i = 0; i < count; i++) {
            printf("└");
            for (int x = 0; x < glyph_width; x++) printf("─");
            printf("┘ ");
        }
        printf("\n\n");
    }

    printf("Font size: %ux%u\n", font->width, font->height);
}

int get_console_width() {
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s font.psf [text]\n", argv[0]);
        return 1;
    }

    struct psf_font font;
    if (load_psf(argv[1], &font) != 0) {
        fprintf(stderr, "Failed to load PSF font '%s'\n", argv[1]);
        return 1;
    }

    print_codepoint_range(&font);

    int console_width = get_console_width();

    int glyph_width = font.width;
    int glyph_total_width = glyph_width + 3; // 1 left frame + 1 right frame + 1 space
    int max_glyphs_per_row = console_width / glyph_total_width;
    if (max_glyphs_per_row < 1) max_glyphs_per_row = 1;

    if (argc >= 3 && argv[2][0] != 0) {
        print_string_glyphs_thin(&font, argv[2], 0);
    } else {
        print_all_glyphs_wrapped(&font, max_glyphs_per_row);
    }

    free(font.glyph_buffer);
    return 0;
}
