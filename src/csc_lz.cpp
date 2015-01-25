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

    ht6_bits_=hashBits;
    ht6_width_=hashWidth;

    wnd_ = NULL;
    mf_ht2_
        = mf_ht3_
        = mf_ht6raw_
        = NULL;
    wnd_=(uint8_t*)malloc(WindowSize+8);
    if (wnd_==NULL)
        goto FREE_ON_ERROR;

    mf_ht2_=(uint32_t*)malloc(sizeof(uint32_t) * 64 * KB);
    if (mf_ht2_==NULL)
        goto FREE_ON_ERROR;

    mf_ht3_=(uint32_t*)malloc(sizeof(uint32_t) * 64 * KB);
    if (mf_ht3_==NULL)
        goto FREE_ON_ERROR;

    /*
    bt_nodes_ = (uint32_t*)malloc(sizeof(uint32_t) * 2 * wnd_size_);
    if (bt_nodes_ == NULL)
        goto FREE_ON_ERROR;
    memset(bt_nodes_, 0, sizeof(uint32_t) * 2 * wnd_size_);

    bt_start_ = (uint32_t*)malloc(sizeof(uint32_t) * (1 << ht6_bits_));
    if (bt_nodes_ == NULL)
        goto FREE_ON_ERROR;
    memset(bt_start_, 0, sizeof(uint32_t) * (1 << ht6_bits_));
    */

    mf_ht6raw_=(uint32_t*)malloc(sizeof(uint32_t)*ht6_width_*(1<<ht6_bits_)+256);
    if (mf_ht6raw_==NULL)
        goto FREE_ON_ERROR;
    mf_ht6_=(uint32_t*)(mf_ht6raw_+(64-(uint32_t)((uint64_t)mf_ht6raw_)%64));

    parser=(ParserAtom*)malloc(sizeof(ParserAtom)*(MinBlockSize+1));
    matchList=(MatchAtom*)malloc(sizeof(MatchAtom)*(2+(MinBlockSize<<3)));
    if (parser==NULL || matchList==NULL)
        goto FREE_ON_ERROR;

    Reset();
    //memset(wnd_,0,wnd_size_);

    mfunits_[0].u[0].len = 1;
    mfunits_[1].u[0].len = 1;
    mfunits_[0].u[0].type = MFUnit::LITERAL;
    mfunits_[1].u[0].type = MFUnit::LITERAL;
    return NO_ERROR;

FREE_ON_ERROR:
    SAFEFREE(mf_ht2_);
    SAFEFREE(mf_ht3_);
    SAFEFREE(mf_ht6raw_);
    SAFEFREE(parser);
    SAFEFREE(matchList);
    SAFEFREE(wnd_);
    return CANT_ALLOC_MEM;
    
}


void LZ::Reset(void)
{
    wnd_curpos_=0;

    rep_dist_[0]=
        rep_dist_[1]=
        rep_dist_[2]=
        rep_dist_[3]=0;
    rep_matchlen_=0;

    passedcnt_=1024;
    litcount_=400;
    model_->Reset();
}

void LZ::Destroy(void)
{
    SAFEFREE(mf_ht2_);
    SAFEFREE(mf_ht3_);
    SAFEFREE(mf_ht6raw_);
    SAFEFREE(parser);
    SAFEFREE(matchList);
    SAFEFREE(wnd_);
}

void LZ::EncodeNormal(uint8_t *src,uint32_t size,uint32_t lzMode)
{
    uint32_t progress=0;
    uint32_t currBlockSize;

    while (progress<size)
    {
        currBlockSize=MIN(wnd_size_-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);
        if (lzMode==1)
            compress_normal(currBlockSize, true, false);
        else if (lzMode==0)
            compress_normal(currBlockSize, false, true);
        else 
            compress_advanced(currBlockSize);
            
        if (wnd_curpos_>=wnd_size_) wnd_curpos_=0;
        progress+=currBlockSize;
    }
    model_->EncodeMatch(64, 0);

    return;
}


uint32_t LZ::CheckDuplicate(uint8_t *src,uint32_t size,uint32_t type)
{
    uint32_t lastWndPos=wnd_curpos_;
    uint32_t progress=0;
    uint32_t currBlockSize;

    while (progress<size)
    {
        currBlockSize=MIN(wnd_size_-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);
        if (LZMinBlockSkip(currBlockSize,type)==DT_NORMAL)
        {
            wnd_curpos_=lastWndPos;
            return DT_NORMAL;
        }

        wnd_curpos_+=currBlockSize;
        if (wnd_curpos_>=wnd_size_) wnd_curpos_=0;
        progress+=currBlockSize;
    }

    return DT_SKIP;
}

void LZ::DuplicateInsert(uint8_t *src,uint32_t size)
{
    uint32_t progress=0;
    uint32_t currBlockSize;

    while (progress<size)
    {
        currBlockSize=MIN(wnd_size_-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);

        wnd_curpos_+=currBlockSize;
        if (wnd_curpos_>=wnd_size_) wnd_curpos_=0;
        progress+=currBlockSize;
    }
    return;
}

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
            //curhash3=HASH3(wnd_[currBlockStartPos+j]);
            curhash6=HASH6(wnd_[currBlockStartPos+j]);
            HT6=&mf_ht6_[curhash6*ht6_width_];
            //for(i=ht6_width_-1;i>0;i--)
            //    HT6[i]=HT6[i-1];
            HT6[0]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
            //mf_ht3_[curhash3]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
        }
    }

    return DT_SKIP;
}

void LZ::slide_pos(uint32_t len, bool mffast)
{
    uint32_t lasthash6 = 0;

    for(uint32_t i = 1; i < len; i++) {
        uint32_t pos = wnd_curpos_ + i;

        uint32_t v = (pos | ((wnd_[pos] & 0x0E) << 28));
        if (!mffast) {
            uint32_t curhash2 = HASH2(wnd_[pos]);
            mf_ht2_[curhash2] = v;
        }
        uint32_t curhash3 = HASH3(wnd_[pos]);
        mf_ht3_[curhash3] = v;


    if (0)
    {
    uint32_t wpos = pos;
    uint32_t curhash4 = HASH6(wnd_[wpos]);
    //uint32_t curhash6 = HASH6(wnd_[wpos]);
    uint32_t cmp_pos = bt_start_[curhash4];
    uint32_t *l = &bt_nodes_[wpos * 2], *r = &bt_nodes_[wpos * 2 + 1];
    uint32_t lenl = 0, lenr = 0;

    for(uint32_t i = 0; i < 16; i++) {
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) break;
        //if (dist < last_dist) break;
        //last_dist = dist;

        uint32_t *tlast = &bt_nodes_[cmp_pos * 2];
        uint32_t len_limit = MIN(wnd_size_ - cmp_pos, 32);
        len_limit = MIN(len_limit, curblock_endpos - wpos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        uint32_t minlen = MIN(lenl, lenr);
        uint32_t match_len = 0;
        if (pmatch[minlen] == pcur[minlen]) {
            while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
                pmatch += 4; pcur += 4; }
            if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
                pmatch += 2; pcur += 2; }
            if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
            match_len = (pcur - wnd_) - wpos;
            if (match_len >= 4) {
                if (match_len >= 24) {
                    *l = tlast[0];
                    *r = tlast[1];
                    break;
                }
            }
        } else {
            pmatch += minlen;
            pcur += minlen;
        }

        if (*pmatch  < *pcur) {
            *l = cmp_pos;
            cmp_pos = *(l = &tlast[1]);
            lenl = match_len;
        } else {
            *r = cmp_pos;
            cmp_pos = *(r = &tlast[0]);
            lenr = match_len;
        }
    }
    bt_start_[curhash4] = wpos;
    }
        if (i + 128 < len) {i += 3; continue;}

        uint32_t curhash6 = HASH6(wnd_[pos]);
        uint32_t *ht6 = &mf_ht6_[curhash6 * ht6_width_];

        if (lasthash6 != curhash6) {
            for(uint32_t j = ht6_width_ - 1; j > 0; j--)
                ht6[j] = ht6[j-1];
        }

        lasthash6 = curhash6;
        ht6[0] = v;
    }

    wnd_curpos_ += len;
}

void LZ::find_match(uint32_t *rep_dist, uint32_t bytes_left, MFUnits *units, uint32_t wpos, bool mffast) 
{
    if (units->mftried)
        return;

    units->mftried = true;
    units->cnt = 1;
    units->bestidx = 0;
    units->u[0].c = wnd_[wpos];
    uint32_t curhash2 = 0;
    uint32_t curhash3 = HASH3(wnd_[wpos]);
    uint32_t curhash6 = HASH6(wnd_[wpos]);
    uint32_t v = (wpos | ((wnd_[wpos] & 0x0E) << 28));
    uint32_t minlen = 4;
    uint32_t *ht6 = &mf_ht6_[curhash6 * ht6_width_];

    for(uint32_t i = 0; i < 4; i++) {
        uint32_t cmp_pos = wpos > rep_dist[i] ? wpos - rep_dist[i] : 
            wpos + wnd_size_ - rep_dist[i];
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) continue;

        uint32_t len_limit = MIN(bytes_left, wnd_size_ - cmp_pos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }

        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len == 1 && i == 0) {
            units->u[units->cnt].type = MFUnit::REP0LEN1_MATCH;
            units->u[units->cnt].len = 1;
            units->u[units->cnt].rep_idx = 0; // for encoding choice
            units->cnt++;
        } else if (match_len >= MIN_LEN_REP) {
            units->u[units->cnt].type = MFUnit::REPDIST_MATCH;
            units->u[units->cnt].rep_idx = i;
            units->u[units->cnt].len = match_len;
            units->cnt++;
            if (match_len >= GOOD_LEN_REP)
                goto MF_INSERT;
        }
    }

    if (0)
    {
    uint32_t curhash4 = HASH6(wnd_[wpos]);
    uint32_t cmp_pos = bt_start_[curhash4];
    uint32_t *l = &bt_nodes_[wpos * 2], *r = &bt_nodes_[wpos * 2 + 1];
    uint32_t lenl = 0, lenr = 0;

    for(uint32_t i = 0; i < 16; i++) {
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) break;
        //if (dist < last_dist) break;
        //last_dist = dist;

        uint32_t *tlast = &bt_nodes_[cmp_pos * 2];
        uint32_t len_limit = MIN(bytes_left, wnd_size_ - cmp_pos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        uint32_t minlen = MIN(lenl, lenr);
        uint32_t match_len = 0;
        if (pmatch[minlen] == pcur[minlen]) {
            while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
                pmatch += 4; pcur += 4; }
            if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
                pmatch += 2; pcur += 2; }
            if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
            match_len = (pcur - wnd_) - wpos;
            if (match_len >= 4) {
                uint32_t dist = wpos > cmp_pos? wpos - cmp_pos : wpos + wnd_size_ - cmp_pos;
                units->u[units->cnt].type = MFUnit::NORMAL_MATCH;
                units->u[units->cnt].dist = dist;
                units->u[units->cnt].len = match_len;
                units->cnt++;
                if (match_len >= 24) {
                    *l = tlast[0];
                    *r = tlast[1];
                    break;
                }
            }
        } else {
            pmatch += minlen;
            pcur += minlen;
        }
        if (*pmatch < *pcur) {
            *l = cmp_pos;
            cmp_pos = *(l = &tlast[1]);
            lenl = match_len;
        } else {
            *r = cmp_pos;
            cmp_pos = *(r = &tlast[0]);
            lenr = match_len;
        }
    }
    bt_start_[curhash4] = wpos;
    }

    for(uint32_t i = 0; i < ht6_width_; i++) {
        if (units->cnt >= units->limit) break;
        uint32_t cmp_pos = ht6[i] & 0x1FFFFFFF;
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) continue;
        if ((ht6[i] >> 29u) != ((wnd_[wpos] & 0x0Eu) >> 1u)) continue;

        uint32_t len_limit = MIN(bytes_left, wnd_size_ - cmp_pos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        if (len_limit <= minlen || pcur[minlen] != pmatch[minlen]) continue;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }

        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len >= MIN_LEN_HT6) {
            uint32_t dist = wpos > cmp_pos? wpos - cmp_pos : wpos + wnd_size_ - cmp_pos;
            units->u[units->cnt].type = MFUnit::NORMAL_MATCH;
            units->u[units->cnt].dist = dist;
            units->u[units->cnt].len = match_len;
            units->cnt++;
            minlen = match_len;
            if (match_len >= GOOD_LEN_HT6)
                goto MF_INSERT;
        }
    }

    minlen -= 2;
    for(uint32_t i = 0; i < 1; i++) {
        if (units->cnt >= units->limit) break;
        if (mf_ht3_[curhash3] == ht6[0]) continue;
        uint32_t cmp_pos = mf_ht3_[curhash3] & 0x1FFFFFFF;
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) continue;
        if ((mf_ht3_[curhash3] >> 29) != ((wnd_[wpos] & 0x0E) >> 1)) continue;

        uint32_t len_limit = MIN(bytes_left, wnd_size_ - cmp_pos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        if (len_limit <= minlen || pcur[minlen] != pmatch[minlen]) continue;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }

        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len >= MIN_LEN_HT3) {
            uint32_t dist = wpos > cmp_pos? wpos - cmp_pos : wpos + wnd_size_ - cmp_pos;
            units->u[units->cnt].type = MFUnit::NORMAL_MATCH;
            units->u[units->cnt].dist = dist;
            units->u[units->cnt].len = match_len;
            units->cnt++;
            minlen = match_len;
            if (match_len >= GOOD_LEN_HT6)
                goto MF_INSERT;
        }
    }

    if (mffast)
        goto MF_QINSERT;

    curhash2 = HASH2(wnd_[wpos]);
    minlen -= 2;
    for(uint32_t i = 0; i < 1; i++) {
        if (units->cnt >= units->limit) break;
        if (mf_ht2_[curhash2] == mf_ht3_[curhash3]) continue;
        uint32_t cmp_pos = mf_ht2_[curhash2] & 0x1FFFFFFF;
        if (cmp_pos >= wpos && cmp_pos < curblock_endpos) continue;

        uint32_t len_limit = MIN(bytes_left, wnd_size_ - cmp_pos);
        uint8_t *pcur = &wnd_[wpos];
        uint8_t *pmatch = &wnd_[cmp_pos];
        uint8_t *pend = &wnd_[cmp_pos + len_limit];

        if (len_limit <= minlen || pcur[minlen] != pmatch[minlen]) continue;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }

        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len >= MIN_LEN_HT2) {
            uint32_t dist = wpos > cmp_pos? wpos - cmp_pos : wpos + wnd_size_ - cmp_pos;
            units->u[units->cnt].type = MFUnit::NORMAL_MATCH;
            units->u[units->cnt].dist = dist;
            units->u[units->cnt].len = match_len;
            units->cnt++;
            if (match_len >= GOOD_LEN_HT6)
                goto MF_INSERT;
        }
    }

MF_INSERT:
    mf_ht2_[curhash2] = v;

MF_QINSERT:
    mf_ht3_[curhash3] = v;
    for(uint32_t j = ht6_width_ - 1; j > 0; j--)
        ht6[j] = ht6[j-1];
    ht6[0] = v;
}

uint32_t LZ::encode_unit(LZ::MFUnit *unit)
{
    uint32_t dist;
    switch (unit->type) {
        case MFUnit::LITERAL:
            model_->EncodeLiteral(unit->c);//,wnd_curpos_-1
            break;
        case MFUnit::REP0LEN1_MATCH:
            model_->EncodeRep0Len1();
            break;
        case MFUnit::REPDIST_MATCH:
            model_->EncodeRepDistMatch(unit->rep_idx, unit->len - 2);
            dist = rep_dist_[unit->rep_idx];
            switch (unit->rep_idx) {
                case 3:
                    rep_dist_[3] = rep_dist_[2];
                case 2:
                    rep_dist_[2] = rep_dist_[1];
                case 1:
                    rep_dist_[1] = rep_dist_[0];
                case 0:
                    rep_dist_[0] = dist;
                break;
            }
            break;
        case MFUnit::NORMAL_MATCH:
            model_->EncodeMatch(unit->dist - 1, unit->len - 2);
            rep_dist_[3] = rep_dist_[2];
            rep_dist_[2] = rep_dist_[1];
            rep_dist_[1] = rep_dist_[0];
            rep_dist_[0] = unit->dist;
            break;
        default:
            break;
    }
    return unit->len;
}

void LZ::get_best_match(MFUnits *units) 
{
    units->bestidx = 0;

    for(uint32_t i = 1; i < units->cnt; i++) {
        MFUnit *unit = &units->u[i];
        MFUnit *bunit = &units->u[units->bestidx];
        if (unit->type == MFUnit::NORMAL_MATCH) {
            if ((unit->len == 2 && unit->dist >= 64)
             || (unit->len == 3 && unit->dist >= 64 * 16)
             || (unit->len <= 6 && unit->dist >= 64 *(1 << (4 * (unit->len - 2))))
             )
                continue;
        }
        if (bunit->type == MFUnit::LITERAL
        || (unit->len > bunit->len + 3)
        || (unit->len == bunit->len + 3 && (unit->dist >> 13) < bunit->dist)
        || (unit->len == bunit->len + 2 && (unit->dist >> 9) < bunit->dist)
        || (unit->len == bunit->len + 1 && (unit->dist >> 5) < bunit->dist)
        || (unit->len == bunit->len && bunit->type != MFUnit::REPDIST_MATCH && unit->dist < bunit->dist)
        || (unit->len >= 2 && unit->len == bunit->len - 1 && unit->dist < (bunit->dist >> 5)) 
        || (unit->len >= 2 && unit->len == bunit->len - 2 && unit->dist < (bunit->dist >> 9)) 
                ) {
            units->bestidx = i;
        }
    }
}

int LZ::match_second_better(MFUnits *units1, MFUnits *units2)
{
    if (units2->bestidx == 0)
        return 0;

    MFUnit *u1 = &units1->u[units1->bestidx];
    MFUnit *u2 = &units2->u[units2->bestidx];
    if (u2->len > u1->len + 3 
            || (u2->len == u1->len + 3 && ((u2->dist >> 13) < u1->dist))
            || (u2->len == u1->len + 2 && ((u2->dist >> 9) < u1->dist))
            || (u2->len == u1->len + 1 && ((u2->dist >> 5) < u1->dist))
            || (u2->len == u1->len && u1->type != MFUnit::REPDIST_MATCH && u2->dist < u1->dist)
            || (u2->len == u1->len - 1 && (u2->dist < (u1->dist >> 5)))
            || (u2->type == MFUnit::NORMAL_MATCH && u2->len == u1->len - 2 && (u2->dist < (u1->dist >> 9)))
            )
        return 1;
    else
        return 0;
}

int LZ::compress_normal(uint32_t size, bool lazy, bool mffast)
{
    // mfpos1 is always current pos in lazy parser
    // mfpos2 is always the next pos in lazy parser
    uint32_t mfpos1 = 0;
    uint32_t progress;

    mfunits_[0].Clear();
    mfunits_[1].Clear();
    curblock_endpos = wnd_curpos_ + size;

    for(progress = 0; progress < size; ) {
        find_match(rep_dist_, size - progress, mfunits_ + mfpos1, wnd_curpos_, mffast);
        get_best_match(mfunits_ + mfpos1);
        if (mfunits_[mfpos1].bestidx == 0) {
            encode_unit(&mfunits_[mfpos1].u[mfunits_[mfpos1].bestidx]);
            mfunits_[mfpos1].Clear();
            progress++;
            slide_pos(1, mffast);
            continue;
        }

        if (!lazy || mfunits_[mfpos1].u[mfunits_[mfpos1].bestidx].len > 24) {
            uint32_t slen = encode_unit(&mfunits_[mfpos1].u[mfunits_[mfpos1].bestidx]);
            mfunits_[mfpos1].Clear();
            progress += slen;
            slide_pos(slen, mffast);
            model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
            continue;
        }

        // lazy parser
        uint32_t mfpos2 = (mfpos1 + 1) % 2;
        find_match(rep_dist_, size - progress - 1, mfunits_ + mfpos2, wnd_curpos_ + 1, mffast);
        get_best_match(mfunits_ + mfpos2);
        if (match_second_better(mfunits_ + mfpos1, mfunits_ + mfpos2)) {
            // choose literal output
            mfunits_[mfpos1].bestidx = 0;
            encode_unit(&mfunits_[mfpos1].u[mfunits_[mfpos1].bestidx]);
            progress += 1;
            slide_pos(1, mffast);
            mfunits_[mfpos1].Clear();
            mfpos1 = mfpos2;
        } else {
            uint32_t slen = encode_unit(&mfunits_[mfpos1].u[mfunits_[mfpos1].bestidx]);
            progress += slen;
            slide_pos(slen, mffast);
            model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
            mfunits_[mfpos1].Clear();
            mfunits_[mfpos2].Clear();
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
    curblock_endpos = wnd_curpos_ + size;

    for(uint32_t i = 0; i < size; ) {
        mfunits_[0].Clear();
        find_match(rep_dist_, size - i, &mfunits_[0], wnd_curpos_, false);
        get_best_match(&mfunits_[0]);
        MFUnit *u = &mfunits_[0].u[mfunits_[0].bestidx];
        if (u->type == MFUnit::LITERAL) {
            model_->EncodeLiteral(u->c);
            i++;
            slide_pos(1, false);
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
                    if (apunits_[apcur].dist == 0) {
                        apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[0];
                        apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[1];
                        apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[2];
                        apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                        apunits_[apcur].state = (apunits_[l].state * 4) & 0x3F;
                    } else if (apunits_[apcur].dist <= 4) {
                        uint32_t len = apcur - l;
                        if (len == 1 && apunits_[apcur].dist == 1) {
                            apunits_[apcur].state = (apunits_[l].state * 4 + 2) & 0x3F;
                            apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[0];
                            apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[1];
                            apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[2];
                            apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                        } else {
                            apunits_[apcur].state = (apunits_[l].state * 4 + 3) & 0x3F;
                            if (apunits_[apcur].dist == 1) {
                                apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[0];
                                apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[1];
                                apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[2];
                                apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                            } else if (apunits_[apcur].dist == 2) {
                                apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[1];
                                apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[0];
                                apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[2];
                                apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                            } else if (apunits_[apcur].dist == 3) {
                                apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[2];
                                apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[0];
                                apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[1];
                                apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[3];
                            } else {
                                apunits_[apcur].rep_dist[0] = apunits_[l].rep_dist[3];
                                apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[0];
                                apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[1];
                                apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[2];
                            }
                       }
                    } else {
                        apunits_[apcur].state = (apunits_[l].state * 4 + 1) & 0x3F;
                        apunits_[apcur].rep_dist[0] = apunits_[apcur].dist- 4;
                        apunits_[apcur].rep_dist[1] = apunits_[l].rep_dist[0];
                        apunits_[apcur].rep_dist[2] = apunits_[l].rep_dist[1];
                        apunits_[apcur].rep_dist[3] = apunits_[l].rep_dist[2];
                    }

                    mfunits_[0].Clear();
                    find_match(apunits_[apcur].rep_dist, size - i - apcur, &mfunits_[0], wnd_curpos_, false);
                    get_best_match(&mfunits_[0]);
                    u = &mfunits_[0].u[mfunits_[0].bestidx];
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

                uint32_t lit_ctx = wnd_curpos_? wnd_[wnd_curpos_ - 1] : 0;
                uint32_t cprice = model_->GetLiteralPrice(apunits_[apcur].state, lit_ctx, wnd_[wnd_curpos_]);
                if (cprice + apunits_[apcur].price < apunits_[apcur + 1].price) {
                    apunits_[apcur + 1].dist = 0;
                    apunits_[apcur + 1].back_pos = apcur;
                    apunits_[apcur + 1].price = cprice + apunits_[apcur].price;
                }

                if (u->type == MFUnit::REP0LEN1_MATCH || (u->type == MFUnit::REPDIST_MATCH && u->rep_idx == 0)) {
                    cprice = model_->GetRep0Len1Price(apunits_[apcur].state);
                    if (cprice + apunits_[apcur].price < apunits_[apcur + 1].price) {
                        apunits_[apcur + 1].dist = 1;
                        apunits_[apcur + 1].back_pos = apcur;
                        apunits_[apcur + 1].price = cprice + apunits_[apcur].price;
                    }
                }

                if (u->len > 24 || (u->len > 1 && u->len + apcur >= aplimit)) {
                    ap_backward(apcur);
                    rep_dist_[0] = apunits_[apcur].rep_dist[0];
                    rep_dist_[1] = apunits_[apcur].rep_dist[1];
                    rep_dist_[2] = apunits_[apcur].rep_dist[2];
                    rep_dist_[3] = apunits_[apcur].rep_dist[3];
                    encode_unit(u);
                    i += apcur + u->len;
                    slide_pos(u->len, false);
                    model_->SetLiteralCtx(wnd_[wnd_curpos_ - 1]);
                    break;
                }
                    
                uint32_t len = u->len;
                while(len > 1) {

                    while (apcur + len >= apend) 
                        apunits_[apend++].price = 0xFFFFFFFF;

                if ((len == 2 && u->dist >= 64)
                 || (len == 3 && u->dist >= 64 * 16)
                 || (len <= 6 && u->dist >= 64 *(1 << (4 * (len - 2))))
                 )
                    break;


                    uint32_t tmpdist = u->dist;
                    if (u->type == MFUnit::REPDIST_MATCH) {
                        cprice = model_->GetRepDistMatchPrice(apunits_[apcur].state, u->dist, len - 2);
                        tmpdist++;
                    } else {
                        cprice = model_->GetMatchPrice(apunits_[apcur].state, u->dist - 1, len - 2);
                        tmpdist += 4;
                    }

                    if (cprice + apunits_[apcur].price < apunits_[apcur + len].price) {
                        apunits_[apcur + len].dist = tmpdist;
                        apunits_[apcur + len].back_pos = apcur;
                        apunits_[apcur + len].price = cprice + apunits_[apcur].price;
                    }
                    len--;
                }
                apcur++;
                slide_pos(1, false);
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


