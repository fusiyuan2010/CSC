#ifndef _CSC_LZ_H_
#define _CSC_LZ_H_
#include <Common.h>
#include <csc_model.h>

#define HASH6(a) (((*(uint32_t*)(&a)^(*(uint16_t*)((&a)+4)<<13))*2654435761u)>>(32-ht6_bits_))
#define HASH5(a) (((*(uint32_t*)(&a)^(*(uint16_t*)((&a)+3)<<13))*2654435761u)>>(32-21))
#define HASH4(a) (((*(uint32_t*)(&a))*2654435761u)>>(32 - 18))
#define HASH3(a) (((a)<<8)^(*((&a)+1)<<5)^(*((&a)+2)))
#define HASH2(a) (*(uint16_t*)&a)

#define CURPOS (wnd_curpos_ | ((wnd_[wnd_curpos_] & 0x0E) << 28))



#define MIN_LEN_REP 2
#define MIN_LEN_HT2 2
#define MIN_LEN_HT3 3
#define MIN_LEN_HT4 4
#define MIN_LEN_HT5 5
#define MIN_LEN_HT6 6

#define GOOD_LEN_REP 24
#define GOOD_LEN_HT6 32



class Model;

class LZ
{
public:
    Model *model_;
    int Init(uint32_t WindowSize, uint32_t hashBits,uint32_t hashWidth);
    int Init(uint32_t WindowSize);
    void EncodeNormal(uint8_t *src,uint32_t size,uint32_t lzMode);
    uint32_t CheckDuplicate(uint8_t *src,uint32_t size,uint32_t type);
    void DuplicateInsert(uint8_t *src,uint32_t size);
    void Reset(void);
    void Destroy(void);
    uint32_t wnd_curpos_;


private:
    uint32_t ht6_bits_;
    //uint32_t m_MaxCycles;

    uint32_t ht6_width_;
    //uint32_t m_lazyParsing;

    uint8_t  *wnd_;
    uint32_t *mf_ht2_;
    uint32_t *mf_ht3_;
    uint32_t *mf_ht4_;
    uint32_t *mf_ht5_;
    uint32_t *mf_ht6_;
    static const int HT6_SIZE_LIMIT = 16;
    uint32_t *mf_ht6raw_;
    uint32_t rep_dist_[4];
    uint32_t rep_matchlen_;
    uint32_t litcount_,passedcnt_;

    uint32_t wnd_size_;
    uint32_t LZMinBlockSkip(uint32_t size,uint32_t type);
    uint32_t curblock_endpos;

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


    struct MFUnit {
        enum {
            // units array at pos 0 is always literal
            LITERAL,
            ONEBYTE_MATCH,
            REPDIST_MATCH,
            NORMAL_MATCH,
        } type;
        
        uint32_t c;
        uint32_t len;
        union {
            uint32_t dist;
            uint32_t rep_idx;
        };
    };

    struct MFUnits {
        // 4 repdist and 1 ht3 and 1 ht2 and 1 literal
        static const int limit = HT6_SIZE_LIMIT + 7;
        MFUnit u[limit];
        int cnt;
        bool mftried;
        int bestidx;

        void Clear() {
            mftried = false;
            cnt = 1;
            bestidx = 0;
        }
    };

    MFUnits mfunits_[2];
    int compress_normal(uint32_t size, bool lazy, bool mffast);
    uint32_t encode_unit(LZ::MFUnits *units);
    void find_match(uint32_t bytes_left, MFUnits *units, uint32_t wpos, bool mffast) ;
    void slide_pos(uint32_t len, bool mffast);

    int match_second_better(MFUnits *units1, MFUnits *units2);
    void get_best_match(MFUnits *units);
    int compress_fast(uint32_t size);
    int compress_optimal(uint32_t size);

};


#endif

