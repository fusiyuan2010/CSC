#include <csc_mf.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <csc_model.h>

#include <emmintrin.h>
#define PREFETCH_T0(addr) _mm_prefetch(((char *)(addr)),_MM_HINT_T0)
#define PREFETCH_FETCH_DIST 32

#define HASH6(a, bits) (((*(uint32_t*)(a)^(*(uint16_t*)((a)+4)<<13))*2654435761u)>>(32-(bits)))

//#define HASH5(a) (((*(uint32_t*)(a)^(*(uint16_t*)((a)+3)<<13))*2654435761u)>>(32-21))

#define HASH4(a, bits) (((*(uint32_t*)(a))*2654435761u)>>(32-bits))

#define HASH3(a) ((*(a)<<8)^(*((a)+1)<<5)^(*((a)+2)))
#define HASH2(a) (*(uint16_t*)(a))




int MatchFinder::Init(uint8_t *wnd, 
        uint32_t wnd_size,
        uint32_t bt_size, 
        uint32_t bt_bits, 
        uint32_t ht_width,
        uint32_t ht_bits
        )
{
    wnd_ = wnd;
    wnd_size_ = wnd_size;
    vld_rge_ = wnd_size_ - MinBlockSize - 4;
    pos_ = vld_rge_;
    bt_pos_ = 0;

    ht_bits_ = ht_bits;
    ht_width_ = ht_width;
    bt_bits_ = bt_bits;
    bt_size_ = bt_size;

    if (!bt_bits_ || !bt_size_) 
        bt_bits_ = bt_size_ = 0;
    if (!ht_bits_ || !ht_width_) 
        ht_bits_ = ht_width_ = 0;

    size_ = HT2_SIZE_ + HT3_SIZE_ + (1 << ht_bits_) * ht_width_;
    if (bt_bits_) {
        size_ += (1 << bt_bits_);
        size_ += bt_size_ * 2;
    }

    mfbuf_ = (uint32_t *)malloc(sizeof(uint32_t) * size_);
    if (!mfbuf_)
        return -1;
    memset(mfbuf_, 0, size_ * sizeof(uint32_t));

    uint32_t cpos = 0;
    ht2_ = mfbuf_ + cpos;
    cpos += HT2_SIZE_;
    ht3_ = mfbuf_ + cpos;
    cpos += HT3_SIZE_;

    if (ht_bits_) {
        ht6_ = mfbuf_ + cpos;
        cpos += ht_width_ * (1 << ht_bits_);
    }

    if (bt_bits_) {
        bt_head_ = mfbuf_ + cpos;
        cpos += (1 << bt_bits_);
        bt_nodes_ = mfbuf_ + cpos;
        cpos += bt_size_ * 2;
    }

    return 0;
}

void MatchFinder::normalize()
{
    uint32_t diff = pos_ - vld_rge_ + 1;
    for(uint32_t i = 0; i < size_; i++)
        mfbuf_[i] = mfbuf_[i] > diff? mfbuf_[i] - diff : 0;
    pos_ -= diff;
}

void MatchFinder::Destroy()
{
    free(mfbuf_);
}

void MatchFinder::SetArg(int bt_cyc, int ht_cyc, int good_len)
{
    bt_cyc_ = bt_cyc;
    ht_cyc_ = ht_cyc;
    good_len_ = good_len;
}

void bug()
{
    printf("!");
}

void MatchFinder::SlidePos(uint32_t wnd_pos, uint32_t len, uint32_t limit)
{
    uint32_t h2, h3, h4, h6, lasth6 = 0;
    for(uint32_t i = 1; i < len; ) {
        uint32_t wpos = wnd_pos + i;
        if (pos_ >= 0xFFFFFFF0) normalize();
        h2 = HASH2(wnd_ + wpos);
        h3 = HASH3(wnd_ + wpos);
        h6 = HASH6(wnd_ + wpos, ht_bits_);
        h4 = HASH6(wnd_ + wpos, bt_bits_);
        ht2_[h2] = pos_;
        ht3_[h3] = pos_;

        uint32_t *ht6 = ht6_ + h6 * ht_width_;
        if (i + 128 < len) {i += 4; pos_ += 4; bt_pos_ += 4; continue;}
        if (h6 != lasth6) {
            for(uint32_t j = ht_width_ - 1; j > 0; j--)
                ht6[j] = ht6[j - 1];
        }
        ht6[0] = pos_;
        lasth6 = h6;

        if (!bt_head_) { pos_++; i++; bt_pos_ ++; continue; }
        if (bt_pos_ >= bt_size_) bt_pos_ -= bt_size_;
        uint32_t dist = pos_ - bt_head_[h4];
        uint32_t *l = &bt_nodes_[bt_pos_ * 2], *r = &bt_nodes_[bt_pos_ * 2 + 1];
        uint32_t lenl = 0, lenr = 0;

        for(uint32_t cyc = 0; ; cyc++) {
            if (cyc >= bt_cyc_ || dist >= bt_size_ || dist >= vld_rge_) { *l = *r = 0; break; }
            uint32_t cmp_pos = wpos >= dist ? wpos - dist : wpos + wnd_size_ - dist;
            uint32_t clen = MIN(lenl, lenr);
            uint32_t climit = MIN(limit - i, wnd_size_ - cmp_pos);
            if (clen >= climit) { *l = *r = 0; break; }

            uint32_t bt_npos = bt_pos_ >= dist ? bt_pos_ - dist : bt_pos_ + bt_size_ - dist;
            uint32_t *tlast = &bt_nodes_[bt_npos * 2];
            PREFETCH_T0(tlast);
            uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos ;
            if (pcur[clen] == pmatch[clen]) {
                uint32_t climit2 = MIN(good_len_, climit);
                clen++;
                while(clen < climit2 && pcur[clen] == pmatch[clen])
                    clen++;

                if (clen >= good_len_) {
                    *l = tlast[0]; *r = tlast[1]; break;
                } else if (clen >= climit2) {
                    *l = *r = 0; break;
                }
            }

            if (pmatch[clen] < pcur[clen]) {
                *l = pos_ - dist;
                dist = pos_ - *(l = &tlast[1]);
                lenl = clen;
            } else {
                *r = pos_ - dist;
                dist = pos_ - *(r = &tlast[0]);
                lenr = clen;
            }
        }
        bt_head_[h4] = pos_;

        bt_pos_++;
        pos_++;
        i++;
    }
}

uint32_t MatchFinder::find_match(MFUnit *ret, uint32_t *rep_dist, uint32_t wpos, uint32_t limit)
{
    static const uint32_t bound[] = {0, 0, 64, 1024, 16 * KB, 256 * KB, 4 * MB};
    uint32_t h2 = HASH2(wnd_ + wpos);
    uint32_t h3 = HASH3(wnd_ + wpos);
    uint32_t h6 = HASH6(wnd_ + wpos, ht_bits_);
    uint32_t h4 = HASH6(wnd_ + wpos, bt_bits_);
    uint32_t minlen = 1, cnt = 0, dist = 0;

    PREFETCH_T0(ht6_ + h6 * ht_width_);
    PREFETCH_T0(ht2_ + h2);
    PREFETCH_T0(ht3_ + h3);

    for(uint32_t i = 0; i < 4; i++) {
        if (rep_dist[i] >= vld_rge_) continue;
        uint32_t cmp_pos = wpos >= rep_dist[i] ? wpos - rep_dist[i] : wpos + wnd_size_ - rep_dist[i];
        uint32_t climit = MIN(limit, wnd_size_ - cmp_pos);
        uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos, *pend = pmatch + climit;
        if (minlen >= climit || pmatch[minlen] != pcur[minlen]) continue;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len && i == 0) {
            // rep0len1
            ret[cnt].len = 1;
            ret[cnt].dist = 1;
            if (cnt + 2 < MF_CAND_LIMIT)
                cnt++;
        }
        if (match_len > minlen) {
            minlen = match_len;
            ret[cnt].len = match_len;
            ret[cnt].dist = 1 + i;
            if (cnt + 2 < MF_CAND_LIMIT)
                cnt++;
            if (match_len >= good_len_) {
                dist = 0xFFFFFFFF; //disable all further find
                break;
            }
        }
    }

    if (pos_ - ht2_[h2] > dist) for(;;) {
        dist = pos_ - ht2_[h2];
        if (dist >= vld_rge_) break;
        uint32_t cmp_pos = wpos > dist ? wpos - dist : wpos + wnd_size_ - dist;
        uint32_t climit = MIN(limit, wnd_size_ - cmp_pos);
        uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos, *pend = pmatch + climit;
        if (minlen >= climit || pmatch[minlen] != pcur[minlen]) break;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len > minlen) {
            minlen = match_len;
            if (match_len <= 6 && dist >= bound[match_len]) break;
            ret[cnt].len = match_len;
            ret[cnt].dist = 4 + dist;
            if (cnt + 2 < MF_CAND_LIMIT)
                cnt++;
            if (match_len >= good_len_) {
                dist = 0xFFFFFFFF; //disable all further find
                break;
            }
        }
        break;
    }

    if (pos_ - ht3_[h3] > dist) for(;;) {
        dist = pos_ - ht3_[h3];
        if (dist >= vld_rge_) break;
        uint32_t cmp_pos = wpos >= dist ? wpos - dist : wpos + wnd_size_ - dist;
        uint32_t climit = MIN(limit, wnd_size_ - cmp_pos);
        uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos, *pend = pmatch + climit;
        if (minlen >= climit || pmatch[minlen] != pcur[minlen]) break;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len > minlen) {
            minlen = match_len;
            if (match_len <= 6 && dist >= bound[match_len]) break;
            ret[cnt].len = match_len;
            ret[cnt].dist = 4 + dist;
            if (cnt + 2 < MF_CAND_LIMIT)
                cnt++;
            if (match_len >= good_len_) {
                dist = 0xFFFFFFFF; //disable all further find
                break;
            }
        }
        break;
    }

    if (bt_head_) {
        dist = pos_ - bt_head_[h4];
        uint32_t *l = &bt_nodes_[bt_pos_ * 2], *r = &bt_nodes_[bt_pos_ * 2 + 1];
        uint32_t lenl = 0, lenr = 0;
        for(uint32_t cyc = 0; ; cyc++) {
            if (cyc >= bt_cyc_ || dist >= bt_size_ || dist >= vld_rge_) { *l = *r = 0; break; }
            uint32_t cmp_pos = wpos >= dist ? wpos - dist : wpos + wnd_size_ - dist;
            uint32_t clen = MIN(lenl, lenr);
            uint32_t climit = MIN(limit, wnd_size_ - cmp_pos);
            if (clen >= climit) { *l = *r = 0; break; }

            uint32_t bt_npos = bt_pos_ >= dist ? bt_pos_ - dist : bt_pos_ + bt_size_ - dist;
            uint32_t *tlast = &bt_nodes_[bt_npos * 2];
            PREFETCH_T0(tlast);
            uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos ;
            if (pcur[clen] == pmatch[clen]) {
                uint32_t climit2 = MIN(good_len_, climit);
                clen++;
                while(clen < climit2 && pcur[clen] == pmatch[clen])
                    clen++;

                if (clen > minlen) {
                    minlen = clen;
                    if (clen > 6 || dist < bound[clen]) {
                        ret[cnt].len = clen;
                        ret[cnt].dist = 4 + dist;
                        if (cnt + 2 < MF_CAND_LIMIT) cnt++;
                    }
                }

                if (clen >= good_len_) {
                    *l = tlast[0]; *r = tlast[1]; dist = 0xFFFFFFFF; break;
                } else if (clen >= climit2) {
                    *l = *r = 0; break;
                }
            }

            if (pmatch[clen] < pcur[clen]) {
                *l = pos_ - dist;
                dist = pos_ - *(l = &tlast[1]);
                lenl = clen;
            } else {
                *r = pos_ - dist;
                dist = pos_ - *(r = &tlast[0]);
                lenr = clen;
            }
        }
        bt_head_[h4] = pos_;
        if (++bt_pos_ >= bt_size_) bt_pos_ -= bt_size_;
    }

    uint32_t *ht6 = ht6_ + h6 * ht_width_;
    for(uint32_t i = 0; i < ht_width_; i++) {
        if (pos_ - ht6[i] <= dist) continue;
        dist = pos_ - ht6[i];
        if (dist >= vld_rge_) continue;
        uint32_t cmp_pos = wpos >= dist ? wpos - dist : wpos + wnd_size_ - dist;
        uint32_t climit = MIN(limit, wnd_size_ - cmp_pos);
        uint8_t *pcur = wnd_ + wpos, *pmatch = wnd_ + cmp_pos, *pend = pmatch + climit;
        if (minlen >= climit || pmatch[minlen] != pcur[minlen]) continue;
        while(pmatch + 4 <= pend && *(uint32_t *)pcur == *(uint32_t *)pmatch) {
            pmatch += 4; pcur += 4; }
        if (pmatch + 2 <= pend && *(uint16_t *)pcur == *(uint16_t *)pmatch) {
            pmatch += 2; pcur += 2; }
        if (pmatch < pend && *pcur == *pmatch) { pmatch++; pcur++; }
        uint32_t match_len = (pcur - wnd_) - wpos;
        if (match_len > minlen) {
            minlen = match_len;
            if (match_len <= 6 && dist >= bound[match_len]) continue;
            ret[cnt].len = match_len;
            ret[cnt].dist = 4 + dist;
            if (cnt + 2 < MF_CAND_LIMIT)
                cnt++;
            if (match_len >= good_len_) {
                dist = 0xFFFFFFFF; //disable all further find
                break;
            }
        }
    }

    ht2_[h2] = pos_;
    ht3_[h3] = pos_;
    for(uint32_t i = ht_width_ - 1; i > 0; i--)
        ht6[i] = ht6[i-1];
    ht6[0] = pos_;
    if (++pos_ >= 0xFFFFFFF0) normalize();
    return cnt;
}

MFUnit MatchFinder::FindMatch(uint32_t *rep_dist, uint32_t wnd_pos, uint32_t limit)
{
    static const uint32_t cof[] = {0, 5, 9, 13};
    mfcand_[0].len = 1;
    mfcand_[0].dist = 0;
    uint32_t n = find_match(mfcand_ + 1, rep_dist, wnd_pos, limit);
    int bestidx = 0;
    for(uint32_t i = 1; i <= n; i++) {
        if (!bestidx || mfcand_[i].len > mfcand_[bestidx].len + 3
            || (mfcand_[i].dist <= 4)
            || (mfcand_[bestidx].dist > 4
                && (mfcand_[i].dist >> cof[mfcand_[i].len - mfcand_[bestidx].len]) < mfcand_[bestidx].dist)) 
            bestidx = i;
    }
    return mfcand_[bestidx];
}

bool MatchFinder::SecondMatchBetter(MFUnit u1, MFUnit u2)
{
    static const uint32_t cof[] = {0, 5, 9, 13};
    return (u2.len > 1 && (
                (u2.len > u1.len + 3)
                || (u2.len > u1.len && u2.dist <= 4)
                || (u2.len + 2 > u1.len && u2.dist <= 4 && u1.dist > 4)
                || (u2.len >= u1.len && u1.dist > 4 
                    && (u2.dist >> cof[u2.len - u1.len]) < u1.dist)
                || (u2.len < u1.len && u2.len + 2 >= u1.len && u1.dist > 4 
                    && (u1.dist >> cof[u1.len - u2.len]) > u2.dist)
                ));
}

void MatchFinder::FindMatchWithPrice(Model *model, uint32_t state, MFUnit *ret, uint32_t *rep_dist, uint32_t wnd_pos, uint32_t limit)
{
    static const uint32_t bound[] = {0, 0, 64, 1024, 16 * KB, 256 * KB, 4 * MB};
    mfcand_[0].len = 1;
    mfcand_[0].dist = 0;

    // ret[0] is the longest match
    // ret[1 .. n] are price tables by match length as index
    uint32_t n = find_match(mfcand_ + 1, rep_dist, wnd_pos, limit);
    ret[0] = mfcand_[n];

    if (ret[0].len >= good_len_)
        return;

    ret[1].dist = 0;
    uint32_t lpos = 1;
    for(uint32_t i = 1; i <= n; i++) {
        uint32_t distprice = 0;
        uint32_t rdist = 0;
        if (mfcand_[i].len == 1 && mfcand_[i].dist == 1) {
            ret[1].price = model->GetRep0Len1Price(state);
            ret[1].dist = 1;
            continue;
        } else if (mfcand_[i].dist <= 4) {
            distprice = model->GetRepDistPrice(state, mfcand_[i].dist - 1);
            rdist = 0;
        } else {
            distprice = model->GetMatchDistPrice(state, mfcand_[i].dist - 5);
            rdist = mfcand_[i].dist - 4;
        }

        while(lpos < mfcand_[i].len) {
            lpos++;
            if (lpos <= 6 && rdist >= bound[lpos]) {
                ret[lpos].dist = 0;
                continue;
            }
            ret[lpos].dist = mfcand_[i].dist;
            ret[lpos].price = distprice + model->GetMatchLenPrice(state, lpos - 2);
        }
    }
}


