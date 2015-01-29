#ifndef _CSC_LZ_H_
#define _CSC_LZ_H_
#include <Common.h>
#include <csc_model.h>
#include <csc_mf.h>

class Model;

class LZ
{
public:
    Model *model_;
    int Init(uint32_t wnd_size, uint32_t hashBits,uint32_t hashWidth);
    int Init(uint32_t WindowSize);

    void EncodeNormal(uint8_t *src, uint32_t size, uint32_t lz_mode);
    uint32_t CheckDuplicate(uint8_t *src,uint32_t size, uint32_t type);
    void DuplicateInsert(uint8_t *src,uint32_t size);

    void Reset(void);
    void Destroy(void);
    uint32_t wnd_curpos_;

private:
    uint8_t  *wnd_;
    uint32_t rep_dist_[4];
    uint32_t wnd_size_;
    uint32_t curblock_endpos;
    uint32_t good_len_;

    // New LZ77 Algorithm====================================
// ============== OPTIMAL ====
    struct APUnit {
        uint32_t dist;
        uint32_t state;
        int back_pos;
        int next_pos;
        uint32_t price;
        uint32_t lit;
        uint32_t rep_dist[4];
    };
// ===========================
    static const int AP_LIMIT = 4096;
    APUnit apunits_[AP_LIMIT + 1];

    int compress_normal(uint32_t size, bool lazy);
    void encode_nonlit(MFUnit u);

    int compress_fast(uint32_t size);
    int compress_advanced(uint32_t size);
    void ap_backward(int idx);

    MatchFinder mf_;
    MFUnit *appt_;
};


#endif

