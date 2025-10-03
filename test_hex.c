#include <stdio.h>



typedef unsigned char uint8_t;


static void asciiDump(FILE * file, const void * addr, const unsigned int offset, const unsigned int len, const unsigned int pad) {
    const uint8_t *ptr = addr;

    for (unsigned int n = 0; n < pad; n++) fputc(' ', file);

    for (unsigned int o = 0; o < len; o++) {
        char c = ptr[offset + o];
        if (c < 32 || c > 126) c='.';
        fputc(c, file);
    }
}

static void hexDump(FILE * file, const char *title, const void *addr, const unsigned int len) {
    fprintf(file, "%s (%u bytes):\n", title, len);

    if (len <= 0) {
        fprintf(file, "  Invalid length: %d\n", len);
        return;
    }

    const uint8_t *ptr = addr;

    // TODO: Perfect other use cases
    const unsigned bytes=1;
    const unsigned cols=16;

    unsigned rem=len;
    unsigned row=0;
    do {
        unsigned row_len = rem > cols ? cols : rem;

        fprintf(file, "%08X: ", row*cols);
        for (unsigned col = 0; col < row_len; col++)
            fprintf(file, " %0*x", bytes*2, ptr[(row * cols) + col]);
        unsigned pad = (((cols - row_len) % cols) * ((bytes*2)+1)) + 1;
        asciiDump(file, addr, row * cols, row_len, pad);
        fprintf(file, "\n");
        rem -= row_len;
        row++;
    } while (rem);
}


int main (int argc, char ** argv) {


    uint8_t data[256];
    
    for (unsigned n=0;n<sizeof(data);n++) {
        data[n] = 'A' + (n % 64);
    }

    hexDump(stdout, "Data Dump Test", data, sizeof(data));


    hexDump(stdout, "Partial Data Dump Test", data, sizeof(data)-10);

    return 0;
}