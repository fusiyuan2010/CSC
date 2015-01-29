#include <csc_enc.h>
#include <csc_memio.h>
#include <Common.h>
#include <csc_encoder_main.h>
#include <stdlib.h>

struct CSCInstance
{
    CSCEncoder *encoder;
    MemIO *io;
    uint32_t raw_blocksize;
};

void CSCEncProps_Init(CSCEncProps *p)
{
    p->dict_size = 100 * MB;
    p->hash_bits = 24;
    p->hash_width = 1;
    p->lz_mode = 2;
    p->DLTFilter = 0;
    p->TXTFilter = 1;
    p->EXEFilter = 1;
    p->csc_blocksize = 32 * KB;
    p->raw_blocksize = 1 * MB;
}

CSCEncHandle CSCEnc_Create(const CSCEncProps *props, 
        ISeqOutStream *outstream)
{
    CSCSettings setting;
    setting.wndSize = props->dict_size;
    setting.hashBits = props->hash_bits;
    setting.hashWidth = props->hash_width;
    setting.lzMode = props->lz_mode;
    setting.EXEFilter = props->EXEFilter;
    setting.DLTFilter = props->DLTFilter;
    setting.TXTFilter = props->TXTFilter;
    setting.outStreamBlockSize = props->csc_blocksize;

    CSCInstance *csc = new CSCInstance();

    csc->io = new MemIO();
    csc->io->Init(outstream, props->csc_blocksize);
    csc->raw_blocksize = props->raw_blocksize;
    setting.io = csc->io;

    csc->encoder = new CSCEncoder();
    csc->encoder->Init(setting);
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

void CSCEnc_WriteProperties(const CSCEncProps *props, uint8_t *s)
{
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

