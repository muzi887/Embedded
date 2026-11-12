#include "lcd.h"
#include "reg.h"
#include "main.h"
#include "usart.h"

// #include "global.h"
#include "crc16.h"
extern u8 flag1s;
char Lcd_RxBuff[Rec5_Len];      // 串口5接收缓冲区
char Lcd_TxBuff[Usart5_Tx_Len]; // 串口5发送缓冲区
// extern u8 FLAG_OPEN, FLAG_CLOSE;
// extern u8 lcd_para_init;
// extern u8 eepromWriteFlag;
// extern u16 gate_range_adc_value;

// u8 gate_range_save_state1 = 0, gate_range_save_state2 = 0, gate_range_save_state3 = 1;
// u16 gate_range_adc_value_temp1 = 0, gate_range_adc_value_temp2 = 0;

void ToLcd_int(char *str,int val)
{
    char str3[] = {0xff,0xff,0xff} ;
    char result[50];

    strcpy(result,str);
    sprintf(result + strlen(result), "%d", val);
    strcat(result,str3);
//snprintf(result, sizeof(result), "%s%d", str1, Num_count);
//snprintf(result, sizeof(result), "%s%d", result, Num_count);
    Usart5_Send(result,strlen(result));   //TOLCD
}

void ToLcd_float(char *str,float val)
{
    char str3[] = {0xff,0xff,0xff} ;
    char result[50];

    strcpy(result,str);
    sprintf(result + strlen(result), "%f", val);
    strcat(result,str3);
//snprintf(result, sizeof(result), "%s%d", str1, Num_count);
//snprintf(result, sizeof(result), "%s%d", result, Num_count);
    Usart5_Send(result,strlen(result));   //TOLCD
}

void ToLcd_string(char *name,char *str)
{
    char str3[] = {0xff,0xff,0xff} ;
    char result[50];

    strcpy(result,name);
    sprintf(result + strlen(result), "%s", str);
    strcat(result,str3);
//snprintf(result, sizeof(result), "%s%d", str1, Num_count);
//snprintf(result, sizeof(result), "%s%d", result, Num_count);
    Usart5_Send(result,strlen(result));   //TOLCD
}

// 接收LCD 命令
u8 order_recv()
{
    u8 i = 0, crcH = 0, crcL = 0, num = 0;
    u16 addr, val = 0x0000, back;
    // u8 cmd = 0, val = 0;
    // 先判断中断接收是否完成
    if (rx_lcd_complete == 0)
        return 0;

    // rxlen = Usart5_Rec_Cnt;
    // Usart5_Rec_Cnt = 0;

    for (i = 0; i < Usart5_Rec_Cnt; i++)
    {
        Lcd_RxBuff[i] = Rece_Buf5[i];
        Rece_Buf5[i] = 0;
        printf("%c", Lcd_RxBuff[i]);
    }

    // if (Usart5_Rec_Cnt)
    // printf("%d", Usart5_Rec_Cnt);
    rx_lcd_complete = 0;
    Usart5_Rec_Cnt = 0;

    // 如果是写回执，则不处理
    if (Lcd_RxBuff[5] == 0x4b)
    {
        return 0;
    }
    //-------------------------------------------
    //  crc16val = crc16(&Lcd_RxBuff[3], rxlen - 5);
    //  crcH = (crc16val >> 8) & 0xFF;
    //  crcL = crc16val & 0xFF;
    //  printf("\n%x  %x ",crcL,crcH);

    addr = (u16)(Lcd_RxBuff[4] << 8) | (u16)(Lcd_RxBuff[5] & 0x00ff);
    num = Lcd_RxBuff[6];
    val = (u16)(Lcd_RxBuff[7] << 8) | (u16)(Lcd_RxBuff[8] & 0x00ff);

    // printf("%x", val);

    if (addr == 0x1303) // 电机运行开关
    {   // 执行命令必须当电机为停止时才能有效
        if (val == MOTOR_UP && sys_moter_state_old == MOTOR_STOP && FLAG_OPENALL != 1)
        {
            // LCD_SET_UP_ON;
            sys_moter_state = MOTOR_UP;
            armReg.range_target=300;
            // enqueue_LCD(&queue_lcd_arm, Lcd_TxBuff, 8);
            //  KeyLed_Ctl(0x01);
        }
        if (val == MOTOR_DOWN && sys_moter_state_old == MOTOR_STOP && FLAG_CLOSEALL != 1)
        {
            // LCD_SET_DOWN_ON;
            sys_moter_state = MOTOR_DOWN;
            armReg.range_target=0;
            // enqueue_LCD(&queue_lcd_arm, Lcd_TxBuff, 8);
            //  KeyLed_Ctl(0x02);
        }
        if (val == MOTOR_STOP)
        {
            // LCD_SET_STOP_ON;
            sys_moter_state = MOTOR_STOP;
            // enqueue_LCD(&queue_lcd_arm, Lcd_TxBuff, 8);
            //  KeyLed_Ctl(0x04);
        }
    }

    // Usart5_Send(Lcd_TxBuff, 8);
    return 1;
}

//// 定时器3中断服务程序
void TIM3_IRQHandler(void) // TIM3中断 100ms
{
    static u16 cnt_3 = 0;
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) // 检查指定的TIM中断发生与否:TIM 中断源
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update); // 清除TIMx的中断待处理位:TIM 中断源

        cnt_3++;
        if(cnt_3==50) {
            flag1s = 1;
            cnt_3=0;
        }
    }
}
