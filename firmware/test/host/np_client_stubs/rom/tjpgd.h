#pragma once

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef enum { JDR_OK = 0, JDR_INTR, JDR_INP, JDR_MEM1, JDR_MEM2, JDR_PAR, JDR_FMT1, JDR_FMT2, JDR_FMT3 } JRESULT;
typedef struct {
    WORD left, right, top, bottom;
} JRECT;
typedef struct JDEC JDEC;
struct JDEC {
    UINT width, height;
    void *device;
};
JRESULT jd_prepare(JDEC *, UINT (*)(JDEC *, BYTE *, UINT), void *, UINT, void *);
JRESULT jd_decomp(JDEC *, UINT (*)(JDEC *, void *, JRECT *), BYTE);
