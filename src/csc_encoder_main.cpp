#include <stdlib.h>
#include "Common.h"
#include <csc_encoder_main.h>

CSCSettings::CSCSettings()
{
    wndSize=32*MB;
    maxSuccBlockSize=MaxChunkSize;
    SetDefaultMethod(2);
}

void CSCSettings::Refresh()
{
    if (wndSize<=64*KB)
        hashBits=16;
    else if (wndSize<=128*KB)
        hashBits=17;
    else if (wndSize<=256*KB)
        hashBits=18;
    else if (wndSize<=512*KB)
        hashBits=19;
    else if (wndSize<=2*MB)
        hashBits=21;
    else if (wndSize<=8*MB)
        hashBits=22;
    else if (wndSize<=32*MB)
        hashBits=23;
    else if (wndSize<=128*MB)
        hashBits=24;
    else 
        hashBits=25;

    for(int i=1;i<hashWidth;i=i<<1)
        hashBits--;

    while(hashWidth*(1<<hashBits)+wndSize>600*MB)
    {
        hashBits--;
    }
}

void CSCSettings::SetDefaultMethod(uint8_t method)
{
    switch(method)
    {
    case 1:
        lzMode=1;
        hashWidth=1;
        break;
    case 2:
        lzMode=2;
        hashWidth=1;
        break;
    case 3:
        lzMode=3;
        hashWidth=2;
        break;
    case 4:
        lzMode=4;
        hashWidth=8;
        break;
    default:
        lzMode=2;
        hashWidth=1;
        break;
    }

    if (wndSize<=64*KB)
        hashBits=16;
    else if (wndSize<=128*KB)
        hashBits=17;
    else if (wndSize<=256*KB)
        hashBits=18;
    else if (wndSize<=512*KB)
        hashBits=19;
    else if (wndSize<=2*MB)
        hashBits=21;
    else if (wndSize<=8*MB)
        hashBits=22;
    else if (wndSize<=32*MB)
        hashBits=23;
    else if (wndSize<=128*MB)
        hashBits=24;
    else 
        hashBits=25;

    for(int i=1;i<hashWidth;i=i<<1)
        hashBits--;

    while(hashWidth*(1<<hashBits)+wndSize>600*MB)
    {
        hashBits--;
    }


    EXEFilter=1;
    DLTFilter=1;
    TXTFilter=1;
}

int CSCEncoder::Init(CSCSettings setting)
{
    int err;

    m_lz.model_ = &m_model;
    typeArg1=typeArg2=typeArg3=0;
    fixedDataType=DT_NONE;


    m_setting=setting;

    m_coder.io_ =m_setting.io;
    m_succBlockSize=m_setting.maxSuccBlockSize;
    m_rcbuffer=(uint8_t*)malloc(m_setting.outStreamBlockSize);
    m_bcbuffer=(uint8_t*)malloc(m_setting.outStreamBlockSize);

    if (m_rcbuffer==NULL || m_bcbuffer==NULL)
    {
        err=CANT_ALLOC_MEM;
        goto FREE_ON_ERROR;
    }


    m_analyzer.Init();
    m_filters.Init();
    m_coder.Init(m_rcbuffer,m_bcbuffer,m_setting.outStreamBlockSize);
    err=m_model.Init(&m_coder);
    if (err<0)
        goto FREE_ON_ERROR;
    err=m_lz.Init(m_setting.wndSize, m_setting.hashBits,m_setting.hashWidth);
    if (err<0)
        goto FREE_ON_ERROR;


    if (m_setting.DLTFilter+m_setting.EXEFilter+m_setting.TXTFilter==0)
        m_useFilters=false;
    else
        m_useFilters=true;

    return NO_ERROR;

FREE_ON_ERROR:
    SAFEFREE(m_rcbuffer);
    SAFEFREE(m_bcbuffer);
    return err;
}


void CSCEncoder::InternalCompress(uint8_t *src,uint32_t size,uint32_t type)
{
    uint32_t lzMode=m_setting.lzMode;

    /*printf("\n%d %d\n",size,type);*/

    if (size==0) return;

    if (type==DT_FAST) 
    {
        type=DT_NORMAL;
        lzMode=0;
    }

    if (type==DT_NORMAL)
    {
        m_model.EncodeInt(type,5);
        m_lz.EncodeNormal(src,size,lzMode);
    }
    else if (type==DT_EXE)
    {
        m_model.EncodeInt(type,5);
        m_filters.Forward_E89(src,size);
        m_lz.EncodeNormal(src,size,m_setting.lzMode);
    }
    else if (type==DT_ENGTXT)
    {
        if (m_filters.Foward_Dict(src,size))
        {
            m_model.EncodeInt(type,5);
            m_model.EncodeInt(size,MaxChunkBits);
        }
        else
            m_model.EncodeInt(DT_NORMAL,5);

        m_lz.EncodeNormal(src,size,m_setting.lzMode);
    }
    /*else if (type==DT_HARD)
    {
    m_model.EncodeBlockType(type);
    m_model.CompressHard(src,size);
    }*/
    /*else if (type==DT_AUDIO)
    {
        m_filters.Forward_Audio(src,size,typeArg1,typeArg2);
        m_model.CompressHard(src,size);
    }
    else if (type==DT_RGB)
    {
        m_model.EncodeInt(typeArg1,16);
        m_model.EncodeInt(typeArg2,6);
        m_filters.Forward_RGB(src,size,typeArg1,typeArg2);
        m_model.CompressRLE(src,size);
        //m_model.CompressValue(src,size,typeArg1,typeArg2/8);
    }*/
    else if (type==DT_BAD)
    {
        m_model.EncodeInt(type,5);
        m_model.CompressBad(src,size);
    }
    else if (type>=DT_DLT && type<DT_DLT+DLT_CHANNEL_MAX)
    {
        m_model.EncodeInt(type,5);
        uint32_t chnNum=DltIndex[type-DT_DLT];
        m_filters.Forward_Delta(src,size,chnNum);
        //m_lz.EncodeNormal(src,size,0);
        //if (chnNum>=0)
        m_model.CompressRLE(src,size);
        //else
        //    m_model.CompressDelta(src,size);
        //m_model.CompressHard(src,size);
    }
    else 
    {
        printf("Bad data type:%d\n",type);
        //m_model.CompressBad(src,size);
    }
}

void CSCEncoder::Compress(uint8_t *src,uint32_t size)
{
    uint32_t lastType,thisType;
    uint32_t lastBegin,lastSize;
    uint32_t currBlockSize;
    uint32_t progress;

    lastBegin=lastSize=0;
    lastType=DT_NORMAL;

    progress=0;



    while (progress<size)
    {
        currBlockSize=MIN(MinBlockSize,size-progress);

        if (lastSize+currBlockSize>m_succBlockSize)
        {
            InternalCompress(src+lastBegin,lastSize,lastType);
            lastBegin=progress;
            lastSize=0;
        }

        if (m_useFilters)
        {
            if (fixedDataType==DT_NONE)
                thisType=m_analyzer.analyze(src+progress,currBlockSize);
            else thisType=fixedDataType;
        }
        else
            thisType=DT_NORMAL;


        if (thisType!=DT_NORMAL)
        {
            if (thisType==DT_EXE && m_setting.EXEFilter==0)
                thisType=DT_NORMAL;
            else if (thisType==DT_ENGTXT && m_setting.TXTFilter==0)
                thisType=DT_NORMAL;
            else if (thisType>=DT_DLT && m_setting.DLTFilter==0)
                thisType=DT_NORMAL;
        }

        if (thisType==DT_BAD)
        {
            if (lastType!=DT_BAD)
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
            }

            if (m_lz.CheckDuplicate(src+progress,currBlockSize,thisType)==DT_NORMAL)
            {
                if (lastType==DT_BAD)
                    InternalCompress(src+lastBegin,lastSize,lastType);

                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
            else
            {
                if (lastType==DT_BAD)
                {
                    lastSize+=currBlockSize;
                }
                else
                {
                    lastBegin=progress;
                    lastSize=currBlockSize;
                    lastType=DT_BAD;
                }
            }
        }
        else if (thisType==DT_NORMAL)
        {
            if (lastType==DT_NORMAL)
            {
                lastSize+=currBlockSize;
            }
            else
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
        }
        else if (thisType==DT_FAST)
        {
            if (lastType==DT_FAST)
            {
                lastSize+=currBlockSize;
            }
            else
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_FAST;
            }
        }
        else if (thisType==DT_SKIP)
        {
            if (lastType==DT_BAD || lastType>=DT_DLT)
                m_lz.DuplicateInsert(src+lastBegin+lastSize,currBlockSize);
            //Important!  because if last type is BAD or DT_DLT,this small block won't 
            //automaticlly insertted into LZ Dictionary.

            lastSize+=currBlockSize;
        }
        else if (thisType==DT_EXE)
        {
            if (lastType==DT_EXE)
            {
                lastSize+=currBlockSize;
            }
            else
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_EXE;
            }
        }
        else if (thisType==DT_ENGTXT)
        {
            if (lastType==DT_ENGTXT)
            {
                lastSize+=currBlockSize;
            }
            else
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_ENGTXT;
            }
        }
        else if (thisType>=DT_DLT && thisType<DT_DLT+DLT_CHANNEL_MAX)
        {
            if (lastType!=thisType)
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
            }

            if (m_lz.CheckDuplicate(src+progress,currBlockSize,thisType)==DT_NORMAL)
            {
                if (lastType==thisType)
                    InternalCompress(src+lastBegin,lastSize,lastType);

                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
            else
            {
                if (lastType==thisType)
                {
                    lastSize+=currBlockSize;
                }
                else
                {
                    lastBegin=progress;
                    lastSize=currBlockSize;
                    lastType=thisType;
                }
            }
        }
        else if (thisType==DT_RGB)
        {
            if (lastType!=thisType)
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
            }

            if (m_lz.CheckDuplicate(src+progress,currBlockSize,thisType)==DT_NORMAL)
            {
                if (lastType==thisType)
                    InternalCompress(src+lastBegin,lastSize,lastType);

                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
            else
            {
                if (lastType==thisType)
                {
                    lastSize+=currBlockSize;
                }
                else
                {
                    lastBegin=progress;
                    lastSize=currBlockSize;
                    lastType=thisType;
                }
            }
        }
        else if (thisType==DT_AUDIO)
        {
            if (lastType!=thisType)
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
            }

            if (m_lz.CheckDuplicate(src+progress,currBlockSize,thisType)==DT_NORMAL)
            {
                if (lastType==thisType)
                    InternalCompress(src+lastBegin,lastSize,lastType);

                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
            else
            {
                if (lastType==thisType)
                {
                    lastSize+=currBlockSize;
                }
                else
                {
                    lastBegin=progress;
                    lastSize=currBlockSize;
                    lastType=thisType;
                }
            }
        }
        else if (thisType==DT_HARD)
        {
            if (lastType!=DT_HARD)
            {
                InternalCompress(src+lastBegin,lastSize,lastType);
            }

            if (m_lz.CheckDuplicate(src+progress,currBlockSize,thisType)==DT_NORMAL)
            {
                if (lastType==DT_HARD)
                    InternalCompress(src+lastBegin,lastSize,lastType);

                lastBegin=progress;
                lastSize=currBlockSize;
                lastType=DT_NORMAL;
            }
            else
            {
                if (lastType==DT_HARD)
                {
                    lastSize+=currBlockSize;
                }
                else
                {
                    lastBegin=progress;
                    lastSize=currBlockSize;
                    lastType=DT_HARD;
                }
            }
        }
        else
        {
            printf("FATAL ERROR!");
            exit(-1);
        }
        progress+=currBlockSize;
    }
    InternalCompress(src+lastBegin,lastSize,lastType);
}


void CSCEncoder::Flush()
{
    //m_model.EncodeInt(SIG_EOF,5);
    m_coder.Flush();
}

void CSCEncoder::WriteEOF()
{
    m_model.EncodeInt(SIG_EOF,5);
}


void CSCEncoder::CheckFileType(uint8_t *src, uint32_t size)
{
    //fixedDataType=m_analyzer.analyzeHeader(src,size,&typeArg1,&typeArg2,&typeArg3);
    fixedDataType=DT_NONE;
    return;
}

void CSCEncoder::Destroy()
{
    m_lz.Destroy();
    m_model.Destroy();
    SAFEFREE(m_rcbuffer);
    SAFEFREE(m_bcbuffer);
}

int64_t CSCEncoder::GetCompressedSize()
{
    return (m_coder.outsize_ + m_coder.rc_size_ + m_coder.bc_size_);
}
