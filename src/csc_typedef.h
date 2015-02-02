#ifndef _CSC_TYPEDEF_H_
#define _CSC_TYPEDEF_H_

#include <stdint.h>

const uint32_t KB = 1024;
const uint32_t MB = 1048576;
const uint32_t MinBlockSize = 8 * KB;


const uint32_t MaxDictSize = 1024 * MB;//Don't change
const uint32_t MinDictSize = 32 * KB;//Don't change

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))


/*****ERRORS*****************/
#define NO_ERROR 0
#define CANT_OPEN_FILE (-100)
#define CANT_CREATE_FILE (-99)
#define NOT_CSC_FILE (-98)
#define VERSION_INVALID (-97)
#define CSC_FILE_INVALID (-95)
#define DECODE_ERROR (-96)
#define CANT_ALLOC_MEM (-94)
#define ALREADY_INITIALIZED (-93)
#define OPERATION_ERROR (-92)
#define FILE_DIDNT_OPEN (-91)
/*****ERRORS*****************/

/******Block Type*************/
const uint32_t DT_NONE = 0x00;
const uint32_t DT_NORMAL = 0x01;
const uint32_t DT_ENGTXT = 0x02;
const uint32_t DT_EXE = 0x03;
const uint32_t DT_FAST = 0x04;

///////////////////////////
const uint32_t DT_NO_LZ = 0x05;

//const uint32_t DT_AUDIO = 0x06;
//const uint32_t DT_AUDIO = 0x06;
const uint32_t DT_BAD = 0x08;
const uint32_t SIG_EOF = 0x09;
const uint32_t DT_DLT = 0x10;
const uint32_t DLT_CHANNEL_MAX = 5;
const uint32_t DltIndex[DLT_CHANNEL_MAX]={1,2,3,4,8};

// DT_SKIP means same with last one
const uint32_t DT_SKIP = 0x1E;
const uint32_t DT_MAXINVALID = 0x1F;
/******Block Type*************/


#endif
