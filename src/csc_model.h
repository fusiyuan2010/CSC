#ifndef _CSC_MODEL_H_
#define _CSC_MODEL_H_
#include "Common.h"
#include <csc_coder.h>


/*  Current specific 
The compression stream is made up by myriads of small data packs.
Each pack:

0-- 0 Rawbyte                    Literal byte
1-- 1 1  MatchDist MatchLen        Ordinary match
disabled-(2-- 1 0 0 0             Last match pair)
disabled-(3-- 1 0 0 1            1-Byte match with last repeat MatchDist)
3-- 1 0 0                        1-Byte match with last repeat MatchDist
4-- 1 0 1 0 0 MatchLen            A match with last repeat MatchDist(0)
5-- 1 0 1 0 1 MatchLen            A match with repeat MatchDist[1]
6-- 1 0 1 1 0 MatchLen            A match with repeat MatchDist[2]
7-- 1 0 1 1 1 MatchLen            A match with repeat MatchDist[3]

MatchDist
64 buckets with different num of extra direct bits.
probDists[64] is the statistical model.

MatchLen
16 buckets with different num of extra direct bits.
p_len_[16] is the statistical model.

Rawbyte
Order-1 coder with only 3-MSBs as context
p_lit_[8][256] is the model

About state type:
pack 0            --- current type 0
pack 1            --- current type 1
pack 2,3        --- current type 2
pack 4,5,6,7    ---    current type 3

The state:
stores last 4 state type.
p_state_ is the model.
*/
enum PackType
{
    P_LITERAL,
    P_MATCH,
    P_REPDIST_MATCH,
    P_1BYTE_MATCH,
    P_REP_MATCH,
    P_INVALID
};


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






class Model
{
public:
    Coder *coder_;

    void Reset(void);
    int Init(Coder *coder);
    void Destroy();

    void EncodeLiteral(uint32_t c);
    uint32_t DecodeLiteral()
    {
        uint32_t i = 1, *p;

        p = &p_lit_[ctx_ * 256];
        do { 
            DecodeBit(coder_, i, p[i]);
        } while (i < 0x100);

        ctx_ = i & 0xFF;
        state_ = (state_ * 4 + 0) & 0x3F;
        return ctx_;
    }

    void SetLiteralCtx(uint32_t c) {ctx_=c;}
    uint32_t GetLiteralCtx() {return ctx_;}


    void EncodeMatch(uint32_t matchDist,uint32_t matchLen);
    void DecodeMatch(uint32_t &matchDist,uint32_t &matchLen)
    {
        matchLen = 0;

        uint32_t slot;
        uint32_t i = 1;
        do { 
            DecodeBit(coder_, i, p_len_[i]);
        } while (i < 0x10);
        slot = i & 0xF;

        if (slot == 15) {
            // a long match length
            i = 1;
            do { 
                matchLen += 129;
                DecodeBit(coder_, i, p_longlen_);
            } while ((i & 0x1) == 0);

            i = 1;
            do { 
                DecodeBit(coder_, i, p_len_[i]);
            } while (i < 0x10);
            slot = i & 0xF;
        }

        if (matchLenExtraBit[slot]>0) {
            DecodeDirect(coder_, i, matchLenExtraBit[slot]);
            matchLen += matchLenBound[slot] + i;
        } else
            matchLen += matchLenBound[slot];

        uint32_t *p;
        i = 1;
        if (matchLen == 1) {
            p = &p_dist3_[0];
            do { 
                DecodeBit(coder_, i, p[i]);
            } while (i < 0x8);
            slot = i & 0x7;

            if (MDistExtraBit3[slot]>0) {
                DecodeDirect(coder_, i, MDistExtraBit3[slot]);
                matchDist = MDistBound3[slot] + i;
            } else
                matchDist = MDistBound3[slot];
        } else if (matchLen == 2) {
            p = &p_dist2_[0];
            do { 
                DecodeBit(coder_, i, p[i]);
            } while (i < 0x10);
            slot = i & 0xF;

            if (MDistExtraBit2[slot]>0) {
                DecodeDirect(coder_, i, MDistExtraBit2[slot]);
                matchDist = MDistBound2[slot] + i;
            } else
                matchDist = MDistBound2[slot];
        } else {
            uint32_t lenCtx = matchLen > 5 ? 3: matchLen - 3;
            p = &p_dist1_[lenCtx * 64];
            do { 
                DecodeBit(coder_, i, p[i]);
            } while (i < 0x40);
            slot = i & 0x3F;

            if (MDistExtraBit1[slot]>0) {
                DecodeDirect(coder_, i, MDistExtraBit1[slot]);
                matchDist = MDistBound1[slot] + i;
            } else
                matchDist = MDistBound1[slot];
        }

        state_ = (state_ * 4 + 1) & 0x3F;
        return;
    }

    void EncodeRepDistMatch(uint32_t repIndex,uint32_t matchLen);
    void DecodeRepDistMatch(uint32_t &repIndex,uint32_t &matchLen)
    {
        matchLen=0;

        uint32_t i,slot;

        i = 1;
        do { 
            DecodeBit(coder_, i, p_repdist_[state_*4+i]);
        } while (i < 0x4);
        repIndex = i & 0x3;

        i = 1;
        do { 
            DecodeBit(coder_, i, p_len_[i]);
        } while (i < 0x10);
        slot = i & 0xF;

        if (slot == 15) {
            // a long match length
            i = 1;
            do { 
                matchLen += 129;
                DecodeBit(coder_, i, p_longlen_);
            } while ((i & 0x1) == 0);

            i = 1;
            do { 
                DecodeBit(coder_, i, p_len_[i]);
            } while (i < 0x10);
            slot = i & 0xF;
        }

        if (matchLenExtraBit[slot]>0) {
            DecodeDirect(coder_, i, matchLenExtraBit[slot]);
            matchLen += matchLenBound[slot] + i;
        } else
            matchLen += matchLenBound[slot];

        state_ =(state_ * 4 + 3) & 0x3F;
    }


    void Encode1BMatch(void);
    void Decode1BMatch(void)
    {
        state_ = (state_ * 4 + 2) & 0x3F;
        ctx_ = 16;
    }

    void CompressDelta(uint8_t *src,uint32_t size);
    void DecompressDelta(uint8_t *dst,uint32_t *size);

    void CompressHard(uint8_t *src,uint32_t size);
    void DecompressHard(uint8_t *dst,uint32_t *size);

    void CompressBad(uint8_t *src,uint32_t size);
    void DecompressBad(uint8_t *dst,uint32_t *size);

    void CompressRLE(uint8_t *src,uint32_t size);
    void DecompressRLE(uint8_t *dst,uint32_t *size);

    void CompressValue(uint8_t *src, uint32_t size,uint32_t width,uint32_t channelNum);

    void EncodeInt(uint32_t num,uint32_t bits);
    uint32_t DecodeInt(uint32_t bits);


    //void DecodePack(PackType *type,uint32_t *a,uint32_t *b);
    uint32_t p_state_[4*4*4*3];//Original [64][3]
    uint32_t state_;

    //Fake Encode --- to get price
    uint32_t GetLiteralPrice(uint32_t fstate,uint32_t fctx,uint32_t c);
    uint32_t Get1BMatchPrice(uint32_t fstate);
    uint32_t GetRepDistMatchPrice(uint32_t fstate,uint32_t repIndex,uint32_t matchLen);
    uint32_t GetMatchPrice(uint32_t fstate,uint32_t matchDist,uint32_t matchLen);

private:
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

    // prob to bits num
    uint32_t p_2_bits_[4096>>3];

    uint32_t ctx_;
};


#endif
