/*
 * All Rights Reserved By Kedacom Ltd.(2018)
 * Author: Summer Shang
 * Date: Jun. 20 2018
 *
 * This module is used to do multipic.
 */
#ifndef _MC_MERGE_H_
#define _MC_MERGE_H_

#include "kdvtype.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCMERGE_CHANNEL_MAX         25

#define MCMERGE_BG_WIDTH_MAX        3840
#define MCMERGE_BG_HEIGHT_MAX       2160

#define MCMERGE_BG_WIDTH_MIN        352
#define MCMERGE_BG_HEIGHT_MIN       288

typedef enum {
    en_MCMERGE_SUCCESS = 0,
    en_MCMERGE_FAIL,
    en_MCMERGE_INVALID_PARAM,
    en_MCMERGE_MEM_ALLOC,
} TMcMergeErrorCode;

typedef enum {
    en_MCMERGE_YUV420,
    en_MCMERGE_YUV422,
} TMcMergeYUVType;

typedef enum
{
    en_MCMERGE_STYLE_MIN,
    en_MCMERGE_STYLE_M1,
    en_MCMERGE_STYLE_M2,
    en_MCMERGE_STYLE_M2_1_BR1,
    en_MCMERGE_STYLE_M2_1_BL1,
    en_MCMERGE_STYLE_M2_1_TR1,
    en_MCMERGE_STYLE_M2_1_TL1,
    en_MCMERGE_STYLE_M3_T1,
    en_MCMERGE_STYLE_M3_B1,
    en_MCMERGE_STYLE_M3_1_B2,
    en_MCMERGE_STYLE_M3_1_T2,
    en_MCMERGE_STYLE_M3_1_R2,
    en_MCMERGE_STYLE_M3_L1,
    en_MCMERGE_STYLE_M4,
    en_MCMERGE_STYLE_M4_1_R3,
    en_MCMERGE_STYLE_M4_1_D3,
    en_MCMERGE_STYLE_M5_1_R4,
    en_MCMERGE_STYLE_M5_1_D4,
    en_MCMERGE_STYLE_M5_2_D3,
    en_MCMERGE_STYLE_M6,
    en_MCMERGE_STYLE_M6_1_5,
    en_MCMERGE_STYLE_M6_2_B4,
    en_MCMERGE_STYLE_M6_1_B5,
    en_MCMERGE_STYLE_M6_B5,
    en_MCMERGE_STYLE_M7_3_TL4,
    en_MCMERGE_STYLE_M7_3_TR4,
    en_MCMERGE_STYLE_M7_3_BL4,
    en_MCMERGE_STYLE_M7_3_BR4,
    en_MCMERGE_STYLE_M7_3_BLR4,
    en_MCMERGE_STYLE_M7_3_TLR4,
    en_MCMERGE_STYLE_M7_1_D6,
    en_MCMERGE_STYLE_M8_1_7,
    en_MCMERGE_STYLE_M8_4_4,
    en_MCMERGE_STYLE_M9,
    en_MCMERGE_STYLE_M9_T4_1_D4,
    en_MCMERGE_STYLE_M10_2_R8,
    en_MCMERGE_STYLE_M10_2_B8,
    en_MCMERGE_STYLE_M10_2_T8,
    en_MCMERGE_STYLE_M10_2_L8,
    en_MCMERGE_STYLE_M10_2_TB8,
    en_MCMERGE_STYLE_M10_1_9,
    en_MCMERGE_STYLE_M10_L4_2_R4,
    en_MCMERGE_STYLE_M11_T5_1_D5,
    en_MCMERGE_STYLE_M11_1_D10,
    en_MCMERGE_STYLE_M12_1_11,
    en_MCMERGE_STYLE_M12_3_RD9,
    en_MCMERGE_STYLE_M13_TL1_12,
    en_MCMERGE_STYLE_M13_TR1_12,
    en_MCMERGE_STYLE_M13_BL1_12,
    en_MCMERGE_STYLE_M13_BR1_12,
    en_MCMERGE_STYLE_M13_1_ROUND12,
    en_MCMERGE_STYLE_M13_TL4_9,
    en_MCMERGE_STYLE_M13_L6_1_R6,
    en_MCMERGE_STYLE_M14_1_13,
    en_MCMERGE_STYLE_M14_TL2_12,
    en_MCMERGE_STYLE_M14_T5_1_2_1_D5,
    en_MCMERGE_STYLE_M15_T3_12,
    en_MCMERGE_STYLE_M15_T4_L3_1_R3_D4,
    en_MCMERGE_STYLE_M16,
    en_MCMERGE_STYLE_M16_1_15,
    en_MCMERGE_STYLE_M17_1,
    en_MCMERGE_STYLE_M17_2,
    en_MCMERGE_STYLE_M17_3,
    en_MCMERGE_STYLE_M18_1,
    en_MCMERGE_STYLE_M18_2,
    en_MCMERGE_STYLE_M18_3,
    en_MCMERGE_STYLE_M19_1,
    en_MCMERGE_STYLE_M19_2,
    en_MCMERGE_STYLE_M20,
    en_MCMERGE_STYLE_M21_1,
    en_MCMERGE_STYLE_M21_2,
    en_MCMERGE_STYLE_M22,
    en_MCMERGE_STYLE_M23_1,
    en_MCMERGE_STYLE_M23_2,
    en_MCMERGE_STYLE_M24,
    en_MCMERGE_STYLE_M25_1,
    en_MCMERGE_STYLE_M25_2,
    en_MCMERGE_STYLE_MAX,
} TMcMergeStyle;

typedef enum
{
    en_MCMERGE_ZOOM_MIN,
    en_MCMERGE_ZOOM_SCALE,                          //全屏缩放
    en_MCMERGE_ZOOM_FILL,                           //保持比例拉伸并保持图像完整
    en_MCMERGE_ZOOM_CUT,                            //保持比例拉伸并充满目标区域
    en_MCMERGE_ZOOM_STILL_CUT,                      //保持原有尺寸,大于合成尺寸的图像会居中并做裁边处理
    en_MCMERGE_ZOOM_STILL_FILL,                     //保持原有尺寸,大于合成尺寸的图像会按照保持比例拉伸并保持图像完整缩放
    en_MCMERGE_ZOOM_MAX,
} TMcMergeZoomType;

//缩放画面合成模块前景图像参数结构
typedef struct
{
    s32 nFgInputSource;                             //前景是否有数据
    TMcMergeZoomType nZoomStyle;                    //缩放模式
    s32 nZoomScaleWidth;                            //针对缩放模式中模式二和三的缩放宽高比例，该参数与nZoomHeightScale成对使用(如前景按照4：3比例缩放，则该值为4，该参数为零则按照保持输入图像比例缩放)
    s32 nZoomScaleHeight;                           //针对缩放模式中模式二和三的缩放宽高比例，该参数与nZoomWidthScale成对使用(如前景按照4：3比例缩放，则该值为3，该参数为零则按照保持输入图像比例缩放)
    s32 nFgPositionNum;                             //画面位置编号（画面位置编号约定按照从上到下从左到右顺序依次编号）
    s32 nFgSrcWidth;                                //前景的输入源图像宽度
    s32 nFgSrcHeight;                               //前景的输入源图像高度
    s32 nFgFrameFieldFormat;                        //前景图像的类型(帧格式或者场格式)（帧格式为FRAME_FORMAT；场格式为FIELD_FORMAT）
    s32 nFgYUVType;                                 //前景图像格式(YUV422或者YUV420)
    s32 nDrawFocusFlag;                             //画面是否勾画边框的标记（nDrawFocusFlag为1表明画边框，nDrawFocusFlag为0表明不画边框）
    s32 nFocusRGB;                                  //画面边框色RGB分量（格式为0x00RRGGBB）
    s32 nFocusWidth;                                //边框统一宽度（帧格式图像须为2的倍数，场格式图像需为4的倍数）,目前只支持与边界线保持一致
    u32 u32Reserved;                                //保留参数
} TMcMergeFgParam, *PTMcMergeFgParam;

//缩放画面合成模块背景图像参数结构
typedef struct
{
    s32 nBgWidth;                                   //背景图像的宽度（最大MCMERGE_BG_WIDTH_MAX）
    s32 nBgHeight;                                  //背景图像的高度（最大MCMERGE_BG_HEIGHT_MAX）
    s32 nBgFrameFieldFormat;                        //背景图像的类型（只支持帧格式）
    s32 nBgYUVType;                                 //背景图像格式（TMcMergeYUVType）
    s32 nDrawBoundaryFlag;                          //画面是否勾画边界线的标记（1表明画边界线，0表明不画边界线）
    s32 nBoundaryRGB;                               //画面边界线颜色RGB分量（格式为0x00RRGGBB）
    s32 nBoundaryWidth;                             //边界线统一宽度（为2的倍数）
    s32 nDrawBackgroundFlag;                        //背景无图像处是否填充背景色（只能填充）
    s32 nBackgroundRGB;                             //画面合成背景填充色RGB分量（格式为0x00RRGGBB）
    u32 u32Reserved;                                //保留参数
} TMcMergeBgParam, *PTMcMergeBgParam;

//缩放画面合成模块参数结构
typedef struct
{
    s32 nMaxFgNum;                                  //画面合成的最大前景数量（默认为最大64画面）
    TMcMergeStyle   nMergeStyle;                    //画面合成模式（共计44种合成类型，详见PicMcMergeStyle枚举类型）
    TMcMergeBgParam tMcMergeBgPic;                  //背景图像参数信息
    TMcMergeFgParam *ptMcMergeFgPic;                //前景图像参数信息（由于前景图像数量不确定，因此此处采用结构体指针形式，实际分配大小由画面合成的最大前景数量决定）
    u32 u32Reserved;                                //保留参数
} TMcMergeParam, *PTMcMergeParam;

//缩放画面合成模块图像信息结构体
typedef struct
{
    u8 *pu8Y;                                       //图像Y分量
    u8 *pu8U;                                       //图像U分量(如果YUV地址连续存放可将U地址设置为NULL)
    u8 *pu8V;                                       //图像V分量(如果YUV地址连续存放可将U地址设置为NULL)
    s32 nYStride;                                   //图像Y分量步长(如果YUV地址连续存放可将nYStride设置为0)
    s32 nUVStride;                                  //图像UV分量步长(如果YUV地址连续存放可将nYStride设置为0)
    u32 u32Reserved;                                //保留参数
} TMcMergePicInfo, *PTMcMergePicInfo;

//画面合成模块输入结构体
typedef struct
{
    TMcMergePicInfo *ptMcMergeInputPic;             //画面合成的N路输入图像的信息（由于前景图像数量不确定，因此此处采用结构体指针形式，实际分配大小由画面合成的最大前景数量决定）
    TMcMergePicInfo tMcMergeOutputPic;              //画面合成的输出图像的信息
    u32 u32Reserved;                                //保留参数
} TMcMergeInput, *PTMcMergeInput;

//画面合成模块输出结构体
typedef struct
{
    u32 u32Reserved;   //保留参数
} TMcMergeOutput, *PTMcMergeOutput;

/* Description:
 *   the function is used to get merger handler which will be used later.
 *
 * Param:
 *   ppvHandler         [OUT]   handler used to keep inner handler
 *   ptMcMergeParam     [IN]    param for mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 * */
s32 McMergeOpen(void** const ppvHandler, const TMcMergeParam* ptMcMergeParam);

/* Description:
 *   the function is used to close mcmerger and release the resource.
 *
 * Param:
 *   pvHandler          [IN]    handler used to close mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 * */
s32 McMergeClose(void* pvHandler);

/* Description:
 *   the function is used set mcmerger param.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   ptMcMergeParam     [IN]    param for mcmerge
 *
 * Return:
 *   TMcMergeErrorCode
 * */
s32 McMergeSetParam(void* pvHandler, TMcMergeParam* ptMcMergeParam);

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
s32 McMergeProcess(void* pvHandler, TMcMergeInput* ptMcMergeIn, TMcMergeOutput* ptMcMergeOut);

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
 * */
s32 McMergeSetBGSize(void* pvHandler, s32 nBgWidth, s32 nBgHeight);

/* Description:
 *   the function is used set mcmerger style.
 *
 * Param:
 *   pvHandler          [IN]    handler which get from McMergeOpen
 *   enStyle            [IN]    merge style, ref to TMcMergeStyle
 *
 * Return:
 *   TMcMergeErrorCode
 * */
s32 McMergeSetStyle(void* pvHandler, TMcMergeStyle enStyle);

/* Description:
 *   the function is used to print no of the style.
 *
 * Param:
 *   none
 *
 * Return:
 *   void
 * */
void McMergeStyleInfo(void);

#ifdef __cplusplus
}
#endif

#endif  // _MC_MERGE_H_
