#ifndef PASS_PERMISSION_H
#define PASS_PERMISSION_H

int Cmd_Permission_ProcessGate(char *received_data, int uart_port);
int Cmd_Permission_ProcessUnit(char *received_data, int uart_port);
int Cmd_Permission_ProcessElevator(char *received_data);
void Cmd_Permission_VerifySignature(char received_data[]);
void Cmd_Permission_CheckBlacklist(void);
void Cmd_Permission_CheckTimeWindow(void);
int Cmd_Permission(char received_data[], const char order_type_meta[8], const char card_number_meta[32], int uart_port);

#endif
