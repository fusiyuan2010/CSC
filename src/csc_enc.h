#ifndef _CSC_ENC_H_
#define _CSC_ENC_H_
#include <csc_common.h>

EXTERN_C_BEGIN

typedef struct _CSCEncProps {
    size_t dict_size;
    uint8_t hash_bits;
    uint8_t hash_width;
    int lz_mode;
    int DLTFilter;
    int TXTFilter;
    int EXEFilter;
    uint32_t csc_blocksize; // must be < 16M
    uint32_t raw_blocksize; // must be < 16M
} CSCEncProps;


void CSCEncProps_Init(CSCEncProps *p);
void CSCEnc_WriteProperties(const CSCEncProps *props, uint8_t *stream);

typedef void * CSCEncHandle;

CSCEncHandle CSCEnc_Create(const CSCEncProps *props, ISeqOutStream *outstream);

void CSCEnc_Destroy(CSCEncHandle p);

int CSCEnc_Encode(CSCEncHandle p, 
        ISeqInStream *instream,
        ICompressProgress *progress);

int CSCEnc_Encode_Flush(CSCEncHandle p);

/*
int CSCEnc_SimpleEncode(uint8_t *dest,
        size_t *destLen,
        const uint8_t *src,
        size_t srcLen,
        ICompressProgress *progress,
        ISzAlloc *alloc);
*/

EXTERN_C_END

#endif

