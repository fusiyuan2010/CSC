#ifndef _CSC_ANALYZER_H_
#define _CSC_ANALYZER_H_

#include "Common.h"


class Analyzer
{
public:
	void Init();
	//~Analyzer();
	uint32_t analyze(uint8_t* src,uint32_t size);
	uint32_t analyzeHeader(uint8_t *src,uint32_t size,uint32_t *typeArg1,uint32_t *typeArg2,uint32_t *typeArg3);

private:
	uint32_t logTable[(MinBlockSize>>4)+1];
	int32_t GetChnIdx(uint8_t *src,uint32_t size);
};


#endif

