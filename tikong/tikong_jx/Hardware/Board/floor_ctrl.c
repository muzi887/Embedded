#include "floor_ctrl.h"
#include "ext_io.h"
#include "bsp_hc595.h"
#include "timer.h"
#include <stdio.h>
#include <string.h>

/* 梯控授权 5 字节位图状态（本模块私有）（40 / 8 = 5） */
static u8 s_elevator_tail5[5];
static u8 s_elevator_tail5_mixed[5];
static u8 s_elevator_tail5_limit[5];

void FloorCtrl_SetTail5(const uint8_t tail5[5])
{
  if (!tail5) return;
  memcpy(s_elevator_tail5, tail5, 5);
}

void FloorCtrl_GetTail5(uint8_t out[5])
{
  if (!out) return;
  memcpy(out, s_elevator_tail5, 5);
}

void FloorCtrl_GetTail5Mixed(uint8_t out[5])
{
  if (!out) return;
  memcpy(out, s_elevator_tail5_mixed, 5);
}

void FloorCtrl_SetTail5Mixed(const uint8_t mixed5[5])
{
  if (!mixed5) return;
  memcpy(s_elevator_tail5_mixed, mixed5, 5);
}

void FloorCtrl_SetLimit(const uint8_t limit[5])
{
  if (!limit) return;
  memcpy(s_elevator_tail5_limit, limit, 5);
}

void FloorCtrl_GetLimit(uint8_t out[5])
{
  if (!out) return;
  memcpy(out, s_elevator_tail5_limit, 5);
}

// 直达某层
int Floor_DirectGo(uint8_t floor)
{
	/* 调试用：避免形参在开优化时因寄存器复用而“看起来变化” */
	volatile uint8_t floor_dbg = floor;

	//printf("elevator num :%d\r\n", floor_dbg);
	if (floor_dbg > 0 && floor_dbg < 41)
	{
		//OE_ENABLE();

		ExtIO_Set(floor_dbg, EXT_IO_LOW);

		//delay_ms(200);

	//	ExtIO_Set(floor_dbg, EXT_IO_HIGH);

		//OE_DISABLE();
 		/* 重新启动 TIM5 倒计时：清 UIF/NVIC 悬挂并复位 CNT，避免 ENABLE 后立即误进中断 */
		TIM5_ClearPendingAndEnable();
		//ctrl_time =0;
		return 1;
	}
	else
	{
		return -1;
	}

}
int Floor_CallOne(uint8_t floor)
{
	/* 调试用：避免形参在开优化时因寄存器复用而“看起来变化” */
	volatile uint8_t floor_dbg = floor;

	//printf("elevator num :%d\r\n", floor_dbg);
	if (floor_dbg > 0 && floor_dbg < 41)
	{
		OE_ENABLE();

		ExtIO_Set(floor_dbg, EXT_IO_LOW);

		delay_ms(200);

		ExtIO_Set(floor_dbg, EXT_IO_HIGH);

		OE_DISABLE();
 		/* 重新启动 TIM5 倒计时：清 UIF/NVIC 悬挂并复位 CNT，避免 ENABLE 后立即误进中断 */
		//TIM5_ClearPendingAndEnable();
		//ctrl_time =0;
		return 1;
	}
	else
	{
		return -1;
	}

}

// 授权某层
int Floor_AuthCheck(uint8_t floor)
{
	if (floor > 0 && floor < 41)
	{
		ExtIO_Set(floor, EXT_IO_LOW);
		TIM5_ClearPendingAndEnable();
		return 1;
	}
	else
	{
		return -1;
	}
}


/** 按 EEPROM/限位 5 字节位图（40 层，与 elevator_tail5 位序一致）对置位楼层做常开授权；返回成功打开的楼层数，0 表示无位置位，-1 参数非法 */
int Floor_AuthCheck_limit(const uint8_t elevator_tail5_limit[5])
{
	int bi;
	int bj;
	int bit_no;
	int n_ok;

	if (!elevator_tail5_limit)
		return -1;

	n_ok = 0;
	bit_no = 0;
	for (bi = 4; bi >= 0; bi--)
	{
		for (bj = 0; bj < 8; bj++)
		{
			if (bit_no >= 40)
				return n_ok;
			if (elevator_tail5_limit[bi] & (1u << (unsigned)bj))
			{
				printf("limit control: at floor[%d]\r\n", bit_no + 1);
				if (Floor_AuthCheck_open((uint8_t)(bit_no + 1)) == 1)
					n_ok++;
			}
			else{
				Floor_Off((uint8_t)(bit_no + 1));
			}
			bit_no++;
		}
	}
	return n_ok;
}

int Floor_AuthCheck_limit_off(const uint8_t elevator_tail5_limit[5])
{
	int bi;
	int bj;
	int bit_no;
	int n_ok;

	if (!elevator_tail5_limit)
		return -1;

	n_ok = 0;
	bit_no = 0;
	for (bi = 4; bi >= 0; bi--)
	{
		for (bj = 0; bj < 8; bj++)
		{
			if (bit_no >= 40)
				return n_ok;
			if (elevator_tail5_limit[bi] & (1u << (unsigned)bj))
			{
				printf("limit control off: at floor[%d]\r\n", bit_no + 1);
				Floor_Off((uint8_t)(bit_no + 1));
				//n_ok++;
			}

			bit_no++;
		}
	}
	return n_ok;
}

int Floor_AuthCheck_open(uint8_t floor)
{
	if (floor > 0 && floor < 41)
	{
		ExtIO_Set(floor, EXT_IO_LOW);
		//TIM5_ClearPendingAndEnable();
		return 1;
	}
	else
	{
		return -1;
	}
}

//关闭某楼层权限
int Floor_Off(uint8_t floor)
{
	/* 关闭楼层 IO */
	ExtIO_Set(floor, EXT_IO_HIGH);
	return 0;
}


// 关闭所有楼层权限
int Floor_AllOff(void)
{
	/* 关闭所有楼层 IO */
	ExtIO_SetMulti(FLOOR_IO_START, FLOOR_IO_COUNT, EXT_IO_HIGH);
	return 0;
}


// 打开所有楼层权限
int Floor_AllOn(void)
{
	/* 打开所有楼层 IO */
	TIM5_ClearPendingAndEnable();
	ctrl_time =0;
	ExtIO_SetMulti(FLOOR_IO_START, FLOOR_IO_COUNT, EXT_IO_LOW);

	return 0;
}
