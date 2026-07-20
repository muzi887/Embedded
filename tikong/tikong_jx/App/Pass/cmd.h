#ifndef __CMD_H
#define __CMD_H

#include "sys.h"
#include "rtc.h"

int CommControl(char received_data[], const char order_type_meta[8], const char card_number_meta[32],int uart_port);
int BlackUserListAdd(char received_data[]);
int BlackUserListDel(char received_data[]);
int BlackUserListClear(char received_data[]);
int AllSetting(char received_data[]);

#endif
