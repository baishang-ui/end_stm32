/*

 * 这个头文件是oled库的 [硬件层] 实现文件，移植的时候需要更改这个文件的内容

*/
#include "stm32f4xx_hal.h"
#include "oled_hal_display.h"
#include "oled_graphics.h"



uint8_t OLED_DisplayBuf[64 / 8][128]; // 显存
bool OLED_ColorMode = true;
bool spi_busy = 0;
static SPI_HandleTypeDef s_oled_hspi1;
static uint8_t s_oled_last_frame[64 / 8][128];
static bool s_oled_last_frame_valid = false;

static void OLED_InvalidateUpdateCache(void)
{
	s_oled_last_frame_valid = false;
}

static void OLED_SPI1_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_SPI1_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();

	GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	s_oled_hspi1.Instance = SPI1;
	s_oled_hspi1.Init.Mode = SPI_MODE_MASTER;
	s_oled_hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	s_oled_hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	s_oled_hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	s_oled_hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	s_oled_hspi1.Init.NSS = SPI_NSS_SOFT;
	s_oled_hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
	s_oled_hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	s_oled_hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
	s_oled_hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	s_oled_hspi1.Init.CRCPolynomial = 10;
	HAL_SPI_Init(&s_oled_hspi1);
}

static void OLED_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	HAL_GPIO_WritePin(OLED_CS_GPIO_Port, OLED_CS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(OLED_DC_GPIO_Port, OLED_DC_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(OLED_RST_GPIO_Port, OLED_RST_Pin, GPIO_PIN_SET);

	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

	GPIO_InitStruct.Pin = OLED_CS_Pin;
	HAL_GPIO_Init(OLED_CS_GPIO_Port, &GPIO_InitStruct);
	GPIO_InitStruct.Pin = OLED_DC_Pin;
	HAL_GPIO_Init(OLED_DC_GPIO_Port, &GPIO_InitStruct);
	GPIO_InitStruct.Pin = OLED_RST_Pin;
	HAL_GPIO_Init(OLED_RST_GPIO_Port, &GPIO_InitStruct);
}

#ifdef OLED_UI_USE_HW_SPI
void OLED_Write_DATA(uint8_t data)
{
	while (spi_busy != 0)
		; // 等待SPI空闲
	spi_busy = 1;

	OLED_DC_Set(); // 设置数据命令线为数据模式
#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Clr(); // 选中OLED
#endif

	HAL_SPI_Transmit(&s_oled_hspi1, &data, 1, 1);

#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Set(); // 取消选中OLED
#endif
	spi_busy = 0;
}

void OLED_Write_CMD(uint8_t data)
{
	while (spi_busy != 0)
		; // 等待SPI空闲
	spi_busy = 1;

	OLED_DC_Clr(); // 设置数据命令线为指令模式
#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Clr(); // 选中OLED
#endif

	HAL_SPI_Transmit(&s_oled_hspi1, &data, 1, 1);

#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Set(); // 取消选中OLED
#endif
	spi_busy = 0;
}

/**
 * 函    数：OLED写数据（硬件SPI）
 * 参    数：Data 要写入数据的起始地址
 * 参    数：Count 要写入数据的数量
 * 返 回 值：无
 */
void OLED_WriteDataArr(uint8_t *Data, uint8_t Count)
{
	while (spi_busy != 0)
		; // 等待SPI空闲
	spi_busy = 1;

	OLED_DC_Set(); // 设置数据命令线为数据模式
#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Clr(); // 选中OLED
#endif

	HAL_SPI_Transmit(&s_oled_hspi1, Data, (uint8_t)Count, 100);

#ifndef OLED_UI_SPI_NSS_HARD_OUTPUT
	OLED_CS_Set(); // 取消选中OLED
#endif
	spi_busy = 0;
}

#else
void OLED_Write_DATA(uint8_t data)
{
	OLED_DC_Set(); // 设置数据命令线为数据模式
	OLED_CS_Clr(); // 选中OLED

	for (uint8_t i = 0; i < 8; i++)
	{
		OLED_SCL_Clr();		// 时钟线置低，准备发送数据位
		if (data & 0x80)	// 检查最高位
			OLED_SDA_Set(); // 如果是1，设置数据线高
		else
			OLED_SDA_Clr(); // 如果是0，设置数据线低

		OLED_SCL_Set(); // 时钟线置高，数据线状态被读取
		data <<= 1;		// 左移数据准备下一位
	}

	OLED_CS_Set(); // 取消选中OLED
}

/**
 * 函    数：OLED写数据
 * 参    数：Data 要写入数据的起始地址
 * 参    数：Count 要写入数据的数量
 * 返 回 值：无
 */
void OLED_WriteDataArr(uint8_t *Data, uint8_t Count)
{

	// CS、DC控制已经在OLED_Write_DATA()中

	/*循环Count次，进行连续的数据写入*/
	for (uint8_t i = 0; i < Count; i++)
	{
		if (OLED_ColorMode)
		{
			OLED_Write_DATA(Data[i]); // 依次发送Data的每一个数据
		}
		else
		{
			OLED_Write_DATA(~Data[i]); // 如果是反色模式，则将每个数据取反再发送
		}
	}
}

void OLED_Write_CMD(uint8_t data)
{

	OLED_DC_Clr(); // 设置数据命令线为指令模式

	OLED_CS_Clr(); // 选中OLED

	for (uint8_t i = 0; i < 8; i++)
	{
		OLED_SCL_Clr();
		if (data & 0x80)
			OLED_SDA_Set();
		else
			OLED_SDA_Clr();
		OLED_SCL_Set();
		data <<= 1;
	}

	OLED_CS_Set(); // 取消选中OLED
}
#endif

// 反显函数
void OLED_ColorTurn(uint8_t i)
{
	if (i == 0)
	{
		OLED_Write_CMD(0xA6); // 正常显示
	}
	if (i == 1)
	{
		OLED_Write_CMD(0xA7); // 反色显示
	}
}

/**
 * 函    数：控制屏幕显示方向
 * 参    数：Page 0=正常显示，1=屏幕内容反转180度
 * 返 回 值：无
 * 说    明：无
 */
void OLED_DisplayTurn(uint8_t i)
{
	// 如果不关闭屏幕，执行时画面会闪烁两次，先上下镜像再左右镜像
	OLED_Write_CMD(0xAE); // 关闭屏幕
	if (i == 0)
	{
		OLED_Write_CMD(0xC8); // 正常显示
		OLED_Write_CMD(0xA1);
	}
	if (i == 1)
	{
		OLED_Write_CMD(0xC0); // 反转显示
		OLED_Write_CMD(0xA0);
	}
	OLED_Write_CMD(0xAF); // 点亮屏幕
	OLED_InvalidateUpdateCache();
}

// 开启OLED显示
void OLED_DisPlay_On(void)
{
	OLED_Write_CMD(0x8D); // 电荷泵使能
	OLED_Write_CMD(0x14); // 开启电荷泵
	OLED_Write_CMD(0xAF); // 点亮屏幕
}

// 关闭OLED显示
void OLED_DisPlay_Off(void)
{
	OLED_Write_CMD(0x8D); // 电荷泵使能
	OLED_Write_CMD(0x10); // 关闭电荷泵
	OLED_Write_CMD(0xAE); // 关闭屏幕
}
/**
 * 函    数：OLED设置显示光标位置
 * 参    数：Page 指定光标所在的页，范围：0~15
 * 参    数：X 指定光标所在的X轴坐标，范围：0~127
 * 返 回 值：无
 * 说    明：OLED默认的Y轴，只能8个Bit为一组写入，即1页等于8个Y轴坐标
 */
void OLED_SetCursor(uint8_t Page, uint8_t X)
{
	/*如果使用此程序驱动1.3寸的OLED显示屏，则需要解除此注释*/
	/*因为1.3寸的OLED驱动芯片（SH1106）有132列*/
	/*屏幕的起始列接在了第2列，而不是第0列*/
	/*所以需要将X加2，才能正常显示*/
#ifdef SH1106
	X += 2;
#endif

	/*通过指令设置页地址和列地址*/
	OLED_Write_CMD(0xB0 | Page);			  // 设置页位置
	OLED_Write_CMD(0x10 | ((X & 0xF0) >> 4)); // 设置X位置高4位
	OLED_Write_CMD(0x00 | (X & 0x0F));		  // 设置X位置低4位
}

// 更新显存到OLED
void OLED_Update(void)
{
	uint8_t j;

	/*遍历每一页*/
	for (j = 0; j < 8; j++)
	{
		uint8_t start = 0;
		uint8_t end = OLED_WIDTH - 1;

		if (s_oled_last_frame_valid && (memcmp(OLED_DisplayBuf[j], s_oled_last_frame[j], OLED_WIDTH) == 0))
		{
			continue;
		}

		if (s_oled_last_frame_valid)
		{
			while ((start < OLED_WIDTH) && (OLED_DisplayBuf[j][start] == s_oled_last_frame[j][start]))
			{
				start++;
			}

			if (start >= OLED_WIDTH)
			{
				continue;
			}

			while ((end > start) && (OLED_DisplayBuf[j][end] == s_oled_last_frame[j][end]))
			{
				end--;
			}
		}

		OLED_SetCursor(j, start);
		OLED_WriteDataArr(&OLED_DisplayBuf[j][start], (uint8_t)(end - start + 1U));
		memcpy(s_oled_last_frame[j], OLED_DisplayBuf[j], OLED_WIDTH);
	}

	s_oled_last_frame_valid = true;
}

/**
 * 函    数：将OLED显存数组部分更新到OLED屏幕
 * 参    数：X 指定区域左上角的横坐标，范围：0~127
 * 参    数：Y 指定区域左上角的纵坐标，范围：0~127
 * 参    数：Width 指定区域的宽度，范围：0~128
 * 参    数：Height 指定区域的高度，范围：0~127
 * 返 回 值：无
 * 说    明：此函数会至少更新参数指定的区域
 *           如果更新区域Y轴只包含部分页，则同一页的剩余部分会跟随一起更新
 * 说    明：所有的显示函数，都只是对OLED显存数组进行读写
 *           随后调用OLED_Update函数或OLED_UpdateArea函数
 *           才会将显存数组的数据发送到OLED硬件，进行显示
 *           故调用显示函数后，要想真正地呈现在屏幕上，还需调用更新函数
 */
void OLED_UpdateArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
	uint8_t j;

	/*参数检查，保证指定区域不会超出屏幕范围*/
	if ((Width == 0U) || (Height == 0U))
	{
		return;
	}
	if (X > OLED_WIDTH - 1)
	{
		return;
	}
	if (Y > OLED_HEIGHT - 1)
	{
		return;
	}
	if (X + Width > OLED_WIDTH)
	{
		Width = OLED_WIDTH - X;
	}
	if (Y + Height > OLED_HEIGHT)
	{
		Height = OLED_HEIGHT - Y;
	}

	/*遍历指定区域涉及的相关页*/
	/*(Y + Height - 1) / 8 + 1的目的是(Y + Height) / 8并向上取整*/
	for (j = Y / 8; j < (Y + Height - 1) / 8 + 1; j++)
	{
		/*设置光标位置为相关页的指定列*/
		OLED_SetCursor(j, X);
		/*连续写入Width个数据，将显存数组的数据写入到OLED硬件*/
		OLED_WriteDataArr(&OLED_DisplayBuf[j][X], Width);
		if (s_oled_last_frame_valid)
		{
			memcpy(&s_oled_last_frame[j][X], &OLED_DisplayBuf[j][X], Width);
		}
	}
}

extern void OLED_Clear(void);

// OLED的初始化
void OLED_Init(void)
{
	OLED_GPIO_Init();
	OLED_SPI1_Init();

	OLED_RES_Clr();
	for (int i = 0; i < 1000; i++)
	{
		for (int j = 0; j < 1000; j++)
		{
		}
	}
	OLED_RES_Set();

#ifdef SH1106
	OLED_Write_CMD(0xAE); // display off
	OLED_Write_CMD(0x02); // set lower column address
	OLED_Write_CMD(0x10); // set higher column address
	OLED_Write_CMD(0x40); // set display start line
	OLED_Write_CMD(0xB0); // set page address
	//	OLED_Write_CMD(0x81);//设置对比度（亮度）
	//	OLED_Write_CMD(0x00);//0x00-0xFF							//影响亮度：越高越亮
	OLED_Write_CMD(0xA1); // set segment remap
	OLED_Write_CMD(0xA6); // normal / reverse
	OLED_Write_CMD(0xA8); // multiplex ratio
	OLED_Write_CMD(0x3F); // duty = 1/64
	OLED_Write_CMD(0xad); // set charge pump enable
	OLED_Write_CMD(0x8b); // 0x8B 内供 VCC
	OLED_Write_CMD(0x32); // 0X30---0X33 set VPP 电荷泵电压 		//影响亮度：越高越亮
	OLED_Write_CMD(0xC8); // Com scan direction
	OLED_Write_CMD(0xD3); // set display offset
	OLED_Write_CMD(0x00); // 0x20
	OLED_Write_CMD(0xD5); // set osc division
	OLED_Write_CMD(0x80);
	OLED_Write_CMD(0xD9); // set pre-charge period
	OLED_Write_CMD(0x22); // 默认0x22
	OLED_Write_CMD(0xDA); // set COM pins
	OLED_Write_CMD(0x12);
	OLED_Write_CMD(0xdb); // set vcomh
	OLED_Write_CMD(0x35); // 0x00 - 0x40，默认0x35				//影响亮度：越高越亮
#endif

#ifdef SSD1306
	OLED_Write_CMD(0xAE);
	OLED_Write_CMD(0x00);
	OLED_Write_CMD(0x10);
	OLED_Write_CMD(0x40);
	//	OLED_Write_CMD(0x81);//设置对比度（亮度）
	//	OLED_Write_CMD(0xCF);
	OLED_Write_CMD(0xA1);
	OLED_Write_CMD(0xC8);
	OLED_Write_CMD(0xA6);
	OLED_Write_CMD(0xA8);
	OLED_Write_CMD(0x3f);
	OLED_Write_CMD(0xD3);
	OLED_Write_CMD(0x00);
	OLED_Write_CMD(0xd5);
	OLED_Write_CMD(0x80);
	OLED_Write_CMD(0xD9);
	OLED_Write_CMD(0xF1);
	OLED_Write_CMD(0xDA);
	OLED_Write_CMD(0x12);
	OLED_Write_CMD(0xDB);
	OLED_Write_CMD(0x40);
	OLED_Write_CMD(0x20);
	OLED_Write_CMD(0x02);
	OLED_Write_CMD(0x8D);
	OLED_Write_CMD(0x14);
	OLED_Write_CMD(0xA4);
	OLED_Write_CMD(0xA6);
	OLED_Write_CMD(0xAF);
#endif

	OLED_Brightness(-1); // 初始化亮度设置函数。设置为-1相当于设置为0
	OLED_Clear();
	OLED_InvalidateUpdateCache();
	OLED_Write_CMD(0xAF); // Display ON
	for (int i = 0; i < 1000; i++)
	{
		for (int j = 0; j < 1000; j++)
		{
		}
	}
}

/**
 * 函    数：OLED设置亮度
 * 参    数：Brightness ，0-255，不同显示芯片效果可能不相同。
 * 返 回 值：无
 * 说    明：不要设置过大或者过小。
 */
void OLED_Brightness(int16_t Brightness)
{

	// 检测亮度设置是否变化，有变化时再发送指令
	static int16_t Last_Brightness;
	if (Brightness == Last_Brightness)
	{
		return;
	}
	else
	{
		Last_Brightness = Brightness;
	}

	if (Brightness > 255)
	{
		Brightness = 255;
	}
	if (Brightness < 0)
	{
		Brightness = 0;
	}
	OLED_Write_CMD(0x81);
	OLED_Write_CMD(Brightness);
}

/**
 * @brief 设置OLED显示模式
 * @param colormode true: 黑色模式，false: 白色模式
 * @note OLED_ColorTurn 0: 正常模式，1: 反转模式
 * @return 无
 */
void OLED_SetColorMode(bool colormode)
{
	OLED_ColorMode = colormode;

	// 检测显示模式设置是否变化，有变化时再发送指令
	static bool Last_OLED_ColorMode;
	if (OLED_ColorMode == Last_OLED_ColorMode)
	{
		return;
	}
	else
	{
		Last_OLED_ColorMode = OLED_ColorMode;
	}

	if (OLED_ColorMode)
		OLED_ColorTurn(0);
	else
		OLED_ColorTurn(1);
}



