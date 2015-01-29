#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <csc_coder.h>
#include <csc_model.h>
#include <csc_lz.h>
#include <stdio.h>

int LZ::Init(uint32_t WindowSize)
{
    return Init(WindowSize, 22, 1);
}

int LZ::Init(uint32_t WindowSize, uint32_t hashBits,uint32_t hashWidth)
{
    wnd_size_=WindowSize;

    if(wnd_size_<32*KB)
        wnd_size_=32*KB;
    if(wnd_size_>MaxDictSize)
        wnd_size_=MaxDictSize;

    wnd_ = NULL;
    wnd_=(uint8_t*)malloc(wnd_size_ + 8);
    if (!wnd_)
        goto FREE_ON_ERROR;

    if (mf_.Init(wnd_, wnd_size_, 64 * MB, 22, 2, 23))
        goto FREE_ON_ERROR;

    good_len_ = 48;
    mf_.SetArg(48, 48, good_len_);
    appt_ = (MFUnit *)malloc(sizeof(MFUnit) * good_len_ + 1);

    Reset();
    memset(wnd_,0,wnd_size_);
    return NO_ERROR;

FREE_ON_ERROR:
    SAFEFREE(wnd_);
    return CANT_ALLOC_MEM;
    
}


void LZ::Reset(void)
{
    wnd_curpos_=0;
    rep_dist_[0] =
        rep_dist_[1] =
        rep_dist_[2] =
        rep_dist_[3] = wnd_size_;

    model_->Reset();
}

void LZ::Destroy(void)
{
    mf_.Destroy();
    SAFEFREE(wnd_);
}

void LZ::EncodeNormal(uint8_t *src, uint32_t size, uint32_t lz_mode)
{
    for(uint32_t i = 0; i < size; ) {
        uint32_t cur_block_size;
        cur_block_size = MIN(wnd_size_ - wnd_curpos_, size - i);
        cur_block_size = MIN(cur_block_size, MinBlockSize);
        memcpy(wnd_ + wnd_curpos_, src + i, cur_block_size);
        if (lz_mode == 0) // fast, with no lazy parser
            compress_normal(cur_block_size, false);
        else if (lz_mode == 1)
            compress_normal(cur_block_size, true);
        else if (lz_mode == 2)
            compress_advanced(cur_block_size);
        if (wnd_curpos_ >= wnd_size_) 
            wnd_curpos_ = 0;
        i += cur_block_size;
    }
    model_->EncodeMatch(64, 0);
    return;
}


uint32_t LZ::CheckDuplicate(uint8_t *src,uint32_t size,uint32_t type)
{
    uint32_t lastWndPos = wnd_curpos_;

    for(uint32_t i = 0; i < size; ) {
        uint32_t cur_block_size = MIN(wnd_size_ - wnd_curpos_, size - i);
        cur_block_size = MIN(cur_block_size, MinBlockSize);

        memcpy(wnd_ + wnd_curpos_, src + i, cur_block_size);
        //if (LZMinBlockSkip(cur_block_size,type)==DT_NORMAL)
        if (0)
        {
            wnd_curpos_=lastWndPos;
            return DT_NORMAL;
        }

        wnd_curpos_ += cur_block_size;
        if (wnd_curpos_ >= wnd_size_) wnd_curpos_=0;
        i += cur_block_size;
    }
    return DT_SKIP;
}

void LZ::DuplicateInsert(uint8_t *src,uint32_t size)
{
    for(uint32_t i = 0; i < size; ) {
        uint32_t cur_block_size = MIN(wnd_size_ - wnd_curpos_, size - i);
        cur_block_size = MIN(cur_block_size, MinBlockSize);
        memcpy(wnd_ + wnd_curpos_, src + i, cur_block_size);
        wnd_curpos_ += cur_block_size;
        if (wnd_curpos_ >= wnd_size_) wnd_curpos_=0;
        i += cur_block_size;
    }
    return;
}

/*
uint32_t LZ::LZMinBlockSkip(uint32_t size,uint32_t type)
{

    uint32_t curhash6,curhash3;
    uint32_t i,j,cmpPos1,cmpPos2,cmpLen,remainLen,remainLen2;
    //uint32_t matchDist;
    uint32_t minMatchLen;
    uint32_t *HT6;


    const uint32_t curblock_endpos=wnd_curpos_+size;
    const uint32_t currBlockStartPos=wnd_curpos_;

    minMatchLen=70;
    remainLen=size;

    for(i=0;i<4;i++)
    {
        cmpPos1=wnd_curpos_>rep_dist_[i]?wnd_curpos_-rep_dist_[i]:wnd_curpos_+wnd_size_-rep_dist_[i];
        if ((cmpPos1<wnd_curpos_ || cmpPos1>curblock_endpos) && (cmpPos1<wnd_size_) )
        {
            cmpPos2=wnd_curpos_;
            remainLen2=MIN(remainLen,wnd_size_-cmpPos1);
            remainLen2=MIN(remainLen2,72);

            if (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]) continue;
            if ((remainLen2<minMatchLen)||
                (wnd_[cmpPos1+minMatchLen+1]!=wnd_[cmpPos2+minMatchLen+1])||
                (wnd_[cmpPos1+(minMatchLen>>1)]!=wnd_[cmpPos2+(minMatchLen>>1)])
                )
                continue;

            cmpPos1+=2;
            cmpPos2+=2;
            cmpLen=2;

            if (remainLen2>3)
                while ((cmpLen<remainLen2-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}
                while((cmpLen<remainLen2)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;
                if (cmpLen>minMatchLen)
                {
                    return DT_NORMAL;
                }
        }
    }

    for(i=0;i<MIN(512,size);i++)
    {
        curhash6=HASH6(wnd_[wnd_curpos_+i]);
        remainLen=size-i;


        cmpPos1=mf_ht6_[curhash6*ht6_width_]&0x1FFFFFFF;

        if ((cmpPos1<wnd_curpos_ || cmpPos1>curblock_endpos) 
            && (cmpPos1<wnd_size_) 
            && ((mf_ht6_[curhash6*ht6_width_]>>29)==((wnd_[wnd_curpos_+i]&0x0E)>>1)))
        {
            //matchDist=wnd_curpos_+i>cmpPos1?
            //    wnd_curpos_+i-cmpPos1:wnd_curpos_+i+wnd_size_-cmpPos1;
            cmpPos2=wnd_curpos_+i;
            remainLen2=MIN(remainLen,wnd_size_-cmpPos1);
            remainLen2=MIN(remainLen2,72);

            if (*(uint32_t*)&wnd_[cmpPos1]==*(uint32_t*)&wnd_[cmpPos2])
            {
                if ((remainLen2<minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen+1]!=wnd_[cmpPos2+minMatchLen+1])||
                    (wnd_[cmpPos1+(minMatchLen>>1)]!=wnd_[cmpPos2+(minMatchLen>>1)])
                    )
                    continue;
                cmpPos1+=4;
                cmpPos2+=4;
                cmpLen=4;

                if (remainLen2>3)
                    while ((cmpLen<remainLen2-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                    {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}
                    while((cmpLen<remainLen2)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;

                    if (cmpLen>=minMatchLen)
                    {
                        return DT_NORMAL;
                    }
            }
        }
    }

    if (type==DT_BAD)
    {
        for (j=0;j<size;j+=5)
        {
            curhash6=HASH6(wnd_[currBlockStartPos+j]);
            HT6=&mf_ht6_[curhash6*ht6_width_];
            for(i=ht6_width_-1;i>0;i--)
                HT6[i]=HT6[i-1];
            HT6[0]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
        }
    }
    else
    {
        for (j=0;j<size;j+=2)
        {
            curhash6=HASH6(wnd_[currBlockStartPos+j]);
            HT6=&mf_ht6_[curhash6*ht6_width_];
            HT6[0]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
        }
    }

    return DT_SKIP;
}
*/

void LZ::encode_nonlit(MFUnit u)
{
    if (u.dist <= 4) {
        if (u.len == 1 && u.dist == 1)
            model_->EncodeRep0Len1();
        else  {
            model_->EncodeRepDistMatch(u.dist - 1, u.len - 2);
            uint32_t dist = rep_dist_[u.dist - 1];
            switch (u.dist) {
                case 4:
                    rep_dist_[3] = rep_dist_[2];
                case 3:
                    rep_dist_[2] = rep_dist_[1];
                case 2:
                    rep_dist_[1] = rep_dist_[0];
                case 1:
                    rep_dist_[0] = dist;
                break;
            }
        }
    } else {
        model_->EncodeMatch(u.dist - 5, u.len - 2);
        rep_dist_[3] = rep_dist_[2];
        rep_dist_[2] = rep_dist_[1];
        rep_dist_[1] = rep_dist_[0];
        rep_dist_[0] = u.dist - 4;
    }
}

int LZ::compress_normal(uint32_t size, bool lazy)
{
    MFUnit u1, u2;
    bool got_u1 = false;
    for(uint32_t i = 0; i < size; ) {
        if (!got_u1)
            u1 = mf_.FindMatch(rep_dist_, wnd_curpos_, size - i);

        if (u1.len == 1 || !lazy || u1.len >= good_len_) {
            if (u1.dist == 0)
                model_->EncodeLiteral(wnd_[wnd_curpos_]);//,wnd_curpos_-1
            else
                encode_nonlit(u1);
            mf_.SlidePos(wnd_curpos_, u1.len, size - i);
            i += u1.len; wnd_curpos_ += u1.len;
            if (u1.dist)
                model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
            got_u1 = false;
            continue;
        }

        u2 = mf_.FindMatch(rep_dist_, wnd_curpos_ + 1, size - i - 1);
        if (mf_.SecondMatchBetter(u1, u2)) {
            // choose literal output
            model_->EncodeLiteral(wnd_[wnd_curpos_]);//,wnd_curpos_-1
            mf_.SlidePos(wnd_curpos_, 1, size - i - 1);
            i++; wnd_curpos_++;
            u1 = u2;
            got_u1 = true;
        } else {
            encode_nonlit(u1);
            mf_.SlidePos(wnd_curpos_ + 1, u1.len - 1, size - i - 1);
            i += u1.len; wnd_curpos_ += u1.len;
            model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
            got_u1 = false;
        }
    }
    return 0;
}


int LZ::compress_fast(uint32_t size)
{
    return 0;
}

int LZ::compress_advanced(uint32_t size)
{
    uint32_t apend = 0, apcur = 0;

    for(uint32_t i = 0; i < size; ) {
        mf_.FindMatchWithPrice(model_, model_->state_, 
                appt_, rep_dist_, wnd_curpos_, size - i);
        if (appt_[0].dist == 0) {
            model_->EncodeLiteral(wnd_[wnd_curpos_]);
            mf_.SlidePos(wnd_curpos_, 1, size - i);
            i++; wnd_curpos_++;
        } else {
            apcur = 0;
            apend = 1;
            apunits_[0].price = 0;
            apunits_[0].back_pos = 0;
            apunits_[0].rep_dist[0] = rep_dist_[0];
            apunits_[0].rep_dist[1] = rep_dist_[1];
            apunits_[0].rep_dist[2] = rep_dist_[2];
            apunits_[0].rep_dist[3] = rep_dist_[3];
            apunits_[0].state = model_->state_;
            uint32_t aplimit = MIN(AP_LIMIT, size - i);
            for(;;) {
                apunits_[apcur].lit = wnd_[wnd_curpos_];
                // fix cur state
                if (apcur) {
                    int l = apunits_[apcur].back_pos;
                    apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[0];
                    apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[1];
                    apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[2];
                    apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                    if (apunits_[apcur].dist == 0) {
                        apunits_[apcur].state = (apunits_[l].state * 4) & 0x3F;
                    } else if (apunits_[apcur].dist <= 4) {
                        uint32_t len = apcur - l;
                        if (len == 1 && apunits_[apcur].dist == 1)
                            apunits_[apcur].state = (apunits_[l].state * 4 + 2) & 0x3F;
                        else {
                            apunits_[apcur].state = (apunits_[l].state * 4 + 3) & 0x3F;
                            uint32_t tmp = apunits_[apcur].rep_dist[apunits_[apcur].dist - 1];
                            switch (apunits_[apcur].dist) {
                                case 4:
                                    apunits_[apcur].rep_dist[3] = apunits_[apcur].rep_dist[2];
                                case 3:
                                    apunits_[apcur].rep_dist[2] = apunits_[apcur].rep_dist[1];
                                case 2:
                                    apunits_[apcur].rep_dist[1] = apunits_[apcur].rep_dist[0];
                                    apunits_[apcur].rep_dist[0] = tmp;
                                    break;
                            }
                       }
                    } else {
                        apunits_[apcur].state = (apunits_[l].state * 4 + 1) & 0x3F;
                        apunits_[apcur].rep_dist[0] = apunits_[apcur].dist- 4;
                        apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[0];
                        apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[1];
                        apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[2];
                    }

                    if (apcur < aplimit)
                        mf_.FindMatchWithPrice(model_, apunits_[apcur].state, 
                            appt_, apunits_[apcur].rep_dist, wnd_curpos_, size - i - apcur);
                }

                if (apcur == aplimit) {
                    ap_backward(apcur);
                    rep_dist_[0] = apunits_[apcur].rep_dist[0];
                    rep_dist_[1] = apunits_[apcur].rep_dist[1];
                    rep_dist_[2] = apunits_[apcur].rep_dist[2];
                    rep_dist_[3] = apunits_[apcur].rep_dist[3];
                    i += apcur;
                    break;
                }

                if (apcur + 1 >= apend) 
                    apunits_[apend++].price = 0xFFFFFFFF;

                if (appt_[0].len >= good_len_ || (appt_[0].len > 1 && appt_[0].len + apcur >= aplimit)) {
                    ap_backward(apcur);
                    i += apcur;
                    rep_dist_[0] = apunits_[apcur].rep_dist[0];
                    rep_dist_[1] = apunits_[apcur].rep_dist[1];
                    rep_dist_[2] = apunits_[apcur].rep_dist[2];
                    rep_dist_[3] = apunits_[apcur].rep_dist[3];
                    encode_nonlit(appt_[0]);
                    mf_.SlidePos(wnd_curpos_, appt_[0].len, size - i);
                    i += appt_[0].len;
                    wnd_curpos_ += appt_[0].len;
                    model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
                    break;
                }
 
                uint32_t lit_ctx = wnd_curpos_? wnd_[wnd_curpos_ - 1] : 0;
                uint32_t cprice = model_->GetLiteralPrice(apunits_[apcur].state, lit_ctx, wnd_[wnd_curpos_]);
                if (cprice + apunits_[apcur].price < apunits_[apcur + 1].price) {
                    apunits_[apcur + 1].dist = 0;
                    apunits_[apcur + 1].back_pos = apcur;
                    apunits_[apcur + 1].price = cprice + apunits_[apcur].price;
                }

                if (appt_[1].dist && appt_[1].price + apunits_[apcur].price < apunits_[apcur + 1].price) {
                    apunits_[apcur + 1].dist = 1;
                    apunits_[apcur + 1].back_pos = apcur;
                    apunits_[apcur + 1].price = appt_[1].price + apunits_[apcur].price;
                }
                   
                uint32_t len = appt_[0].len;
                while (apcur + len >= apend) 
                    apunits_[apend++].price = 0xFFFFFFFF;

                while(len > 1) {
                    if (appt_[len].dist && appt_[len].price + apunits_[apcur].price < apunits_[apcur + len].price) {
                        apunits_[apcur + len].dist = appt_[len].dist;
                        apunits_[apcur + len].back_pos = apcur;
                        apunits_[apcur + len].price = appt_[len].price + apunits_[apcur].price;
                    }
                    len--;
                }
                apcur++;
                mf_.SlidePos(wnd_curpos_, 1, size - i - apcur);
                wnd_curpos_++;
            }
        }
    }
    return 0;
}

void LZ::ap_backward(int end)
{
    for(int i = end; i; ) {
        apunits_[apunits_[i].back_pos].next_pos = i;
        i = apunits_[i].back_pos;
    }

    for(int i = 0; i != end; ) {
        uint32_t next = apunits_[i].next_pos;
        if (apunits_[next].dist == 0) {
            model_->EncodeLiteral(apunits_[i].lit);
        } else if (apunits_[next].dist <= 4) {
            if (next - i == 1 && apunits_[next].dist == 1)
                model_->EncodeRep0Len1();
            else  
                model_->EncodeRepDistMatch(apunits_[next].dist - 1, next - i - 2);
            model_->SetLiteralCtx(apunits_[next - 1].lit);
        } else {
            model_->EncodeMatch(apunits_[next].dist - 5, next - i - 2);
            model_->SetLiteralCtx(apunits_[next - 1].lit);
        }
        i = next;
    }
}


