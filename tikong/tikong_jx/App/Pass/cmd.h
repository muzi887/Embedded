#ifndef __CMD_H
#define __CMD_H
#include "sys.h"
#include "RTC.h"
int BlackUserListAdd(char received_data[]);
int BlackUserListDel(char received_data[]);
int CommContrl(char received_data[], const char order_type_meta[8], const char card_number_meta[32],int uart_port);
int BlackUserListClear(char received_data[]);
int AllSeting(char received_data[]);

/** AT 序列全部成功后调用（main）：若本次由 Cmd_Setting 触发则写入 EEPROM */
void Cmd_Setting_OnAtSequenceDone(void);
/** AT 序列失败/中止时调用（main）：清除“待写 EEPROM”以免误写 */
void Cmd_Setting_OnAtSequenceAbort(void);

/** 解析 14 位 YYYYMMDDhhmmss → RTC_Time；0 成功，-1 失败 */
int parseYMDHMS(const char *s, RTC_Time *t);
#endif
