#define l32 s32
#include "kdvtype.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include "libyuv.h"
#include "img_imageunit.h"
#include "mcmerge.h"

#include <sys/time.h>

#define true    1
#define false   0

#define PIC_WIDTH_MAX       1920
#define PIC_HEIGHT_MAX      1080

#define ALIGN_BITS          64

#define SAFE_FCLOSE(p)      if(p){fclose(p); p = NULL;}
#define SAFE_DELS(p)        if(p){free(p); p = NULL;}

#define debug   printf
#define warning printf

#define ASSERT(eq)  if(!(eq)){warning("%s\n", #eq); break;}
#define ALIGN_MEM(p, a)    ((long)p + a - (((long)p) & (a - 1)))

enum tagEnScaleType
{
    EN_SCALE_MIN,
    EN_SCALE_SCALE,           // scale ignore ratio
    EN_SCALE_FILL,            // keep ratio fill black
    EN_SCALE_CUT,             // keep ratio cut
    EN_SCALE_MAX,
};

static struct option g_tOptStr[] = 
{
    {"sw", 1, NULL, 0},
    {"sh", 1, NULL, 0},
    {"dw", 1, NULL, 0},
    {"dh", 1, NULL, 0},
    {0, 0, NULL, 0},
};

static char g_chSrcFile[1024];
static char g_chDstFile[1024];

static int g_nSrcWidth  = 704;
static int g_nSrcHeight = 576;
static int g_nDstWidth  = 704;
static int g_nDstHeight = 576;
static int g_nScaleType = EN_SCALE_MIN;

static int g_nStartSrcX = 0;
static int g_nStartSrcY = 0;
static int g_nEndSrcX   = 0;
static int g_nEndSrcY   = 0;
static int g_nStartDstX = 0;
static int g_nStartDstY = 0;
static int g_nEndDstX   = 0;
static int g_nEndDstY   = 0;

static int g_nMergeStyle = 4;

void GetOption(int argc, const char* argv[])
{
    char chOpt      = 0;
    int  nLongOptNo = 0;

    while ((chOpt = getopt_long(argc, argv, "i:o:t:m:h", g_tOptStr, &nLongOptNo)) != -1)
    {
        switch (chOpt)
        {
            case 0:
                debug("option %s: param %s\n", g_tOptStr[nLongOptNo].name, optarg);
                switch(nLongOptNo)
                {
                    case 0:
                        g_nSrcWidth = atoi(optarg);
                        break;
                    case 1:
                        g_nSrcHeight = atoi(optarg);
                        break;
                    case 2:
                        g_nDstWidth = atoi(optarg);
                        break;
                    case 3:
                        g_nDstHeight = atoi(optarg);
                        break;
                    default:
                        break;
                }
                break;
            case 'i':
                debug("option i: param %s\n", optarg);
                snprintf(g_chSrcFile, sizeof(g_chSrcFile), "%s", optarg);
                break;
            case 'o':
                debug("option o: param %s\n", optarg);
                snprintf(g_chDstFile, sizeof(g_chDstFile), "%s", optarg);
                break;
            case 'm':
                debug("option m: param %s\n", optarg);
                g_nMergeStyle = atoi(optarg);
                break;
            case 't':
                debug("option t: param %s\n", optarg);
                g_nScaleType = atoi(optarg);
                break;
            case 'h':
                debug("\nusage: %s --sw xx --sh xx -i xx --dw xx --dh xx -o xx -t xx -m xx\n", argv[0]);
                debug("      --sw: source picture width\n");
                debug("      --sh: source picture height\n");
                debug("       -i : source picture file name\n");
                debug("      --dw: dst picture width\n");
                debug("      --dh: dst picture height\n");
                debug("       -o : dst picture file name\n");
                debug("       -m : merge type from %d - %d\n", en_MCMERGE_STYLE_MIN + 1, en_MCMERGE_STYLE_MAX - 1);
                //McMergeStyleInfo();
                debug("       -t : scale tyep, %d == scale, %d == fill, %d == cut\n",
                    EN_SCALE_SCALE, EN_SCALE_FILL, EN_SCALE_CUT);
                debug("\n");
                break;
            default:
                debug("invalid option %d\n", chOpt);
                break;
        }
    }
    
    return;
}


int CheckParam()
{
    int ret = true;
    
    /* scale type check */
    if (g_nScaleType <=  EN_SCALE_MIN || g_nScaleType >= EN_SCALE_MAX)
    {
        warning("invalid scale type %d\n", g_nScaleType);
        ret = false;
    }
    
    /* */
    if (g_nSrcWidth <= 0 || g_nSrcWidth > PIC_WIDTH_MAX ||
        g_nSrcHeight <= 0 || g_nSrcHeight > PIC_HEIGHT_MAX ||
        g_nDstWidth <= 0 || g_nDstWidth > PIC_WIDTH_MAX ||
        g_nDstHeight <= 0 || g_nDstHeight > PIC_HEIGHT_MAX)
    {
        warning("sw = %d, sh = %d, dw = %d, dh = %d\n", g_nSrcWidth, g_nSrcHeight, g_nDstWidth, g_nDstHeight);
        warning("max width %d, max height %d\n", PIC_WIDTH_MAX, PIC_HEIGHT_MAX);
        warning("invalid w or h\n");
        ret = false;
    }
    return ret;
}


void CaculateScaleParam()
{
    int nSpare = 0;
    g_nStartSrcX    = 0;
    g_nStartSrcY    = 0;
    g_nEndSrcX      = g_nSrcWidth;
    g_nEndSrcY      = g_nSrcHeight;
    g_nStartDstX    = 0;
    g_nStartDstY    = 0;
    g_nEndDstX      = g_nDstWidth;
    g_nEndDstY      = g_nDstHeight;

    switch (g_nScaleType)
    {
    case EN_SCALE_SCALE:
        debug("scale mode just return\n");
        break;
    case EN_SCALE_FILL:
        /* 16:9 ==> 4:3 type, fill black top and bottom */
        if (g_nSrcWidth * g_nDstHeight > g_nSrcHeight * g_nDstWidth)
        {
            nSpare = (g_nDstHeight - g_nSrcHeight * g_nDstWidth / g_nSrcWidth) &(-4);
            g_nStartDstY = nSpare >> 1;
            g_nEndDstY = g_nDstHeight - g_nStartDstY;
        }
        /* 4:3 ==> 16:9 type, fill black left and right */
        else if (g_nSrcWidth * g_nDstHeight < g_nSrcHeight * g_nDstWidth)
        {
            nSpare = (g_nDstWidth - g_nSrcWidth * g_nDstHeight / g_nSrcHeight) &(-4);
            g_nStartDstX = nSpare >> 1;
            g_nEndDstX = g_nDstWidth - g_nStartDstX;
        }
        else
        {
            debug("same ratio, just return\n");
        }
        break;
    case EN_SCALE_CUT:
        /* 16:9 ==> 4:3 type, src cut left and right */
        if (g_nSrcWidth * g_nDstHeight > g_nSrcHeight * g_nDstWidth)
        {
            nSpare = (g_nSrcWidth - g_nDstWidth * g_nSrcHeight / g_nDstHeight) &(-4);
            g_nStartSrcX = nSpare >> 1;
            g_nEndSrcX = g_nSrcWidth - g_nStartSrcX;
        }
        /* 4:3 ==> 16:9 type, src cut top and bottom */
        else if (g_nSrcWidth * g_nDstHeight < g_nSrcHeight * g_nDstWidth)
        {
            nSpare = (g_nSrcHeight - g_nDstHeight * g_nSrcWidth / g_nDstWidth) &(-4);
            g_nStartSrcY = nSpare >> 1;
            g_nEndSrcY = g_nSrcHeight - g_nStartSrcY;
        }
        else
        {
            debug("same ratio, just return\n");
        }
        break;
    default:
        break;
    }
    return;
}

#define PIC_MERGE_CHANNEL_MAX       25

static s32 atKedaStyle[en_MCMERGE_STYLE_MAX] =
{
    PIC_MERGE_ZOOM_M0,
    PIC_MERGE_ZOOM_M1,
    PIC_MERGE_ZOOM_M2,
    PIC_MERGE_ZOOM_M2_1_BR1,
    PIC_MERGE_ZOOM_M2_1_BL1,
    PIC_MERGE_ZOOM_M2_1_TR1,
    PIC_MERGE_ZOOM_M2_1_TL1,
    PIC_MERGE_ZOOM_M3_T1,
    PIC_MERGE_ZOOM_M3_B1,
    PIC_MERGE_ZOOM_M3_1_B2,
    PIC_MERGE_ZOOM_M3_1_T2,
    PIC_MERGE_ZOOM_M3_1_R2,
    PIC_MERGE_ZOOM_M3_L1,
    PIC_MERGE_ZOOM_M4,
    PIC_MERGE_ZOOM_M4_1_R3,
    PIC_MERGE_ZOOM_M4_1_D3,
    PIC_MERGE_ZOOM_M5_1_R4,
    PIC_MERGE_ZOOM_M5_1_D4,
    PIC_MERGE_ZOOM_M5_2_D3,
    PIC_MERGE_ZOOM_M6,
    PIC_MERGE_ZOOM_M6_1_5,
    PIC_MERGE_ZOOM_M6_2_B4,
    PIC_MERGE_ZOOM_M6_1_B5,
    PIC_MERGE_ZOOM_M6_B5,
    PIC_MERGE_ZOOM_M7_3_TL4,
    PIC_MERGE_ZOOM_M7_3_TR4,
    PIC_MERGE_ZOOM_M7_3_BL4,
    PIC_MERGE_ZOOM_M7_3_BR4,
    PIC_MERGE_ZOOM_M7_3_BLR4,
    PIC_MERGE_ZOOM_M7_3_TLR4,
    PIC_MERGE_ZOOM_M7_1_D6,
    PIC_MERGE_ZOOM_M8_1_7,
    PIC_MERGE_ZOOM_M8_4_4,
    PIC_MERGE_ZOOM_M9,
    PIC_MERGE_ZOOM_M9_T4_1_D4,
    PIC_MERGE_ZOOM_M10_2_R8,
    PIC_MERGE_ZOOM_M10_2_B8,
    PIC_MERGE_ZOOM_M10_2_T8,
    PIC_MERGE_ZOOM_M10_2_L8,
    PIC_MERGE_ZOOM_M10_2_TB8,
    PIC_MERGE_ZOOM_M10_1_9,
    PIC_MERGE_ZOOM_M10_L4_2_R4,
    PIC_MERGE_ZOOM_M11_T5_1_D5,
    PIC_MERGE_ZOOM_M11_1_D10,
    PIC_MERGE_ZOOM_M12_1_11,
    PIC_MERGE_ZOOM_M12_3_RD9,
    PIC_MERGE_ZOOM_M13_TL1_12,
    PIC_MERGE_ZOOM_M13_TR1_12,
    PIC_MERGE_ZOOM_M13_BL1_12,
    PIC_MERGE_ZOOM_M13_BR1_12,
    PIC_MERGE_ZOOM_M13_1_ROUND12,
    PIC_MERGE_ZOOM_M13_TL4_9,
    PIC_MERGE_ZOOM_M13_L6_1_R6,
    PIC_MERGE_ZOOM_M14_1_13,
    PIC_MERGE_ZOOM_M14_TL2_12,
    PIC_MERGE_ZOOM_M14_T5_1_2_1_D5,
    PIC_MERGE_ZOOM_M15_T3_12,
    PIC_MERGE_ZOOM_M15_T4_L3_1_R3_D4,
    PIC_MERGE_ZOOM_M16,
    PIC_MERGE_ZOOM_M16_1_15,
    PIC_MERGE_ZOOM_M17_1,
    PIC_MERGE_ZOOM_M17_2,
    PIC_MERGE_ZOOM_M17_3,
    PIC_MERGE_ZOOM_M18_1,
    PIC_MERGE_ZOOM_M18_2,
    PIC_MERGE_ZOOM_M18_3,
    PIC_MERGE_ZOOM_M19_1,
    PIC_MERGE_ZOOM_M19_2,
    PIC_MERGE_ZOOM_M20,
    PIC_MERGE_ZOOM_M21_1,
    PIC_MERGE_ZOOM_M21_2,
    PIC_MERGE_ZOOM_M22,
    PIC_MERGE_ZOOM_M23_1,
    PIC_MERGE_ZOOM_M23_2,
    PIC_MERGE_ZOOM_M24,
    PIC_MERGE_ZOOM_M25_1,
    PIC_MERGE_ZOOM_M25_2,
};

static void* m_pvKedaMerge = NULL;
static TImageMergeZoomInput *m_ptImageMergeZoomInput = NULL;
static TImageMergeZoomOutput *m_ptImageMergeZoomOutput = NULL;

BOOL32 CreateKedaMerger(s32 nStyle)
{
    TImageMergeZoomParam* m_ptImageMergeZoomParam;             //»­ÃæºÏ³ÉÄ£¿é³õÊ¼»¯²ÎÊý½á¹¹
    m_ptImageMergeZoomParam = new TImageMergeZoomParam;
    memset(m_ptImageMergeZoomParam, 0, sizeof(TImageMergeZoomParam));

    m_ptImageMergeZoomParam->ptMergeZoomFgPic = new TMergeZoomFgParam[PIC_MERGE_CHANNEL_MAX];

    /* input variable */
    m_ptImageMergeZoomInput = new TImageMergeZoomInput;
    m_ptImageMergeZoomInput->ptImageMergeZoomInputPic = new TImageMergeZoomPicInfo[PIC_MERGE_CHANNEL_MAX];

    /* output variable */
    m_ptImageMergeZoomOutput = new TImageMergeZoomOutput;

    m_ptImageMergeZoomParam->l32MaxFgNum    = PIC_MERGE_CHANNEL_MAX;                        //»­ÃæºÏ³ÉµÄ×î´óÇ°¾°ÊýÁ¿£¨Ä¬ÈÏÎª×î´ó64»­Ãæ£©
    m_ptImageMergeZoomParam->l32MergeStyle  = atKedaStyle[nStyle];                              //»­ÃæºÏ³ÉÄ£Ê½£¨¹²¼Æ22ÖÖºÏ³ÉÀàÐÍ£¬Ïê¼ûPicMergeStyleÃ¶¾ÙÀàÐÍ£©

    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BoundaryRGB         = 0x00FFFFFF;               //»­Ãæ±ß½çÏßÑÕÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BoundaryWidth       = 2;                        //±ß½çÏßÍ³Ò»¿í¶È£¨Ö¡¸ñÊ½Í¼ÏñÐëÎª2µÄ±¶Êý£¬³¡¸ñÊ½Í¼ÏñÐèÎª4µÄ±¶Êý£©
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BgWidth             = g_nDstWidth;               //±³¾°Í¼ÏñµÄ¿í¶È
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BgHeight            = g_nDstHeight;              //±³¾°Í¼ÏñµÄ¸ß¶È
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BgFrameFieldFormat  = FRAME_FORMAT;             //±³¾°Í¼ÏñµÄÀàÐÍ(Ö¡¸ñÊ½»òÕß³¡¸ñÊ½)£¨Ö¡¸ñÊ½ÎªFRAME_FORMAT£»³¡¸ñÊ½ÎªFIELD_FORMAT£©
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BgYUVType           = YUV420;                    //±³¾°Í¼Ïñ¸ñÊ½(YUV422»òÕßYUV420)
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32DrawBoundaryFlag    = 1;                        //»­ÃæÊÇ·ñ¹´»­±ß½çÏßµÄ±ê¼Ç£¨nDrawBoundaryFlagÎª1±íÃ÷»­±ß½çÏß£¬nDrawBoundaryFlagÎª0±íÃ÷²»»­±ß½çÏß£©
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32DrawBackgroundFlag  = 1;                        //±³¾°ÎÞÍ¼Ïñ´¦ÊÇ·ñÌî³ä±³¾°É«
    m_ptImageMergeZoomParam->tMergeZoomBgPic.l32BackgroundRGB       = 0x00000000;               //»­ÃæºÏ³É±³¾°Ìî³äÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©

    for (s32 i = 0; i <PIC_MERGE_CHANNEL_MAX; i++)
    {
        //Ç°¾°Í¼Ïñ²ÎÊýÐÅÏ¢
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgInputSource   = FG_PIC_INPUT;                 //Ç°¾°Í¼ÏñµÄÊäÈëÄÚÈÝ£¨ÆäÖÐFG_PIC_INPUTÎªÇ°¾°ÓÐÍ¼ÏñÊäÈë£»NO_PIC_INPUTÎªÇ°¾°ÎÞÍ¼ÏñÊäÈë£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32ZoomStyle       = g_nScaleType;  //Ëõ·ÅÄ£Ê½£¨PIC_ZOOM_ONE£ºÈ«ÆÁËõ·Å£¬PIC_ZOOM_TWO£º±£³Ö±ÈÀýÀ­Éì²¢±£³ÖÍ¼ÏñÍêÕû£¬PIC_ZOOM_THREE£º±£³Ö±ÈÀýÀ­Éì²¢³äÂúÄ¿±êÇøÓò£¬PIC_ZOOM_FOUR£º±£³ÖÔ­ÓÐ³ß´ç,´óÓÚºÏ³É³ß´çµÄÍ¼Ïñ»á¾ÓÖÐ²¢×ö²Ã±ß´¦Àí, PIC_ZOOM_FIVE ±£³ÖÔ­ÓÐ³ß´ç,´óÓÚºÏ³É³ß´çµÄÍ¼Ïñ»á°´ÕÕ±£³Ö±ÈÀýÀ­Éì²¢±£³ÖÍ¼ÏñÍêÕûËõ·Å£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgPositionNum   = i + 1;                        //»­ÃæÎ»ÖÃ±àºÅ£¨»­ÃæÎ»ÖÃ±àºÅÔ¼¶¨°´ÕÕ´ÓÉÏµ½ÏÂ´Ó×óµ½ÓÒË³ÐòÒÀ´Î±àºÅ£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgSrcWidth      = g_nSrcWidth;              //Ç°¾°µÄÊäÈëÔ´Í¼Ïñ¿í¶È
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgSrcHeight     = g_nSrcHeight;             //Ç°¾°µÄÊäÈëÔ´Í¼Ïñ¸ß¶È
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgFrameFieldFormat = FRAME_FORMAT;              //Ç°¾°Í¼ÏñµÄÀàÐÍ(Ö¡¸ñÊ½»òÕß³¡¸ñÊ½)£¨Ö¡¸ñÊ½ÎªFRAME_FORMAT£»³¡¸ñÊ½ÎªFIELD_FORMAT£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FgYUVType       = YUV420;                       //Ç°¾°Í¼Ïñ¸ñÊ½(YUV422»òÕßYUV420)
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32DrawFocusFlag   = 0;//(i % 3 == 0) ? 0 : 1;                            //»­ÃæÊÇ·ñ¹´»­±ß¿òµÄ±ê¼Ç£¨nDrawFocusFlagÎª1±íÃ÷»­±ß¿ò£¬nDrawFocusFlagÎª0±íÃ÷²»»­±ß¿ò£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FocusRGB        = 0x00FF0000;                   //»­Ãæ±ß¿òÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32FocusWidth      = 2;                            //±ß¿òÍ³Ò»¿í¶È£¨Ö¡¸ñÊ½Í¼ÏñÐëÎª2µÄ±¶Êý£¬³¡¸ñÊ½Í¼ÏñÐèÎª4µÄ±¶Êý£©,Ä¿Ç°Ö»Ö§³ÖÓë±ß½çÏß±£³ÖÒ»ÖÂ
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32ZoomScaleWidth  = 0;                            //Õë¶ÔËõ·ÅÄ£Ê½ÖÐÄ£Ê½¶þºÍÈýµÄËõ·Å¿í¸ß±ÈÀý£¬¸Ã²ÎÊýÓënZoomHeightScale³É¶ÔÊ¹ÓÃ(ÈçÇ°¾°°´ÕÕ4£º3±ÈÀýËõ·Å£¬Ôò¸ÃÖµÎª4£¬¸Ã²ÎÊýÎªÁãÔò°´ÕÕ±£³ÖÊäÈëÍ¼Ïñ±ÈÀýËõ·Å)
        m_ptImageMergeZoomParam->ptMergeZoomFgPic[i].l32ZoomScaleHeight = 0;                            //Õë¶ÔËõ·ÅÄ£Ê½ÖÐÄ£Ê½¶þºÍÈýµÄËõ·Å¿í¸ß±ÈÀý£¬¸Ã²ÎÊýÓënZoomWidthScale³É¶ÔÊ¹ÓÃ(ÈçÇ°¾°°´ÕÕ4£º3±ÈÀýËõ·Å£¬Ôò¸ÃÖµÎª3£¬¸Ã²ÎÊýÎªÁãÔò°´ÕÕ±£³ÖÊäÈëÍ¼Ïñ±ÈÀýËõ·Å)
    }

    s32 nResult = ImageUnitOpen(&m_pvKedaMerge, m_ptImageMergeZoomParam, (void *)1, IMG_MERGE_ZOOM);

    BOOL32 bRet = TRUE;
    if(m_pvKedaMerge == NULL || en_MCMERGE_SUCCESS != nResult) {
        printf("CImageMergeZoomWrapper: VideoUnitEncoderOpen failure! %d\n", nResult);
        bRet = FALSE;
    }

    delete [] m_ptImageMergeZoomParam->ptMergeZoomFgPic;
    delete m_ptImageMergeZoomParam;

    return bRet;
}


BOOL32 DestroyKedaMerger()
{
    if (m_pvKedaMerge) {
        ImageUnitClose(m_pvKedaMerge);
        m_pvKedaMerge = NULL;
    }

    if (m_ptImageMergeZoomOutput)
    {
        delete m_ptImageMergeZoomOutput;
    }

    if (m_ptImageMergeZoomInput) {
        if (m_ptImageMergeZoomInput->ptImageMergeZoomInputPic) {
            delete []m_ptImageMergeZoomInput->ptImageMergeZoomInputPic;
        }
        delete m_ptImageMergeZoomInput;
    }

    return true;
}

int KedaMergeProc(u8* pbyIn, u8* pbyOut)
{
    int i = 0;

    m_ptImageMergeZoomInput->tImageMergeZoomOutputPic.pu8Y = pbyOut;
    m_ptImageMergeZoomInput->tImageMergeZoomOutputPic.pu8U = NULL;
    m_ptImageMergeZoomInput->tImageMergeZoomOutputPic.pu8V = NULL;
    m_ptImageMergeZoomInput->tImageMergeZoomOutputPic.l32YStride    = g_nDstWidth;
    m_ptImageMergeZoomInput->tImageMergeZoomOutputPic.l32UVStride   = g_nDstWidth >> 1;

    for (i = 0 ; i < PIC_MERGE_CHANNEL_MAX; i++)
    {
        m_ptImageMergeZoomInput->ptImageMergeZoomInputPic[i].pu8Y = pbyIn;
        m_ptImageMergeZoomInput->ptImageMergeZoomInputPic[i].pu8U = NULL;
        m_ptImageMergeZoomInput->ptImageMergeZoomInputPic[i].pu8V = NULL;
        m_ptImageMergeZoomInput->ptImageMergeZoomInputPic[i].l32YStride  = g_nSrcWidth;
        m_ptImageMergeZoomInput->ptImageMergeZoomInputPic[i].l32UVStride = g_nSrcWidth >> 1;
    }

    s32 nRet = ImageUnitProcess(m_pvKedaMerge, m_ptImageMergeZoomInput, m_ptImageMergeZoomOutput);
    if (en_MCMERGE_SUCCESS != nRet)
    {
        printf("ImageUnitProcess: proc failed. %d\n", nRet);
    }
    return 0;
}

static void* m_pvImageHandle = NULL;
static TMcMergeInput *m_ptMcMergeInput = NULL;
static TMcMergeOutput *m_ptMcMergeOutput = NULL;

BOOL32 CreateMcMerger(s32 nStyle)
{
    TMcMergeParam* m_ptMcMergeParam;             //»­ÃæºÏ³ÉÄ£¿é³õÊ¼»¯²ÎÊý½á¹¹
    m_ptMcMergeParam = new TMcMergeParam;
    memset(m_ptMcMergeParam, 0, sizeof(TMcMergeParam));

    m_ptMcMergeParam->ptMcMergeFgPic = new TMcMergeFgParam[MCMERGE_CHANNEL_MAX];

    /* input variable */
    m_ptMcMergeInput = new TMcMergeInput;
    m_ptMcMergeInput->ptMcMergeInputPic = new TMcMergePicInfo[MCMERGE_CHANNEL_MAX];

    /* output variable */
    m_ptMcMergeOutput = new TMcMergeOutput;

    m_ptMcMergeParam->nMaxFgNum    = MCMERGE_CHANNEL_MAX;                        //»­ÃæºÏ³ÉµÄ×î´óÇ°¾°ÊýÁ¿£¨Ä¬ÈÏÎª×î´ó64»­Ãæ£©
    m_ptMcMergeParam->nMergeStyle  = nStyle;                              //»­ÃæºÏ³ÉÄ£Ê½£¨¹²¼Æ22ÖÖºÏ³ÉÀàÐÍ£¬Ïê¼ûPicMergeStyleÃ¶¾ÙÀàÐÍ£©

    m_ptMcMergeParam->tMcMergeBgPic.nBoundaryRGB         = 0x00FFFFFF;               //»­Ãæ±ß½çÏßÑÕÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©
    m_ptMcMergeParam->tMcMergeBgPic.nBoundaryWidth       = 2;                        //±ß½çÏßÍ³Ò»¿í¶È£¨Ö¡¸ñÊ½Í¼ÏñÐëÎª2µÄ±¶Êý£¬³¡¸ñÊ½Í¼ÏñÐèÎª4µÄ±¶Êý£©
    m_ptMcMergeParam->tMcMergeBgPic.nBgWidth             = g_nDstWidth;               //±³¾°Í¼ÏñµÄ¿í¶È
    m_ptMcMergeParam->tMcMergeBgPic.nBgHeight            = g_nDstHeight;              //±³¾°Í¼ÏñµÄ¸ß¶È
    m_ptMcMergeParam->tMcMergeBgPic.nBgFrameFieldFormat  = 1;             //±³¾°Í¼ÏñµÄÀàÐÍ(Ö¡¸ñÊ½»òÕß³¡¸ñÊ½)£¨Ö¡¸ñÊ½ÎªFRAME_FORMAT£»³¡¸ñÊ½ÎªFIELD_FORMAT£©
    m_ptMcMergeParam->tMcMergeBgPic.nBgYUVType           = 1;                   //±³¾°Í¼Ïñ¸ñÊ½(YUV422»òÕßYUV420)
    m_ptMcMergeParam->tMcMergeBgPic.nDrawBoundaryFlag    = 1;                        //»­ÃæÊÇ·ñ¹´»­±ß½çÏßµÄ±ê¼Ç£¨nDrawBoundaryFlagÎª1±íÃ÷»­±ß½çÏß£¬nDrawBoundaryFlagÎª0±íÃ÷²»»­±ß½çÏß£©
    m_ptMcMergeParam->tMcMergeBgPic.nDrawBackgroundFlag  = 1;                        //±³¾°ÎÞÍ¼Ïñ´¦ÊÇ·ñÌî³ä±³¾°É«
    m_ptMcMergeParam->tMcMergeBgPic.nBackgroundRGB       = 0x00000000;               //»­ÃæºÏ³É±³¾°Ìî³äÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©

    for (s32 i = 0; i <MCMERGE_CHANNEL_MAX; i++)
    {
        //Ç°¾°Í¼Ïñ²ÎÊýÐÅÏ¢
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgInputSource   = 0;                 //Ç°¾°Í¼ÏñµÄÊäÈëÄÚÈÝ£¨ÆäÖÐFG_PIC_INPUTÎªÇ°¾°ÓÐÍ¼ÏñÊäÈë£»NO_PIC_INPUTÎªÇ°¾°ÎÞÍ¼ÏñÊäÈë£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nZoomStyle       = g_nScaleType;  //Ëõ·ÅÄ£Ê½£¨PIC_ZOOM_ONE£ºÈ«ÆÁËõ·Å£¬PIC_ZOOM_TWO£º±£³Ö±ÈÀýÀ­Éì²¢±£³ÖÍ¼ÏñÍêÕû£¬PIC_ZOOM_THREE£º±£³Ö±ÈÀýÀ­Éì²¢³äÂúÄ¿±êÇøÓò£¬PIC_ZOOM_FOUR£º±£³ÖÔ­ÓÐ³ß´ç,´óÓÚºÏ³É³ß´çµÄÍ¼Ïñ»á¾ÓÖÐ²¢×ö²Ã±ß´¦Àí, PIC_ZOOM_FIVE ±£³ÖÔ­ÓÐ³ß´ç,´óÓÚºÏ³É³ß´çµÄÍ¼Ïñ»á°´ÕÕ±£³Ö±ÈÀýÀ­Éì²¢±£³ÖÍ¼ÏñÍêÕûËõ·Å£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgPositionNum   = i;                        //»­ÃæÎ»ÖÃ±àºÅ£¨»­ÃæÎ»ÖÃ±àºÅÔ¼¶¨°´ÕÕ´ÓÉÏµ½ÏÂ´Ó×óµ½ÓÒË³ÐòÒÀ´Î±àºÅ£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgSrcWidth      = g_nSrcWidth;              //Ç°¾°µÄÊäÈëÔ´Í¼Ïñ¿í¶È
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgSrcHeight     = g_nSrcHeight;             //Ç°¾°µÄÊäÈëÔ´Í¼Ïñ¸ß¶È
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgFrameFieldFormat = 1;              //Ç°¾°Í¼ÏñµÄÀàÐÍ(Ö¡¸ñÊ½»òÕß³¡¸ñÊ½)£¨Ö¡¸ñÊ½ÎªFRAME_FORMAT£»³¡¸ñÊ½ÎªFIELD_FORMAT£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFgYUVType       = 1;                       //Ç°¾°Í¼Ïñ¸ñÊ½(YUV422»òÕßYUV420)
        m_ptMcMergeParam->ptMcMergeFgPic[i].nDrawFocusFlag   = (i % 3 == 0) ? 0 : 1;                            //»­ÃæÊÇ·ñ¹´»­±ß¿òµÄ±ê¼Ç£¨nDrawFocusFlagÎª1±íÃ÷»­±ß¿ò£¬nDrawFocusFlagÎª0±íÃ÷²»»­±ß¿ò£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFocusRGB        = 0x00FF0000;                   //»­Ãæ±ß¿òÉ«RGB·ÖÁ¿£¨¸ñÊ½Îª0x00RRGGBB£©
        m_ptMcMergeParam->ptMcMergeFgPic[i].nFocusWidth      = 2;                            //±ß¿òÍ³Ò»¿í¶È£¨Ö¡¸ñÊ½Í¼ÏñÐëÎª2µÄ±¶Êý£¬³¡¸ñÊ½Í¼ÏñÐèÎª4µÄ±¶Êý£©,Ä¿Ç°Ö»Ö§³ÖÓë±ß½çÏß±£³ÖÒ»ÖÂ
        m_ptMcMergeParam->ptMcMergeFgPic[i].nZoomScaleWidth  = 0;                            //Õë¶ÔËõ·ÅÄ£Ê½ÖÐÄ£Ê½¶þºÍÈýµÄËõ·Å¿í¸ß±ÈÀý£¬¸Ã²ÎÊýÓënZoomHeightScale³É¶ÔÊ¹ÓÃ(ÈçÇ°¾°°´ÕÕ4£º3±ÈÀýËõ·Å£¬Ôò¸ÃÖµÎª4£¬¸Ã²ÎÊýÎªÁãÔò°´ÕÕ±£³ÖÊäÈëÍ¼Ïñ±ÈÀýËõ·Å)
        m_ptMcMergeParam->ptMcMergeFgPic[i].nZoomScaleHeight = 0;                            //Õë¶ÔËõ·ÅÄ£Ê½ÖÐÄ£Ê½¶þºÍÈýµÄËõ·Å¿í¸ß±ÈÀý£¬¸Ã²ÎÊýÓënZoomWidthScale³É¶ÔÊ¹ÓÃ(ÈçÇ°¾°°´ÕÕ4£º3±ÈÀýËõ·Å£¬Ôò¸ÃÖµÎª3£¬¸Ã²ÎÊýÎªÁãÔò°´ÕÕ±£³ÖÊäÈëÍ¼Ïñ±ÈÀýËõ·Å)
    }

    s32 nResult = McMergeOpen(&m_pvImageHandle, m_ptMcMergeParam);

    BOOL32 bRet = TRUE;
    if(m_pvImageHandle == NULL || en_MCMERGE_SUCCESS != nResult) {
        printf("CMcMergeWrapper: VideoUnitEncoderOpen failure! %d\n", nResult);
        bRet = FALSE;
    }

    delete [] m_ptMcMergeParam->ptMcMergeFgPic;
    delete m_ptMcMergeParam;

    return bRet;
}


BOOL32 DestroyMcMerger()
{
    if (m_pvImageHandle) {
        McMergeClose(m_pvImageHandle);
        m_pvImageHandle = NULL;
    }

    if (m_ptMcMergeOutput)
    {
        delete m_ptMcMergeOutput;
    }

    if (m_ptMcMergeInput) {
        if (m_ptMcMergeInput->ptMcMergeInputPic) {
            delete []m_ptMcMergeInput->ptMcMergeInputPic;
        }
        delete m_ptMcMergeInput;
    }

    return true;
}

int McMergeProc(u8* pbyIn, u8* pbyOut)
{
    int i = 0;

    m_ptMcMergeInput->tMcMergeOutputPic.pu8Y = pbyOut;
    m_ptMcMergeInput->tMcMergeOutputPic.pu8U = NULL;
    m_ptMcMergeInput->tMcMergeOutputPic.pu8V = NULL;
    m_ptMcMergeInput->tMcMergeOutputPic.nYStride    = g_nDstWidth;
    m_ptMcMergeInput->tMcMergeOutputPic.nUVStride   = g_nDstWidth >> 1;

    for (i = 0 ; i < MCMERGE_CHANNEL_MAX; i++)
    {
        m_ptMcMergeInput->ptMcMergeInputPic[i].pu8Y = pbyIn;
        m_ptMcMergeInput->ptMcMergeInputPic[i].pu8U = NULL;
        m_ptMcMergeInput->ptMcMergeInputPic[i].pu8V = NULL;
        m_ptMcMergeInput->ptMcMergeInputPic[i].nYStride  = g_nSrcWidth;
        m_ptMcMergeInput->ptMcMergeInputPic[i].nUVStride = g_nSrcWidth >> 1;
    }

    s32 nRet = McMergeProcess(m_pvImageHandle, m_ptMcMergeInput, m_ptMcMergeOutput);
    if (en_MCMERGE_SUCCESS != nRet)
    {
        printf("ImageUnitProcess: proc failed. %d\n", nRet);
    }
    return 0;
}

void DoScaler()
{
    FILE *in, *out;

    unsigned char *pInOri, *pOutOri;
    unsigned char *pIn, *pOut;
    unsigned char *pInY, *pInU, *pInV;
    unsigned char *pOutY, *pOutU, *pOutV;

    int nSrcYStride, nSrcUStride, nSrcVStride;
    int nDstYStride, nDstUStride, nDstVStride;
    
    int nSrcSize, nDstSize;
    int nSrcWidth, nSrcHeight, nDstWidth, nDstHeight;

    do
    {
        /* open file */
        in  = fopen(g_chSrcFile, "rb");
        out = fopen(g_chDstFile, "wb");
        ASSERT(NULL != in);
        ASSERT(NULL != out);

        /* allocate buffer */
        nSrcSize = g_nSrcWidth * g_nSrcHeight * 3 / 2;
        nDstSize = g_nDstWidth * g_nDstHeight * 3 / 2;
        pInOri     = (unsigned char*)malloc(nSrcSize + ALIGN_BITS);
        pOutOri    = (unsigned char*)malloc(nDstSize + ALIGN_BITS);
        pIn = ALIGN_MEM(pInOri, ALIGN_BITS);
        pOut = ALIGN_MEM(pOutOri, ALIGN_BITS);
        printf("ori in %p align %p\n", pInOri, pIn);
        printf("ori out %p align %p\n", pOutOri, pOut);
        ASSERT(NULL != pInOri);
        ASSERT(NULL != pOutOri);

        /* read input yuv */
        fread(pIn, 1, nSrcSize, in);

        CreateMcMerger(g_nMergeStyle);
        CreateKedaMerger(g_nMergeStyle);
        unsigned int dwTpl = 33333;     // 30fps Tpl(time per loop)
        int count = 0;
        do {
            unsigned int dwUSec = 0;
            struct timeval tv_start;
            struct timeval tv_stop;
            gettimeofday(&tv_start, NULL);
        /* caculate variable */
        pInY        = pIn + g_nStartSrcY * g_nSrcWidth + g_nStartSrcX;
        pInU        = pIn + g_nSrcWidth * g_nSrcHeight + (g_nStartSrcY * g_nSrcWidth >> 2) + (g_nStartSrcX >> 1);
        pInV        = pIn + g_nSrcWidth * g_nSrcHeight * 5 / 4 + (g_nStartSrcY * g_nSrcWidth >> 2) + (g_nStartSrcX >> 1);

        pOutY       = pOut + g_nStartDstY * g_nDstWidth + g_nStartDstX;
#ifdef MERGE_KEDACOM
        KedaMergeProc(pInY, pOutY);
#elif defined(MERGE_MCMERGE)
        McMergeProc(pInY, pOutY);
#endif
            gettimeofday(&tv_stop, NULL);
            if (tv_stop.tv_sec > tv_start.tv_sec) {
                dwUSec = (tv_stop.tv_sec - tv_start.tv_sec)  * 1000 * 1000 - tv_start.tv_usec + tv_stop.tv_usec;
            } else {
                dwUSec = tv_stop.tv_usec - tv_start.tv_usec;
            }

            count++;
            if (!(count % 30))
            {
                printf("elapse %d\n", dwTpl - dwUSec);
            }
            if (dwUSec < dwTpl) // 30fps
                usleep(dwTpl - dwUSec);
            else
                printf("dwUSec %u\n", dwUSec);
        } while(0);

        /* write to output yuv file */
        fwrite(pOut, 1, nDstSize, out);

    } while(0);

    DestroyMcMerger();
    DestroyKedaMerger();

    /* close file */
    SAFE_FCLOSE(out);
    SAFE_FCLOSE(in);
    
    SAFE_DELS(pInOri);
    SAFE_DELS(pOutOri);
    return;
}

void I420RectTest()
{
    /* test I420Rect efficiency compare with scale */
    uint8_t* dst_y = NULL;
    uint8_t* dst_u = NULL;
    uint8_t* dst_v = NULL;
    int x = 0;
    int y = 0;
    int width   = 4;
    int height  = 4;
    int dst_stride_y    = width;
    int dst_stride_u    = dst_stride_y >> 1;
    int dst_stride_v    = dst_stride_y >> 1;
    int value_y         = 123;
    int value_u         = 56;
    int value_v         = 230;
    dst_y = (uint8_t*)malloc(width * height * 3 >> 1);
    dst_u = dst_y + width * height;
    dst_v = dst_u + (width * height >> 2);

    while (1) {
        libyuv::I420Rect(dst_y, dst_stride_y,
                 dst_u, dst_stride_u,
                 dst_v, dst_stride_v,
                 x, y,
                 width, height,
                 value_y, value_u, value_v);
        usleep(1 * 1000);
        /* enable or disable this break
         * to test I420Rect or I420Scale,
         * then you can compare the cpu ratio */
        break;
    }
    uint8_t* src_y  = NULL;
    uint8_t* src_u  = NULL;
    uint8_t* src_v  = NULL;
    int src_width   = 1920;
    int src_height  = 4;
    int src_stride_y= src_width;
    int src_stride_u= src_stride_y >> 1;
    int src_stride_v= src_stride_u;
    src_y = (uint8_t*)malloc(width * height * 3 >> 1);
    src_u = src_y + src_width * src_height;
    src_v = src_u + (src_width * src_height >> 2);
    while (1) {
        libyuv::I420Scale(dst_y, dst_stride_y,
                          dst_u, dst_stride_u,
                          dst_v, dst_stride_v,
                          width, height,
                          src_y, src_stride_y,
                          src_u, src_stride_u,
                          src_v, src_stride_v,
                          src_width, src_height,
                          libyuv::kFilterNone);
        usleep(1 * 1000);
    }
    return;
}

void TestOpenShut()
{
    /* test open and shut */
    while (1)
    {
        CreateMcMerger(g_nMergeStyle);
        sleep(1);
        DestroyMcMerger();
        sleep(1);
    }
    return;
}

s32 main(s32 argc, const s8* argv[])
{
    printf("hello welcome to merger test!\n");
    s32 count = 0;

    /* read YUV data */
    /* throw thread to do merger */
    /* main loop to get command */
	do
	{
        /* get option */
        GetOption(argc, argv);
        TestOpenShut();

    	/* check param */
        if (!CheckParam())
        {
            debug("check param fail\n");
            break;
        }

        /* do you test here */
        /* do scaler */
        DoScaler();
    } while (0);

    return 0;
}
