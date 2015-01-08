#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <csc_coder.h>
#include <csc_model.h>
#include <csc_lz.h>


int LZ::Init(uint32_t WindowSize,uint32_t operation)
{
    return Init(WindowSize,operation,22,1);
}

int LZ::Init(uint32_t WindowSize,uint32_t operation,uint32_t hashBits,uint32_t hashWidth)
{
    m_operation=operation;
    wnd_Size=WindowSize;

    if(wnd_Size<32*KB)
        wnd_Size=32*KB;
    if(wnd_Size>MaxDictSize)
        wnd_Size=MaxDictSize;


    wnd_=(uint8_t*)malloc(WindowSize+8);
    if (wnd_==NULL)
        goto FREE_ON_ERROR;

    if (operation==ENCODE)
    {
            mf_ht2_=(uint32_t*)malloc(sizeof(uint32_t)*64*KB);
            if (mf_ht2_==NULL)
                goto FREE_ON_ERROR;
            mf_ht3_=(uint32_t*)malloc(sizeof(uint32_t)*64*KB);
            if (mf_ht3_==NULL)
                goto FREE_ON_ERROR;

        h6_bits_=hashBits;
        h6_width_=hashWidth;

        mf_ht6raw_=(uint32_t*)malloc(sizeof(uint32_t)*h6_width_*(1<<h6_bits_)+256);
        if (mf_ht6raw_==NULL)
            goto FREE_ON_ERROR;
        mf_ht6_=(uint32_t*)(mf_ht6raw_+(64-(uint32_t)((uint64_t)mf_ht6raw_)%64));

        parser=(ParserAtom*)malloc(sizeof(ParserAtom)*(MinBlockSize+1));
        matchList=(MatchAtom*)malloc(sizeof(MatchAtom)*(2+(MinBlockSize<<3)));
        if (parser==NULL || matchList==NULL)
            goto FREE_ON_ERROR;
    }
    Reset();
    //memset(wnd_,0,wnd_Size);
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

    parse_[0].total=
        parse_[1].total=1;
    parse_[0].candidate[0].type=
        parse_[1].candidate[0].type=P_LITERAL;
    parse_[0].candidate[0].price=
        parse_[1].candidate[0].price=0;
    parse_[0].candidate[0].len=
        parse_[1].candidate[0].len=1;

    passedcnt_=1024;
    litcount_=400;
    model_->Reset();
}


void LZ::Destroy(void)
{
    model_->Destroy();
    if (m_operation==ENCODE)
    {
        SAFEFREE(mf_ht2_);
        SAFEFREE(mf_ht3_);
        SAFEFREE(mf_ht6raw_);
        SAFEFREE(parser);
        SAFEFREE(matchList);
    }
    SAFEFREE(wnd_);
}




int LZ::Decode(uint8_t *dst,uint32_t *size,uint32_t sizeLimit)
{
    uint32_t i=0,j,k,v;
    uint32_t a,b,cpyPos;
    uint32_t dstInWndPos;
    uint8_t *wndDst,*wndCpy;
    uint32_t lastCopySize;
    bool notEnd;
    //PackType x;

    notEnd=true;
    dstInWndPos=wnd_curpos_;
    lastCopySize=0;

    while(notEnd && i<=sizeLimit)
    {
        v=0;
        DecodeBit(model_->coder_,v,model_->p_state_[model_->state_*3+0]);
        if (v==0) 
        {
            wnd_[wnd_curpos_++]=model_->DecodeLiteral();
            i++;
        }
        else
        {
            v=0;
            DecodeBit(model_->coder_,v,model_->p_state_[model_->state_*3+1]);
            if (v==1) 
            {
                model_->DecodeMatch(a,b);
                if (b==2&&a==2047) 
                {
                    //printf("\n%u %u\n",wnd_curpos_,i);
                    notEnd=false;
                    break;
                }
                b+=1;
                rep_dist_[3]=rep_dist_[2];
                rep_dist_[2]=rep_dist_[1];
                rep_dist_[1]=rep_dist_[0];
                rep_dist_[0]=a;
                cpyPos=wnd_curpos_>=a?wnd_curpos_-a:wnd_curpos_+wnd_Size-a;
                if (cpyPos>wnd_Size || cpyPos+b>wnd_Size || b+i>sizeLimit)
                {
                    return DECODE_ERROR;
                }
                wndDst=wnd_+wnd_curpos_;
                wndCpy=wnd_+cpyPos;
                i+=b;
                wnd_curpos_+=b;
                while(b--) *wndDst++=*wndCpy++;
                model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            }
            else
            {
                v=0;
                DecodeBit(model_->coder_,v,model_->p_state_[model_->state_*3+2]);
                if (v==0) 
                {
                    model_->Decode1BMatch();
                    cpyPos=wnd_curpos_>rep_dist_[0]?
                        wnd_curpos_-rep_dist_[0]:wnd_curpos_+wnd_Size-rep_dist_[0];
                    wnd_[wnd_curpos_++]=wnd_[cpyPos];
                    i++;
                    model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);

                }
                else 
                {
                    model_->DecodeRepDistMatch(a,b);
                    b+=1;
                    if (b+i>sizeLimit) 
                    {
                        return DECODE_ERROR;
                    }
                    k=rep_dist_[a];
                    for(j=a;j>0;j--) 
                        rep_dist_[j]=rep_dist_[j-1];
                    rep_dist_[0]=k;
                    cpyPos=wnd_curpos_>k?wnd_curpos_-k:wnd_curpos_+wnd_Size-k;
                    if (cpyPos+b>wnd_Size)
                    {
                        return DECODE_ERROR;
                    }
                    wndDst=wnd_+wnd_curpos_;
                    wndCpy=wnd_+cpyPos;
                    i+=b;
                    wnd_curpos_+=b;
                    while(b--) *wndDst++=*wndCpy++;
                    model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
                }
            }
        }


        if (wnd_curpos_>=wnd_Size) 
        {
            wnd_curpos_=0;
            memcpy(dst+lastCopySize,wnd_+dstInWndPos,i-lastCopySize);
            dstInWndPos=0;
            lastCopySize=i;
        }
    }
    *size=i;
    memcpy(dst+lastCopySize,wnd_+dstInWndPos,i-lastCopySize);
    return NO_ERROR;
}

void LZ::EncodeNormal(uint8_t *src,uint32_t size,uint32_t lzMode)
{
    uint32_t progress=0;
    uint32_t currBlockSize;

    while (progress<size)
    {
        currBlockSize=MIN(wnd_Size-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);
        if (lzMode==1)
            LZMinBlock(currBlockSize);
        else if (lzMode==2)
            LZMinBlockNew(currBlockSize,2,3,100000);
        else if (lzMode==3)
            LZMinBlockNew(currBlockSize,10,6,100000);
        else if (lzMode==0)
            LZMinBlockFast(currBlockSize);
        else if (lzMode==4)
            LZMinBlockNew(currBlockSize,24,16,100000);
            

        if (wnd_curpos_>=wnd_Size) wnd_curpos_=0;
        progress+=currBlockSize;
    }
    model_->EncodeMatch(2047,2);

    return;
}


uint32_t LZ::CheckDuplicate(uint8_t *src,uint32_t size,uint32_t type)
{
    uint32_t lastWndPos=wnd_curpos_;
    uint32_t progress=0;
    uint32_t currBlockSize;

    while (progress<size)
    {
        currBlockSize=MIN(wnd_Size-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);
        if (LZMinBlockSkip(currBlockSize,type)==DT_NORMAL)
        {
            wnd_curpos_=lastWndPos;
            return DT_NORMAL;
        }

        wnd_curpos_+=currBlockSize;
        if (wnd_curpos_>=wnd_Size) wnd_curpos_=0;
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
        currBlockSize=MIN(wnd_Size-wnd_curpos_,size-progress);
        currBlockSize=MIN(currBlockSize,MinBlockSize);

        memcpy(wnd_+wnd_curpos_,src+progress,currBlockSize);

        wnd_curpos_+=currBlockSize;
        if (wnd_curpos_>=wnd_Size) wnd_curpos_=0;
        progress+=currBlockSize;
    }
    return;
}

inline void LZ::InsertHash(uint32_t currHash3, uint32_t currHash6, uint32_t len)
{
    uint32_t    *HT6=&mf_ht6_[currHash6*h6_width_];
    uint32_t    *HT3=&mf_ht3_[currHash3];

    uint32_t lastCurrHash6;
    uint32_t i;

    
    HT3[0]=CURPOS;
    mf_ht2_[*(uint16_t*)(wnd_+wnd_curpos_)]=CURPOS;

    for(i=h6_width_-1;i>0;i--)
        HT6[i]=HT6[i-1];

    HT6[0]=CURPOS;
    len--;
    wnd_curpos_++;

    lastCurrHash6=currHash6;

    while(len>0)
    {
        currHash6=HASH6(wnd_[wnd_curpos_]);
        currHash3=HASH3(wnd_[wnd_curpos_]);
        HT6=&mf_ht6_[currHash6*h6_width_];
        HT3=&mf_ht3_[currHash3];

        mf_ht2_[*(uint16_t*)(wnd_+wnd_curpos_)]=CURPOS;

        if (lastCurrHash6!=currHash6)
        {
            for(i=h6_width_-1;i>0;i--)
                HT6[i]=HT6[i-1];
        }
        HT6[0]=CURPOS;
        HT3[0]=CURPOS;

        if (len>256)
        {
            len-=4;
            wnd_curpos_+=4;
        }
        len--;
        wnd_curpos_++;    
        lastCurrHash6=currHash6;
    }
}


inline void LZ::InsertHashFast(uint32_t currHash6, uint32_t len)
{
    uint32_t    *HT6=&mf_ht6_[currHash6*h6_width_];

    HT6[0]=CURPOS;
    len--;
    wnd_curpos_++;

    while(len>0)
    {
        currHash6=HASH6(wnd_[wnd_curpos_]);
        HT6=&mf_ht6_[currHash6*h6_width_];
        HT6[0]=CURPOS;
        if (len>256)
        {
            len-=4;
            wnd_curpos_+=4;
        }
        len--;
        wnd_curpos_++;    
    }
}


uint32_t LZ::LZMinBlockSkip(uint32_t size,uint32_t type)
{

    uint32_t currHash6,currHash3;
    uint32_t i,j,cmpPos1,cmpPos2,cmpLen,remainLen,remainLen2;
    //uint32_t matchDist;
    uint32_t minMatchLen;
    uint32_t *HT6;


    const uint32_t currBlockEndPos=wnd_curpos_+size;
    const uint32_t currBlockStartPos=wnd_curpos_;

    minMatchLen=70;
    remainLen=size;

    for(i=0;i<4;i++)
    {
        cmpPos1=wnd_curpos_>rep_dist_[i]?wnd_curpos_-rep_dist_[i]:wnd_curpos_+wnd_Size-rep_dist_[i];
        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size) )
        {
            cmpPos2=wnd_curpos_;
            remainLen2=MIN(remainLen,wnd_Size-cmpPos1);
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
        currHash6=HASH6(wnd_[wnd_curpos_+i]);
        remainLen=size-i;


        cmpPos1=mf_ht6_[currHash6*h6_width_]&0x1FFFFFFF;

        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) 
            && (cmpPos1<wnd_Size) 
            && ((mf_ht6_[currHash6*h6_width_]>>29)==((wnd_[wnd_curpos_+i]&0x0E)>>1)))
        {
            //matchDist=wnd_curpos_+i>cmpPos1?
            //    wnd_curpos_+i-cmpPos1:wnd_curpos_+i+wnd_Size-cmpPos1;
            cmpPos2=wnd_curpos_+i;
            remainLen2=MIN(remainLen,wnd_Size-cmpPos1);
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
            currHash6=HASH6(wnd_[currBlockStartPos+j]);
            HT6=&mf_ht6_[currHash6*h6_width_];
            for(i=h6_width_-1;i>0;i--)
                HT6[i]=HT6[i-1];
            HT6[0]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
        }
    }
    else
    {
        for (j=0;j<size;j+=2)
        {
            //currHash3=HASH3(wnd_[currBlockStartPos+j]);
            currHash6=HASH6(wnd_[currBlockStartPos+j]);
            HT6=&mf_ht6_[currHash6*h6_width_];
            //for(i=h6_width_-1;i>0;i--)
            //    HT6[i]=HT6[i-1];
            HT6[0]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
            //mf_ht3_[currHash3]=(currBlockStartPos+j)|((wnd_[(currBlockStartPos+j)]&0x0E)<<28);
        }
    }

    return DT_SKIP;
}



int LZ::LZMinBlock(uint32_t size)
{
    uint32_t currHash6,currHash3,currHash2;
    uint32_t progress=0;

    uint32_t matchDist,lastHashFDist;
    uint32_t minMatchLen;
    uint32_t *HT6,*HT3,*HT2;

    uint32_t bestCandidate0,bestCandidate1;
    bool outPrevMatch;
    uint8_t outPrevChar;
    uint32_t lastDist;
    uint32_t distExtra;

    uint32_t i,j,cmpPos1,cmpPos2,cmpLen,remainLen,remainLen2;
    bool gotMatch;

    currBlockEndPos=wnd_curpos_+size;

    parse_pos_=0;

    while(progress<size)
    {
        currHash6=HASH6(wnd_[wnd_curpos_]);
        currHash3=HASH3(wnd_[wnd_curpos_]);
        currHash2=HASH2(wnd_[wnd_curpos_]);

        parse_[parse_pos_].total=1;
        parse_[parse_pos_].candidate[0].price=0;

        lastHashFDist=0xFFFFFFFF;
        gotMatch=false;

        if (parse_pos_==0)
        {
            minMatchLen=1;
            remainLen=size-progress;
        }
        else
        {
            minMatchLen=parse_[0].candidate[bestCandidate0].len;
            remainLen=size-progress;
            if (remainLen>0)
                remainLen--;

            if (parse_[0].candidate[bestCandidate0].type!=P_MATCH&&minMatchLen>5)
                minMatchLen-=1;
        }

        if (minMatchLen<1)
            minMatchLen=1;

        if (++passedcnt_==2048) 
        {
            passedcnt_>>=1;
            litcount_>>=1;
        }

        if (litcount_>0.9*passedcnt_) goto HASH6SEARCH;

        if (remainLen>=MIN_LEN_HT2)
        for(i=0;i<4;i++)
        {
            cmpPos1=wnd_curpos_>rep_dist_[i]?wnd_curpos_-rep_dist_[i]:wnd_curpos_+wnd_Size-rep_dist_[i];
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size))
            {
                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);

                if (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<=minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
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
                        minMatchLen=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_REPDIST_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8+3;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=i;
                        parse_[parse_pos_].total++;
                        if (minMatchLen>GOOD_LEN_REP)
                        {
                            gotMatch=true;
                            break;
                        }
                        if (rep_dist_[i]<lastHashFDist)
                            lastHashFDist=rep_dist_[i];
                    }
            }
        }

        if (gotMatch) goto DETERMINE;

        cmpPos1=wnd_curpos_>rep_dist_[0]?wnd_curpos_-rep_dist_[0]:wnd_curpos_+wnd_Size-rep_dist_[0];

        if ((minMatchLen<3) && (cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos)
            && (cmpPos1<wnd_Size) && (wnd_[cmpPos1]==wnd_[wnd_curpos_]) )
        {
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_1BYTE_MATCH;
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=1;
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=48;
            parse_[parse_pos_].total++;
        }


HASH6SEARCH:

        HT6=&mf_ht6_[currHash6*h6_width_];
        lastDist=1;
        distExtra=0;
        if (remainLen>=MIN_LEN_HT6)
        for(i=0;i<h6_width_;i++)
        {
            cmpPos1=HT6[i]&0x1FFFFFFF;
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size) && ((HT6[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
            {
                matchDist=wnd_curpos_>cmpPos1?wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;
                if (matchDist<lastDist) break;
                lastDist=matchDist;

                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);

                if (*(uint32_t*)&wnd_[cmpPos1]!=*(uint32_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<=minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
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

                    if    ( (cmpLen>minMatchLen) &&
                        (
                           ((cmpLen==MIN_LEN_HT6)&&(matchDist<(1<<19)))
                        || ((cmpLen==MIN_LEN_HT6+1)&&(matchDist<(1<<22)))
                        || (cmpLen>MIN_LEN_HT6+1)
                        )
                        )
                    { 
                        uint32_t countExtra=(matchDist>>7);
                        distExtra=0;
                        while(countExtra>0)
                        {
                            countExtra>>=2;
                            distExtra+=3;
                        }
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=matchDist;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8-14-distExtra;
                        parse_[parse_pos_].total++;
                        minMatchLen=cmpLen;
                        if (cmpLen>GOOD_LEN_HT6)
                        {
                            gotMatch=true;
                            break;
                        }
                        if (matchDist<lastHashFDist)
                            lastHashFDist=matchDist;
                    }
            }
        }

        if (parse_[0].total>1) goto DETERMINE;
        if (litcount_>0.8*passedcnt_) goto DETERMINE;

        minMatchLen=1;
        HT3=&mf_ht3_[currHash3];
        //lastDist=1;

        if (remainLen>=MIN_LEN_HT3)
        for(i=0;i<1;i++)
        {
            cmpPos1=HT3[i]&0x1FFFFFFF;
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size) && ((HT3[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
            {
                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);
                matchDist=wnd_curpos_>cmpPos1?wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;
                //if (matchDist<lastDist) break;
                if (matchDist>=lastHashFDist)
                    break;
                lastDist=matchDist;

                if (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<=minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
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


                    if ( (cmpLen>minMatchLen) && 
                        (
                           ((cmpLen==MIN_LEN_HT2+1)&&(matchDist<(1<<9)))
                        || ((cmpLen==MIN_LEN_HT2+2)&&(matchDist<(1<<12)))
                        || ((cmpLen==MIN_LEN_HT2+3)&&(matchDist<(1<<16)))
                        //|| ((cmpLen==MIN_LEN_HT6)&&(matchDist<(1<<18))

                        )
                        )
                    {
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=matchDist;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8-13+4*(matchDist<64);
                        parse_[parse_pos_].total++;
                        minMatchLen=cmpLen;
                        lastHashFDist=matchDist;
                    }
            }
        }

        //if (parse_[0].total>1) goto DETERMINE;
        if (litcount_>0.8*passedcnt_) goto DETERMINE;


        minMatchLen=1;
        HT2=&mf_ht2_[currHash2];
        //lastDist=1;
        if (remainLen>=MIN_LEN_HT2)
        for(i=0;i<1;i++)
        {
            cmpPos1=HT2[i]&0x1FFFFFFF;
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size) && ((HT2[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
            {
                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);
                matchDist=wnd_curpos_>cmpPos1?wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;
                //if (matchDist<lastDist) break;
                //lastDist=matchDist;
                if (matchDist>=lastHashFDist)
                    break;

                if (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<=minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
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


                    if ( (cmpLen>minMatchLen) &&
                        ((
                        (cmpLen==MIN_LEN_HT2)&&(matchDist<(1<<6)))
                        )
                        )
                    {
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=matchDist;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8-10;
                        parse_[parse_pos_].total++;
                        minMatchLen=cmpLen;
                        lastHashFDist=matchDist;
                    }
            }
        }




DETERMINE:

        if (parse_[0].total==1) litcount_++;

        outPrevMatch=false;


        if (parse_pos_==0)
        {
            bestCandidate0=0;
            for (i=1;i<parse_[0].total;i++)
            {
                if (parse_[0].candidate[i].price>parse_[0].candidate[bestCandidate0].price)
                    bestCandidate0=i;
            }


            if (parse_[0].candidate[bestCandidate0].len+progress>=size)
            {
                if (parse_[0].candidate[bestCandidate0].len+progress>size)
                {
                    parse_[0].candidate[bestCandidate0].len=size-progress;
                }
                goto OUTCANDIDATE;
            }

            if (parse_[0].candidate[bestCandidate0].price>230)
            {
                goto OUTCANDIDATE;
            }

            if (bestCandidate0>0)
            {
                outPrevChar=wnd_[wnd_curpos_];
                InsertHash(currHash3,currHash6,1);
                parse_pos_=1;
                continue;
            }
            else
                goto OUTCANDIDATE;
        }
        else
        {
            bestCandidate1=0;

            for (i=1;i<parse_[1].total;i++)
            {
                if (parse_[1].candidate[i].price>parse_[1].candidate[bestCandidate1].price)
                    bestCandidate1=i;
            }

            if (parse_[1].candidate[bestCandidate1].len+progress+1>=size)
            {
                model_->EncodeLiteral(outPrevChar);//,wnd_curpos_-1
                progress++;

                if (parse_[1].candidate[bestCandidate1].len+progress>size)
                {
                    parse_[1].candidate[bestCandidate1].len=size-progress;
                }
                parse_[0]=parse_[1];
                bestCandidate0=bestCandidate1;
                parse_pos_=0;
                goto OUTCANDIDATE;
            }

            if (parse_[1].candidate[bestCandidate1].price>230)
            {
                model_->EncodeLiteral(outPrevChar);//,wnd_curpos_-1
                progress++;
                parse_[0]=parse_[1];
                bestCandidate0=bestCandidate1;
                parse_pos_=0;
                goto OUTCANDIDATE;
            }

            if (parse_[1].candidate[bestCandidate1].price>parse_[0].candidate[bestCandidate0].price+8)
            {
                model_->EncodeLiteral(outPrevChar);//,wnd_curpos_-1
                progress++;
                parse_[0]=parse_[1];
                bestCandidate0=bestCandidate1;

                outPrevChar=wnd_[wnd_curpos_];
                InsertHash(currHash3,currHash6,1);
                continue;
            }
            else
            {
                parse_pos_=0;
                outPrevMatch=true;
                goto OUTCANDIDATE;
            }
        }


OUTCANDIDATE:
        switch (parse_[0].candidate[bestCandidate0].type)
        {
        case P_LITERAL:
            model_->EncodeLiteral(wnd_[wnd_curpos_]);//,wnd_curpos_
            InsertHash(currHash3,currHash6,1);
            progress++;
            break;
        case P_MATCH:
            model_->EncodeMatch(parse_[0].candidate[bestCandidate0].dist,parse_[0].candidate[bestCandidate0].len-1);
            rep_dist_[3]=rep_dist_[2];
            rep_dist_[2]=rep_dist_[1];
            rep_dist_[1]=rep_dist_[0];
            rep_dist_[0]=parse_[0].candidate[bestCandidate0].dist;
            if (outPrevMatch)
                InsertHash(currHash3,currHash6,parse_[0].candidate[bestCandidate0].len-1);
            else
                InsertHash(currHash3,currHash6,parse_[0].candidate[bestCandidate0].len);
            progress+=parse_[0].candidate[bestCandidate0].len;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        case P_REPDIST_MATCH:
            matchDist=rep_dist_[parse_[0].candidate[bestCandidate0].dist];
            for(j=parse_[0].candidate[bestCandidate0].dist;j>0;j--)
                rep_dist_[j]=rep_dist_[j-1];
            rep_dist_[0]=matchDist;
            model_->EncodeRepDistMatch(parse_[0].candidate[bestCandidate0].dist,parse_[0].candidate[bestCandidate0].len-1);
            if (outPrevMatch)
                InsertHash(currHash3,currHash6,parse_[0].candidate[bestCandidate0].len-1);
            else
                InsertHash(currHash3,currHash6,parse_[0].candidate[bestCandidate0].len);
            progress+=parse_[0].candidate[bestCandidate0].len;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        case P_1BYTE_MATCH:
            model_->Encode1BMatch();
            if (outPrevMatch) 
                ;
            else
                InsertHash(currHash3,currHash6,1);
            progress++;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        }


    }

    return NO_ERROR;

}



int LZ::LZMinBlockFast(uint32_t size)
{
    uint32_t currHash6=0;
    uint32_t progress=0;

    uint32_t matchDist=0;
    uint32_t minMatchLen=1;
    uint32_t *HT6;

    uint32_t bestCandidate0;
    uint32_t lastDist=0;
    uint32_t distExtra=0;

    uint32_t i,j,cmpPos1,cmpPos2,cmpLen,remainLen,remainLen2;
    bool gotMatch;

    const uint32_t currBlockEndPos=wnd_curpos_+size;

    parse_pos_=0;

    while(progress<size)
    {

        currHash6=HASH6(wnd_[wnd_curpos_]);

        parse_[parse_pos_].total=1;
        parse_[parse_pos_].candidate[0].price=5;

        gotMatch=false;

        minMatchLen=1;
        remainLen=size-progress;

        if (remainLen>=MIN_LEN_HT2)
        for(i=0;i<4;i++)
        {
            cmpPos1=wnd_curpos_>rep_dist_[i]?wnd_curpos_-rep_dist_[i]:wnd_curpos_+wnd_Size-rep_dist_[i];
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size))
            {
                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);
                if (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
                    (wnd_[cmpPos1+(minMatchLen>>1)]!=wnd_[cmpPos2+(minMatchLen>>1)])
                    )
                    continue;

                cmpPos1+=2;
                cmpPos2+=2;
                cmpLen=2;

                if (remainLen2>3)
                    while ((cmpLen<remainLen2-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                    {
                        cmpPos1+=4;
                        cmpPos2+=4;
                        cmpLen+=4;
                    }

                    while((cmpLen<remainLen2)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) 
                        cmpLen++;

                    if (cmpLen>minMatchLen)
                    {
                        minMatchLen=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_REPDIST_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8+3;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=i;
                        parse_[parse_pos_].total++;
                        if (minMatchLen>GOOD_LEN_REP)
                        {
                            gotMatch=true;
                            break;
                        }
                    }
            }
        }

        if (gotMatch) goto DETERMINE;

        cmpPos1=wnd_curpos_>rep_dist_[0]?wnd_curpos_-rep_dist_[0]:wnd_curpos_+wnd_Size-rep_dist_[0];

        if ((minMatchLen<3) && (cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos)
            && (cmpPos1<wnd_Size) && (wnd_[cmpPos1]==wnd_[wnd_curpos_]) )
        {
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_1BYTE_MATCH;
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=1;
            parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=48;
            parse_[parse_pos_].total++;
        }


        HT6=&mf_ht6_[currHash6*h6_width_];
        lastDist=1;
        distExtra=0;
        if (remainLen>=MIN_LEN_HT6)
        for(i=0;i<1;i++)
        {
            cmpPos1=HT6[i]&0x1FFFFFFF;
            if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size) && ((HT6[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
            { 
                matchDist=wnd_curpos_>cmpPos1?wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;
                if (matchDist<lastDist) break;
                lastDist=matchDist;

                cmpPos2=wnd_curpos_;
                remainLen2=MIN(remainLen,wnd_Size-cmpPos1);

                if (*(uint32_t*)&wnd_[cmpPos1]!=*(uint32_t*)&wnd_[cmpPos2]) continue;
                if ((remainLen2<minMatchLen)||
                    (wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
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

                    if    ( (cmpLen>minMatchLen) &&
                        (
                        ((cmpLen>=MIN_LEN_HT6)&&(matchDist<(1<<18)))
                        || (cmpLen>=MIN_LEN_HT6+1)
                        ))
                    { 
                        uint32_t countExtra=(matchDist>>7);
                        distExtra=0;
                        while(countExtra>0)
                        {
                            countExtra>>=2;
                            distExtra+=3;
                        }
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].type=P_MATCH;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].len=cmpLen;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].dist=matchDist;
                        parse_[parse_pos_].candidate[parse_[parse_pos_].total].price=cmpLen*8-10-distExtra;
                        parse_[parse_pos_].total++;
                        minMatchLen=cmpLen;
                        if (cmpLen>GOOD_LEN_HT6)
                        {
                            gotMatch=true;
                            break;
                        }
                    }
            }
        }


DETERMINE:



        bestCandidate0=0;
        for (i=1;i<parse_[0].total;i++)
        {
            if (parse_[0].candidate[i].price>parse_[0].candidate[bestCandidate0].price)
                bestCandidate0=i;
        }


        switch (parse_[0].candidate[bestCandidate0].type)
        {
        case P_LITERAL:
            model_->EncodeLiteral(wnd_[wnd_curpos_]);//,wnd_curpos_
            InsertHashFast(currHash6,1);
            progress++;
            break;
        case P_MATCH:
            model_->EncodeMatch(parse_[0].candidate[bestCandidate0].dist,parse_[0].candidate[bestCandidate0].len-1);
            rep_dist_[3]=rep_dist_[2];
            rep_dist_[2]=rep_dist_[1];
            rep_dist_[1]=rep_dist_[0];
            rep_dist_[0]=parse_[0].candidate[bestCandidate0].dist;
            InsertHashFast(currHash6,parse_[0].candidate[bestCandidate0].len);
            progress+=parse_[0].candidate[bestCandidate0].len;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        case P_REPDIST_MATCH:
            matchDist=rep_dist_[parse_[0].candidate[bestCandidate0].dist];
            for(j=parse_[0].candidate[bestCandidate0].dist;j>0;j--)
                rep_dist_[j]=rep_dist_[j-1];
            rep_dist_[0]=matchDist;
            model_->EncodeRepDistMatch(parse_[0].candidate[bestCandidate0].dist,parse_[0].candidate[bestCandidate0].len-1);
            InsertHashFast(currHash6,parse_[0].candidate[bestCandidate0].len);
            progress+=parse_[0].candidate[bestCandidate0].len;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        case P_1BYTE_MATCH:
            model_->Encode1BMatch();
            InsertHashFast(currHash6,1);
            progress++;
            model_->SetLiteralCtx(wnd_[wnd_curpos_-1]);
            break;
        }


    }
    if (progress>size)
        printf("\nff %u\n",wnd_curpos_);

    return NO_ERROR;

}
