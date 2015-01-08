#include <csc_model.h>
#include <math.h>
#include <stdlib.h>

int Model::Init(Coder *coder)
{
    coder_ = coder;
    for (int i = 0; i < (4096 >> 3); i++)
        p_2_bits_[i]= (uint32_t)(128 * 
                log((float)(i * 8 + 4) / 4096) / log(0.5));

    p_delta_=NULL;
    p_lit_ = (uint32_t*)malloc(256 * 257 * sizeof(uint32_t));
    if (!p_lit_)
        return CANT_ALLOC_MEM;

    return NO_ERROR;
}

void Model::Destroy()
{
    free(p_lit_);
    free(p_delta_);
}


void Model::Reset(void)
{
    free(p_delta_);
    p_delta_ = NULL;
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
}

void Model::EncodeLiteral(uint32_t c)//,uint32_t pos
{
    EncodeBit(coder_, 0, p_state_[state_ * 3 + 0]);

    state_ = (state_ * 4) & 0x3F;
    uint32_t *p = &p_lit_[ctx_ * 256];
    ctx_ = c;
    c = c | 0x100;
    do {
        EncodeBit(coder_,(c >> 7) & 1,p[c>>8]);
        c <<= 1;
    } while (c < 0x10000);
}


void Model::Encode1BMatch(void)
{
    EncodeBit(coder_, 1, p_state_[state_ *3 + 0]);
    EncodeBit(coder_, 0, p_state_[state_ *3 + 1]);
    EncodeBit(coder_, 0, p_state_[state_ *3 + 2]);
    state_ = (state_ * 4 + 2) & 0x3F;
}


void Model::EncodeRepDistMatch(uint32_t repIndex,uint32_t matchLen)
{
    EncodeBit(coder_, 1, p_state_[state_ *3 + 0]);
    EncodeBit(coder_, 0, p_state_[state_ *3 + 1]);
    EncodeBit(coder_, 1, p_state_[state_ *3 + 2]);

    uint32_t i = 1, j;
    j = (repIndex >> 1) & 1; EncodeBit(coder_, j, p_repdist_[state_ * 4 + i]); i += i + j;
    j = repIndex & 1; EncodeBit(coder_, j, p_repdist_[state_*4 + i]); 

    uint32_t c;
    if (matchLen > 129) {
        c = 15 | 0x10;
        do {
            EncodeBit(coder_, (c >> 3) & 1, p_len_[c >> 4]);
            c <<= 1;
        } while (c < 0x100);

        matchLen -= 129;
        while(matchLen > 129) {
            matchLen -= 129;
            EncodeBit(coder_, 0, p_longlen_);
        }

        EncodeBit(coder_, 1, p_longlen_);
    }

    uint32_t slot;
    for (slot = 0; slot < 17; slot++)
        if (matchLenBound[slot] > matchLen) break;

    slot--;
    c = slot | 0x10;
    do {
        EncodeBit(coder_, (c >> 3) & 1, p_len_[c >> 4]);
        c <<= 1;
    } while (c < 0x100);

    if (matchLenExtraBit[slot] > 0) 
        EncodeDirect(coder_, matchLen - matchLenBound[slot], matchLenExtraBit[slot]);

    state_ = (state_ * 4 + 3) & 0x3F;
}

void Model::EncodeMatch(uint32_t matchDist, uint32_t match_len)
{
    EncodeBit(coder_, 1,p_state_[state_ * 3 + 0]);
    EncodeBit(coder_, 1,p_state_[state_ * 3 + 1]);

    uint32_t c;
    uint32_t match_len2 = match_len;
    if (match_len2 > 129) {
        c = 15 | 0x10;
        do {
            EncodeBit(coder_, (c >> 3) & 1, p_len_[c >> 4]);
            c <<= 1;
        } while (c < 0x100);

        match_len2 -= 129;
        while(match_len2 > 129) {
            match_len2 -= 129;
            EncodeBit(coder_, 0, p_longlen_);
        }

        EncodeBit(coder_, 1, p_longlen_);
    }

    uint32_t slot;
    for (slot = 0; slot < 17; slot++)
        if (matchLenBound[slot] > match_len2) break;

    slot--;
    c = slot | 0x10;
    do {
        EncodeBit(coder_, (c >> 3) & 1, p_len_[c >> 4]);
        c <<= 1;
    } while (c < 0x100);

    if (matchLenExtraBit[slot] > 0) 
        EncodeDirect(coder_, match_len2 - matchLenBound[slot], matchLenExtraBit[slot]);


    if (match_len == 1) {
        for (slot = 0; slot < 9; slot++)
            if (MDistBound3[slot] > matchDist) break;

        slot--;
        c = slot | 0x08;
        uint32_t *p = &p_dist3_[0];
        do {
            EncodeBit(coder_,(c >> 2) & 1, p[c >> 3]);
            c <<= 1;
        } while (c < 0x40);
        if (MDistExtraBit3[slot]>0) 
            EncodeDirect(coder_, matchDist - MDistBound3[slot], MDistExtraBit3[slot]);
    } else if (match_len == 2) {
        for (slot = 0; slot < 17; slot++)
            if (MDistBound2[slot] > matchDist) break;

        slot--;
        c = slot | 0x10;
        uint32_t *p = &p_dist2_[0];
        do {
            EncodeBit(coder_, (c >> 3) & 1, p[c >> 4]);
            c <<= 1;
        } while (c < 0x100);

        if (MDistExtraBit2[slot]>0)
            EncodeDirect(coder_, matchDist - MDistBound2[slot], MDistExtraBit2[slot]);
    }
    else
    {
        uint32_t len_ctx = match_len > 5? 3: match_len - 3;
        for (slot = 0; slot < 65; slot++)
            if (MDistBound1[slot] > matchDist) break;

        slot--;
        c = slot | 0x40;
        uint32_t *p = &p_dist1_[len_ctx * 64];
        do {
            EncodeBit(coder_, (c >> 5) & 1, p[c >> 6]);
            c <<= 1;
        } while (c < 0x1000);

        if (MDistExtraBit1[slot]>0)
            EncodeDirect(coder_, matchDist - MDistBound1[slot], MDistExtraBit1[slot]);
    }
    state_ = (state_ * 4 + 1) & 0x3F;
}


void Model::EncodeInt(uint32_t num,uint32_t bits)
{
    EncodeDirect(coder_,num,bits);
}

uint32_t Model::DecodeInt(uint32_t bits)
{
    uint32_t num;
    DecodeDirect(coder_,num,bits);
    return num;
}

void Model::CompressDelta(uint8_t *src,uint32_t size)
{

    uint32_t i;
    uint32_t *p;
    uint32_t c;
    uint32_t sCtx=0;

    if (p_delta_==NULL)
    {
        p_delta_=(uint32_t*)malloc(256*256*4);
        for (i=0;i<256*256;i++)
            p_delta_[i]=2048;
    }


    EncodeDirect(coder_,size,MaxChunkBits);
    for(i=0;i<size;i++)
    {
        p=&p_delta_[sCtx*256];
        c=src[i]|0x100;
        do
        {
            EncodeBit(coder_,(c >> 7) & 1,p[c>>8]);
            c <<= 1;
        }
        while (c < 0x10000);

        sCtx=src[i];
    }
    return;
}

void Model::DecompressDelta(uint8_t *dst,uint32_t *size)
{
    uint32_t c,i;
    uint32_t *p;
    uint32_t sCtx=0;

    if (p_delta_==NULL)
    {
        p_delta_=(uint32_t*)malloc(256*256*4);
        for (i=0;i<256*256;i++)
            p_delta_[i]=2048;
    }

    DecodeDirect(coder_,*size,MaxChunkBits);
    for(i=0;i<*size;i++)
    {
        p=&p_delta_[sCtx*256];
        c=1;
        do 
        { 
            DecodeBit(coder_,c,p[c]);
        } while (c < 0x100);

        dst[i]=c&0xFF;
        sCtx=dst[i];
    }
    return;
}

void Model::CompressHard(uint8_t *src,uint32_t size)
{

    uint32_t i;
    uint32_t *p;
    uint32_t c;
    uint32_t sCtx=8;


    EncodeDirect(coder_,size,MaxChunkBits);
    for(i=0;i<size;i++)
    {
        p=&p_lit_[sCtx*256];
        c=src[i]|0x100;
        do
        {
            EncodeBit(coder_,(c >> 7) & 1,p[c>>8]);
            c <<= 1;
        }
        while (c < 0x10000);

        sCtx=src[i]>>4;
    }
    return;
}

void Model::DecompressHard(uint8_t *dst,uint32_t *size)
{
    uint32_t c,i;
    uint32_t *p;
    uint32_t sCtx=8;

    DecodeDirect(coder_,*size,MaxChunkBits);
    for(i=0;i<*size;i++)
    {
        p=&p_lit_[sCtx*256];
        c=1;
        do 
        { 
            DecodeBit(coder_,c,p[c]);
        } while (c < 0x100);

        dst[i]=c&0xFF;
        sCtx=dst[i]>>4;
    }
    return;
}


void Model::CompressBad(uint8_t *src,uint32_t size)
{
    uint32_t i;
    EncodeDirect(coder_,size,MaxChunkBits);
    for(i=0;i<size;i++)
        coder_->EncDirect16(src[i],8);
    return;
}

void Model::DecompressBad(uint8_t *dst,uint32_t *size)
{
    uint32_t i;
    DecodeDirect(coder_,*size,MaxChunkBits);
    for(i=0;i<*size;i++)
        dst[i]=coder_->DecDirect16(8);
    return;
}

void Model::CompressRLE(uint8_t *src, uint32_t size)
{
    uint32_t i,j,slot,c,len,m;
    uint32_t sCtx=0;
    uint32_t *p=NULL;

    EncodeDirect(coder_,size,MaxChunkBits);

    if (p_delta_==NULL)
    {
        p_delta_=(uint32_t*)malloc(256*256*4);
        for (i=0;i<256*256;i++)
            p_delta_[i]=2048;
    }

    for (i=0;i<size;)
    {
        if (i>0 && size-i>3 && src[i-1]==src[i] 
        && src[i]==src[i+1]
        && src[i]==src[i+2])
        {
            j=i;

            j+=3;
            len=3;

            while(j<size && src[j]==src[j-1] && len<130)
            {
                len++;
                j++;
            }

            if (len>10)
            {
                sCtx=src[j-1];
                len-=10;

                EncodeBit(coder_,1,p_rle_flag_);

                for (slot=0;slot<17;slot++)
                    if (matchLenBound[slot]>len) break;

                slot--;

                c=slot|0x10;

                do
                {
                    EncodeBit(coder_,(c >> 3) & 1,p_rle_len_[c>>4]);
                    c <<= 1;
                }
                while (c < 0x100);

                if (matchLenExtraBit[slot]>0)
                {
                    m=len-matchLenBound[slot];
                    EncodeDirect(coder_,m,matchLenExtraBit[slot]);
                }
                i=j;
                continue;
            }
        }

        EncodeBit(coder_,0,p_rle_flag_);

        p=&p_delta_[sCtx*256];

        c=src[i]|0x100;
        do
        {
            EncodeBit(coder_,(c >> 7) & 1,p[c>>8]);
            c <<= 1;
        }
        while (c < 0x10000);

        sCtx=src[i];
        i++;
    }
    return;
}

void Model::DecompressRLE(uint8_t *dst, uint32_t *size)
{
    uint32_t c,flag,slot,len,i;
    uint32_t sCtx=0;
    uint32_t *p=NULL;

    if (p_delta_==NULL)
    {
        p_delta_=(uint32_t*)malloc(256*256*4);
        for (i=0;i<256*256;i++)
            p_delta_[i]=2048;
    }

    DecodeDirect(coder_,*size,MaxChunkBits);
    for (i=0;i<*size;)
    {
        flag=0;
        DecodeBit(coder_,flag,p_rle_flag_);
        if (flag==0)
        {
            p=&p_delta_[sCtx*256];
            c=1;
            do 
            { 
                DecodeBit(coder_,c,p[c]);
            } while (c < 0x100);
            dst[i]=c&0xFF;
            sCtx=dst[i];
            i++;
        }
        else
        {
            c=1;
            do 
            { 
                DecodeBit(coder_,c,p_rle_len_[c]);
            } while (c < 0x10);
            slot=c&0xF;

            if (matchLenExtraBit[slot]>0)
            {
                DecodeDirect(coder_,c,matchLenExtraBit[slot]);
                len=matchLenBound[slot]+c;
            }
            else
                len=matchLenBound[slot];

            len+=10;

            while(len-->0)
            {
                dst[i]=dst[i-1];
                i++;
            }

            sCtx=dst[i-1];

        }
    }
    return;
}


void Model::CompressValue(uint8_t *src, uint32_t size,uint32_t width,uint32_t channelNum)
{
    /*uint32_t probSig=2048;
    uint32_t *probTmp=(uint32_t*)malloc(512*256*4);
    for(uint32_t i=0;i<512*256;i++)
    {
        probTmp[i]=2048;
    }

    for(int i=0;i<channelNum;i++)
    {
        for(int j=i;j<size;j+=channelNum)
        {
            uint32_t *p1,*p2;
            if (j>channelNum)
                p1=&probTmp[src[j-channelNum]*256];
            else
                p1=&probTmp[0];
            if (j>channelNum*width)
                p2=&probTmp[(src[j-channelNum*width]+256)*256];
            else
                p2=&probTmp[256*256];

            uint32_t c=src[j]|0x100;
            do
            {
                EncodeBit2(coder_,(c >> 7) & 1,p1[c>>8],p2[c>>8]);
                c <<= 1;
            }
            while (c < 0x10000);
        }
    }*/


    /*rc1=*(bufx+i-CTX1)<<9;
    rc3=*(bufx+i-CTX2)<<9|256;if (CTX1==3 && i>Lung*3) rc3=bufx[i-(Lung*3)]<<9|256;
    *(bufx+i) = nread();i+=CTX1;*/
}
