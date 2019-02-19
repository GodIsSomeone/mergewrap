/*
 * This file is an implement for the multi-pictures all in one.
 *
 * All Rights Reserved by Kedacom.com(-2019)
 * Author: Summer Shang (shangdejian@kedacom.com)
 * Date: Jan. 2rd 2019
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libyuv.h"
#include "mcmerge.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MCMERGE_DEBUG
#define debug(fmt, ...)                 printf("\e[32;40m" "%s-%d: " fmt "\e[0m" "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

#define warning(fmt, ...)               printf("\e[31;40m" "%s-%d: " fmt "\e[0m" "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define SAFE_FREE(p)                    if (p){free(p);(p)=NULL;}

#define FILL_RECT

#define FALSE                           0
#define TRUE                            1

#define FRAC(a, b)                      ((a << 8) + b)
#define FRAC_A(num)                     ((num & 0xff00) >> 8)
#define FRAC_B(num)                     (num & 0xff)

/* BT601 RGB->YUV */
#define YUVCLIP(x)                      (x > 255 ? 255: (x < 0 ? 0 : x))
#define RGB2Y(u8R, u8G, u8B)            YUVCLIP((((76 * u8R + 150 * u8G + 29 * u8B + 128) >> 8)))
#define RGB2U(u8R, u8G, u8B)            YUVCLIP((((-43 * u8R - 84 * u8G + 128 * u8B + 128) >> 8) + 128))
#define RGB2V(u8R, u8G, u8B)            YUVCLIP((((128 * u8R - 107 * u8G - 20 * u8B + 128) >> 8) + 128))

#define COLORSPLIT(nColor, offset)      (u8)((nColor>>offset)&0xFF)
#define RGB2R(nRGB)                     COLORSPLIT(nRGB, 16)
#define RGB2G(nRGB)                     COLORSPLIT(nRGB, 8)
#define RGB2B(nRGB)                     COLORSPLIT(nRGB, 0)

#define MCMERGE_STYLE_ID_INFO(id)       #id

#define COORDINATE_ALIGN                0xFFFE
#define SCALE_ALIGN                     (-4)

#define MC_ADDRESS_ALIGN                32

#define MC_BORDER_GAP                   4               //边框之间的最大空隙，小于最大空隙认为相连

static const s32 g_nExtraWinNum         = 4;            //窗口风格中的最大额外窗口数量


/* 背景色的缓冲宽高 */
static const s32 g_nBgColorBufWidth     = 256;
static const s32 g_nBgColorBufHeight    = 256;

typedef struct tagMcList    TMcList, *PTMcList;
struct tagMcList
{
    PTMcList    pLast;
    void*       pData;
    PTMcList    pNext;
};

typedef struct
{
    u8  byY;
    u8  byU;
    u8  byV;
} TMcColorYUV;

typedef struct
{
    s32 nFrom;
    s32 nTo;
} TMcBorderLen, *PTMcBorderLen;

typedef struct
{
    s32 nCoordinate;
    TMcList* nLenList;
} TMcBorderInfo, *PTMcBorderInfo;

/* 前景画面信息结构体 */
typedef struct
{
    s16 s16Left;            //水平方向坐标
    s16 s16Top;             //垂直方向坐标
    s16 s16Width;           //宽度
    s16 s16Height;          //高度
    s16 s16XShift;          //左上角横坐标偏移量
    s16 s16YShift;          //左上角纵坐标偏移量
    s16 s16WShrink;         //宽度缩减量
    s16 s16HShrink;         //高度缩减量
}TMcMergeRect, *PTMcMergeRect;

typedef struct
{
    s32 bIsShow;

    TMcColorYUV tColorYUV;

    s16 s16Top;
    s16 s16Bottom;
    s16 s16Width;
    s16 s16Left;
    s16 s16Right;
    s16 s16Height;
} TMcFocusInfo, *PTMcFocusInfo;

typedef struct tagMcMergeHandler
{
    s32             nSubWinNum;                 //画面合成风格的窗口数量
    s32             nExtraWinNum;               //画面合成额外窗口数量(不填充前景图片)
    TMcMergeParam   tMcMergeParam;              //画面合成参数
    PTMcMergeRect   ptSrcRect;                  //source position in fg picture
    PTMcMergeRect   ptDstRect;                  //subwin position in background
    PTMcMergeRect   ptExtraRect;                //extra win position in background
    PTMcFocusInfo   ptFocusInfo;                //focus information
    PTMcList        plistBorderInfoVer;         //垂直边框信息
    PTMcList        plistBorderInfoHor;         //水平边框信息
    s32             nBorderNum;                 //边框数量(水平，垂直的线条数量)
    PTMcMergeRect   ptBorderRect;               //边框坐标
    TMcColorYUV     tBorderYUV;                 //外边框色
    TMcColorYUV     tBgYUV;                     //背景色
} TMcMergeHandler, *PTMcMergeHandler;

/* extra win info in the background */
typedef struct tagMcMergeExtraWinInfo
{
    TMcMergeStyle   enStyle;
    s32             nWinNum;

    const TMcMergeRect*     ptWinRect;
} TMcMergeExtraWin;

static inline s32
McMergeCheckMergeStyle(s32 nMergeStyle);
static inline s32
McMergeCheckBgSize(s32 nBgWidth, s32 nBgHeight);
static void
McMergeCopyMergeParam(TMcMergeParam *ptDst, const TMcMergeParam *ptSrc);
static void
McMergeStyle2MergeWinNumber(TMcMergeStyle nMergeStyle, s32& nMergeWinNum);
static s32
CaculateSubwinPosition(PTMcMergeHandler ptHandler);
static s32
CalculateScaleParam(PTMcMergeRect ptSrcRect, PTMcMergeRect ptDstRect, s32 nZoomType);
static s32
CaculateBorderPosition(PTMcMergeHandler ptHandler);
static s32
GetBorderLineNum(PTMcList plistBorderInfo);
static void
GetBorderInfo(PTMcList plistBorderInfo, PTMcMergeRect ptBorderRect, TMcMergeBgParam* ptBgParam, BOOL32 bVertical);
static s32
GenerateBorderCoordate(PTMcMergeHandler ptHandler);
static void
ClearBorderInfo(PTMcList plistBorderInfo);
static void
PrintBorderInfo(PTMcList plistBorderInfo);
static PTMcList
RefreshBorderInfo(PTMcList plistBorderInfo, s32 nCoordinate, s32 nFrom, s32 nTo);
static PTMcList
GetBorderLenCell(s32 nFrom, s32 nTo);
static void
PutBorderLenCell(PTMcList plistBorderLen);
static PTMcList
JoinLine(PTMcList plistBorderLen, s32 nFrom, s32 nTo);
static s32
CaculateExtrawinPosition(PTMcMergeHandler ptHandler);
static s32
McMergeGenBgColor(PTMcMergeHandler ptHandler);
static s32
McMergeGenBorderColor(PTMcMergeHandler ptHandler);
static s32
GenerateColor(s32 nRGB, TMcColorYUV *ptColorYUV);
static s32
GenFocusInfo(PTMcMergeHandler ptHandler);
static s32
DoGenFocusInfo(PTMcMergeBgParam ptBgParam,
               PTMcMergeFgParam ptFgParam, 
               PTMcFocusInfo ptFocusInfo,
               PTMcMergeRect ptDstRect);

static const s8* atMcMergeStyleInfo[en_MCMERGE_STYLE_MAX + 1] =
{
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_MIN),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M2_1_BR1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M2_1_BL1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M2_1_TR1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M2_1_TL1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_T1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_B1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_1_B2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_1_T2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_1_R2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M3_L1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M4_1_R3),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M4_1_D3),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M5_1_R4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M5_1_D4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M5_2_D3),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M6),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M6_1_5),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M6_2_B4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M6_1_B5),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M6_B5),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_TL4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_TR4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_BL4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_BR4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_BLR4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_3_TLR4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M7_1_D6),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M8_1_7),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M8_4_4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M9),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M9_T4_1_D4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_2_R8),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_2_B8),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_2_T8),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_2_L8),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_2_TB8),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_1_9),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M10_L4_2_R4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M11_T5_1_D5),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M11_1_D10),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M12_1_11),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M12_3_RD9),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_TL1_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_TR1_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_BL1_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_BR1_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_1_ROUND12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_TL4_9),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M13_L6_1_R6),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M14_1_13),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M14_TL2_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M14_T5_1_2_1_D5),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M15_T3_12),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M15_T4_L3_1_R3_D4),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M16),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M16_1_15),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M17_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M17_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M17_3),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M18_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M18_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M18_3),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M19_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M19_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M20),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M21_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M21_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M22),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M23_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M23_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M24),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M25_1),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_M25_2),
    MCMERGE_STYLE_ID_INFO(en_MCMERGE_STYLE_MAX),
};

static const TMcMergeRect atMergePointstyle[en_MCMERGE_STYLE_MAX][MCMERGE_CHANNEL_MAX + 1] =
{
    /* en_MCMERGE_STYLE_MIN */
    {
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M1
    {
        {FRAC(0,1), FRAC(0,1), FRAC(1,1), FRAC(1,1)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M2
    {
        {FRAC(0,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M2_1_BR1
    {
        {FRAC(0,3), FRAC(0,3), FRAC(3,3), FRAC(3,3)},
        {FRAC(2,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M2_1_BL1
    {
        {FRAC(0,3), FRAC(0,3), FRAC(3,3), FRAC(3,3)},
        {FRAC(0,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M2_1_TR1
    {
        {FRAC(0,3), FRAC(0,3), FRAC(3,3), FRAC(3,3)},
        {FRAC(2,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M2_1_TL1
    {
        {FRAC(0,3), FRAC(0,3), FRAC(3,3), FRAC(3,3)},
        {FRAC(0,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_T1
    {
        {FRAC(1,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_B1
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(1,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_1_B2
    {
        {FRAC(0,4), FRAC(0,4), FRAC(4,4), FRAC(2,4)},
        {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_1_T2
    {
        {FRAC(0,2), FRAC(0,2), FRAC(1,2), FRAC(1,2)},
        {FRAC(1,2), FRAC(0,2), FRAC(1,2), FRAC(1,2)}, {FRAC(0,2), FRAC(1,2), FRAC(2,2), FRAC(1,2)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_1_R2
    {
        {FRAC(0,12), FRAC(0,12), FRAC(9,12), FRAC(12,12)},
        {FRAC(9,12), FRAC(2,12), FRAC(3,12), FRAC(4,12)}, {FRAC(9,12), FRAC(6,12), FRAC(3,12), FRAC(4,12)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M3_L1
    {
        {FRAC(0,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M4
    {
        {FRAC(0,2), FRAC(0,2), FRAC(1,2), FRAC(1,2)}, {FRAC(1,2), FRAC(0,2), FRAC(1,2), FRAC(1,2)},
        {FRAC(0,2), FRAC(1,2), FRAC(1,2), FRAC(1,2)}, {FRAC(1,2), FRAC(1,2), FRAC(1,2), FRAC(1,2)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M4_1_R3
    {
        {FRAC(0,6), FRAC(1,6), FRAC(4,6), FRAC(4,6)},
        {FRAC(4,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M4_1_D3
    {
        {FRAC(1,6), FRAC(0,6), FRAC(4,6), FRAC(4,6)},
        {FRAC(0,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)}, {FRAC(2,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M5_1_R4
    {
        {FRAC(0,16), FRAC(2,16), FRAC(12,16), FRAC(12,16)}, {FRAC(12,16), FRAC(0,16), FRAC(4,16), FRAC(4,16)},
        {FRAC(12,16), FRAC(4,16), FRAC(4,16), FRAC(4,16)}, {FRAC(12,16), FRAC(8,16), FRAC(4,16), FRAC(4,16)}, {FRAC(12,16), FRAC(12,16), FRAC(4,16), FRAC(4,16)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M5_1_D4
    {
        {FRAC(2,16), FRAC(0, 16), FRAC(12,16), FRAC(12,16)}, {FRAC(0,16), FRAC(12,16), FRAC(4,16), FRAC(4,16)},
        {FRAC(4,16), FRAC(12,16), FRAC(4, 16), FRAC(4, 16)}, {FRAC(8,16), FRAC(12,16), FRAC(4,16), FRAC(4,16)}, {FRAC(12,16), FRAC(12,16), FRAC(4,16), FRAC(4,16)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M5_2_D3
    {
        {FRAC(0,36), FRAC(3, 36), FRAC(18,36), FRAC(18,36)}, {FRAC(18,36), FRAC(3, 36), FRAC(18,36), FRAC(18,36)},
        {FRAC(0,36), FRAC(21,36), FRAC(12,36), FRAC(12,36)}, {FRAC(12,36), FRAC(21,36), FRAC(12,36), FRAC(12,36)}, {FRAC(24,36), FRAC(21,36), FRAC(12,36), FRAC(12,36)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M6
    {
        {FRAC(0,6), FRAC(0,6), FRAC(2,6), FRAC(3,6)}, {FRAC(2,6), FRAC(0,6), FRAC(2,6), FRAC(3,6)}, {FRAC(4,6), FRAC(0,6), FRAC(2,6), FRAC(3,6)},
        {FRAC(0,6), FRAC(3,6), FRAC(2,6), FRAC(3,6)}, {FRAC(2,6), FRAC(3,6), FRAC(2,6), FRAC(3,6)}, {FRAC(4,6), FRAC(3,6), FRAC(2,6), FRAC(3,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M6_1_5
    {
        {FRAC(0,3), FRAC(0,3), FRAC(2,3), FRAC(2,3)}, {FRAC(2,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)}, {FRAC(2,3), FRAC(1,3), FRAC(1,3), FRAC(1,3)},
        {FRAC(0,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)}, {FRAC(1,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)}, {FRAC(2,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M6_2_B4
    {
        {FRAC(0,12), FRAC(0,12), FRAC(6,12), FRAC(8,12)}, {FRAC(6,12), FRAC(0,12), FRAC(6,12), FRAC(8,12)}, {FRAC(0,12), FRAC(8,12), FRAC(3,12), FRAC(4,12)},
        {FRAC(3,12), FRAC(8,12), FRAC(3,12), FRAC(4,12)}, {FRAC(6,12), FRAC(8,12), FRAC(3,12), FRAC(4,12)}, {FRAC(9,12), FRAC(8,12), FRAC(3,12), FRAC(4,12)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M6_1_B5
    {
        {FRAC(0,5), FRAC(0,5), FRAC(5,5), FRAC(4,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M6_B5
    {
        {FRAC(1,10), FRAC(0,10), FRAC(8,10), FRAC(8,10)}, {FRAC(0,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(4,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(8,105), FRAC(2,10), FRAC(2,10)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_TL4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},{FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_TR4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_BL4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_BR4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_BLR4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_3_TLR4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M7_1_D6
    {
        {FRAC(1,12), FRAC(0, 12), FRAC(10,12), FRAC(10,12)}, {FRAC(0,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(2,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)},
        {FRAC(4,12), FRAC(10,12), FRAC(2, 12), FRAC(2, 12)}, {FRAC(6,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(8,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)},{FRAC(10,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M8_1_7
    {
        {FRAC(0,4), FRAC(0,4), FRAC(3,4), FRAC(3,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M8_4_4
    {
        {FRAC(0,20), FRAC(0, 20), FRAC(8,20), FRAC(10,20)}, {FRAC(8,20), FRAC(0, 20), FRAC(8,20), FRAC(10,20)}, {FRAC(16,20), FRAC(0, 20), FRAC(4, 20), FRAC(5,20)}, {FRAC(16,20), FRAC(5, 20), FRAC(4,20), FRAC(5,20)},
        {FRAC(0,20), FRAC(10,20), FRAC(8,20), FRAC(10,20)}, {FRAC(8,20), FRAC(10,20), FRAC(8,20), FRAC(10,20)}, {FRAC(16,20), FRAC(10,20), FRAC(4, 20), FRAC(5,20)}, {FRAC(16,20), FRAC(15,20), FRAC(4,20), FRAC(5,20)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M9
    {
        {FRAC(0,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)}, {FRAC(1,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)}, {FRAC(2,3), FRAC(0,3), FRAC(1,3), FRAC(1,3)},
        {FRAC(0,3), FRAC(1,3), FRAC(1,3), FRAC(1,3)}, {FRAC(1,3), FRAC(1,3), FRAC(1,3), FRAC(1,3)}, {FRAC(2,3), FRAC(1,3), FRAC(1,3), FRAC(1,3)},
        {FRAC(0,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)}, {FRAC(1,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)}, {FRAC(2,3), FRAC(2,3), FRAC(1,3), FRAC(1,3)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M9_T4_1_D4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_2_R8
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_2_B8
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_2_T8
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_2_L8
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_2_TB8
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_1_9
    {
        {FRAC(0,5), FRAC(0,5), FRAC(4,5), FRAC(4,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M10_L4_2_R4
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M11_T5_1_D5
    {
        {FRAC(0,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(1,5), FRAC(3,5), FRAC(3,5)},
        {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M11_1_D10
    {
        {FRAC(1,5), FRAC(0,5), FRAC(3,5), FRAC(3,5)}, {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M12_1_11
    {
        {FRAC(0,6), FRAC(0,6), FRAC(5,6), FRAC(5,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M12_3_RD9
    {
        {FRAC(0,6), FRAC(0,6), FRAC(3,6), FRAC(3,6)}, {FRAC(3,6), FRAC(0,6), FRAC(3,6), FRAC(3,6)}, {FRAC(0,6), FRAC(3,6), FRAC(3,6), FRAC(3,6)}, {FRAC(3,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,4), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_TL1_12
    {
        {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_TR1_12
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(2,4), FRAC(2,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_BL1_12
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_BR1_12
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(2,4), FRAC(2,4), FRAC(2,4), FRAC(2,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_1_ROUND12
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(1,4), FRAC(1,4), FRAC(2,4), FRAC(2,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_TL4_9
    {
        {FRAC(0,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(2,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(2,5), FRAC(2,5), FRAC(2,5)}, {FRAC(2,5), FRAC(2,5), FRAC(2,5), FRAC(2,5)},
        {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M13_L6_1_R6
    {
        {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(1,6), FRAC(4,6), FRAC(4,6)}, {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M14_1_13
    {
        {FRAC(0,7), FRAC(0,7), FRAC(6,7), FRAC(6,7)}, {FRAC(6,7), FRAC(0,7), FRAC(1,7), FRAC(1,7)}, {FRAC(6,7), FRAC(1,7), FRAC(1,7), FRAC(1,7)}, {FRAC(6,7), FRAC(2,7), FRAC(1,7), FRAC(1,7)},
        {FRAC(6,7), FRAC(3,7), FRAC(1,7), FRAC(1,7)}, {FRAC(6,7), FRAC(4,7), FRAC(1,7), FRAC(1,7)}, {FRAC(6,7), FRAC(5,7), FRAC(1,7), FRAC(1,7)}, {FRAC(0,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)},
        {FRAC(1,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)}, {FRAC(2,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)}, {FRAC(3,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)}, {FRAC(4,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)},
        {FRAC(5,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)}, {FRAC(6,7), FRAC(6,7), FRAC(1,7), FRAC(1,7)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M14_TL2_12
    {
        {FRAC(0, 20), FRAC(0, 20), FRAC(8,20), FRAC(10,20)}, {FRAC(8, 20), FRAC(0, 20), FRAC(8,20), FRAC(10,20)}, {FRAC(16,20), FRAC(0, 20), FRAC(4,20), FRAC(5,20)},
        {FRAC(16,20), FRAC(5, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(0, 20), FRAC(10,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(4, 20), FRAC(10,20), FRAC(4,20), FRAC(5,20)},
        {FRAC(8, 20), FRAC(10,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(12,20), FRAC(10,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(16,20), FRAC(10,20), FRAC(4,20), FRAC(5,20)},
        {FRAC(0, 20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(4, 20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(8, 20), FRAC(15,20), FRAC(4,20), FRAC(5,20)},
        {FRAC(12,20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(16,20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M14_T5_1_2_1_D5
    {
        {FRAC(0, 20), FRAC(0, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(4, 20), FRAC(0, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(8,20), FRAC(0, 20), FRAC(4,20), FRAC(5, 20)},
        {FRAC(12,20), FRAC(0, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(16,20), FRAC(0, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(0,20), FRAC(5, 20), FRAC(8,20), FRAC(10,20)},
        {FRAC(8, 20), FRAC(5, 20), FRAC(4,20), FRAC(5, 20)}, {FRAC(12,20), FRAC(5, 20), FRAC(8,20), FRAC(10,20)}, {FRAC(8,20), FRAC(10,20), FRAC(4,20), FRAC(5, 20)},
        {FRAC(0, 20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(4, 20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(8,20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)},
        {FRAC(12,20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)}, {FRAC(16,20), FRAC(15,20), FRAC(4,20), FRAC(5, 20)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M15_T3_12
    {
        {FRAC(0,12), FRAC(0,12), FRAC(4,12), FRAC(6,12)}, {FRAC(4,12), FRAC(0,12), FRAC(4,12), FRAC(6,12)}, {FRAC(8, 12), FRAC(0,12), FRAC(4,12), FRAC(6,12)},
        {FRAC(0,12), FRAC(6,12), FRAC(2,12), FRAC(3,12)}, {FRAC(2,12), FRAC(6,12), FRAC(2,12), FRAC(3,12)}, {FRAC(4, 12), FRAC(6,12), FRAC(2,12), FRAC(3,12)},
        {FRAC(6,12), FRAC(6,12), FRAC(2,12), FRAC(3,12)}, {FRAC(8,12), FRAC(6,12), FRAC(2,12), FRAC(3,12)}, {FRAC(10,12), FRAC(6,12), FRAC(2,12), FRAC(3,12)},
        {FRAC(0,12), FRAC(9,12), FRAC(2,12), FRAC(3,12)}, {FRAC(2,12), FRAC(9,12), FRAC(2,12), FRAC(3,12)}, {FRAC(4, 12), FRAC(9,12), FRAC(2,12), FRAC(3,12)},
        {FRAC(6,12), FRAC(9,12), FRAC(2,12), FRAC(3,12)}, {FRAC(8,12), FRAC(9,12), FRAC(2,12), FRAC(3,12)}, {FRAC(10,12), FRAC(9,12), FRAC(2,12), FRAC(3,12)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M15_T4_L3_1_R3_D4
    {
        {FRAC(1,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(3,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(5, 10), FRAC(0,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(7,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(0,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2, 10), FRAC(2,10), FRAC(6,10), FRAC(6,10)},
        {FRAC(8,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(0,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8, 10), FRAC(4,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(0,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(1, 10), FRAC(8,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(3,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(5,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(7, 10), FRAC(8,10), FRAC(2,10), FRAC(2,10)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M16
    {
        {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(1,4)},
        {FRAC(0,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(1,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(2,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)}, {FRAC(3,4), FRAC(3,4), FRAC(1,4), FRAC(1,4)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M16_1_15
    {
        {FRAC(0,8), FRAC(0,8), FRAC(7,8), FRAC(7,8)}, {FRAC(7,8), FRAC(0,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(1,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(2,8), FRAC(1,8), FRAC(1,8)},
        {FRAC(7,8), FRAC(3,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(4,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(5,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(6,8), FRAC(1,8), FRAC(1,8)},
        {FRAC(0,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(1,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(2,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(3,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)},
        {FRAC(4,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(5,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(6,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)}, {FRAC(7,8), FRAC(7,8), FRAC(1,8), FRAC(1,8)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M17_1
    {
        {FRAC(0,5), FRAC(0,5), FRAC(3,5), FRAC(3,5)}, {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(1,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M17_2
    {
        {FRAC(0,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(1,5), FRAC(1,5), FRAC(3,5), FRAC(3,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M17_3
    {
        {FRAC(1,10), FRAC(0,10), FRAC(4,10), FRAC(4,10)}, {FRAC(5,10), FRAC(0,10), FRAC(4,10), FRAC(4,10)}, {FRAC(0,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(4,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(0,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(2,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(4,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(0,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(4,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(8,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M18_1
    {
        {FRAC(0,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(2,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(0,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(2,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(2,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M18_2
    {
        {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(1,6), FRAC(2,6), FRAC(2,6)}, {FRAC(2,6), FRAC(1,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(4,6), FRAC(1,6), FRAC(2,6), FRAC(2,6)}, {FRAC(0,6), FRAC(3,6), FRAC(2,6), FRAC(2,6)}, {FRAC(2,6), FRAC(3,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(3,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M18_3
    {
        {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(4,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M19_1
    {
        {FRAC(0,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(2,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M19_2
    {
        {FRAC(0,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(2,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(2,5), FRAC(2,5), FRAC(2,5)},
        {FRAC(2,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M20
    {
        {FRAC(0, 6), FRAC(0, 6), FRAC(3,6), FRAC(3,6)}, {FRAC(3, 6), FRAC(0, 6), FRAC(3,6), FRAC(3,6)}, {FRAC(0, 6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M21_1
    {
        {FRAC(0,6), FRAC(0,6), FRAC(4,6), FRAC(4,6)}, {FRAC(4,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(3,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M21_2
    {
        {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(1,6), FRAC(4,6), FRAC(4,6)}, {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M22
    {
        {FRAC(0,5), FRAC(0,5), FRAC(2,5), FRAC(2,5)}, {FRAC(2,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(2,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(1,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M23_1
    {
        {FRAC(1,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(3,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(5,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)}, {FRAC(7,10), FRAC(0,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(0,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(4,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(8,10), FRAC(2,10), FRAC(2,10), FRAC(2,10)}, {FRAC(0,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(4,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(6,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(4,10), FRAC(2,10), FRAC(2,10)}, {FRAC(0,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(2,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(4,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(6,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(8,10), FRAC(6,10), FRAC(2,10), FRAC(2,10)}, {FRAC(1,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)},
        {FRAC(3,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(5,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {FRAC(7,10), FRAC(8,10), FRAC(2,10), FRAC(2,10)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
   //en_MCMERGE_STYLE_M23_2
    {
        {FRAC(1,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(0,6), FRAC(2,6), FRAC(2,6)}, {FRAC(0,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(2,6), FRAC(2,6), FRAC(2,6)}, {FRAC(4,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M24
    {
        {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(2,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(4,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(1,6), FRAC(1,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(1,6), FRAC(2,6), FRAC(2,6)}, {FRAC(5,6), FRAC(1,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(2,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(0,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(3,6), FRAC(2,6), FRAC(2,6)}, {FRAC(3,6), FRAC(3,6), FRAC(2,6), FRAC(2,6)},
        {FRAC(5,6), FRAC(3,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(4,6), FRAC(1,6), FRAC(1,6)}, {FRAC(0,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(1,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {FRAC(2,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(3,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(4,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)}, {FRAC(5,6), FRAC(5,6), FRAC(1,6), FRAC(1,6)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M25
    {
        {FRAC(0,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(2,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(3,5), FRAC(1,5), FRAC(1,5)},
        {FRAC(0,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(1,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(2,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(3,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)}, {FRAC(4,5), FRAC(4,5), FRAC(1,5), FRAC(1,5)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
    //en_MCMERGE_STYLE_M25_2
    {
        {FRAC(1,12), FRAC(0,12), FRAC(2,12), FRAC(2,12)},  {FRAC(1,12), FRAC(2,12), FRAC(2,12), FRAC(2,12)}, {FRAC(1,12), FRAC(4,12), FRAC(2,12), FRAC(2,12)}, {FRAC(3,12), FRAC(0,12), FRAC(6,12), FRAC(6,12)}, {FRAC(9,12), FRAC(0,12), FRAC(2,12), FRAC(2,12)},
        {FRAC(9,12), FRAC(2,12), FRAC(2,12), FRAC(2,12)}, {FRAC(9,12), FRAC(4, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(0, 12), FRAC(6, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(2, 12), FRAC(6, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(4, 12), FRAC(6, 12), FRAC(2,12), FRAC(2,12)},
        {FRAC(6,12), FRAC(6,12), FRAC(2,12), FRAC(2,12)}, {FRAC(8,12), FRAC(6, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(10,12), FRAC(6, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(0, 12), FRAC(8, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(2, 12), FRAC(8, 12), FRAC(2,12), FRAC(2,12)},
        {FRAC(4,12), FRAC(8,12), FRAC(2,12), FRAC(2,12)}, {FRAC(6,12), FRAC(8, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(8, 12), FRAC(8, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(10,12), FRAC(8, 12), FRAC(2,12), FRAC(2,12)}, {FRAC(0, 12), FRAC(10,12), FRAC(2,12), FRAC(2,12)},
        {FRAC(2,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(4,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(6, 12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(8, 12), FRAC(10,12), FRAC(2,12), FRAC(2,12)}, {FRAC(10,12), FRAC(10,12), FRAC(2,12), FRAC(2,12)},
        {0xFF, 0xFF, 0xFF, 0xFF}
    },
};

static const TMcMergeRect g_atExtraWinM2[] =
{
    {FRAC(0,4), FRAC(0,4), FRAC(4,4), FRAC(1,4)},
    {FRAC(0,4), FRAC(3,4), FRAC(4,4), FRAC(1,4)}
};
static const TMcMergeRect g_atExtraWinM3_T1[] =
{
    {FRAC(0,4), FRAC(0,4), FRAC(1,4), FRAC(2,4)},
    {FRAC(3,4), FRAC(0,4), FRAC(1,4), FRAC(2,4)}
};
static const TMcMergeRect g_atExtraWinM3_B1[] =
{
    {FRAC(0,4), FRAC(2,4), FRAC(1,4), FRAC(2,4)},
    {FRAC(3,4), FRAC(2,4), FRAC(1,4), FRAC(2,4)}
};
static const TMcMergeRect g_atExtraWinM3_1_R2[] =
{
    {FRAC(9,12), FRAC(0,12), FRAC(3,12), FRAC(2,12)},
    {FRAC(9,12), FRAC(10,12), FRAC(3,12), FRAC(2,12)},
};
static const TMcMergeRect g_atExtraWinM3_L1[] =
{
    {FRAC(0,4), FRAC(0,4), FRAC(2,4), FRAC(1,4)},
    {FRAC(0,4), FRAC(3,4), FRAC(2,4), FRAC(1,4)},
};
static const TMcMergeRect g_atExtraWinM4_1_R3[] =
{
    {FRAC(0,6), FRAC(0,6), FRAC(4,6), FRAC(1,6)},
    {FRAC(0,6), FRAC(5,6), FRAC(4,6), FRAC(1,6)},
};
static const TMcMergeRect g_atExtraWinM4_1_D3[] =
{
    {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(4,6)},
    {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(4,6)},
};
static const TMcMergeRect g_atExtraWinM5_1_R4[] =
{
    {FRAC(0,16), FRAC(0,16), FRAC(12,16), FRAC(2,16)},
    {FRAC(0,16), FRAC(14,16), FRAC(12,16), FRAC(2,16)},
};
static const TMcMergeRect g_atExtraWinM5_1_D4[] =
{
    {FRAC(0,16), FRAC(0,16), FRAC(2,16), FRAC(12,16)},
    {FRAC(14,16), FRAC(0,16), FRAC(2,16), FRAC(12,16)},
};
static const TMcMergeRect g_atExtraWinM5_2_D3[] =
{
    {FRAC(0,36), FRAC(0,36), FRAC(36,36), FRAC(3,36)},
    {FRAC(0,16), FRAC(33,36), FRAC(36,36), FRAC(3,36)},
};
static const TMcMergeRect g_atExtraWinM6_B5[] =
{
    {FRAC(0,10), FRAC(0,10), FRAC(1,10), FRAC(8,10)},
    {FRAC(9,10), FRAC(0,10), FRAC(1,10), FRAC(8,10)},
};
static const TMcMergeRect g_atExtraWinM7_1_D6[] =
{
    {FRAC(0,12), FRAC(0,12), FRAC(1,12), FRAC(10,12)},
    {FRAC(11,12), FRAC(0,12), FRAC(1,12), FRAC(10,12)},
};
static const TMcMergeRect g_atExtraWinM9_T4_1_D4[] =
{
    {FRAC(0,4), FRAC(1,4), FRAC(1,4), FRAC(2,4)},
    {FRAC(3,4), FRAC(1,4), FRAC(1,4), FRAC(2,4)},
};
static const TMcMergeRect g_atExtraWinM11_T5_1_D5[] =
{
    {FRAC(0,5), FRAC(1,5), FRAC(1,5), FRAC(3,5)},
    {FRAC(4,5), FRAC(1,5), FRAC(1,5), FRAC(3,5)},
};
static const TMcMergeRect g_atExtraWinM11_1_D10[] =
{
    {FRAC(0,5), FRAC(0,5), FRAC(1,5), FRAC(3,5)},
    {FRAC(4,5), FRAC(0,5), FRAC(1,5), FRAC(3,5)},
};
static const TMcMergeRect g_atExtraWinM13_L6_1_R6[] =
{
    {FRAC(1,6), FRAC(0,6), FRAC(4,6), FRAC(1,6)},
    {FRAC(1,6), FRAC(5,6), FRAC(4,6), FRAC(1,6)},
};
static const TMcMergeRect g_atExtraWinM15_T4_L3_1_R3_D4[] =
{
    {FRAC(0,10), FRAC(0,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(9,10), FRAC(0,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(0,10), FRAC(8,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(9,10), FRAC(8,10), FRAC(1,10), FRAC(2,10)},
};
static const TMcMergeRect g_atExtraWinM17_3[] =
{
    {FRAC(0,10), FRAC(0,10), FRAC(1,10), FRAC(4,10)},
    {FRAC(9,10), FRAC(0,10), FRAC(1,10), FRAC(4,10)},
};
static const TMcMergeRect g_atExtraWinM23_1[] =
{
    {FRAC(0,10), FRAC(0,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(9,10), FRAC(0,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(0,10), FRAC(8,10), FRAC(1,10), FRAC(2,10)},
    {FRAC(9,10), FRAC(8,10), FRAC(1,10), FRAC(2,10)},
};
static const TMcMergeRect g_atExtraWinM23_2[] =
{
    {FRAC(0,6), FRAC(0,6), FRAC(1,6), FRAC(2,6)},
    {FRAC(5,6), FRAC(0,6), FRAC(1,6), FRAC(2,6)},
};
static const TMcMergeRect g_atExtraWinM25_2[] =
{
    {FRAC(0,12), FRAC(0,12), FRAC(1,12), FRAC(6,12)},
    {FRAC(11,12), FRAC(0,12), FRAC(1,12), FRAC(6,12)},
};

#define EXTRA_WIN_INFO_CONSTRCTOR(type) \
{en_MCMERGE_STYLE_##type, sizeof(g_atExtraWin##type)/sizeof(g_atExtraWin##type[0]), g_atExtraWin##type}

static const TMcMergeExtraWin g_atExtraWinInfo[] =
{
    EXTRA_WIN_INFO_CONSTRCTOR(M2),
    EXTRA_WIN_INFO_CONSTRCTOR(M3_T1),
    EXTRA_WIN_INFO_CONSTRCTOR(M3_B1),
    EXTRA_WIN_INFO_CONSTRCTOR(M3_1_R2),
    EXTRA_WIN_INFO_CONSTRCTOR(M3_L1),
    EXTRA_WIN_INFO_CONSTRCTOR(M4_1_R3),
    EXTRA_WIN_INFO_CONSTRCTOR(M4_1_D3),
    EXTRA_WIN_INFO_CONSTRCTOR(M5_1_R4),
    EXTRA_WIN_INFO_CONSTRCTOR(M5_1_D4),
    EXTRA_WIN_INFO_CONSTRCTOR(M5_2_D3),
    EXTRA_WIN_INFO_CONSTRCTOR(M6_B5),
    EXTRA_WIN_INFO_CONSTRCTOR(M7_1_D6),
    EXTRA_WIN_INFO_CONSTRCTOR(M9_T4_1_D4),
    EXTRA_WIN_INFO_CONSTRCTOR(M11_T5_1_D5),
    EXTRA_WIN_INFO_CONSTRCTOR(M11_1_D10),
    EXTRA_WIN_INFO_CONSTRCTOR(M13_L6_1_R6),
    EXTRA_WIN_INFO_CONSTRCTOR(M15_T4_L3_1_R3_D4),
    EXTRA_WIN_INFO_CONSTRCTOR(M17_3),
    EXTRA_WIN_INFO_CONSTRCTOR(M23_1),
    EXTRA_WIN_INFO_CONSTRCTOR(M23_2),
    EXTRA_WIN_INFO_CONSTRCTOR(M25_2),
};

static const TMcMergeRect g_aOutBorderRect[en_MCMERGE_STYLE_MAX * 4] = {0};
static const s32 g_nOutBorderLineNum = 0;

/* Description:
 *   the function is used to get merger handler which will be used later.
 *
 * Param:
 *   ppvHandler         [OUT]   handler used to keep inner handler
 *   ptMcMergeParam     [IN]    param for mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jun. 22 2018
 * */
s32
McMergeOpen(void** const ppvHandler, const TMcMergeParam* ptMcMergeParam)
{
    BOOL32 bIsSuccss = FALSE;
    s32 nRet = en_MCMERGE_INVALID_PARAM;

    PTMcMergeHandler ptHandler = NULL;

    /* param check */
    if (!ppvHandler || !ptMcMergeParam) {
        warning("ppvHandler %p ptMcMergeParam %p", ppvHandler, ptMcMergeParam);
        return en_MCMERGE_INVALID_PARAM;
    }

    do {
        /* input validation */
        if (ptMcMergeParam->nMaxFgNum <= 0 ||
            ptMcMergeParam->nMaxFgNum > MCMERGE_CHANNEL_MAX) {
            warning("invalid nMaxFgNum %d max %d", ptMcMergeParam->nMaxFgNum, MCMERGE_CHANNEL_MAX);
            break;
        }
        if (en_MCMERGE_SUCCESS != McMergeCheckMergeStyle(ptMcMergeParam->nMergeStyle)) {
            break;
        }
        const TMcMergeBgParam *ptBgParam = &ptMcMergeParam->tMcMergeBgPic;
        if (en_MCMERGE_SUCCESS != 
                McMergeCheckBgSize(ptBgParam->nBgWidth, ptBgParam->nBgHeight)) {
            break;
        }

        /* allocate mem */
        nRet = en_MCMERGE_MEM_ALLOC;
        *ppvHandler = malloc(sizeof(TMcMergeHandler));
        if (NULL == *ppvHandler) {
            break;
        }

        ptHandler = (PTMcMergeHandler)(*ppvHandler);
        memset(ptHandler, 0, sizeof(TMcMergeHandler));

        ptHandler->ptSrcRect = (PTMcMergeRect)malloc(sizeof(TMcMergeRect) * ptMcMergeParam->nMaxFgNum);
        ptHandler->ptDstRect = (PTMcMergeRect)malloc(sizeof(TMcMergeRect) * ptMcMergeParam->nMaxFgNum);
        ptHandler->ptFocusInfo = (PTMcFocusInfo)malloc(sizeof(TMcFocusInfo) * ptMcMergeParam->nMaxFgNum);
        ptHandler->ptExtraRect = (PTMcMergeRect)malloc(sizeof(TMcMergeRect) * g_nExtraWinNum);
        ptHandler->tMcMergeParam.ptMcMergeFgPic =
            (PTMcMergeFgParam)malloc(sizeof(TMcMergeFgParam) * ptMcMergeParam->nMaxFgNum);
        if (!ptHandler->ptSrcRect || !ptHandler->ptDstRect ||
            !ptHandler->ptFocusInfo || !ptHandler->ptExtraRect ||
            !ptHandler->tMcMergeParam.ptMcMergeFgPic) {
            warning("ptSrcRect=%p, ptDstRect=%p, ptFocusInfo=%p, ptExtraRect=%p, ptMcMergeFgPic=%p",
                    ptHandler->ptSrcRect, ptHandler->ptDstRect, ptHandler->ptFocusInfo,
                    ptHandler->ptExtraRect, ptHandler->tMcMergeParam.ptMcMergeFgPic);
            break;
        }
        memset(ptHandler->ptSrcRect, 0, sizeof(TMcMergeRect) * ptMcMergeParam->nMaxFgNum);
        memset(ptHandler->ptDstRect, 0, sizeof(TMcMergeRect) * ptMcMergeParam->nMaxFgNum);
        memset(ptHandler->ptFocusInfo, 0, sizeof(TMcFocusInfo) * ptMcMergeParam->nMaxFgNum);
        memset(ptHandler->ptExtraRect, 0, sizeof(TMcMergeRect) * g_nExtraWinNum);
        memset(ptHandler->tMcMergeParam.ptMcMergeFgPic, 0,
            sizeof(TMcMergeFgParam) * ptMcMergeParam->nMaxFgNum);

        /* copy value to compare when set param interface called */
        McMergeStyle2MergeWinNumber(ptMcMergeParam->nMergeStyle, ptHandler->nSubWinNum);
        if (ptHandler->nSubWinNum > ptMcMergeParam->nMaxFgNum)
        {
            warning("conflict param mergetype %d nMaxFgNum %d\n",
                ptMcMergeParam->nMergeStyle, ptMcMergeParam->nMaxFgNum);
            nRet = en_MCMERGE_INVALID_PARAM;
            break;
        }
        McMergeCopyMergeParam(&ptHandler->tMcMergeParam, ptMcMergeParam);

        /* generate background color YUV */
        McMergeGenBgColor(ptHandler);

        /* caculate the postion for merge */
        nRet = CaculateSubwinPosition(ptHandler);
        nRet = CaculateExtrawinPosition(ptHandler);

        /* generate border information */
        nRet = CaculateBorderPosition(ptHandler);
        McMergeGenBorderColor(ptHandler);

        /* generate focus information */
        nRet = GenFocusInfo(ptHandler);

        if (en_MCMERGE_SUCCESS == nRet) {
            bIsSuccss = TRUE;
        }
    } while (0);

    /* recycle the resource */
    if (FALSE == bIsSuccss && ptHandler) {
        SAFE_FREE(ptHandler->ptBorderRect)
        SAFE_FREE(ptHandler->tMcMergeParam.ptMcMergeFgPic);
        SAFE_FREE(ptHandler->ptExtraRect);
        SAFE_FREE(ptHandler->ptFocusInfo);
        SAFE_FREE(ptHandler->ptDstRect);
        SAFE_FREE(ptHandler->ptSrcRect);
        SAFE_FREE(*ppvHandler);
    }

    return nRet;
}


/* Description:
 *   the function is used to close merger handler which has been opened
 *
 * Param:
 *   pvHandler          [IN]   handler pointer
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jun. 22 2018
 * */
s32
McMergeClose(void* pvHandler)
{
    PTMcMergeHandler ptHandler = (PTMcMergeHandler)pvHandler;
    if (ptHandler) {
        SAFE_FREE(ptHandler->ptBorderRect)
        SAFE_FREE(ptHandler->tMcMergeParam.ptMcMergeFgPic);
        SAFE_FREE(ptHandler->ptExtraRect);
        SAFE_FREE(ptHandler->ptFocusInfo);
        SAFE_FREE(ptHandler->ptDstRect);
        SAFE_FREE(ptHandler->ptSrcRect);
        SAFE_FREE(ptHandler);
    }

    return en_MCMERGE_SUCCESS;
}

 
/* Description:
 *   the function is used to do the merge.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   ptMcMergeIn        [IN]    input picture infomation for mcmerge
 *   ptMcMergeOut       [IN]    output picture infomation from mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 * */
s32
McMergeProcess(void* pvHandler, TMcMergeInput* ptMcMergeIn, TMcMergeOutput* ptMcMergeOut)
{
    PTMcMergeHandler ptHandler = NULL;

    if (!pvHandler || !ptMcMergeIn) {
        return en_MCMERGE_INVALID_PARAM;
    }

    ptHandler = (PTMcMergeHandler)pvHandler;

    do {
        s32 i = 0;

        /* set out param */
        s32 nDstYStride = ptMcMergeIn->tMcMergeOutputPic.nYStride ?
            ptMcMergeIn->tMcMergeOutputPic.nYStride : ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth;
        s32 nDstUVStride = ptMcMergeIn->tMcMergeOutputPic.nUVStride ?
            ptMcMergeIn->tMcMergeOutputPic.nUVStride : (ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth >> 1);
        u8* pbyOutY = ptMcMergeIn->tMcMergeOutputPic.pu8Y;
        u8* pbyOutU = ptMcMergeIn->tMcMergeOutputPic.pu8U ? ptMcMergeIn->tMcMergeOutputPic.pu8U :
            pbyOutY + nDstYStride * ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight;
        u8* pbyOutV = ptMcMergeIn->tMcMergeOutputPic.pu8V ? ptMcMergeIn->tMcMergeOutputPic.pu8V :
            pbyOutU + (nDstYStride * ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight >> 2);
        /* fill extra windows with background color */
#ifndef FILL_RECT
        for (i = 0; i < ptHandler->nExtraWinNum; i++) {
            s32 nDstWidth   = ptHandler->ptExtraRect[i].s16Width;
            s32 nDstHeight  = ptHandler->ptExtraRect[i].s16Height;
            libyuv::I420Scale(ptHandler->pbyBgColorBufAlign, g_nBgColorBufWidth,
                    ptHandler->pbyBgColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                    ptHandler->pbyBgColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                    g_nBgColorBufWidth, g_nBgColorBufHeight,
                    pbyOutY + ptHandler->ptExtraRect[i].s16Top * nDstYStride + ptHandler->ptExtraRect[i].s16Left, nDstYStride,
                    pbyOutU + ((ptHandler->ptExtraRect[i].s16Top * nDstUVStride + ptHandler->ptExtraRect[i].s16Left) >> 1), nDstUVStride,
                    pbyOutV + ((ptHandler->ptExtraRect[i].s16Top * nDstUVStride + ptHandler->ptExtraRect[i].s16Left) >> 1), nDstUVStride,
                    nDstWidth, nDstHeight,
                    libyuv::kFilterNone);
        }
#else
        for (i = 0; i < ptHandler->nExtraWinNum; i++)
        {
            libyuv::I420Rect(
                    pbyOutY, nDstYStride,
                    pbyOutU, nDstUVStride,
                    pbyOutV, nDstUVStride,
                    ptHandler->ptExtraRect[i].s16Left, ptHandler->ptExtraRect[i].s16Top,
                    ptHandler->ptExtraRect[i].s16Width, ptHandler->ptExtraRect[i].s16Height,
                    ptHandler->tBgYUV.byY, ptHandler->tBgYUV.byU, ptHandler->tBgYUV.byV);
        }
#endif

        /* fill subwin with data */
        for (i = 0; i < ptHandler->nSubWinNum; i++) {
            /* set in param */
            s32 nSrcYStride = ptMcMergeIn->ptMcMergeInputPic[i].nYStride ?
                ptMcMergeIn->ptMcMergeInputPic[i].nYStride : ptHandler->ptSrcRect[i].s16Width;
            s32 nSrcUVStride = ptMcMergeIn->ptMcMergeInputPic[i].nUVStride ?
                ptMcMergeIn->ptMcMergeInputPic[i].nUVStride : (ptHandler->ptSrcRect[i].s16Width >> 1);
            u8* pbyInY = ptMcMergeIn->ptMcMergeInputPic[i].pu8Y;
            u8* pbyInU = ptMcMergeIn->ptMcMergeInputPic[i].pu8U ? ptMcMergeIn->ptMcMergeInputPic[i].pu8U :
                pbyInY + nSrcYStride * ptHandler->ptSrcRect[i].s16Height;
            u8* pbyInV = ptMcMergeIn->ptMcMergeInputPic[i].pu8V ? ptMcMergeIn->ptMcMergeInputPic[i].pu8V :
                pbyInU + (nSrcYStride * ptHandler->ptSrcRect[i].s16Height >> 2);
            s32 nSrcWidth   = ptHandler->ptSrcRect[i].s16Width - ptHandler->ptSrcRect[i].s16WShrink;
            s32 nSrcHeight  = ptHandler->ptSrcRect[i].s16Height - ptHandler->ptSrcRect[i].s16HShrink;

            s32 nDstWidth   = ptHandler->ptDstRect[i].s16Width - ptHandler->ptDstRect[i].s16WShrink;
            s32 nDstHeight  = ptHandler->ptDstRect[i].s16Height - ptHandler->ptDstRect[i].s16HShrink;

            /* scale by libyuv */
            libyuv::I420Scale(pbyInY + ptHandler->ptSrcRect[i].s16YShift * nSrcYStride + ptHandler->ptSrcRect[i].s16XShift, nSrcYStride,
                  pbyInU + ((ptHandler->ptSrcRect[i].s16YShift * nSrcUVStride + ptHandler->ptSrcRect[i].s16XShift) >> 1), nSrcUVStride,
                  pbyInV + ((ptHandler->ptSrcRect[i].s16YShift * nSrcUVStride + ptHandler->ptSrcRect[i].s16XShift) >> 1), nSrcUVStride,
                  nSrcWidth, nSrcHeight,
                  pbyOutY + (ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift) * nDstYStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift), nDstYStride,
                  pbyOutU + (((ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                  pbyOutV + (((ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                  nDstWidth, nDstHeight,
//                  libyuv::kFilterNone
                  libyuv::kFilterBilinear
                  );
#ifndef FILL_RECT
            /* fill background in the gap if have */
            if (ptHandler->ptDstRect[i].s16YShift) {
                /* top and bottom, we supposed the left and right no gap. */
                /* top */
                libyuv::I420Scale(ptHandler->pbyBgColorBufAlign, g_nBgColorBufWidth,
                        ptHandler->pbyBgColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                        ptHandler->pbyBgColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                        g_nBgColorBufWidth, g_nBgColorBufHeight,
                        pbyOutY + (ptHandler->ptDstRect[i].s16Top) * nDstYStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift), nDstYStride,
                        pbyOutU + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                        pbyOutV + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                        nDstWidth, ptHandler->ptDstRect[i].s16YShift,
                        libyuv::kFilterNone);
                /* bottom */
                libyuv::I420Scale(ptHandler->pbyBgColorBufAlign, g_nBgColorBufWidth,
                        ptHandler->pbyBgColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                        ptHandler->pbyBgColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                        g_nBgColorBufWidth, g_nBgColorBufHeight,
                        pbyOutY + (ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift + nDstHeight) * nDstYStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift), nDstYStride,
                        pbyOutU + (((ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift + nDstHeight) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                        pbyOutV + (((ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift + nDstHeight) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift)) >> 1), nDstUVStride,
                        nDstWidth, ptHandler->ptDstRect[i].s16HShrink - ptHandler->ptDstRect[i].s16YShift,
                        libyuv::kFilterNone);
            } else if (ptHandler->ptDstRect[i].s16XShift) {
                /* left and right, we supposed the top and bottom no gap. */
                /* left */
                libyuv::I420Scale(ptHandler->pbyBgColorBufAlign, g_nBgColorBufWidth,
                        ptHandler->pbyBgColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                        ptHandler->pbyBgColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                        g_nBgColorBufWidth, g_nBgColorBufHeight,
                        pbyOutY + (ptHandler->ptDstRect[i].s16Top) * nDstYStride + (ptHandler->ptDstRect[i].s16Left), nDstYStride,
                        pbyOutU + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left)) >> 1), nDstUVStride,
                        pbyOutV + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left)) >> 1), nDstUVStride,
                        ptHandler->ptDstRect[i].s16XShift, nDstHeight,
                        libyuv::kFilterNone);
                /* right */
                libyuv::I420Scale(ptHandler->pbyBgColorBufAlign, g_nBgColorBufWidth,
                        ptHandler->pbyBgColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                        ptHandler->pbyBgColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                        g_nBgColorBufWidth, g_nBgColorBufHeight,
                        pbyOutY + (ptHandler->ptDstRect[i].s16Top) * nDstYStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift + nDstWidth), nDstYStride,
                        pbyOutU + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift + nDstWidth)) >> 1), nDstUVStride,
                        pbyOutV + (((ptHandler->ptDstRect[i].s16Top) * nDstUVStride + (ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift + nDstWidth)) >> 1), nDstUVStride,
                        ptHandler->ptDstRect[i].s16WShrink - ptHandler->ptDstRect[i].s16XShift, nDstHeight,
                        libyuv::kFilterNone);
            }
#else
            if (ptHandler->ptDstRect[i].s16YShift) {
                libyuv::I420Rect(
                        pbyOutY, nDstYStride,
                        pbyOutU, nDstUVStride,
                        pbyOutV, nDstUVStride,
                        ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift,
                        ptHandler->ptDstRect[i].s16Top,
                        nDstWidth, ptHandler->ptDstRect[i].s16YShift,
                        ptHandler->tBgYUV.byY, ptHandler->tBgYUV.byU, ptHandler->tBgYUV.byV);
                libyuv::I420Rect(
                        pbyOutY, nDstYStride,
                        pbyOutU, nDstUVStride,
                        pbyOutV, nDstUVStride,
                        ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift,
                        ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16YShift + nDstHeight,
                        nDstWidth, ptHandler->ptDstRect[i].s16HShrink - ptHandler->ptDstRect[i].s16YShift,
                        ptHandler->tBgYUV.byY, ptHandler->tBgYUV.byU, ptHandler->tBgYUV.byV);
            } else if (ptHandler->ptDstRect[i].s16XShift) {
                libyuv::I420Rect(
                        pbyOutY, nDstYStride,
                        pbyOutU, nDstUVStride,
                        pbyOutV, nDstUVStride,
                        ptHandler->ptDstRect[i].s16Left,
                        ptHandler->ptDstRect[i].s16Top,
                        ptHandler->ptDstRect[i].s16XShift, nDstHeight,
                        ptHandler->tBgYUV.byY, ptHandler->tBgYUV.byU, ptHandler->tBgYUV.byV);
                libyuv::I420Rect(
                        pbyOutY, nDstYStride,
                        pbyOutU, nDstUVStride,
                        pbyOutV, nDstUVStride,
                        ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16XShift + nDstWidth,
                        ptHandler->ptDstRect[i].s16Top,
                        ptHandler->ptDstRect[i].s16WShrink - ptHandler->ptDstRect[i].s16XShift, nDstHeight,
                        ptHandler->tBgYUV.byY, ptHandler->tBgYUV.byU, ptHandler->tBgYUV.byV);
            }
#endif
        }

        /* draw border */
        if (ptHandler->tMcMergeParam.tMcMergeBgPic.nDrawBoundaryFlag) {
#ifndef FILL_RECT
            for (i = 0; i < ptHandler->nBorderNum; i++) {
                s32 nDstWidth   = ptHandler->ptBorderRect[i].s16Width;
                s32 nDstHeight  = ptHandler->ptBorderRect[i].s16Height;
                libyuv::I420Scale(ptHandler->pbyBorderColorBufAlign, g_nBgColorBufWidth,
                        ptHandler->pbyBorderColorBufAlign + g_nBgColorBufWidth * g_nBgColorBufHeight, g_nBgColorBufWidth >> 1,
                        ptHandler->pbyBorderColorBufAlign + (g_nBgColorBufWidth * g_nBgColorBufHeight * 5 >> 2), g_nBgColorBufWidth >> 1,
                        g_nBgColorBufWidth, g_nBgColorBufHeight,
                        pbyOutY + ptHandler->ptBorderRect[i].s16Top * nDstYStride + ptHandler->ptBorderRect[i].s16Left, nDstYStride,
                        pbyOutU + ((ptHandler->ptBorderRect[i].s16Top * nDstUVStride + ptHandler->ptBorderRect[i].s16Left) >> 1), nDstUVStride,
                        pbyOutV + ((ptHandler->ptBorderRect[i].s16Top * nDstUVStride + ptHandler->ptBorderRect[i].s16Left) >> 1), nDstUVStride,
                        nDstWidth, nDstHeight,
                        libyuv::kFilterNone);
            }
#else
            for (i = 0; i < ptHandler->nBorderNum; i++) {
                libyuv::I420Rect(
                        pbyOutY, nDstYStride,
                        pbyOutU, nDstUVStride,
                        pbyOutV, nDstUVStride,
                        ptHandler->ptBorderRect[i].s16Left, ptHandler->ptBorderRect[i].s16Top,
                        ptHandler->ptBorderRect[i].s16Width, ptHandler->ptBorderRect[i].s16Height,
                        ptHandler->tBorderYUV.byY, ptHandler->tBorderYUV.byU, ptHandler->tBorderYUV.byV);
            }
#endif
        }

        /* draw focus info */
        for (i = 0; i < ptHandler->nSubWinNum; i++) {
            s32 nDrawFocus  = ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nDrawFocusFlag;
            s32 nFocusW     = ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nFocusWidth;
            nFocusW = (nFocusW >> 1) << 1;
            if (!nDrawFocus) {
                continue;
            }
            /* draw top horizontal */
            libyuv::I420Rect(
                    pbyOutY, nDstYStride,
                    pbyOutU, nDstUVStride,
                    pbyOutV, nDstUVStride,
                    ptHandler->ptFocusInfo[i].s16Left, ptHandler->ptFocusInfo[i].s16Top,
                    ptHandler->ptFocusInfo[i].s16Width, nFocusW,
                    ptHandler->ptFocusInfo[i].tColorYUV.byY, ptHandler->ptFocusInfo[i].tColorYUV.byU, ptHandler->ptFocusInfo[i].tColorYUV.byV);
            /* draw bottom horizontal */
            libyuv::I420Rect(
                    pbyOutY, nDstYStride,
                    pbyOutU, nDstUVStride,
                    pbyOutV, nDstUVStride,
                    ptHandler->ptFocusInfo[i].s16Left,
                    ptHandler->ptFocusInfo[i].s16Top + ptHandler->ptFocusInfo[i].s16Height - nFocusW,
                    ptHandler->ptFocusInfo[i].s16Width, nFocusW,
                    ptHandler->ptFocusInfo[i].tColorYUV.byY, ptHandler->ptFocusInfo[i].tColorYUV.byU, ptHandler->ptFocusInfo[i].tColorYUV.byV);
            /* draw left vertical */
            libyuv::I420Rect(
                    pbyOutY, nDstYStride,
                    pbyOutU, nDstUVStride,
                    pbyOutV, nDstUVStride,
                    ptHandler->ptFocusInfo[i].s16Left, ptHandler->ptFocusInfo[i].s16Top,
                    nFocusW, ptHandler->ptFocusInfo[i].s16Height,
                    ptHandler->ptFocusInfo[i].tColorYUV.byY, ptHandler->ptFocusInfo[i].tColorYUV.byU, ptHandler->ptFocusInfo[i].tColorYUV.byV);
            /* draw right vertical */
            libyuv::I420Rect(
                    pbyOutY, nDstYStride,
                    pbyOutU, nDstUVStride,
                    pbyOutV, nDstUVStride,
                    ptHandler->ptFocusInfo[i].s16Left + ptHandler->ptFocusInfo[i].s16Width - nFocusW,
                    ptHandler->ptFocusInfo[i].s16Top,
                    nFocusW, ptHandler->ptFocusInfo[i].s16Height,
                    ptHandler->ptFocusInfo[i].tColorYUV.byY, ptHandler->ptFocusInfo[i].tColorYUV.byU, ptHandler->ptFocusInfo[i].tColorYUV.byV);
        }
    } while(0);

    return en_MCMERGE_SUCCESS;
}


/* Description:
 *   validate the merge style is OK.
 *
 * Param:
 *   nMergeStyle        [IN]    merge style
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jun. 25 2018
 * */
static inline s32
McMergeCheckMergeStyle(s32 nMergeStyle)
{
    s32 nRet = en_MCMERGE_SUCCESS;
    if (nMergeStyle > en_MCMERGE_STYLE_MIN &&
        nMergeStyle < en_MCMERGE_STYLE_MAX) {
        return en_MCMERGE_SUCCESS;
    } else {
        warning("invalid nMergeStyle %d", nMergeStyle);
        return en_MCMERGE_INVALID_PARAM;
    }

}


/* Description:
 *   validate the merge background size is OK.
 *
 * Param:
 *   nBgWidth           [IN]    background width
 *   nBgHeight          [IN]    background height
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jun. 26 2018
 * */
static inline s32
McMergeCheckBgSize(s32 nBgWidth, s32 nBgHeight)
{
    if (nBgWidth >= MCMERGE_BG_WIDTH_MIN && nBgWidth <= MCMERGE_BG_WIDTH_MAX &&
        nBgHeight >= MCMERGE_BG_HEIGHT_MIN && nBgHeight <= MCMERGE_BG_HEIGHT_MAX &&
        (0 == nBgWidth % 4) && (0 == nBgHeight % 4)) {
        return en_MCMERGE_SUCCESS;
    } else {
        warning("invalid background size w = %d h = %d", nBgWidth, nBgHeight);
        return en_MCMERGE_INVALID_PARAM;
    }
}

/* Description:
 *   function to copy out merge param in.
 *
 * Param:
 *   ptDst          [IN]    destination
 *   ptSrc          [IN]    source
 *
 * Return:
 *   void
 *
 * Author: Summer Shang
 * Date: Jun. 27 2018
 * */
static void
McMergeCopyMergeParam(TMcMergeParam *ptDst, const TMcMergeParam *ptSrc)
{
    /* we don't check null pointer here */
    s32 nMergeWinNum;
    McMergeStyle2MergeWinNumber(ptSrc->nMergeStyle, nMergeWinNum);

    ptDst->nMaxFgNum = ptSrc->nMaxFgNum;
    ptDst->nMergeStyle = ptSrc->nMergeStyle;
    memcpy(&ptDst->tMcMergeBgPic, &ptSrc->tMcMergeBgPic, sizeof(TMcMergeBgParam));
    memcpy(ptDst->ptMcMergeFgPic, ptSrc->ptMcMergeFgPic, sizeof(TMcMergeFgParam)*nMergeWinNum);

    return;
}


/* Description:
 *   function convert merge style to merge window number.
 *
 * Param:
 *   nMergeStyle            [IN]    destination
 *   nMergeWinNum           [OUT]   merge windows number, 0 for error
 *
 * Return:
 *   void
 *
 * Author: Summer Shang
 * Date: Jun. 27 2018
 * */
static void
McMergeStyle2MergeWinNumber(TMcMergeStyle nMergeStyle, s32& nMergeWinNum)
{
    switch (nMergeStyle)
    {
        case en_MCMERGE_STYLE_M1:
            nMergeWinNum = 1;
            break;
        case en_MCMERGE_STYLE_M2:
        case en_MCMERGE_STYLE_M2_1_BR1:
        case en_MCMERGE_STYLE_M2_1_BL1:
        case en_MCMERGE_STYLE_M2_1_TR1:
        case en_MCMERGE_STYLE_M2_1_TL1:
            nMergeWinNum = 2;
            break;
        case en_MCMERGE_STYLE_M3_T1:
        case en_MCMERGE_STYLE_M3_B1:
        case en_MCMERGE_STYLE_M3_1_B2:
        case en_MCMERGE_STYLE_M3_1_T2:
        case en_MCMERGE_STYLE_M3_1_R2:
        case en_MCMERGE_STYLE_M3_L1:
            nMergeWinNum = 3;
            break;
        case en_MCMERGE_STYLE_M4:
        case en_MCMERGE_STYLE_M4_1_R3:
        case en_MCMERGE_STYLE_M4_1_D3:
            nMergeWinNum = 4;
            break;
        case en_MCMERGE_STYLE_M5_1_R4:
        case en_MCMERGE_STYLE_M5_1_D4:
        case en_MCMERGE_STYLE_M5_2_D3:
            nMergeWinNum = 5;
            break;
        case en_MCMERGE_STYLE_M6:
        case en_MCMERGE_STYLE_M6_1_5:
        case en_MCMERGE_STYLE_M6_2_B4:
        case en_MCMERGE_STYLE_M6_1_B5:
        case en_MCMERGE_STYLE_M6_B5:
            nMergeWinNum = 6;
            break;
        case en_MCMERGE_STYLE_M7_3_TL4:
        case en_MCMERGE_STYLE_M7_3_TR4:
        case en_MCMERGE_STYLE_M7_3_BL4:
        case en_MCMERGE_STYLE_M7_3_BR4:
        case en_MCMERGE_STYLE_M7_3_BLR4:
        case en_MCMERGE_STYLE_M7_3_TLR4:
        case en_MCMERGE_STYLE_M7_1_D6:
            nMergeWinNum = 7;
            break;
        case en_MCMERGE_STYLE_M8_1_7:
        case en_MCMERGE_STYLE_M8_4_4:
            nMergeWinNum = 8;
            break;
        case en_MCMERGE_STYLE_M9:
        case en_MCMERGE_STYLE_M9_T4_1_D4:
            nMergeWinNum = 9;
            break;
        case en_MCMERGE_STYLE_M10_2_R8:
        case en_MCMERGE_STYLE_M10_2_B8:
        case en_MCMERGE_STYLE_M10_2_T8:
        case en_MCMERGE_STYLE_M10_2_L8:
        case en_MCMERGE_STYLE_M10_2_TB8:
        case en_MCMERGE_STYLE_M10_1_9:
        case en_MCMERGE_STYLE_M10_L4_2_R4:
            nMergeWinNum = 10;
            break;
        case en_MCMERGE_STYLE_M11_T5_1_D5:
        case en_MCMERGE_STYLE_M11_1_D10:
            nMergeWinNum = 11;
            break;
        case en_MCMERGE_STYLE_M12_1_11:
        case en_MCMERGE_STYLE_M12_3_RD9:
            nMergeWinNum = 12;
            break;
        case en_MCMERGE_STYLE_M13_TL1_12:
        case en_MCMERGE_STYLE_M13_TR1_12:
        case en_MCMERGE_STYLE_M13_BL1_12:
        case en_MCMERGE_STYLE_M13_BR1_12:
        case en_MCMERGE_STYLE_M13_1_ROUND12:
        case en_MCMERGE_STYLE_M13_TL4_9:
        case en_MCMERGE_STYLE_M13_L6_1_R6:
            nMergeWinNum = 13;
            break;
        case en_MCMERGE_STYLE_M14_1_13:
        case en_MCMERGE_STYLE_M14_TL2_12:
        case en_MCMERGE_STYLE_M14_T5_1_2_1_D5:
            nMergeWinNum = 14;
            break;
        case en_MCMERGE_STYLE_M15_T3_12:
        case en_MCMERGE_STYLE_M15_T4_L3_1_R3_D4:
            nMergeWinNum = 15;
            break;
        case en_MCMERGE_STYLE_M16:
        case en_MCMERGE_STYLE_M16_1_15:
            nMergeWinNum = 16;
            break;
        case en_MCMERGE_STYLE_M17_1:
        case en_MCMERGE_STYLE_M17_2:
        case en_MCMERGE_STYLE_M17_3:
            nMergeWinNum = 17;
            break;
        case en_MCMERGE_STYLE_M18_1:
        case en_MCMERGE_STYLE_M18_2:
        case en_MCMERGE_STYLE_M18_3:
            nMergeWinNum = 18;
            break;
        case en_MCMERGE_STYLE_M19_1:
        case en_MCMERGE_STYLE_M19_2:
            nMergeWinNum = 19;
            break;
        case en_MCMERGE_STYLE_M20:
            nMergeWinNum = 20;
            break;
        case en_MCMERGE_STYLE_M21_1:
        case en_MCMERGE_STYLE_M21_2:
            nMergeWinNum = 21;
            break;
        case en_MCMERGE_STYLE_M22:
            nMergeWinNum = 22;
            break;
        case en_MCMERGE_STYLE_M23_1:
        case en_MCMERGE_STYLE_M23_2:
            nMergeWinNum = 23;
            break;
        case en_MCMERGE_STYLE_M24:
            nMergeWinNum = 24;
            break;
        case en_MCMERGE_STYLE_M25_1:
        case en_MCMERGE_STYLE_M25_2:
            nMergeWinNum = 25;
            break;
        default:
            warning("invalid merge style %d", nMergeStyle);
            nMergeWinNum = 0;
            break;
    }
    return;
}

/* Description:
 *   function to get merge subwin position.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jun. 28 2018
 * */
static s32
CaculateSubwinPosition(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_SUCCESS;
    s32 i = 0;

    s32 nEndTop   = 0;
    s32 nEndLeft  = 0;
    s32 nMutiple  = 0;
    s32 nRealStartLeft  = 0;
    s32 nRealStartTop   = 0;
    s32 nRealEndLeft    = 0;
    s32 nRealEndTop     = 0;

    s32 nBgWidth    = ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth;
    s32 nBgHeight   = ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight;
    s32 nMcMergeStyle = ptHandler->tMcMergeParam.nMergeStyle;

    /* get background based position */
    /* get denominator */
    nMutiple = FRAC_B(atMergePointstyle[nMcMergeStyle][0].s16Left);
    for (i = 0; i < ptHandler->nSubWinNum; i++) {
        s32 nNewIndex = ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nFgPositionNum;
        /* get coordinate of the topleft point */
        nRealStartLeft = (nBgWidth * FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Left) / nMutiple);
        nRealStartTop = (nBgHeight * FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Top) / nMutiple);

        /* get relative coordinate of the bottomright point */
        nEndLeft = (FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Left) + FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Width));
        nEndTop = (FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Top) + FRAC_A(atMergePointstyle[nMcMergeStyle][nNewIndex].s16Height));

        /* get real coordinate of the bottomright point */
        nRealEndLeft = nBgWidth * nEndLeft / nMutiple;
        nRealEndTop = nBgHeight * nEndTop / nMutiple;

        /* coordinate align to 2 */
        nRealStartLeft  &= COORDINATE_ALIGN;
        nRealEndLeft    &= COORDINATE_ALIGN;
        nRealStartTop   &= COORDINATE_ALIGN;
        nRealEndTop     &= COORDINATE_ALIGN;
        debug("merge %s pos %d->%d: [%d, %d, %d, %d]", atMcMergeStyleInfo[nMcMergeStyle],
                i, nNewIndex, nRealStartLeft, nRealStartTop, nRealEndLeft, nRealEndTop);
        memset(&(ptHandler->ptDstRect[i]), 0, sizeof(TMcMergeRect));
        ptHandler->ptDstRect[i].s16Left     = nRealStartLeft;
        ptHandler->ptDstRect[i].s16Top      = nRealStartTop;
        ptHandler->ptDstRect[i].s16Width    = nRealEndLeft - nRealStartLeft;
        ptHandler->ptDstRect[i].s16Height   = nRealEndTop - nRealStartTop;

        memset(&(ptHandler->ptSrcRect[i]), 0, sizeof(TMcMergeRect));
        ptHandler->ptSrcRect[i].s16Left     = 0;
        ptHandler->ptSrcRect[i].s16Top      = 0;
        ptHandler->ptSrcRect[i].s16Width    = ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nFgSrcWidth;
        ptHandler->ptSrcRect[i].s16Height   = ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nFgSrcHeight;

        CalculateScaleParam(&(ptHandler->ptSrcRect[i]), &(ptHandler->ptDstRect[i]),
                ptHandler->tMcMergeParam.ptMcMergeFgPic[i].nZoomStyle);
    }

    return nRet;
}


/* Description:
 *   function to get merge subwin position and dimension after zoom.
 *
 * Param:
 *   ptSrcRect              [IN OUT]    fg source info
 *   ptDstRect              [IN OUT]    fg destination info
 *   nZoomType              [IN]        TMcMergeZoomType
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jul. 3 2018
 * */
static s32
CalculateScaleParam(PTMcMergeRect ptSrcRect, PTMcMergeRect ptDstRect, s32 nZoomType)
{
    s32 nSpare          = 0;
    s32 nSrcWidth       = ptSrcRect->s16Width;
    s32 nSrcHeight      = ptSrcRect->s16Height;
    s32 nDstWidth       = ptDstRect->s16Width;
    s32 nDstHeight      = ptDstRect->s16Height;

    switch (nZoomType)
    {
    case en_MCMERGE_ZOOM_SCALE:
        //debug("scale mode just return\n");
        break;
    case en_MCMERGE_ZOOM_FILL:
        /* 16:9 ==> 4:3 type, fill black top and bottom */
        if (nSrcWidth * nDstHeight > nSrcHeight * nDstWidth) {
            nSpare = (nDstHeight - nSrcHeight * nDstWidth / nSrcWidth) &(SCALE_ALIGN);
            ptDstRect->s16YShift    = nSpare >> 1;
            ptDstRect->s16HShrink   = nSpare;
            debug("spare %d", nSpare);
        }
        /* 4:3 ==> 16:9 type, fill black left and right */
        else if (nSrcWidth * nDstHeight < nSrcHeight * nDstWidth) {
            nSpare = (nDstWidth - nSrcWidth * nDstHeight / nSrcHeight) &(SCALE_ALIGN);
            ptDstRect->s16XShift    = nSpare >> 1;
            ptDstRect->s16WShrink   = nSpare;
        }
        else {
            //debug("same ratio, just return\n");
        }
        break;
    case en_MCMERGE_ZOOM_CUT:
        /* 16:9 ==> 4:3 type, src cut left and right */
        if (nSrcWidth * nDstHeight > nSrcHeight * nDstWidth) {
            nSpare = (nSrcWidth - nDstWidth * nSrcHeight / nDstHeight) &(SCALE_ALIGN);
            ptSrcRect->s16XShift    = nSpare >> 1;
            ptSrcRect->s16WShrink   = nSpare;
        }
        /* 4:3 ==> 16:9 type, src cut top and bottom */
        else if (nSrcWidth * nDstHeight < nSrcHeight * nDstWidth) {
            nSpare = (nSrcHeight - nDstHeight * nSrcWidth / nDstWidth) &(SCALE_ALIGN);
            ptSrcRect->s16YShift    = nSpare >> 1;
            ptSrcRect->s16HShrink   = nSpare;
        }
        else {
            //debug("same ratio, just return\n");
        }
        break;
    default:
        warning("invalid zoom type %d", nZoomType);
        break;
    }
    debug("spare is %d\n", nSpare);
    return en_MCMERGE_SUCCESS;
}

/* Description:
 *   function to get merge xtrawin position.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 16 2018
 * */
static s32
CaculateExtrawinPosition(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_SUCCESS;
    s32 i = 0;

    s32 nEndTop   = 0;
    s32 nEndLeft  = 0;
    s32 nMutiple  = 0;
    s32 nRealStartLeft  = 0;
    s32 nRealStartTop   = 0;
    s32 nRealEndLeft    = 0;
    s32 nRealEndTop     = 0;

    s32 nBgWidth    = ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth;
    s32 nBgHeight   = ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight;

    s32 nExtraInfoNum = sizeof(g_atExtraWinInfo) / sizeof(g_atExtraWinInfo[0]);

    const TMcMergeRect* ptRect = NULL;

    /* do we have extra windows? */
    for (i = 0; i < nExtraInfoNum; i++) {
        if (ptHandler->tMcMergeParam.nMergeStyle == (s32)g_atExtraWinInfo[i].enStyle) {
            ptHandler->nExtraWinNum = g_atExtraWinInfo[i].nWinNum;
            ptRect = g_atExtraWinInfo[i].ptWinRect;
            debug("Pos %d: Merge style %s: extra win num %d\n", i,
                    atMcMergeStyleInfo[ptHandler->tMcMergeParam.nMergeStyle], ptHandler->nExtraWinNum);
            break;
        }
    }

    /* set the extra windows to zero */
    if(i == nExtraInfoNum) {
        ptHandler->nExtraWinNum = 0;
    }
    
    /* get background based position */
    for (i = 0; i < ptHandler->nExtraWinNum; i++) {
        /* get denominator */
        nMutiple = FRAC_B(ptRect[0].s16Left);
        /* get coordinate of the topleft point */
        nRealStartLeft = (nBgWidth * FRAC_A(ptRect[i].s16Left) / nMutiple);
        nRealStartTop = (nBgHeight * FRAC_A(ptRect[i].s16Top) / nMutiple);

        /* get relative coordinate of the bottomright point */
        nEndLeft = (FRAC_A(ptRect[i].s16Left) + FRAC_A(ptRect[i].s16Width));
        nEndTop = (FRAC_A(ptRect[i].s16Top) + FRAC_A(ptRect[i].s16Height));

        /* get real coordinate of the bottomright point */
        nRealEndLeft = nBgWidth * nEndLeft / nMutiple;
        nRealEndTop = nBgHeight * nEndTop / nMutiple;

        /* coordinate align to 2 */
        nRealStartLeft  &= COORDINATE_ALIGN;
        nRealEndLeft    &= COORDINATE_ALIGN;
        nRealStartTop   &= COORDINATE_ALIGN;
        nRealEndTop     &= COORDINATE_ALIGN;
        debug("merge %s extra pos %d: [%d, %d, %d, %d]",
                atMcMergeStyleInfo[ptHandler->tMcMergeParam.nMergeStyle],
                i, nRealStartLeft, nRealStartTop, nRealEndLeft, nRealEndTop);
        memset(&(ptHandler->ptExtraRect[i]), 0, sizeof(TMcMergeRect));
        ptHandler->ptExtraRect[i].s16Left     = nRealStartLeft;
        ptHandler->ptExtraRect[i].s16Top      = nRealStartTop;
        ptHandler->ptExtraRect[i].s16Width    = nRealEndLeft - nRealStartLeft;
        ptHandler->ptExtraRect[i].s16Height   = nRealEndTop - nRealStartTop;
    }

    return nRet;
}


/* Description:
 *   function to get merge border position.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 22 2018
 * */
static void
ClearBorderInfo(PTMcList plistBorderInfo)
{
    PTMcList        plistBorderInfoNext = NULL;
    PTMcList        plistBorderLen      = NULL;
    PTMcList        plistBorderLenNext  = NULL;
    PTMcBorderInfo  ptBorderInfo        = NULL;
    PTMcBorderLen   ptBorderLen         = NULL;

    while (plistBorderInfo) {
        ptBorderInfo = (PTMcBorderInfo)plistBorderInfo->pData;
        if (ptBorderInfo) {
            plistBorderLen = ptBorderInfo->nLenList;
        }
        while (plistBorderLen) {
            plistBorderLenNext = plistBorderLen->pNext;
            PutBorderLenCell(plistBorderLen);
            plistBorderLen = plistBorderLenNext;
        }
        SAFE_FREE(ptBorderInfo);
        plistBorderInfoNext = plistBorderInfo->pNext;
        free(plistBorderInfo);
        plistBorderInfo = plistBorderInfoNext;
    }
    return;
}

/* Description:
 *   function to get merge border position.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 21 2018
 * */
static s32
CaculateBorderPosition(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_SUCCESS;
    s32 i    = 0;

    PTMcList        pListBorderInfo = NULL;
    PTMcList        pListBorderLen  = NULL;
    PTMcBorderInfo  ptBorderInfo    = NULL;
    PTMcBorderLen   ptBorderLen     = NULL;

    for (i = 0; i < ptHandler->nSubWinNum; i++) {
        s32 nTop    = ptHandler->ptDstRect[i].s16Top;
        s32 nBottom = ptHandler->ptDstRect[i].s16Top + ptHandler->ptDstRect[i].s16Height;
        s32 nLeft   = ptHandler->ptDstRect[i].s16Left;
        s32 nRight  = ptHandler->ptDstRect[i].s16Left + ptHandler->ptDstRect[i].s16Width;
        //debug("subwin %d: top\t %d bottom\t %d left\t %d right\t %d\n", i, nTop, nBottom, nLeft, nRight);

        /* horizontal border infomation */
        pListBorderInfo = ptHandler->plistBorderInfoHor;
        pListBorderInfo = RefreshBorderInfo(pListBorderInfo, nTop, nLeft, nRight);
        pListBorderInfo = RefreshBorderInfo(pListBorderInfo, nBottom, nLeft, nRight);
        ptHandler->plistBorderInfoHor = pListBorderInfo;

        /* vertical border infomation */
        pListBorderInfo = ptHandler->plistBorderInfoVer;
        pListBorderInfo = RefreshBorderInfo(pListBorderInfo, nLeft, nTop, nBottom);
        pListBorderInfo = RefreshBorderInfo(pListBorderInfo, nRight, nTop, nBottom);
        ptHandler->plistBorderInfoVer = pListBorderInfo;

        //PrintBorderInfo(ptHandler->plistBorderInfoHor);
        //PrintBorderInfo(ptHandler->plistBorderInfoVer);
    }

    nRet = GenerateBorderCoordate(ptHandler);

    ClearBorderInfo(ptHandler->plistBorderInfoVer);
    ptHandler->plistBorderInfoVer = NULL;
    ClearBorderInfo(ptHandler->plistBorderInfoHor);
    ptHandler->plistBorderInfoHor = NULL;

    return nRet;
}

/* Description:
 *   get borderline number from borderline info list.
 *
 * Param:
 *   plistBorderInfo        [IN]    borderline list
 *
 * Return:
 *   border line number
 *
 * Author: Summer Shang
 * Date: Aug. 28 2018
 * */
static s32
GetBorderLineNum(PTMcList plistBorderInfo)
{
    s32 nNum = 0;

    PTMcList        plistBorderLen      = NULL;
    PTMcBorderInfo  ptBorderInfo        = NULL;
    PTMcBorderLen   ptBorderLen         = NULL;

    while (plistBorderInfo) {
        ptBorderInfo = (PTMcBorderInfo)plistBorderInfo->pData;
        if (ptBorderInfo) {
            plistBorderLen = ptBorderInfo->nLenList;
        }

        while (plistBorderLen) {
            nNum++;
            plistBorderLen = plistBorderLen->pNext;
        }
        plistBorderInfo = plistBorderInfo->pNext;
    }

    return nNum;
}

/* Description:
 *   get coordiate for the line
 *
 * Param:
 *   plistBorderInfo    [IN]    information list to show
 *
 * Return:
 *
 * Author: Summer Shang
 * Date: Aug. 28 2018
 * */
static void
GetBorderInfo(PTMcList plistBorderInfo, PTMcMergeRect ptBorderRect, TMcMergeBgParam* ptBgParam, BOOL32 bVertical)
{
    PTMcList        plistBorderLen      = NULL;
    PTMcBorderInfo  ptBorderInfo        = NULL;
    PTMcBorderLen   ptBorderLen         = NULL;

    s32 nCnt    = 0;
    s16 wOffset = (ptBgParam->nBoundaryWidth >> 2) << 1;

    while (plistBorderInfo) {
        ptBorderInfo = (PTMcBorderInfo)plistBorderInfo->pData;
        if (ptBorderInfo) {
            plistBorderLen  = ptBorderInfo->nLenList;
        }
        while (plistBorderLen) {
            ptBorderLen = (PTMcBorderLen)plistBorderLen->pData;
            /* we supposed the data always exist */
            if (bVertical) {
                ptBorderRect[nCnt].s16Left      = ptBorderInfo->nCoordinate;
                ptBorderRect[nCnt].s16Top       = ptBorderLen->nFrom;
                ptBorderRect[nCnt].s16Width     = ptBgParam->nBoundaryWidth;
                ptBorderRect[nCnt].s16Height    = ptBorderLen->nTo - ptBorderLen->nFrom;
                /* left, right edge check */
                if (0 == ptBorderRect[nCnt].s16Left) {
                } else if (ptBgParam->nBgWidth <= ptBorderRect[nCnt].s16Left) {
                    ptBorderRect[nCnt].s16Left = ptBgParam->nBgWidth - ptBgParam->nBoundaryWidth;
                } else {
                    ptBorderRect[nCnt].s16Left -= wOffset;
                    if (ptBorderRect[nCnt].s16Top >= wOffset) {
                        ptBorderRect[nCnt].s16Top  -= wOffset;
                    }
                }
                if (ptBorderRect[nCnt].s16Top + ptBorderRect[nCnt].s16Height + ptBgParam->nBoundaryWidth
                    <= ptBgParam->nBgHeight) {
                    ptBorderRect[nCnt].s16Height += ptBgParam->nBoundaryWidth;
                }
            } else {
                ptBorderRect[nCnt].s16Left      = ptBorderLen->nFrom;
                ptBorderRect[nCnt].s16Top       = ptBorderInfo->nCoordinate;
                ptBorderRect[nCnt].s16Width     = ptBorderLen->nTo - ptBorderLen->nFrom;
                ptBorderRect[nCnt].s16Height    = ptBgParam->nBoundaryWidth;
                /* top, bottom edge check */
                if (0 == ptBorderRect[nCnt].s16Top) {
                } else if (ptBgParam->nBgHeight <= ptBorderRect[nCnt].s16Top) {
                    ptBorderRect[nCnt].s16Top = ptBgParam->nBgHeight - ptBgParam->nBoundaryWidth;
                } else {
                    ptBorderRect[nCnt].s16Top  -= wOffset;
                    if (ptBorderRect[nCnt].s16Left >= wOffset) {
                        ptBorderRect[nCnt].s16Left -= wOffset;
                    }
                }
                if (ptBorderRect[nCnt].s16Left + ptBorderRect[nCnt].s16Width + ptBgParam->nBoundaryWidth
                    <= ptBgParam->nBgWidth) {
                    ptBorderRect[nCnt].s16Width += ptBgParam->nBoundaryWidth;
                }
            }

            nCnt++;
            plistBorderLen = plistBorderLen->pNext;
        }
        plistBorderInfo = plistBorderInfo->pNext;
    }

    return;
}

/* Description:
 *   generate the real coordinate for the border
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 28 2018
 * */
static s32
GenerateBorderCoordate(PTMcMergeHandler ptHandler)
{
    s32 nRet        = en_MCMERGE_SUCCESS;
    s32 nLineNum    = 0;
    s32 nVerLineNum = 0;
    s32 nHorLineNum = 0;

    PTMcList        plistBorderInfo = NULL;
    PTMcList        pListBorderLen  = NULL;
    PTMcBorderInfo  ptBorderInfo    = NULL;
    PTMcBorderLen   ptBorderLen     = NULL;

    nHorLineNum = GetBorderLineNum(ptHandler->plistBorderInfoHor);
    nVerLineNum = GetBorderLineNum(ptHandler->plistBorderInfoVer);
    nLineNum = nHorLineNum + nVerLineNum;
    debug("vertical %d horizontal %d total %d for style %s\n",
            nVerLineNum, nHorLineNum, nLineNum,
            atMcMergeStyleInfo[ptHandler->tMcMergeParam.nMergeStyle]);

    if (ptHandler->ptBorderRect) {
        free(ptHandler->ptBorderRect);
    }
    ptHandler->ptBorderRect = (PTMcMergeRect)malloc(sizeof(TMcMergeRect) * nLineNum);
    if (ptHandler->ptBorderRect) {
        memset(ptHandler->ptBorderRect, 0, sizeof(TMcMergeRect) * nLineNum);
        GetBorderInfo(ptHandler->plistBorderInfoHor, ptHandler->ptBorderRect,
                &(ptHandler->tMcMergeParam.tMcMergeBgPic), FALSE);
        GetBorderInfo(ptHandler->plistBorderInfoVer, ptHandler->ptBorderRect + nHorLineNum,
                &(ptHandler->tMcMergeParam.tMcMergeBgPic), TRUE);
        ptHandler->nBorderNum = nLineNum;
    } else {
        nRet = en_MCMERGE_FAIL;
        ptHandler->nBorderNum = 0;
        warning("malloc rect for border line fail\n");
    }

#if 0
    for (s32 i = 0; i < nLineNum; i++) {
        debug("x\t%d\ty\t%d\tw\t%d\th\t%d",
                ptHandler->ptBorderRect[i].s16Left,
                ptHandler->ptBorderRect[i].s16Top,
                ptHandler->ptBorderRect[i].s16Width,
                ptHandler->ptBorderRect[i].s16Height);
    }
#endif

    return nRet;
}

/* Description:
 *   output coordiate info
 *
 * Param:
 *   plistBorderInfo    [IN]    information list to show
 *
 * Return:
 *
 * Author: Summer Shang
 * Date: Aug. 28 2018
 * */
static void
PrintBorderInfo(PTMcList plistBorderInfo)
{
    PTMcList        plistBorderLen      = NULL;
    PTMcBorderInfo  ptBorderInfo        = NULL;
    PTMcBorderLen   ptBorderLen         = NULL;

    while (plistBorderInfo) {
        ptBorderInfo    = (PTMcBorderInfo)plistBorderInfo->pData;
        plistBorderLen  = ptBorderInfo->nLenList;
        debug("coordinate %d\n", ptBorderInfo->nCoordinate);
        while (plistBorderLen) {
            ptBorderLen = (PTMcBorderLen)plistBorderLen->pData;
            debug("[%d, %d] ", ptBorderLen->nFrom, ptBorderLen->nTo);
            plistBorderLen = plistBorderLen->pNext;
            printf("\n");
        }
        plistBorderInfo = plistBorderInfo->pNext;
    }

    return;
}

/* Description:
 *   function to refresh the coordiate length wharerver vertical or horizontal.
 *
 * Param:
 *   plistBorderInfo    [IO]    list of border info
 *   nCoordinate        [IN]    coordinate of the border
 *   nFrom              [IN]    init value of begin point
 *   nTo                [IN]    init value of end point
 *
 * Return:
 *   PTMcList pointer, NULL for failure
 *
 * Author: Summer Shang
 * Date: Aug. 28 2018
 * */
static PTMcList
RefreshBorderInfo(PTMcList plistBorderInfo, s32 nCoordinate, s32 nFrom, s32 nTo)
{
    s32 i    = 0;
    s32 j    = 0;

    PTMcList        plistBorderInfoHead = NULL;
    PTMcList        plistBorderInfoLast = NULL;

    PTMcList        plistBorderLen      = NULL;
    PTMcBorderInfo  ptBorderInfo        = NULL;
    PTMcBorderLen   ptBorderLen         = NULL;

    plistBorderInfoHead = plistBorderInfo;

    /* find the right coordinate info */
    while (plistBorderInfo) {
        ptBorderInfo = (PTMcBorderInfo)plistBorderInfo->pData;
        if (ptBorderInfo->nCoordinate == nCoordinate) {
            plistBorderLen = ptBorderInfo->nLenList;
            break;
        }
        plistBorderInfoLast = plistBorderInfo;              // used to record the end of the list
        plistBorderInfo = plistBorderInfo->pNext;
    }

    if (plistBorderLen) {
        /* find and join it */
        plistBorderLen = JoinLine(plistBorderLen, nFrom, nTo);
        if (plistBorderLen) {
            /* find the head of the list */
            while (plistBorderLen->pLast) {
                plistBorderLen = plistBorderLen->pLast;
            }
        }
        ptBorderInfo->nLenList = plistBorderLen;

    } else {
        /* new coordinate, create */
        PTMcList plistBorderInfoNew = NULL;
        BOOL32 bSuccess = TRUE;
        do {
            plistBorderInfoNew = (PTMcList)malloc(sizeof(TMcList));
            ptBorderInfo = (PTMcBorderInfo)malloc(sizeof(TMcBorderInfo));
            plistBorderLen = GetBorderLenCell(nFrom, nTo);
            if (!plistBorderInfoNew || !ptBorderInfo || !plistBorderLen) {
                warning("fail to new coordiate info\n");
                bSuccess = FALSE;
                break;
            }

            plistBorderInfoNew->pLast   = plistBorderInfoLast;
            plistBorderInfoNew->pNext   = NULL;
            plistBorderInfoNew->pData   = ptBorderInfo;

            if (plistBorderInfoLast) {
                /* add the new coordiate info to the list */
                plistBorderInfoLast->pNext = plistBorderInfoNew;
            } else {
                /* this coordinate is the newest one */
                plistBorderInfoHead = plistBorderInfoNew;
            }

            ptBorderInfo->nCoordinate   = nCoordinate;
            ptBorderInfo->nLenList      = plistBorderLen;
        } while (0);
        if (!bSuccess) {
            PutBorderLenCell(plistBorderLen);
            SAFE_FREE(ptBorderInfo);
            SAFE_FREE(plistBorderInfoNew);
        }
    }

    /* get the head */
    return plistBorderInfoHead;
}

/* Description:
 *   function to get mem cell of PTMcList and PTMcBorderLen.
 *
 * Param:
 *   nFrom              [IN]    init value of begin point
 *   nTo                [IN]    init value of end point
 *
 * Return:
 *   PTMcList pointer, NULL for failure
 *
 * Author: Summer Shang
 * Date: Aug. 23 2018
 * */
static PTMcList
GetBorderLenCell(s32 nFrom, s32 nTo)
{
    PTMcList        plistBorderLen  = NULL;
    PTMcBorderLen   ptBorderLen     = NULL;
    BOOL32          bSuccess        = FALSE;

    do {
        plistBorderLen  = (PTMcList)malloc(sizeof(TMcList));
        ptBorderLen     = (PTMcBorderLen)malloc(sizeof(TMcBorderLen));

        if (!plistBorderLen || !ptBorderLen) {
            warning("malloc fail!");
            break;
        }

        memset(plistBorderLen, 0, sizeof(TMcList));
        plistBorderLen->pData = ptBorderLen;

        ptBorderLen->nFrom  = nFrom;
        ptBorderLen->nTo    = nTo;

        bSuccess = TRUE;
    } while(0);

    if (!bSuccess) {
        SAFE_FREE(plistBorderLen);
        SAFE_FREE(ptBorderLen);
    }

    return plistBorderLen;
}


/* Description:
 *   function to put mem cell of PTMcList and PTMcBorderLen.
 *
 * Param:
 *
 * Return:
 *
 * Author: Summer Shang
 * Date: Aug. 23 2018
 * */
static void
PutBorderLenCell(PTMcList plistBorderLen)
{
    PTMcBorderLen ptBorderLen = NULL;

    if (plistBorderLen) {
        ptBorderLen = (PTMcBorderLen)plistBorderLen->pData;
        SAFE_FREE(ptBorderLen);
        free(plistBorderLen);
    }

    return;
}


/* Description:
 *   function to join new section in the line.
 *   The line already existed is arranged for little to large.
 *   We look up for the large direction recursively.
 *
 * Param:
 *   plistBorderLen     [IN]    compared section pointer
 *   nFrom              [IN]    begin point of section
 *   nTo                [IN]    end point of section
 *
 * Return:
 *   PTMcList pointer which contains the new dimension, NULL for end of list
 *
 * Author: Summer Shang
 * Date: Aug. 23 2018
 * */
static PTMcList
JoinLine(PTMcList plistBorderLen, s32 nFrom, s32 nTo)
{
    PTMcList      plistBorderLenCur     = NULL;
    PTMcList      plistBorderLenNext    = NULL;
    PTMcList      plistBorderLenLast    = NULL;

    PTMcBorderLen ptBorderLen   = NULL;

    BOOL32 bFromRecevied    = FALSE;
    BOOL32 bToRecevied      = FALSE;
    BOOL32 bIsDestroy       = FALSE;
    BOOL32 bNewCell         = FALSE;
    BOOL32 bKeepLook        = FALSE;
    BOOL32 bFront           = FALSE;

    /* end of the list */
    if (!plistBorderLen) {
        return NULL;
    }

    /* 新的边框A和老边框B存在以下六种关系，
     * 前互不干扰，前交叉，被包含，包含，后交叉，后互不干扰 
     *
     * A0|_______________|A1   B0|_____________|B1
     * A0|_______________|B0___A1|_____________|B1
     * B0|_______________|A0___A1|_____________|B1
     * A0|_______________|B0___B1|_____________|A1
     * B0|_______________|A0___B1|_____________|A1
     * B0|_______________|B1   A0|_____________|A1
     *
     * */
    ptBorderLen = (PTMcBorderLen)plistBorderLen->pData;
    if (ptBorderLen->nFrom > nTo) {
        /* 前互不干扰，插入新的单元，返回 */
        bNewCell = TRUE;
        bFront   = TRUE;
    } else if (ptBorderLen->nFrom <= nTo && ptBorderLen->nFrom > nFrom) {
        /* 前交叉，仅替换起始坐标，返回 */
        ptBorderLen->nFrom = nFrom;
        plistBorderLenCur = plistBorderLen;
    } else if (ptBorderLen->nFrom <= nFrom && ptBorderLen->nTo >= nTo) {
        /* 被包含，不动，返回 */
        plistBorderLenCur = plistBorderLen;
    } else if (ptBorderLen->nFrom >= nFrom && ptBorderLen->nTo <= nTo) {
        /* 包含，销毁当前单元，继续查找 */
        bIsDestroy  = TRUE;
        bKeepLook   = TRUE;
    } else if (ptBorderLen->nTo < nTo && ptBorderLen->nTo >= nFrom) {
        /* 后交叉，替换起始坐标，销毁当前单元，继续查找 */
        nFrom = ptBorderLen->nFrom;
        bIsDestroy  = TRUE;
        bKeepLook   = TRUE;
    } else if (ptBorderLen->nTo < nFrom) {
        /* 右互不干扰，继续查找 */
        //plistBorderLenNext = plistBorderLen->pNext;
        //plistBorderLenLast = plistBorderLen->pLast;
        bKeepLook = TRUE;
    }

    /* 销毁当前单元，生成新的列表 */
    if (bIsDestroy) {
        plistBorderLenNext = plistBorderLen->pNext;
        plistBorderLenLast = plistBorderLen->pLast;
        if (plistBorderLenNext) {
            plistBorderLenNext->pLast = plistBorderLen->pLast;
        }
        if (plistBorderLenLast) {
            plistBorderLenLast->pNext = plistBorderLenNext;
        }
        PutBorderLenCell(plistBorderLen);               // recycle the cell
        plistBorderLen = NULL;
    }

    /* 继续查找 */
    if (bKeepLook) {
        plistBorderLenCur = JoinLine(plistBorderLenNext, nFrom, nTo);
        /* 列表末尾或者没有内存加入失败 */
        if (!plistBorderLenCur) {
            bNewCell = TRUE;
        }
    }

    /* 准备插入新的列表 */
    if (bNewCell) {
        plistBorderLenCur = GetBorderLenCell(nFrom, nTo);
        if (plistBorderLenCur) {
            if (bFront) {
                /* 插入列表前 */
                plistBorderLenCur->pNext    = plistBorderLen;
                plistBorderLenCur->pLast    = plistBorderLen->pLast;
                if (plistBorderLen->pLast) {
                    plistBorderLen->pLast->pNext = plistBorderLenCur;
                }
                plistBorderLen->pLast       = plistBorderLenCur;
            } else if (bIsDestroy) {
                /* 最后一个cell */
                plistBorderLenCur->pNext = plistBorderLenNext;
                plistBorderLenCur->pLast = plistBorderLenLast;
                if (plistBorderLenNext) {
                    plistBorderLenNext->pLast = plistBorderLenCur;
                }
                if (plistBorderLenLast) {
                    plistBorderLenLast->pNext = plistBorderLenCur;
                }
            } else {
                /* 插入列表后 */
                plistBorderLenCur->pNext    = plistBorderLen->pNext;
                plistBorderLenCur->pLast    = plistBorderLen;
                if (plistBorderLen->pNext) {
                    plistBorderLen->pNext->pLast = plistBorderLenCur;
                }
                plistBorderLen->pNext       = plistBorderLenCur;
            }
        } else {
            warning("lack of mem! danger!!!\n");
            if (plistBorderLen) {
                /* 直接插入失败 */
                plistBorderLenCur = plistBorderLen;
            } else if (plistBorderLenNext) {
                /* 销毁后创建失败，如果当前是列表头，返回下一个指针 */
                plistBorderLenCur = plistBorderLenNext;
            } else if (plistBorderLenLast) {
                /* 销毁后创建失败，如果当前是列表尾，返回上一个指针 */
                plistBorderLenCur = plistBorderLenLast;
            } else {
                warning("list is empty, may crash!!!\n");
            }
        }
    }

    /* NULL end of list */
    return plistBorderLenCur;
}

/* Description:
 *   generate background color YUV.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 29 2018
 * */
static s32
McMergeGenBgColor(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_FAIL;

    nRet = GenerateColor(ptHandler->tMcMergeParam.tMcMergeBgPic.nBackgroundRGB,
            &ptHandler->tBgYUV);

    return en_MCMERGE_SUCCESS;

}

/* Description:
 *   generate boarder color YUV.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Aug. 29 2018
 * */
static s32
McMergeGenBorderColor(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_FAIL;
 
    nRet = GenerateColor(ptHandler->tMcMergeParam.tMcMergeBgPic.nBoundaryRGB,
            &ptHandler->tBorderYUV);

    return nRet;
}

/* Description:
 *   generate color YUV.
 *
 * Param:
 *   nRGB                   [IN]    RGB
 *   ptColorYUV             [OUT]   YUV
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jul. 24 2018
 * */
static s32
GenerateColor(s32 nRGB, TMcColorYUV *ptColorYUV)
{
    u8  byR     = 0;
    u8  byG     = 0;
    u8  byB     = 0;

    /* we don't check param here */
    byR = RGB2R(nRGB);
    byG = RGB2G(nRGB);
    byB = RGB2B(nRGB);
    ptColorYUV->byY   = RGB2Y(byR, byG, byB);
    ptColorYUV->byU   = RGB2U(byR, byG, byB);
    ptColorYUV->byV   = RGB2V(byR, byG, byB);

    return en_MCMERGE_SUCCESS;
}


/* Description:
 *   the function is used to print no of the style.
 *
 * Param:
 *   none
 *
 * Return:
 *   void
 * */
void McMergeStyleInfo(void)
{
    for (s32 i = 0; i < en_MCMERGE_STYLE_MAX; i++) {
        warning("%d\t%s\n", i, atMcMergeStyleInfo[i]);
    }

    return;
}

/* Description:
 *   function to call generation method of the focus info.
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jan. 15 2019
 * */
static s32
GenFocusInfo(PTMcMergeHandler ptHandler)
{
    s32 nRet = en_MCMERGE_SUCCESS;
    s32 i = 0;

    /* loop generate the info for evey subwin */
    for (; i < ptHandler->nSubWinNum; i++) {
        nRet = DoGenFocusInfo(&(ptHandler->tMcMergeParam.tMcMergeBgPic),
                    &(ptHandler->tMcMergeParam.ptMcMergeFgPic[i]),
                    &(ptHandler->ptFocusInfo[i]),
                    &(ptHandler->ptDstRect[i]));
    }
    return nRet;
}

/* Description:
 *   function to generate the merge focus position,
 *   color, status(show or not).
 *
 * Param:
 *   ptHandler              [IN]    inner handler
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jan. 15 2019
 * */
static s32
DoGenFocusInfo(PTMcMergeBgParam ptBgParam,
               PTMcMergeFgParam ptFgParam, 
               PTMcFocusInfo ptFocusInfo,
               PTMcMergeRect ptDstRect)
{
    s32 nBorderW    = 0;
    s32 nOffset     = 0;

    if (ptBgParam->nDrawBoundaryFlag) {
        nBorderW    = ptBgParam->nBoundaryWidth;
        nOffset     = (ptBgParam->nBoundaryWidth >> 2) << 1;
    }

    memset(ptFocusInfo, 0, sizeof(PTMcFocusInfo));

    ptFocusInfo->bIsShow    = ptFgParam->nDrawFocusFlag ? TRUE : FALSE;
    GenerateColor(ptFgParam->nFocusRGB, &(ptFocusInfo->tColorYUV));

    ptFocusInfo->s16Left    = ptDstRect->s16Left;
    ptFocusInfo->s16Right   = ptDstRect->s16Left + ptDstRect->s16Width;
    ptFocusInfo->s16Top     = ptDstRect->s16Top;
    ptFocusInfo->s16Bottom  = ptDstRect->s16Top + ptDstRect->s16Height;
 
    ptFocusInfo->s16Left    += (ptDstRect->s16Left ? (nBorderW - nOffset) : nBorderW);
    ptFocusInfo->s16Right   -= 
        (ptFocusInfo->s16Right == ptBgParam->nBgWidth ? nBorderW : nOffset);
    ptFocusInfo->s16Top     += (ptDstRect->s16Top ? (nBorderW - nOffset) : nBorderW);
    ptFocusInfo->s16Bottom  -=
        (ptFocusInfo->s16Bottom == ptBgParam->nBgHeight ? nBorderW : nOffset);

    ptFocusInfo->s16Width   = ptFocusInfo->s16Right - ptFocusInfo->s16Left;
    ptFocusInfo->s16Height  = ptFocusInfo->s16Bottom - ptFocusInfo->s16Top;

    return en_MCMERGE_SUCCESS;
}

/* Description:
 *   the function is used set mcmerger param.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   ptMcMergeParam     [IN]    param for mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jan. 16 2019
 * */
s32 McMergeSetParam(void* pvHandler, TMcMergeParam* ptMcMergeParam)
{
    return en_MCMERGE_SUCCESS;
}

/* Description:
 *   the function is used set mcmerger background.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   nBgWidth           [IN]    background width, range is below
 *                              [MCMERGE_BG_WIDTH_MIN , MCMERGE_BG_WIDTH_MAX]
 *   nBgHeight          [IN]    background width, range is below
 *                              [MCMERGE_BG_HEIGHT_MIN , MCMERGE_BG_HEIGHT_MAX]
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jan. 16 2019
 * */
s32 McMergeSetBGSize(void* pvHandler, s32 nBgWidth, s32 nBgHeight)
{
    s32 nRet = en_MCMERGE_FAIL;
    PTMcMergeHandler ptHandler = NULL;

    do {
        if (en_MCMERGE_SUCCESS != McMergeCheckBgSize(nBgWidth, nBgHeight)) {
            warning("invalid w %d h%d\n", nBgWidth, nBgHeight);
            return nRet;
        }

        if (NULL == pvHandler) {
            warning("null pointer of handler");
            break;
        }

        ptHandler = (PTMcMergeHandler)pvHandler;
        if (nBgWidth    == ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth &&
            nBgHeight   == ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight) {
            debug("the same w %d h %d\n", nBgWidth, nBgHeight);
            nRet = en_MCMERGE_SUCCESS;
            break;
        }

        ptHandler->tMcMergeParam.tMcMergeBgPic.nBgWidth     = nBgWidth;
        ptHandler->tMcMergeParam.tMcMergeBgPic.nBgHeight    = nBgHeight;

        /* background size changed, every positions
         * are needed to recaculate */

        /* caculate the postion for merge */
        nRet = CaculateSubwinPosition(ptHandler);
        nRet = CaculateExtrawinPosition(ptHandler);

        /* generate border information */
        nRet = CaculateBorderPosition(ptHandler);

        /* generate focus information */
        nRet = GenFocusInfo(ptHandler);
    } while (0);

    return nRet;
}

/* Description:
 *   the function is used set mcmerger style.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   enStyle            [IN]    merge style, ref to TMcMergeStyle
 *
 * Return:
 *   TMcMergeErrorCode
 *
 * Author: Summer Shang
 * Date: Jan. 16 2019
 * */
s32 McMergeSetStyle(void* pvHandler, TMcMergeStyle enStyle)
{
    s32 nRet        = en_MCMERGE_FAIL;
    s32 nSubWinNum  = 0;
    PTMcMergeHandler ptHandler = NULL;

    do {
        if (en_MCMERGE_SUCCESS != McMergeCheckMergeStyle(enStyle)) {
            warning("invalid style %d\n", enStyle);
            return nRet;
        }

        if (NULL == pvHandler) {
            warning("null pointer of handler");
            break;
        }

        ptHandler = (PTMcMergeHandler)pvHandler;
        if (enStyle == ptHandler->tMcMergeParam.nMergeStyle) {
            debug("same style %d no need to change!\n", enStyle);
            nRet = en_MCMERGE_SUCCESS;
            break;
        }

        McMergeStyle2MergeWinNumber(enStyle, nSubWinNum);
        if (nSubWinNum > ptHandler->tMcMergeParam.nMaxFgNum) {
            warning("conflict param mergetype %d nMaxFgNum %d\n",
                enStyle, ptHandler->tMcMergeParam.nMaxFgNum);
            nRet = en_MCMERGE_INVALID_PARAM;
            break;
        }

        ptHandler->nSubWinNum   = nSubWinNum;

        /* style changed, every positions
         * are needed to recaculate */

        /* caculate the postion for merge */
        nRet = CaculateSubwinPosition(ptHandler);
        nRet = CaculateExtrawinPosition(ptHandler);

        /* generate border information */
        nRet = CaculateBorderPosition(ptHandler);

        /* generate focus information */
        nRet = GenFocusInfo(ptHandler);

    } while(0);
 
    return nRet;
}

#ifdef __cplusplus
}
#endif
