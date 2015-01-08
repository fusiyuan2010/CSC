#include <stdio.h>
#include <stdlib.h>
#include <csc_dec.h>
#include <Types.h>


struct StdioSeqStream
{
    union {
        ISeqInStream is;
        ISeqOutStream os;
    };
    FILE *f;
};


int stdio_read(void *p, void *buf, size_t *size)
{
    StdioSeqStream *sss = (StdioSeqStream *)p;
    *size = fread(buf, 1, *size, sss->f);
    return 0;
}

size_t stdio_write(void *p, const void *buf, size_t size)
{
    StdioSeqStream *sss = (StdioSeqStream *)p;
    return fwrite(buf, 1, size, sss->f);
}

int main(int argc, char *argv[])
{
    FILE *fin, *fout;
    fin = fopen(argv[1], "rb");
    fout = fopen(argv[2], "wb");
    if (fin == NULL || fout == NULL) {
        fprintf(stderr, "File open failed\n");
        return -1;
    }

    StdioSeqStream isss, osss;
    isss.f = fin;
    isss.is.Read = stdio_read;
    osss.f = fout;
    osss.os.Write = stdio_write;

    CSCDecProps p;
    unsigned char buf[CSC_PROP_SIZE];
    fread(buf, 1, CSC_PROP_SIZE, fin);
    CSCDec_ReadProperties(&p, buf);
    CSCDecHandle h = CSCDec_Create(&p, (ISeqInStream*)&isss);
    CSCDec_Decode(h, (ISeqOutStream*)&osss, NULL);
    CSCDec_Destroy(h);

    fclose(fin);
    fclose(fout);
}

