#ifndef _CCSC_H
#define _CCSC_H

#include "Common.h"
#include <csc_model.h>
#include <csc_filters.h>
#include <csc_memio.h>
#include <csc_analyzer.h>
#include <csc_lz.h>
#include <csc_coder.h>


struct CSCSettings
{
    uint32_t hashBits;
    uint32_t hashWidth;
    uint32_t wndSize;
    uint32_t maxSuccBlockSize;
    uint32_t outStreamBlockSize;
    uint32_t InBufferSize;


    uint8_t lzMode;
    MemIO *io;

    uint8_t DLTFilter;
    uint8_t TXTFilter;
    uint8_t EXEFilter;
    
    void SetDefaultMethod(uint8_t method);
    void Refresh();
    CSCSettings();
};


class CCsc
{
public:
    CCsc();
    ~CCsc();

    int Init(uint32_t operation,CSCSettings setting);
    //operation should be ENCODE | DECODE
    

    void WriteEOF();
    //Should be called when finished compression of one part.

    void Flush();
    //Should be called when finished the whole compression.

    void Destroy();

    void Compress(uint8_t *src,uint32_t size);

    int Decompress(uint8_t *src,uint32_t *size);
    //*size==0 means meets the EOF in raw stream.

    void CheckFileType(uint8_t *src,uint32_t size);
    //Should be called before compress a file.src points
    //to first several bytes of file.
    
    void EncodeInt(uint32_t num,uint32_t bits);
    uint32_t DecodeInt(uint32_t bits);
    //Put/Get num on CSC Stream


    int64_t GetCompressedSize();
    //Get current compressed size.
    

private:
    CSCSettings m_setting;
    bool m_initialized;

    uint32_t m_operation;
    uint8_t *m_rcbuffer;
    uint8_t *m_bcbuffer;
    uint32_t fixedDataType; //
    uint32_t typeArg1,typeArg2,typeArg3;

    Filters m_filters;
    Coder m_coder;
    Model m_model;
    
    LZ m_lz;
    Analyzer m_analyzer;

    uint32_t m_succBlockSize;
    //This determines how much maximumly the CCsc:Decompress can decompress
    // in one time. 

    bool m_useFilters;
    void InternalCompress(uint8_t *src,uint32_t size,uint32_t type);
    //compress the buffer and treat them in one type.It's called after analyze the data.

};

#endif
