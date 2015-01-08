#ifndef _CSC_LZ_H_
#define _CSC_LZ_H_
#include <Common.h>
#include <csc_model.h>

#define HASH6(a) (((*(uint32_t*)(&a)^(*(uint16_t*)((&a)+4)<<13))*2654435761u)>>(32-h6_bits_))
#define HASH3(a) (((a)<<8)^(*((&a)+1)<<5)^(*((&a)+2)))
#define HASH2(a) (*(uint16_t*)&a)

#define CURPOS (wnd_curpos_ | ((wnd_[wnd_curpos_] & 0x0E) << 28))



#define MIN_LEN_HT2 2
#define MIN_LEN_REP 2
#define MIN_LEN_HT6 6
#define MIN_LEN_HT3 3

#define GOOD_LEN_REP 32
#define GOOD_LEN_HT6 34



class Model;

class LZ
{
public:
    Model *model_;
    int Init(uint32_t WindowSize,uint32_t operation,uint32_t hashBits,uint32_t hashWidth);
    int Init(uint32_t WindowSize,uint32_t operation);
    void EncodeNormal(uint8_t *src,uint32_t size,uint32_t lzMode);
    uint32_t CheckDuplicate(uint8_t *src,uint32_t size,uint32_t type);
    void DuplicateInsert(uint8_t *src,uint32_t size);
    void Reset(void);
    int Decode(uint8_t *dst,uint32_t *size,uint32_t sizeLimit);
    void Destroy(void);
    uint32_t wnd_curpos_;


private:
    uint32_t m_operation;

    uint32_t h6_bits_;
    //uint32_t m_MaxCycles;

    uint32_t h6_width_;
    //uint32_t m_lazyParsing;

    uint8_t  *wnd_;
    uint32_t *mf_ht2_;
    uint32_t *mf_ht3_;
    uint32_t *mf_ht6_;
    uint32_t *mf_ht6raw_;
    uint32_t rep_dist_[4];
    uint32_t rep_matchlen_;
    uint32_t litcount_,passedcnt_;

    struct 
    {
        uint32_t total;
        struct 
        {
            PackType type;
            uint32_t len;
            uint32_t dist;
            uint32_t price;
        } candidate[32];
    } parse_[2];
    uint32_t parse_pos_;

    uint32_t wnd_Size;
    //uint32_t wnd_curpos_;

    int LZMinBlock(uint32_t size);
    int LZMinBlockFast(uint32_t size);
    uint32_t LZMinBlockSkip(uint32_t size,uint32_t type);
    void InsertHash(uint32_t currHash3,uint32_t currHash6,uint32_t slideLen);
    void InsertHashFast(uint32_t currHash6,uint32_t slideLen);

    uint32_t currBlockEndPos;

// New LZ77 Algorithm===============================
    int LZMinBlockNew(uint32_t size,uint32_t TryLazy,uint32_t LazyStep,uint32_t GoodLen);
    void LZBackward(const uint32_t initWndPos,uint32_t start,uint32_t end);


    struct ParserAtom
    {
        uint8_t finalChoice;
        uint32_t finalLen;
        uint32_t fstate;
        uint32_t price;

        struct 
        {
            uint32_t startPos;
            uint16_t choiceNum;
        } choices;

        uint32_t repDist[4];

        struct
        {
            uint16_t fromPos;
            uint8_t choice;
            uint32_t fromLen;
        } backChoice;
    };

    struct MatchAtom
    {
        uint32_t pos;
        uint16_t len;
    };

    ParserAtom *parser;
    MatchAtom  *matchList;
    uint32_t matchListSize;

    uint32_t cHash2;
    uint32_t cHash3;
    uint32_t cHash6;
    void NewInsert1();
    void NewInsertN(uint32_t len);
    uint32_t FindMatch(uint32_t idx,uint32_t minMatchLen);
// New LZ77 Algorithm====================================
};


#endif

