#include <csc_enc.h>
#include <csc_memio.h>
#include <csc_typedef.h>
#include <csc_encoder_main.h>
#include <stdlib.h>

struct CSCInstance
{
    CSCEncoder *encoder;
    MemIO *io;
    uint32_t raw_blocksize;
};

void CSCEncProps_Init(CSCProps *p, uint32_t dict_size, int level)
{
    if (dict_size < 32 * KB) dict_size = 32 * KB;
    if (dict_size > 1024 * MB) dict_size = 1024 * MB;
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    p->dict_size = dict_size + 10 * KB; // a little more, real size is 8KB smaller than set number
    p->DLTFilter = 1;
    p->TXTFilter = 1;
    p->EXEFilter = 1;
    p->csc_blocksize = 64 * KB;
    p->raw_blocksize = 2 * MB;

    uint32_t hbits = 20;
    if (dict_size < MB)
        hbits = 19;
    else if (dict_size <= 4 * MB) 
        hbits = 20;
    else if (dict_size <= 16 * MB) 
        hbits = 21;
    else if (dict_size <= 64 * MB) 
        hbits = 22;
    else if (dict_size <= 256 * MB) 
        hbits = 23;
    else
        hbits = 24;

    if (dict_size <= 16 * MB) 
        p->bt_size = dict_size;
    else if (dict_size <= 64 * MB) 
        p->bt_size = (dict_size - 16 * MB) / 2 + 16 * MB;
    else if (dict_size <= 256 * MB) 
        p->bt_size = (dict_size - 64 * MB) / 4 + 40 * MB;
    else
        p->bt_size = (dict_size - 256 * MB) / 8 + 88 * MB;

    p->good_len = 32;
    p->hash_bits = hbits;
    p->bt_hash_bits = hbits + 1;
    switch (level) {
        case 1:
            p->hash_width = 1;
            p->lz_mode = 2;
            p->bt_size = 0;
            p->hash_bits++;
            break;
        case 2:
            p->hash_width = 8;
            p->lz_mode = 2;
            p->bt_size = 0;
            p->good_len = 24;
            break;
        case 3:
            p->hash_width = 2;
            p->lz_mode = 3;
            p->bt_size = 0;
            p->good_len = 24;
            p->hash_bits++;
            break;
        case 4:
            p->hash_width = 2;
            p->lz_mode = 3;
            p->bt_cyc = 16;
            p->good_len = 36;
            break;
        case 5:
            p->hash_width = 4;
            p->lz_mode = 3;
            p->good_len = 48;
            p->bt_cyc = 32;
            break;
    }

    if (p->bt_size == dict_size) {
        p->hash_width = 0;
    }
}

uint64_t CSCEnc_EstMemUsage(const CSCProps *p)
{
    uint64_t ret = 0;
    ret += p->dict_size;
    ret += p->csc_blocksize * 2;
    if (p->bt_size) 
        ret += ((1 << p->bt_hash_bits) + 2 * p->bt_size) * sizeof(uint32_t);
    if (p->hash_width)
        ret += (p->hash_width * (1 << p->hash_bits)) * sizeof(uint32_t);
    ret += 80 * KB *sizeof(uint32_t);
    ret += 256 * 256 * sizeof(uint32_t);
    ret += 2 * MB;
    return ret;
}

CSCEncHandle CSCEnc_Create(const CSCProps *props, 
        ISeqOutStream *outstream)
{
    CSCInstance *csc = new CSCInstance();

    csc->io = new MemIO();
    csc->io->Init(outstream, props->csc_blocksize);
    csc->raw_blocksize = props->raw_blocksize;

    csc->encoder = new CSCEncoder();
    csc->encoder->Init(props, csc->io);
    return (void*)csc;
}

void CSCEnc_Destroy(CSCEncHandle p)
{
    CSCInstance *csc = (CSCInstance *)p;
    csc->encoder->Destroy();
    delete csc->encoder;
    delete csc->io;
    delete csc;
}

void CSCEnc_WriteProperties(const CSCProps *props, uint8_t *s, int full)
{
    (void)full;
    s[0] = ((props->dict_size >> 24) & 0xff);
    s[1] = ((props->dict_size >> 16) & 0xff);
    s[2] = ((props->dict_size >> 8) & 0xff);
    s[3] = ((props->dict_size) & 0xff);
    s[4] = ((props->csc_blocksize >> 16) & 0xff);
    s[5] = ((props->csc_blocksize >> 8) & 0xff);
    s[6] = ((props->csc_blocksize) & 0xff);
    s[7] = ((props->raw_blocksize >> 16) & 0xff);
    s[8] = ((props->raw_blocksize >> 8) & 0xff);
    s[9] = ((props->raw_blocksize) & 0xff);
}

int CSCEnc_Encode(CSCEncHandle p, 
        ISeqInStream *is,
        ICompressProgress *progress)
{
    int ret = 0;
    CSCInstance *csc = (CSCInstance *)p;
    uint8_t *buf = new uint8_t[csc->raw_blocksize];
    uint64_t insize = 0;

    for(;;) {
        size_t size = csc->raw_blocksize;
        ret = is->Read(is, buf, &size);
        if (ret >= 0 && size) {
            insize += size;
            csc->encoder->Compress(buf, size);
            ret = 0;
            if (progress)
                progress->Progress(progress, insize, csc->encoder->GetCompressedSize());
        }

        if (ret < 0 || size < csc->raw_blocksize)
            break;
    }
    delete []buf;
    return ret;
}

int CSCEnc_Encode_Flush(CSCEncHandle p)
{
    CSCInstance *csc = (CSCInstance *)p;
    csc->encoder->WriteEOF();
    csc->encoder->Flush();
    return 0;
}

