#include <csc_model.h>
#include <math.h>

#include <stdlib.h>



#define FEncodeBit(price,v,p) do\
{\
	if (v)\
		price+=p_2_bits_[p>>3];\
	else\
		price+=p_2_bits_[(4096-p)>>3];\
}while(0)


uint32_t Model::GetLiteralPrice(uint32_t fstate,uint32_t fctx,uint32_t c)
{
	uint32_t result=0;

	FEncodeBit(result,0,p_state_[fstate*3+0]);

	c=c|0x100;
	uint32_t *p=&p_lit_[(fctx)*256];
	do
	{
		FEncodeBit(result,(c >> 7) & 1,p[c>>8]);
		c <<= 1;
	}
	while (c < 0x10000);

	return result;
}

uint32_t Model::Get1BMatchPrice(uint32_t fstate)
{
	uint32_t result=0;
	FEncodeBit(result,1,p_state_[fstate*3+0]);
	FEncodeBit(result,0,p_state_[fstate*3+1]);
	FEncodeBit(result,0,p_state_[fstate*3+2]);
	return result;
}


uint32_t Model::GetRepDistMatchPrice(uint32_t fstate,uint32_t repIndex,uint32_t matchLen)
{
	uint32_t result=0;
	matchLen--;

	static uint32_t i,j,slot,c;

	FEncodeBit(result,1,p_state_[fstate*3+0]);
	FEncodeBit(result,0,p_state_[fstate*3+1]);
	FEncodeBit(result,1,p_state_[fstate*3+2]);

	i=1;
	j=(repIndex>>1)&1; FEncodeBit(result,j,p_repdist_[fstate*4+i]); i+=i+j;
	j=(repIndex)&1; FEncodeBit(result,j,p_repdist_[fstate*4+i]); 


	if (matchLen>129)
	{
		c=15|0x10;
		do
		{
			FEncodeBit(result,(c >> 3) & 1,p_len_[c>>4]);
			c <<= 1;
		}
		while (c < 0x100);

		matchLen-=129;

		while(matchLen>129)
		{
			matchLen-=129;
			FEncodeBit(result,0,p_longlen_);
		}

		FEncodeBit(result,1,p_longlen_);
	}

	for (slot=0;slot<17;slot++)
		if (matchLenBound[slot]>matchLen) break;

	slot--;

	c=slot|0x10;

	do
	{
		FEncodeBit(result,(c >> 3) & 1,p_len_[c>>4]);
		c <<= 1;
	}
	while (c < 0x100);

	result+=matchLenExtraBit[slot]*128;

	return result;
}


uint32_t Model::GetMatchPrice(uint32_t fstate,uint32_t matchDist,uint32_t matchLen)
{
	matchLen--;//no -1 in LZ part when call this

	uint32_t result=0;
    uint32_t slot,c;
	uint32_t realMatchLen=matchLen;

	FEncodeBit(result,1,p_state_[fstate*3+0]);
	FEncodeBit(result,1,p_state_[fstate*3+1]);

	if (matchLen>129)
	{
		c=15|0x10;
		do
		{
			FEncodeBit(result,(c >> 3) & 1,p_len_[c>>4]);
			c <<= 1;
		}
		while (c < 0x100);

		matchLen-=129;

		while(matchLen>129)
		{
			matchLen-=129;
			FEncodeBit(result,0,p_longlen_);
		}

		FEncodeBit(result,1,p_longlen_);
	}

	for (slot=0;slot<17;slot++)
		if (matchLenBound[slot]>matchLen) break;

	slot--;

	c=slot|0x10;

	do
	{
		FEncodeBit(result,(c >> 3) & 1,p_len_[c>>4]);
		c <<= 1;
	}
	while (c < 0x100);

	result+=matchLenExtraBit[slot]*128;

	if (realMatchLen==1)
	{
		for (slot=0;slot<9;slot++)
			if (MDistBound3[slot]>matchDist) break;

		slot--;

		c=slot|0x08;

		uint32_t *p=&p_dist3_[0];

		do
		{
			FEncodeBit(result,(c >> 2) & 1,p[c>>3]);
			c <<= 1;
		}
		while (c < 0x40);

		result+=MDistExtraBit3[slot]*128;

	}
	else if (realMatchLen==2)
	{
		for (slot=0;slot<17;slot++)
			if (MDistBound2[slot]>matchDist) break;

		slot--;

		c=slot|0x10;

		uint32_t *p=&p_dist2_[0];
		do
		{
			FEncodeBit(result,(c >> 3) & 1,p[c>>4]);
			c <<= 1;
		}
		while (c < 0x100);

		result+=MDistExtraBit2[slot]*128;
	}
	else
	{
		uint32_t lenCtx=realMatchLen>5?3:realMatchLen-3;
		for (slot=0;slot<65;slot++)
			if (MDistBound1[slot]>matchDist) break;

		slot--;

		c=slot|0x40;

		uint32_t *p=&p_dist1_[lenCtx*64];
		do
		{
			FEncodeBit(result,(c >> 5) & 1,p[c>>6]);
			c <<= 1;
		}
		while (c < 0x1000);

		result+=MDistExtraBit1[slot]*128;
	}
	
	return result;
}
