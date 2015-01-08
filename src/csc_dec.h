#ifndef _CSC_DEC_H_
#define _CSC_DEC_H_
#include <csc_common.h>

EXTERN_C_BEGIN

typedef struct _CSCDecProps {
    size_t dict_size;
    uint32_t csc_blocksize; // must be < 16M
    uint32_t raw_blocksize; 
} CSCDecProps;


// DecProps usually needs to be read from existing data
void CSCDec_ReadProperties(CSCDecProps *props, uint8_t *stream);

typedef void * CSCDecHandle;

CSCDecHandle CSCDec_Create(const CSCDecProps *props, ISeqInStream *instream);

void CSCDec_Destroy(CSCDecHandle p);

int CSCDec_Decode(CSCDecHandle p, 
        ISeqOutStream *outstream,
        ICompressProgress *progress);


/*
int CSCDec_SimpleDecode(uint8_t *dest,
        size_t *destLen,
        const uint8_t *src,
        size_t srcLen,
        ICompressProgress *progress,
        ISzAlloc *alloc);
*/

EXTERN_C_END

#endif

