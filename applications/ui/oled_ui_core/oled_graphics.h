#ifndef __OLED_H
#define __OLED_H

// 检测是否是C++编译器
#ifdef __cplusplus
extern "C" {
#endif

#include "oled_hal_display.h"			//oled底层驱动头文件
#include "oled_graphics_fonts.h"				//oled字体库头文件
#include "stdbool.h"


//使用宏定义的方式确定oled的横向像素与竖向像素
#define OLED_WIDTH				    (128)					
#define OLED_HEIGHT 			    (64)
/*半角字符参数取值*/
/*此参数值不仅用于判断，而且用于计算横向字符偏移，默认值为字体像素宽度*/
#define OLED_10X20_HALF			    (10)
#define OLED_8X16_HALF			    (8)
#define OLED_7X12_HALF			    (7)				
#define OLED_6X8_HALF 			    (6)

/*全角大小参数取值*/
#define OLED_8X8_FULL			    (8)
#define OLED_12X12_FULL			    (12)
#define OLED_16X16_FULL			    (16)
#define OLED_20X20_FULL			    (20)

/*IsFilled参数数值*/
#define OLED_UNFILLED			    (0)
#define OLED_FILLED				    (1)

/**关于字符串最大长度的宏，用于格式化输出字符串*/
#define  MAX_STRING_LENGTH          128


/**
 * @brief 清空整块 OLED 显存缓冲区。
 * @param 无。
 * @return 无。
 */
void OLED_Clear(void);

/**
 * @brief 清空显存中的指定矩形区域。
 * @param X 区域左上角 X 坐标。
 * @param Y 区域左上角 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 * @return 无。
 */
void OLED_ClearArea(int16_t X, int16_t Y, int16_t Width, int16_t Height);

/**
 * @brief 对整块 OLED 显存执行反色处理。
 * @param 无。
 * @return 无。
 */
void OLED_Reverse(void);

/**
 * @brief 对显存中的指定矩形区域执行反色处理。
 * @param X 区域左上角 X 坐标。
 * @param Y 区域左上角 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 * @return 无。
 */
void OLED_ReverseArea(int16_t X, int16_t Y, int16_t Width, int16_t Height);

/**
 * @brief 在显存中绘制一张单色位图。
 * @param X 图片左上角 X 坐标。
 * @param Y 图片左上角 Y 坐标。
 * @param Width 图片宽度。
 * @param Height 图片高度。
 * @param Image 位图数据指针。
 * @return 无。
 */
void OLED_ShowImage(int16_t X, int16_t Y, uint16_t Width, uint16_t Height, const uint8_t *Image);

/**
 * @brief 绘制一个 ASCII 字符。
 * @param X 字符左上角 X 坐标。
 * @param Y 字符左上角 Y 坐标。
 * @param Char 需要显示的 ASCII 字符。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);

/**
 * @brief 绘制无符号十进制数字。
 * @param X 数字左上角 X 坐标。
 * @param Y 数字左上角 Y 坐标。
 * @param Number 需要显示的数字。
 * @param Length 固定显示位数。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 绘制有符号十进制数字。
 * @param X 数字左上角 X 坐标。
 * @param Y 数字左上角 Y 坐标。
 * @param Number 需要显示的数字。
 * @param Length 固定显示位数。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 绘制十六进制数字。
 * @param X 数字左上角 X 坐标。
 * @param Y 数字左上角 Y 坐标。
 * @param Number 需要显示的数字。
 * @param Length 固定显示位数。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 绘制二进制数字。
 * @param X 数字左上角 X 坐标。
 * @param Y 数字左上角 Y 坐标。
 * @param Number 需要显示的数字。
 * @param Length 固定显示位数。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 绘制浮点数。
 * @param X 数字左上角 X 坐标。
 * @param Y 数字左上角 Y 坐标。
 * @param Number 需要显示的浮点数。
 * @param IntLength 整数部分固定显示位数。
 * @param FraLength 小数部分固定显示位数。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowFloatNum(int16_t X, int16_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);

/**
 * @brief 绘制 ASCII 字符串。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param String 以空字符结尾的 ASCII 字符串。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowString(int16_t X, int16_t Y, const char *String, uint8_t FontSize);

/**
 * @brief 绘制中英文混合字符串。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param String UTF-8 字符串。
 * @param ChineseFontSize 中文字号。
 * @param ASCIIFontSize ASCII 字号。
 * @return 无。
 */
void OLED_ShowMixString(int16_t X, int16_t Y, const char *String, uint8_t ChineseFontSize, uint8_t ASCIIFontSize);

/**
 * @brief 绘制中文字符串。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param Chinese UTF-8 中文字符串。
 * @param FontSize 中文字号。
 * @return 无。
 */
void OLED_ShowChinese(int16_t X, int16_t Y, const char *Chinese, uint8_t FontSize);

/**
 * @brief 按 printf 格式绘制 ASCII 字符串。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param FontSize 字号。
 * @param format printf 风格格式化字符串。
 * @return 无。
 */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, const char *format, ...);

/**
 * @brief 按 printf 格式绘制中英文混合字符串。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param ChineseFontSize 中文字号。
 * @param ASCIIFontSize ASCII 字号。
 * @param format printf 风格格式化字符串。
 * @return 无。
 */
void OLED_PrintfMix(int16_t X, int16_t Y, uint8_t ChineseFontSize,uint8_t ASCIIFontSize,const char *format, ...);

/**
 * @brief 在指定裁剪区域内绘制位图。
 * @param X_Pic 图片原始左上角 X 坐标。
 * @param Y_Pic 图片原始左上角 Y 坐标。
 * @param PictureWidth 图片宽度。
 * @param PictureHeight 图片高度。
 * @param X_Area 裁剪区域左上角 X 坐标。
 * @param Y_Area 裁剪区域左上角 Y 坐标。
 * @param AreaWidth 裁剪区域宽度。
 * @param AreaHeight 裁剪区域高度。
 * @param Image 位图数据指针。
 * @return 无。
 */
void OLED_ShowImageArea(int16_t X_Pic, int16_t Y_Pic, int16_t PictureWidth, int16_t PictureHeight, int16_t X_Area, int16_t Y_Area, int16_t AreaWidth, int16_t AreaHeight, const uint8_t *Image);

/**
 * @brief 在指定裁剪区域内绘制 ASCII 字符。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符左上角 X 坐标。
 * @param Y 字符左上角 Y 坐标。
 * @param Char 需要显示的 ASCII 字符。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowCharArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight, int16_t X, int16_t Y, char Char, uint8_t FontSize);

/**
 * @brief 在指定裁剪区域内绘制 ASCII 字符串。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param String 以空字符结尾的 ASCII 字符串。
 * @param FontSize 字号。
 * @return 无。
 */
void OLED_ShowStringArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight, int16_t X, int16_t Y, const char *String, uint8_t FontSize);

/**
 * @brief 在指定裁剪区域内绘制中文字符串。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param Chinese UTF-8 中文字符串。
 * @param FontSize 中文字号。
 * @return 无。
 */
void OLED_ShowChineseArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight, int16_t X, int16_t Y, const char *Chinese, uint8_t FontSize);

/**
 * @brief 在指定裁剪区域内按 printf 格式绘制 ASCII 字符串。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param FontSize 字号。
 * @param format printf 风格格式化字符串。
 * @return 无。
 */
void OLED_PrintfArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight, int16_t X, int16_t Y,uint8_t FontSize, const char *format, ...);

/**
 * @brief 在指定裁剪区域内绘制中英文混合字符串。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param String UTF-8 字符串。
 * @param ChineseFontSize 中文字号。
 * @param ASCIIFontSize ASCII 字号。
 * @return 无。
 */
void OLED_ShowMixStringArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight,int16_t X, int16_t Y, const char *String, uint8_t ChineseFontSize,uint8_t ASCIIFontSize);

/**
 * @brief 在指定裁剪区域内按 printf 格式绘制中英文混合字符串。
 * @param RangeX 裁剪区域左上角 X 坐标。
 * @param RangeY 裁剪区域左上角 Y 坐标。
 * @param RangeWidth 裁剪区域宽度。
 * @param RangeHeight 裁剪区域高度。
 * @param X 字符串左上角 X 坐标。
 * @param Y 字符串左上角 Y 坐标。
 * @param ChineseFontSize 中文字号。
 * @param ASCIIFontSize ASCII 字号。
 * @param format printf 风格格式化字符串。
 * @return 无。
 */
void OLED_PrintfMixArea(int16_t RangeX,int16_t RangeY,int16_t RangeWidth,int16_t RangeHeight,int16_t X, int16_t Y, uint8_t ChineseFontSize,uint8_t ASCIIFontSize, const char *format, ...);

/**
 * @brief 绘制一个像素点。
 * @param X 点 X 坐标。
 * @param Y 点 Y 坐标。
 * @return 无。
 */
void OLED_DrawPoint(int16_t X, int16_t Y);

/**
 * @brief 获取指定像素点当前颜色。
 * @param X 点 X 坐标。
 * @param Y 点 Y 坐标。
 * @return 像素点颜色值。
 */
uint8_t OLED_GetPoint(uint8_t X, uint8_t Y);

/**
 * @brief 绘制直线。
 * @param X0 起点 X 坐标。
 * @param Y0 起点 Y 坐标。
 * @param X1 终点 X 坐标。
 * @param Y1 终点 Y 坐标。
 * @return 无。
 */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);

/**
 * @brief 绘制矩形。
 * @param X 左上角 X 坐标。
 * @param Y 左上角 Y 坐标。
 * @param Width 矩形宽度。
 * @param Height 矩形高度。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, uint8_t IsFilled);

/**
 * @brief 绘制三角形。
 * @param X0 第 1 个顶点 X 坐标。
 * @param Y0 第 1 个顶点 Y 坐标。
 * @param X1 第 2 个顶点 X 坐标。
 * @param Y1 第 2 个顶点 Y 坐标。
 * @param X2 第 3 个顶点 X 坐标。
 * @param Y2 第 3 个顶点 Y 坐标。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);

/**
 * @brief 绘制圆形。
 * @param X 圆心 X 坐标。
 * @param Y 圆心 Y 坐标。
 * @param Radius 半径。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawCircle(int16_t X, int16_t Y, int16_t Radius, uint8_t IsFilled);

/**
 * @brief 绘制椭圆。
 * @param X 圆心 X 坐标。
 * @param Y 圆心 Y 坐标。
 * @param A 横向半轴长度。
 * @param B 纵向半轴长度。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawEllipse(int16_t X, int16_t Y, int16_t A, int16_t B, uint8_t IsFilled);

/**
 * @brief 绘制圆弧。
 * @param X 圆心 X 坐标。
 * @param Y 圆心 Y 坐标。
 * @param Radius 半径。
 * @param StartAngle 起始角度。
 * @param EndAngle 结束角度。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawArc(int16_t X, int16_t Y, int16_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);

/**
 * @brief 绘制圆角矩形。
 * @param X 左上角 X 坐标。
 * @param Y 左上角 Y 坐标。
 * @param Width 矩形宽度。
 * @param Height 矩形高度。
 * @param Radius 圆角半径。
 * @param IsFilled 是否填充，使用 OLED_FILLED 或 OLED_UNFILLED。
 * @return 无。
 */
void OLED_DrawRoundedRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, int16_t Radius, uint8_t IsFilled);

/**
 * @brief 绘制一个简单 3D 立方体线框。
 * @param centerX 中心点 X 坐标。
 * @param centerY 中心点 Y 坐标。
 * @param size 立方体大小。
 * @param perspective 透视强度。
 * @return 无。
 */
void DrawCube3D(int16_t centerX, int16_t centerY, float size, uint8_t perspective);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif



