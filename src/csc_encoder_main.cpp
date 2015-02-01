#include <stdlib.h>
#include <Common.h>
#include <csc_encoder_main.h>
#include <csc_common.h>

int CSCEncoder::Init(const CSCProps *p, MemIO *io)
{
    typeArg1=typeArg2=typeArg3=0;
    fixedDataType=DT_NONE;

    rawblock_limit_ = p->raw_blocksize;

    analyzer_.Init();
    filters_.Init();
    coder_.Init(io);

    if (model_.Init(&coder_) < 0)
        return -1;

    p_ = *p;
    if (lz_.Init(p, &model_) < 0)
        return -1;

    if (p->DLTFilter + p->EXEFilter + p->TXTFilter == 0)
        use_filters_ = false;
    else
        use_filters_ = true;

    return 0;
}

void CSCEncoder::compress_block(uint8_t *src,uint32_t size, uint32_t type)
{
    if (size == 0) 
        return;

    uint8_t cur_lz_mode = p_.lz_mode;
    /*
    if (type == DT_FAST)  {
        type = DT_NORMAL;
        cur_lz_mode = 0;
    }
    */

    if (type == DT_NORMAL) {
        model_.EncodeInt(type, 5);
        lz_.EncodeNormal(src, size, cur_lz_mode);
    } else if (type == DT_EXE) {
        model_.EncodeInt(type, 5);
        filters_.Forward_E89(src, size);
        lz_.EncodeNormal(src, size, cur_lz_mode);
    } else if (type == DT_ENGTXT) {
        if (filters_.Foward_Dict(src, size)) {
            model_.EncodeInt(type, 5);
            model_.EncodeInt(size, MaxChunkBits);
        } else
            model_.EncodeInt(DT_NORMAL, 5);
        lz_.EncodeNormal(src, size, cur_lz_mode);
    } else if (type == DT_FAST) {
        // DT_FAST is the same output stream with NORMAL
        model_.EncodeInt(DT_NORMAL, 5);
        lz_.EncodeNormal(src, size, 4);
    } else if (type == DT_BAD) {
        model_.EncodeInt(type, 5);
        lz_.EncodeNormal(src, size, 5);
        model_.CompressBad(src, size);
    } else if (type >= DT_DLT && type < DT_DLT + DLT_CHANNEL_MAX) {
        uint32_t chnNum = DltIndex[type - DT_DLT];
        model_.EncodeInt(type, 5);
        lz_.EncodeNormal(src, size, 5);
        filters_.Forward_Delta(src, size, chnNum);
        model_.CompressRLE(src,size);
        //model_.CompressBad(src, size);
    } else  {
        printf("Bad data type:%d\n", type);
    }
}

void CSCEncoder::Compress(uint8_t *src,uint32_t size)
{
    uint32_t last_type, this_type;
    uint32_t last_begin, last_size;
    uint32_t cur_block_size;

    last_begin = last_size=0;
    last_type = DT_NORMAL;

    for(uint32_t i = 0; i < size; ) {
        cur_block_size = MIN(MinBlockSize, size - i);

        if (use_filters_) {
            if (fixedDataType == DT_NONE)
                this_type = analyzer_.analyze(src + i, cur_block_size);
            else 
                this_type=fixedDataType;
        } else
            this_type=DT_NORMAL;

        if (this_type == DT_SKIP)
            this_type = last_type;

        if (this_type != DT_NORMAL) {
            if (this_type == DT_EXE && p_.EXEFilter==0)
                this_type = DT_NORMAL;
            else if (this_type == DT_ENGTXT && p_.TXTFilter==0)
                this_type = DT_NORMAL;
            else if (this_type >= DT_DLT && p_.DLTFilter==0)
                this_type = DT_NORMAL;
        }

        if (this_type >= DT_NO_LZ) {
            if (lz_.IsDuplicateBlock(src + i, cur_block_size)) {
                this_type = DT_FAST;
            }
        }
        
        if (last_type != this_type || last_size + cur_block_size > rawblock_limit_) {
            compress_block(src + last_begin, last_size, last_type);
            last_begin = i;
            last_size = 0;
        }

        last_type = this_type;
        last_size += cur_block_size;
        i += cur_block_size;
    }
    compress_block(src + last_begin, last_size, last_type);
}


void CSCEncoder::Flush()
{
    coder_.Flush();
}

void CSCEncoder::WriteEOF()
{
    model_.EncodeInt(SIG_EOF,5);
}


void CSCEncoder::CheckFileType(uint8_t *src, uint32_t size)
{
    //fixedDataType=analyzer_.analyzeHeader(src,size,&typeArg1,&typeArg2,&typeArg3);
    fixedDataType=DT_NONE;
    return;
}

void CSCEncoder::Destroy()
{
    coder_.Destroy();
    lz_.Destroy();
    model_.Destroy();
}

int64_t CSCEncoder::GetCompressedSize()
{
    return (coder_.outsize_ + coder_.rc_size_ + coder_.bc_size_);
}

