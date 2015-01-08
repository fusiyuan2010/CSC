#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <csc_coder.h>
#include <csc_model.h>
#include <csc_lz.h>



inline void LZ::NewInsert1()
{
    uint32_t    *HT6=&mf_ht6_[cHash6*h6_width_];
    uint32_t i;

    mf_ht2_[cHash2]=CURPOS;
    mf_ht3_[cHash3]=CURPOS;

    for(i=h6_width_-1;i>0;i--)
        HT6[i]=HT6[i-1];

    HT6[0]=CURPOS;
    wnd_curpos_++;
}

inline void LZ::NewInsertN(uint32_t len)
{
    uint32_t    *HT6=&mf_ht6_[cHash6*h6_width_];

    uint32_t lastCurrHash6;
    uint32_t i;

    mf_ht2_[cHash2]=CURPOS;
    mf_ht3_[cHash3]=CURPOS;

    for(i=h6_width_-1;i>0;i--)
        HT6[i]=HT6[i-1];

    HT6[0]=CURPOS;
    len--;
    wnd_curpos_++;

    lastCurrHash6=cHash6;

    while(len>0)
    {
        cHash6=HASH6(wnd_[wnd_curpos_]);
        cHash3=HASH3(wnd_[wnd_curpos_]);
        HT6=&mf_ht6_[cHash6*h6_width_];

        mf_ht2_[*(uint16_t*)(wnd_+wnd_curpos_)]=CURPOS;
        mf_ht3_[cHash3]=CURPOS;

        if (lastCurrHash6!=cHash6)
        {
            for(i=h6_width_-1;i>0;i--)
                HT6[i]=HT6[i-1];
        }
        HT6[0]=CURPOS;

        if (len>256)
        {
            len-=4;
            wnd_curpos_+=4;
        }
        len--;
        wnd_curpos_++;    
        lastCurrHash6=cHash6;
    }
}


uint32_t LZ::FindMatch(uint32_t idx,uint32_t minMatchLen)
{
    uint32_t cmpPos1,cmpPos2;
    uint32_t remainLen,cmpLen,matchDist;
    uint32_t i,mostRecPos,lastDist;
    uint32_t maxValidDist=0;
    uint32_t *HT6,*HT3,*HT2;
    uint32_t longestMatch=0;

    parser[idx].choices.startPos=matchListSize;
    parser[idx].choices.choiceNum=0;

    cHash6=HASH6(wnd_[wnd_curpos_]);
    cHash3=HASH3(wnd_[wnd_curpos_]);
    cHash2=HASH2(wnd_[wnd_curpos_]);


    //===================find repeat distance match=============================
    for(i=0;i<4;i++)
    {
        cmpPos1=wnd_curpos_>parser[idx].repDist[i]?
            wnd_curpos_-parser[idx].repDist[i]:wnd_curpos_+wnd_Size-parser[idx].repDist[i];

        if (i==0) mostRecPos=cmpPos1;
        cmpLen=0;

        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) && (cmpPos1<wnd_Size))
        {
            cmpPos2=wnd_curpos_;
            remainLen=MIN(currBlockEndPos-wnd_curpos_, wnd_Size-cmpPos1);

            if (remainLen<MIN_LEN_REP || remainLen<minMatchLen)
                continue;

            if ((wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
                (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2])
                )
                continue;

            cmpPos1+=2;
            cmpPos2+=2;
            cmpLen=2;

            if (remainLen>3)
                while ((cmpLen<remainLen-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}

                while((cmpLen<remainLen)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;

                if (cmpLen>minMatchLen)
                {
                    longestMatch=minMatchLen=
                        matchList[matchListSize].len=cmpLen;
                    matchList[matchListSize].pos=i;
                    matchListSize++;
                    parser[idx].choices.choiceNum++;
                    //repMatchNum++;

                    if (parser[idx].repDist[i]>maxValidDist)
                        maxValidDist=parser[idx].repDist[i];
                }
        }
    }
    //===================find repeat distance match=============================


    //===================find repeat byte match=============================
    if ((mostRecPos<wnd_curpos_ || mostRecPos>currBlockEndPos)
        && (mostRecPos<wnd_Size) && (wnd_[mostRecPos]==wnd_[wnd_curpos_]) )
    {
        matchList[matchListSize].len=1;
        matchList[matchListSize].pos=4;
        matchListSize++;
        parser[idx].choices.choiceNum++;
    }
    //===================find repeat byte match=============================

    //===================find hash 6 match=============================
    HT6=&mf_ht6_[cHash6*h6_width_];
    lastDist=1;
    for(i=0;i<h6_width_;i++)
    {
        cmpPos1=HT6[i]&0x1FFFFFFF;
        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos)  
            && (cmpPos1<wnd_Size) 
            && ((HT6[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1))) //cache
        {
            matchDist=wnd_curpos_>cmpPos1?
                wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;

            if (matchDist<lastDist) break;
            lastDist=matchDist;

            cmpPos2=wnd_curpos_;
            remainLen=MIN(currBlockEndPos-wnd_curpos_, wnd_Size-cmpPos1);

            if (remainLen<MIN_LEN_HT6 || remainLen<minMatchLen)
                continue;

            if ((wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
                (*(uint32_t*)&wnd_[cmpPos1]!=*(uint32_t*)&wnd_[cmpPos2]))
                continue;

            cmpPos1+=4;
            cmpPos2+=4;
            cmpLen=4;

            if (remainLen>3)
                while ((cmpLen<remainLen-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}
                while((cmpLen<remainLen)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;

                if    (cmpLen>minMatchLen)
                { 
                    if (cmpLen==MIN_LEN_HT2 && matchDist>=63)
                        continue;
                    if (cmpLen==MIN_LEN_HT3 && matchDist>=2047)
                        continue;
                    //if (cmpLen==4 && matchDist>=256*KB)
                    //    continue;

                    longestMatch=minMatchLen=
                        matchList[matchListSize].len=cmpLen;
                    matchList[matchListSize].pos=matchDist+4;
                    matchListSize++;
                    parser[idx].choices.choiceNum++;
                }
        }
    }

    if (minMatchLen>MIN_LEN_HT3) 
        return minMatchLen;

    //===================find hash 3 match=============================
    HT3=&mf_ht3_[cHash3];

    for(i=0;i<1;i++)
    {
        cmpPos1=HT3[i]&0x1FFFFFFF;

        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) 
            && (cmpPos1<wnd_Size)
            && ((HT3[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
        {
            cmpPos2=wnd_curpos_;
            remainLen=MIN(currBlockEndPos-wnd_curpos_, wnd_Size-cmpPos1);
            matchDist=wnd_curpos_>cmpPos1?
                wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;

            if (remainLen<MIN_LEN_HT3 || remainLen<minMatchLen)
                continue;

            if ((wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
                (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]))
                continue;

            cmpPos1+=2;
            cmpPos2+=2;
            cmpLen=2;

            if (remainLen>3)
                while ((cmpLen<remainLen-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}
                while((cmpLen<remainLen)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;


                if (cmpLen>minMatchLen)
                {
                    if (cmpLen==MIN_LEN_HT2 && matchDist>=63)
                        continue;
                    if (cmpLen==MIN_LEN_HT3 && matchDist>=2047)
                        continue;
                    //if (cmpLen==4 && matchDist>=256*KB)
                    //    continue;

                    longestMatch=minMatchLen=
                        matchList[matchListSize].len=cmpLen;
                    matchList[matchListSize].pos=matchDist+4;
                    matchListSize++;
                    parser[idx].choices.choiceNum++;
                }
        }
    }
    //===================find hash 3 match=============================

    if (minMatchLen>MIN_LEN_HT2) 
        return minMatchLen;

    //===================find hash 2 match=============================
    HT2=&mf_ht2_[cHash2];

    for(i=0;i<1;i++)
    {
        cmpPos1=HT2[i]&0x1FFFFFFF;

        if ((cmpPos1<wnd_curpos_ || cmpPos1>currBlockEndPos) 
            && (cmpPos1<wnd_Size)
            && ((HT2[i]>>29)==((wnd_[wnd_curpos_]&0x0E)>>1)))
        {
            cmpPos2=wnd_curpos_;
            remainLen=MIN(currBlockEndPos-wnd_curpos_, wnd_Size-cmpPos1);
            matchDist=wnd_curpos_>cmpPos1?
                wnd_curpos_-cmpPos1:wnd_curpos_+wnd_Size-cmpPos1;


            if (remainLen<MIN_LEN_HT2 || remainLen<minMatchLen)
                continue;

            if ((wnd_[cmpPos1+minMatchLen]!=wnd_[cmpPos2+minMatchLen])||
                (*(uint16_t*)&wnd_[cmpPos1]!=*(uint16_t*)&wnd_[cmpPos2]))
                continue;


            cmpPos1+=2;
            cmpPos2+=2;
            cmpLen=2;

            if (remainLen>3)
                while ((cmpLen<remainLen-3)&&(*(uint32_t*)(wnd_+cmpPos1)==*(uint32_t*)(wnd_+cmpPos2)))
                {cmpPos1+=4;cmpPos2+=4;cmpLen+=4;}
                while((cmpLen<remainLen)&&(wnd_[cmpPos1++]==wnd_[cmpPos2++])) cmpLen++;


                if (cmpLen>minMatchLen)
                {
                    if (cmpLen==MIN_LEN_HT2 && matchDist>=63)
                        continue;
                    if (cmpLen==MIN_LEN_HT3 && matchDist>=2047)
                        continue;
                    //if (cmpLen==4 && matchDist>=256*KB)
                    //    continue;

                    longestMatch=minMatchLen=
                        matchList[matchListSize].len=cmpLen;
                    matchList[matchListSize].pos=matchDist+4;
                    matchListSize++;
                    parser[idx].choices.choiceNum++;
                }
        }
    }
    //===================find hash 2 match=============================
    return longestMatch;
}

//uint32_t literalNum=0;

int LZ::LZMinBlockNew(uint32_t size,uint32_t TryLazy,uint32_t LazyStep,uint32_t GoodLen)
{
    uint32_t maxReach=0;
    const uint32_t initWndPos=wnd_curpos_;
    uint32_t fCtx;
    uint32_t lastBackPos=0;
    uint32_t lazyNum=0,lazyEndPos=0;

    currBlockEndPos=wnd_curpos_+size;
    matchListSize=0;

    for(int i=0;i<size+1;i++)
        parser[i].price=0x0FFFFFFF;

    parser[0].price=0;
    parser[0].fstate=model_->state_;
    parser[0].repDist[0]=rep_dist_[0];parser[0].repDist[1]=rep_dist_[1];
    parser[0].repDist[2]=rep_dist_[2];parser[0].repDist[3]=rep_dist_[3];
    fCtx=model_->GetLiteralCtx();

    for(int i=0;i<size;)
    {
        uint32_t curOpPrice;
        uint32_t newReach,longestMatch;

        longestMatch=FindMatch(i,(lazyNum>0)?(maxReach-i-1):1);
        
        newReach=i+MAX(1,longestMatch);
        //if (wnd_curpos_>540)
        //    printf("asdf");
        curOpPrice=model_->GetLiteralPrice(parser[i].fstate,fCtx,wnd_[wnd_curpos_]);
        if (parser[i+1].price>parser[i].price+curOpPrice)
        {
            parser[i+1].backChoice.choice=0;
            parser[i+1].backChoice.fromPos=i;
            parser[i+1].price=parser[i].price+curOpPrice;
            parser[i+1].fstate=(parser[i].fstate*4)&0x3F;
            parser[i+1].repDist[0]=parser[i].repDist[0];
            parser[i+1].repDist[1]=parser[i].repDist[1];
            parser[i+1].repDist[2]=parser[i].repDist[2];
            parser[i+1].repDist[3]=parser[i].repDist[3];

        }

        for(int j=0;j<parser[i].choices.choiceNum;j++)
        {
            uint32_t tmpChoiceIdx=j+parser[i].choices.startPos;
            uint32_t tmpCurState;
            uint32_t tmpCurLen;

            tmpCurLen=matchList[tmpChoiceIdx].len;
            if (matchList[tmpChoiceIdx].pos<4)
            {
                curOpPrice=model_->GetRepDistMatchPrice(parser[i].fstate,
                    matchList[tmpChoiceIdx].pos,
                    tmpCurLen);
                tmpCurState=3;
            }
            else if (matchList[tmpChoiceIdx].pos==4)
            {
                curOpPrice=model_->Get1BMatchPrice(parser[i].fstate);
                tmpCurState=2;
            }
            else
            {
                curOpPrice=model_->GetMatchPrice(parser[i].fstate,
                    matchList[tmpChoiceIdx].pos-4,
                    tmpCurLen);
                tmpCurState=1;
            }

            {
                int k=tmpCurLen;

                if (parser[i+k].price>parser[i].price+curOpPrice)
                {

                    parser[i+k].backChoice.choice=j+1;
                    parser[i+k].backChoice.fromPos=i;
                    parser[i+k].backChoice.fromLen=k;
                    parser[i+k].price=parser[i].price+curOpPrice;
                    parser[i+k].fstate=(parser[i].fstate*4+tmpCurState)&0x3F;
                    if ((tmpCurState==2)||(tmpCurState==3 && matchList[tmpChoiceIdx].pos==0))
                    {
                        parser[i+k].repDist[0]=parser[i].repDist[0];
                        parser[i+k].repDist[1]=parser[i].repDist[1];
                        parser[i+k].repDist[2]=parser[i].repDist[2];
                        parser[i+k].repDist[3]=parser[i].repDist[3];
                    }
                    else if (tmpCurState==1)
                    {
                        parser[i+k].repDist[0]=matchList[tmpChoiceIdx].pos-4;
                        parser[i+k].repDist[1]=parser[i].repDist[0];
                        parser[i+k].repDist[2]=parser[i].repDist[1];
                        parser[i+k].repDist[3]=parser[i].repDist[2];
                    }
                    else
                    {
                        if (matchList[tmpChoiceIdx].pos==1)
                        {
                            parser[i+k].repDist[0]=parser[i].repDist[1];
                            parser[i+k].repDist[1]=parser[i].repDist[0];
                            parser[i+k].repDist[2]=parser[i].repDist[2];
                            parser[i+k].repDist[3]=parser[i].repDist[3];
                        }
                        else if (matchList[tmpChoiceIdx].pos==2)
                        {
                            parser[i+k].repDist[0]=parser[i].repDist[2];
                            parser[i+k].repDist[1]=parser[i].repDist[0];
                            parser[i+k].repDist[2]=parser[i].repDist[1];
                            parser[i+k].repDist[3]=parser[i].repDist[3];
                        }
                        else if (matchList[tmpChoiceIdx].pos==3)
                        {
                            parser[i+k].repDist[0]=parser[i].repDist[3];
                            parser[i+k].repDist[1]=parser[i].repDist[0];
                            parser[i+k].repDist[2]=parser[i].repDist[1];
                            parser[i+k].repDist[3]=parser[i].repDist[2];
                        }
                    }    
                }
            }
        }

        if (longestMatch>TryLazy && lazyNum==0)
        {
            lazyNum=LazyStep;
            lazyEndPos=i+longestMatch;
        }

        if (lazyNum>0)
        {
            lazyNum--;
            if (lazyNum==0)
            {
                NewInsertN(lazyEndPos-i);
                i=lazyEndPos;
                fCtx=wnd_[wnd_curpos_-1];
            }
            else
            {
                fCtx=wnd_[wnd_curpos_];
                NewInsert1();
                i++;
            }
        }
        else
        {
            fCtx=wnd_[wnd_curpos_];
            NewInsert1();
            i++;
        }

        if (newReach>maxReach)
            maxReach=newReach;

        if (maxReach-lastBackPos>1024 || longestMatch>GoodLen)
        {
            if (maxReach>i)
                NewInsertN(maxReach-i);
            LZBackward(initWndPos,lastBackPos,maxReach);
            lastBackPos=i=maxReach;
            fCtx=wnd_[wnd_curpos_-1];

            lazyNum=0;
            parser[maxReach].price=0;
            parser[maxReach].fstate=model_->state_;
            parser[maxReach].repDist[0]=rep_dist_[0];parser[maxReach].repDist[1]=rep_dist_[1];
            parser[maxReach].repDist[2]=rep_dist_[2];parser[maxReach].repDist[3]=rep_dist_[3];
        }
    }
    
    LZBackward(initWndPos,lastBackPos,size);
    

    /*float a=(float)model_->GetMatchPrice(model_->state,13,2)/128;
    printf("match price: 2 37 %f\n",a);
    a=(float)model_->GetMatchPrice(model_->state,33,3)/128;
    printf("match price: 3 1023 %f\n",a);
    a=(float)model_->GetMatchPrice(model_->state,165,4)/128;
    printf("match price: 4 64kB %f\n",a);
    a=(float)model_->GetMatchPrice(model_->state,235,5)/128;
    printf("match price: 5 1MB %f\n",a);
    a=(float)model_->GetLiteralPrice(model_->state,0,'t')/128;
    printf("litral price: t %f\n",a);
    a=(float)model_->GetRepDistMatchPrice(model_->state,2,4)/128;
    printf("match rep price: 2 4 %f\n",a);
    printf("=============================\n");*/
    //printf("%d\n",literalNum);
    return NO_ERROR;
}



void LZ::LZBackward(const uint32_t initWndPos,uint32_t start,uint32_t end)
{
    for(int i=end;i>start;)
    {
        parser[parser[i].backChoice.fromPos].finalChoice=parser[i].backChoice.choice;
        parser[parser[i].backChoice.fromPos].finalLen=parser[i].backChoice.fromLen;
        i=parser[i].backChoice.fromPos;
    }

    for(int i=start;i<end;)
    {
        if (parser[i].finalChoice==0)
        {
            model_->EncodeLiteral(wnd_[initWndPos+i]);
            //literalNum++;
            i++;
        }
        else
        {
            uint32_t tmpIdx=parser[i].finalChoice-1+parser[i].choices.startPos;
            if (matchList[tmpIdx].pos<4)
            {
                model_->EncodeRepDistMatch(matchList[tmpIdx].pos,parser[i].finalLen-1);

                uint32_t tmpDist=rep_dist_[matchList[tmpIdx].pos];
                for(int j=matchList[tmpIdx].pos;j>0;j--)
                    rep_dist_[j]=rep_dist_[j-1];
                rep_dist_[0]=tmpDist;

                i+=parser[i].finalLen;
                model_->SetLiteralCtx(wnd_[initWndPos+i-1]);
            }
            else if (matchList[tmpIdx].pos==4)
            {
                model_->Encode1BMatch();
                model_->SetLiteralCtx(wnd_[initWndPos+i]);
                i++;
            }
            else
            {
                model_->EncodeMatch(matchList[tmpIdx].pos-4,parser[i].finalLen-1);

                rep_dist_[3]=rep_dist_[2];
                rep_dist_[2]=rep_dist_[1];
                rep_dist_[1]=rep_dist_[0];
                rep_dist_[0]=matchList[tmpIdx].pos-4;

                i+=parser[i].finalLen;
                model_->SetLiteralCtx(wnd_[initWndPos+i-1]);
            }

        }    
    }
}
