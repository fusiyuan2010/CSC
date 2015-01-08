#include <csc_dec.h>
#include <csc_memio.h>
#include <csc_filters.h>
#include <Common.h>
#include <stdlib.h>
#include <string.h>

#define DecodeBit(coder, v, p) do{\
    if (coder->rc_range_<(1<<24)) {\
        coder->rc_range_<<=8;\
        coder->rc_code_=(coder->rc_code_<<8)+*coder->prc_++;\
        coder->rc_size_++;\
        if (coder->rc_size_==coder->rc_bufsize_) {\
            coder->outsize_+=coder->rc_size_;\
            coder->io_->ReadRCData(coder->rc_buf_,coder->rc_bufsize_);\
            coder->rc_size_=0;\
            coder->prc_ = coder->rc_buf_;\
        }\
    }\
    \
    uint32_t bound = (coder->rc_range_ >> 12) * p;\
    if (coder->rc_code_ < bound) {\
        coder->rc_range_ = bound;\
        p += (0xFFF-p) >> 5;\
        v = v + v + 1;\
    } else {\
        coder->rc_range_ -= bound;\
        coder->rc_code_ -= bound;\
        p -= p >> 5;\
        v = v + v;\
    }\
} while(0)

#define DecodeDirect(x,v,l) do{if ((l) <= 16)\
        v = x->coder_decode_direct(l);\
    else {\
        v = (x->coder_decode_direct((l) - 16) << 16);\
        v = v | x->coder_decode_direct(16);\
    }}while (0)


static uint32_t MDistBound1[65]=
{
    1,            2,            3,            4,
    6,            8,            10,            12,
    16,            20,            24,            28,
    36,            44,            52,            60,
    76,            92,            108,        124,
    156,        188,        252,        316,
    444,        572,        828,        1084,
    1596,        2108,        3132,        4156,
    6204,        8252,        12348,        16444,
    24636,        32828,        49212,        65596,
    98364,        131132,        196668,        262204,
    393276,        524348,        786492,        1048636,
    1572924,    2097212,    3145788,    4194364,
    6291516,    8388668,    12582972,    16777276,
    25165884,    33554492,    50331708,    67108924,
    100663356,    134217788,    201326652,    268435516,
    unsigned(-1),
};

static uint32_t MDistExtraBit1[64]=
{
    0,    0,    0,    1,
    1,    1,    1,    2,
    2,    2,    2,    3,
    3,    3,    3,    4,
    4,    4,    4,    5,
    5,    6,    6,    7,
    7,    8,    8,    9,
    9,    10,    10,    11,
    11,    12,    12,    13,
    13,    14,    14,    15,
    15,    16,    16,    17,
    17,    18,    18,    19,
    19,    20,    20,    21,
    21,    22,    22,    23,
    23,    24,    24,    25,
    25,    26,    26,    28,
};


static uint32_t matchLenBound[17]=
{
    1,        2,        3,        4,
    5,        6,        8,        10,
    14,        18,        26,        34,
    50,        66,        98,        130,
    unsigned(-1),
};

static uint32_t matchLenExtraBit[16]=
{

    0,    0,    0,    0,
    0,    1,    1,    2,
    2,    3,    3,    4,
    4,    5,    5,    6,
};

static uint32_t MDistBound2[17]=
{
    1,        2,        4,        8,
    16,        32,        48,        64,
    96,        128,    192,    256,
    384,    512,    768,    1024,
    unsigned(-1),
};


static uint32_t MDistExtraBit2[16]=
{
    0,    1,    2,    3,
    4,    4,    4,    5,
    5,    6,    6,    7,
    7,    8,    8,    10,
};



static uint32_t MDistBound3[9]=
{
    1,        2,        3,        4,
    6,        8,        16,        32,
    unsigned(-1),
};


static uint32_t MDistExtraBit3[8]=
{
    0,    0,    0,    1,
    1,    3,    4,    5,
};

class CSCDecoder
{
    uint32_t coder_decode_direct(uint32_t len) {

#define BCRCheckBound() do{if (bc_size_ == bc_bufsize_) \
        {\
            outsize_ += bc_size_;\
            io_->ReadBCData(bc_buf_, bc_bufsize_);\
            bc_size_ = 0;\
            pbc_ = bc_buf_;\
        }}while(0)

        uint32_t result;
        while(bc_curbits_ < len) {
            bc_curval_ = (bc_curval_ << 8) | *pbc_;
            pbc_++;
            bc_size_++;
            BCRCheckBound();
            bc_curbits_ += 8;
        }
        result = (bc_curval_ >> (bc_curbits_ - len)) & ((1 << len) - 1);
        bc_curbits_ -= len;
        return result;
    }

    uint32_t decode_int(uint32_t bits) {
        uint32_t num;
        DecodeDirect(this, num, bits);
        return num;
    }

    void decode_bad(uint8_t *dst, uint32_t *size) {
        DecodeDirect(this, *size, MaxChunkBits);
        for(uint32_t i = 0; i < *size; i++)
            dst[i] = coder_decode_direct(8);
    }

    void decode_rle(uint8_t *dst, uint32_t *size) {
        uint32_t c, flag, slot, len, i;
        uint32_t sCtx = 0;
        uint32_t *p=NULL;

        if (p_delta_==NULL) {
            p_delta_=(uint32_t*)malloc(256*256*4);
            for (i=0;i<256*256;i++)
                p_delta_[i]=2048;
        }

        DecodeDirect(this, *size, MaxChunkBits);
        for (i = 0; i < *size; ) {
            flag=0;
            DecodeBit(this, flag, p_rle_flag_);
            if (flag == 0) {
                p=&p_delta_[sCtx*256];
                c=1;
                do  { 
                    DecodeBit(this, c, p[c]);
                } while (c < 0x100);
                dst[i] = c & 0xFF;
                sCtx = dst[i];
                i++;
            } else {
                c=1;
                do { 
                    DecodeBit(this, c, p_rle_len_[c]);
                } while (c < 0x10);

                slot = c & 0xF;
                if (matchLenExtraBit[slot] > 0){
                    DecodeDirect(this, c, matchLenExtraBit[slot]);
                    len = matchLenBound[slot] + c;
                } else
                    len = matchLenBound[slot];

                len += 10;
                while(len-- > 0) {
                    dst[i] = dst[i-1];
                    i++;
                }
                sCtx = dst[i-1];
            }
        }
    }

    uint32_t decode_literal() {
        uint32_t i = 1, *p;

        p = &p_lit_[ctx_ * 256];
        do { 
            DecodeBit(this, i, p[i]);
        } while (i < 0x100);

        ctx_ = i & 0xFF;
        state_ = (state_ * 4 + 0) & 0x3F;
        return ctx_;
    }

    void decode_match(uint32_t &matchDist, uint32_t &matchLen) {
        matchLen = 0;

        uint32_t slot;
        uint32_t i = 1;
        do { 
            DecodeBit(this, i, p_len_[i]);
        } while (i < 0x10);
        slot = i & 0xF;

        if (slot == 15) {
            // a long match length
            i = 1;
            do { 
                matchLen += 129;
                DecodeBit(this, i, p_longlen_);
            } while ((i & 0x1) == 0);

            i = 1;
            do { 
                DecodeBit(this, i, p_len_[i]);
            } while (i < 0x10);
            slot = i & 0xF;
        }

        if (matchLenExtraBit[slot]>0) {
            DecodeDirect(this, i, matchLenExtraBit[slot]);
            matchLen += matchLenBound[slot] + i;
        } else
            matchLen += matchLenBound[slot];

        uint32_t *p;
        i = 1;
        if (matchLen == 1) {
            p = &p_dist3_[0];
            do { 
                DecodeBit(this, i, p[i]);
            } while (i < 0x8);
            slot = i & 0x7;

            if (MDistExtraBit3[slot]>0) {
                DecodeDirect(this, i, MDistExtraBit3[slot]);
                matchDist = MDistBound3[slot] + i;
            } else
                matchDist = MDistBound3[slot];
        } else if (matchLen == 2) {
            p = &p_dist2_[0];
            do { 
                DecodeBit(this, i, p[i]);
            } while (i < 0x10);
            slot = i & 0xF;

            if (MDistExtraBit2[slot]>0) {
                DecodeDirect(this, i, MDistExtraBit2[slot]);
                matchDist = MDistBound2[slot] + i;
            } else
                matchDist = MDistBound2[slot];
        } else {
            uint32_t lenCtx = matchLen > 5 ? 3: matchLen - 3;
            p = &p_dist1_[lenCtx * 64];
            do { 
                DecodeBit(this, i, p[i]);
            } while (i < 0x40);
            slot = i & 0x3F;

            if (MDistExtraBit1[slot]>0) {
                DecodeDirect(this, i, MDistExtraBit1[slot]);
                matchDist = MDistBound1[slot] + i;
            } else
                matchDist = MDistBound1[slot];
        }

        state_ = (state_ * 4 + 1) & 0x3F;
        return;
    }

    void set_lit_ctx(uint32_t c) {
        ctx_=c;
    }

    void decode_1byte_match(void) {
        state_ = (state_ * 4 + 2) & 0x3F;
        ctx_ = 16;
    }

    void decode_repdist_match(uint32_t &repIndex,uint32_t &matchLen) {
        matchLen=0;

        uint32_t i,slot;
        i = 1;
        do { 
            DecodeBit(this, i, p_repdist_[state_*4+i]);
        } while (i < 0x4);
        repIndex = i & 0x3;

        i = 1;
        do { 
            DecodeBit(this, i, p_len_[i]);
        } while (i < 0x10);
        slot = i & 0xF;

        if (slot == 15) {
            // a long match length
            i = 1;
            do { 
                matchLen += 129;
                DecodeBit(this, i, p_longlen_);
            } while ((i & 0x1) == 0);

            i = 1;
            do { 
                DecodeBit(this, i, p_len_[i]);
            } while (i < 0x10);
            slot = i & 0xF;
        }

        if (matchLenExtraBit[slot]>0) {
            DecodeDirect(this, i, matchLenExtraBit[slot]);
            matchLen += matchLenBound[slot] + i;
        } else
            matchLen += matchLenBound[slot];

        state_ = (state_ * 4 + 3) & 0x3F;
    }

    int lz_decode(uint8_t *dst, uint32_t *size, uint32_t limit);
    void lz_copy2dict(uint8_t *src, uint32_t size);


public:
    int Init(MemIO *io, uint32_t dict_size, uint32_t csc_blocksize) {
        io_ = io;
        // entropy coder init
        rc_low_ = 0;
        rc_range_ = 0xFFFFFFFF;
        rc_cachesize_ = 1;
        rc_cache_ = 0;
        rc_code_=0;

        rc_size_ = bc_size_ = 0;
        bc_curbits_ = bc_curval_ =0;

        outsize_ = 0;

        rc_buf_ = bc_buf_ = NULL;
        p_delta_ = NULL;
        wnd_ = NULL;
        p_lit_ = NULL;
        filters_ = NULL;

        prc_ = rc_buf_ = (uint8_t *)malloc(csc_blocksize);
        pbc_ = bc_buf_ = (uint8_t *)malloc(csc_blocksize);
        if (!prc_ || !pbc_)
            goto FREE_ON_ERROR;

        io_->ReadRCData(rc_buf_, rc_bufsize_);
        io_->ReadBCData(bc_buf_, bc_bufsize_);

        rc_code_ = ((uint32_t)prc_[1] << 24) 
            | ((uint32_t)prc_[2] << 16) 
            | ((uint32_t)prc_[3] << 8) 
            | ((uint32_t)prc_[4]);
        prc_ += 5;
        rc_size_ += 5;

        // model
        p_lit_ = (uint32_t*)malloc(256 * 257 * sizeof(uint32_t));
        if (!p_lit_)
            goto FREE_ON_ERROR;

        filters_ = new Filters();
        filters_->Init();

#define INIT_PROB(P, K) do{for(int i = 0; i < K; i++) P[i] = 2048;}while(0)
        INIT_PROB(p_state_, 64 * 3);
        INIT_PROB(p_lit_, 256 * 257);
        INIT_PROB(p_repdist_, 64 * 4);
        INIT_PROB(p_dist1_, 64 * 4);
        INIT_PROB(p_dist2_, 16);
        INIT_PROB(p_dist3_, 8);
        INIT_PROB(p_rle_len_, 16);
        INIT_PROB(p_len_, 16);
#undef INIT_PROB

        p_longlen_ = 2048;
        p_rle_flag_ = 2048;
        state_ = 0;    
        ctx_ = 256;

        // LZ engine
        wnd_size_ = dict_size;
        wnd_ = (uint8_t *)malloc(wnd_size_ + 8);
        if (!wnd_)
            goto FREE_ON_ERROR;

        wnd_curpos_=0;
        rep_dist_[0]=
            rep_dist_[1]=
            rep_dist_[2]=
            rep_dist_[3]=0;
        return 0;

FREE_ON_ERROR:
        Destroy();
        return CANT_ALLOC_MEM;
    }

    void Destroy()
    {
        free(p_lit_);
        free(p_delta_);
        free(wnd_);
        free(rc_buf_);
        free(bc_buf_);
        if (filters_) {
            filters_->Destroy();
            delete filters_;
        }
    }

    int Decompress(uint8_t *dst, uint32_t *size, uint32_t max_bsize);

private:
    MemIO *io_;
    Filters *filters_;

    // For entropy coder
    uint8_t *rc_buf_;
    uint8_t *bc_buf_;        

    //indicates the full size of buffer range/bit coder
    uint32_t rc_bufsize_;
    uint32_t bc_bufsize_;                

    //identify it's a encoder/decoder
    uint32_t m_op;        

    uint64_t rc_low_,rc_cachesize_;
    uint32_t rc_range_,rc_code_;
    uint8_t rc_cache_;

    // for bit coder
    uint32_t bc_curbits_;
    uint32_t bc_curval_;    

    //the i/o pointer of range coder and bit coder
    uint8_t *prc_;
    uint8_t *pbc_;    

    //byte counts of output bytes by range coder and bit coder
    uint32_t bc_size_;
    uint32_t rc_size_;
    int64_t outsize_;

    // For model
    uint32_t p_rle_flag_;
    uint32_t p_rle_len_[16];

    // prob of literals
    uint32_t *p_lit_;//Original [17][256]
    uint32_t *p_delta_;

    uint32_t p_repdist_[64*4];
    uint32_t p_dist1_[4*64];
    uint32_t p_dist2_[16];
    uint32_t p_dist3_[8];

    uint32_t p_len_[16];
    uint32_t p_longlen_;
    uint32_t ctx_;
    uint32_t p_state_[4*4*4*3];//Original [64][3]
    uint32_t state_;

    // For LZ Engine
    uint32_t rep_dist_[4];
    uint32_t wnd_size_;
    uint8_t  *wnd_;
    uint32_t wnd_curpos_;
};

int CSCDecoder::lz_decode(uint8_t *dst, uint32_t *size, uint32_t limit)
{
    uint32_t copied_size = 0;
    uint32_t copied_wndpos = wnd_curpos_;
    uint32_t i;

    for(i = 0; i <= limit; ) {
        uint32_t v=0;
        DecodeBit(this, v, p_state_[state_ *3 + 0]);
        if (v==0) {
            wnd_[wnd_curpos_++] = decode_literal();
            i++;
        } else {
            v=0;
            DecodeBit(this ,v, p_state_[state_ * 3 + 1]);

            uint32_t dist, len, cpy_pos;
            uint8_t *cpy_src ,*cpy_dst;
            if (v==1) {
                decode_match(dist, len);
                if (len == 2 && dist == 2047) 
                    // End of a block
                    break;
                len++;
                rep_dist_[3] = rep_dist_[2];
                rep_dist_[2] = rep_dist_[1];
                rep_dist_[1] = rep_dist_[0];
                rep_dist_[0] = dist;
                cpy_pos = wnd_curpos_ >= dist? 
                    wnd_curpos_ - dist : wnd_curpos_ + wnd_size_ - dist;
                if (cpy_pos > wnd_size_ || cpy_pos + len >wnd_size_ || len + i > limit)
                    return DECODE_ERROR;

                cpy_dst = wnd_ + wnd_curpos_;
                cpy_src = wnd_ + cpy_pos;
                i += len;
                wnd_curpos_ += len;
                while(len--) 
                    *cpy_dst++ = *cpy_src++;
                set_lit_ctx(wnd_[wnd_curpos_ - 1]);
            } else {
                v = 0;
                DecodeBit(this , v, p_state_[state_ * 3 + 2]);
                if (v==0) {
                    decode_1byte_match();
                    cpy_pos = wnd_curpos_ > rep_dist_[0]?
                        wnd_curpos_ - rep_dist_[0] : wnd_curpos_ + wnd_size_ - rep_dist_[0];
                    wnd_[wnd_curpos_++] = wnd_[cpy_pos];
                    i++;
                    set_lit_ctx(wnd_[wnd_curpos_-1]);
                } else {
                    uint32_t dist_idx;
                    decode_repdist_match(dist_idx, len);
                    len++;
                    if (len + i > limit) 
                        return DECODE_ERROR;

                    dist = rep_dist_[dist_idx];
                    for(int j = dist_idx ; j > 0; j--) 
                        rep_dist_[j] = rep_dist_[j-1];
                    rep_dist_[0] = dist;

                    cpy_pos = wnd_curpos_ >= dist? 
                        wnd_curpos_ - dist : wnd_curpos_ + wnd_size_ - dist;
                    if (cpy_pos + len >wnd_size_ || len + i > limit) 
                        return DECODE_ERROR;
                    cpy_dst = wnd_ + wnd_curpos_;
                    cpy_src = wnd_ + cpy_pos;
                    i += len;
                    wnd_curpos_ += len;
                    while(len--) 
                        *cpy_dst++ = *cpy_src++;
                    set_lit_ctx(wnd_[wnd_curpos_ - 1]);
                }
            }
        }

        if (wnd_curpos_ >= wnd_size_) {
            wnd_curpos_ = 0;
            memcpy(dst + copied_size ,wnd_ + copied_wndpos, i - copied_size);
            copied_wndpos = 0;
            copied_size = i;
        }
    }
    *size = i;
    memcpy(dst + copied_size ,wnd_ + copied_wndpos, *size - copied_size);
    return 0;
}

void CSCDecoder::lz_copy2dict(uint8_t *src, uint32_t size)
{
    uint32_t progress=0;
    uint32_t cur_block;

    while (progress < size) {
        cur_block = MIN(wnd_size_ - wnd_curpos_, size - progress);
        cur_block = MIN(cur_block , MinBlockSize);
        memcpy(wnd_ + wnd_curpos_, src + progress, cur_block);
        wnd_curpos_ += cur_block;
        wnd_curpos_ = wnd_curpos_ >= wnd_size_? 0 : wnd_curpos_;
        progress += cur_block;
    }
}

int CSCDecoder::Decompress(uint8_t *dst, uint32_t *size, uint32_t max_bsize)
{
    int ret = 0;
    uint32_t type = decode_int(5);
    switch (type) {
        case DT_NORMAL:
            ret = lz_decode(dst, size, max_bsize);
            if (ret<0)
                return ret;
            break;
        case DT_EXE:
            ret = lz_decode(dst, size, max_bsize);
            if (ret<0)
                return ret;
            filters_->Inverse_E89(dst, *size);
            break;
        case DT_ENGTXT:
            *size = decode_int(MaxChunkBits);
            ret = lz_decode(dst, size, max_bsize);
            if (ret<0)
                return ret;
            filters_->Inverse_Dict(dst, *size);
            break;
        case DT_BAD:
            decode_bad(dst,size);
            lz_copy2dict(dst,*size);
            break;
        //case DT_HARD:
        //    m_model.DecompressHard(dst,size);
        //    m_lz.DuplicateInsert(dst,*size);
        //    break;
        /*case DT_AUDIO:
            m_model.DecompressHard(dst,size);
            m_filters.Inverse_Audio(dst,*size,typeArg1,typeArg2);
            m_lz.DuplicateInsert(dst,*size);
            break;
        case DT_RGB:
            typeArg1=m_model.DecodeInt(16);
            typeArg2=m_model.DecodeInt(6);
            m_model.DecompressHard(dst,size);
            m_filters.Inverse_RGB(dst,*size,typeArg1,typeArg2);
            m_lz.DuplicateInsert(dst,*size);
            break;*/
        case SIG_EOF:
            *size=0;
            break;
        default:
            if (type>=DT_DLT && type<DT_DLT+DLT_CHANNEL_MAX) {
                uint32_t chnNum=DltIndex[type-DT_DLT];
                decode_rle(dst,size);
                filters_->Inverse_Delta(dst,*size,chnNum);
                lz_copy2dict(dst, *size);
            } else
                return DECODE_ERROR;
            break;
    }
    return ret;
}

struct CSCInstance
{
    CSCDecoder *decoder;
    MemIO *io;
    uint32_t raw_blocksize;
};

CSCDecHandle CSCDec_Create(const CSCDecProps *props, ISeqInStream *instream)
{
    CSCInstance *csc = new CSCInstance();

    csc->io = new MemIO();
    csc->io->Init(instream, props->csc_blocksize);
    csc->raw_blocksize = props->raw_blocksize;

    csc->decoder = new CSCDecoder();
    csc->decoder->Init(csc->io, props->dict_size, props->csc_blocksize);
    return (void*)csc;
}

void CSCDec_Destroy(CSCDecHandle p)
{
    CSCInstance *csc = (CSCInstance *)p;
    csc->decoder->Destroy();
    delete csc->decoder;
    delete csc->io;
    delete csc;
}

void CSCDec_ReadProperties(CSCDecProps *props, uint8_t *s)
{
    props->dict_size = ((uint32_t)s[0] << 24) + (s[1] << 16) + (s[2] << 8) + s[3];
    props->csc_blocksize = ((uint32_t)s[4] << 16) + (s[5] << 8) + s[6];
    props->raw_blocksize = ((uint32_t)s[7] << 24) + (s[8] << 8) + s[9];
}

int CSCDec_Decode(CSCDecHandle p, 
        ISeqOutStream *os,
        ICompressProgress *progress)
{
    int ret = 0;
    CSCInstance *csc = (CSCInstance *)p;
    uint8_t *buf = new uint8_t[csc->raw_blocksize];

    for(;;) {
        uint32_t size;
        ret = csc->decoder->Decompress(buf, &size, csc->raw_blocksize);
        if (size == 0 || ret < 0)
            break;

        size_t wrote = os->Write(os, buf, size);
        if (wrote < size) {
            ret = -1;
            break;
        }
    }
    delete []buf;
    return ret;
}


