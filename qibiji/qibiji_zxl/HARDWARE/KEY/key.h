#ifndef __KEY_H
#define __KEY_H
#include "sys.h"

/* ---------------------------------------------------------------------------
 * 面板按键（KEY）模块 —— 对外唯一接口
 *
 * 职责：把 5 路按键电平翻译成「一次按键事件」。
 *
 * 用法：上电调一次 KEY_Init()；
 *       此后每 10ms 调一次 KEY_Tick()（采样 + 去抖）；
 *       主循环用 KEY_Pop() 取事件，取走即清，无事件返回 KEY_NONE。
 * --------------------------------------------------------------------------- */

typedef enum
{
    KEY_NONE = 0,
    KEY_UP,             /* K1 UP   = 1 */
    KEY_DOWN,           /* K2 DOWN = 2 */
    KEY_STOP,           /* K5 STOP = 3，旧名 KEY_ENTER */
    KEY_SET,            /* K4 SET  = 4，旧名 KEY_LEFT  */
    KEY_CANCEL          /* K3 CAN  = 5，旧名 KEY_RIGHT */
} key_code_t; // 一个类型和编译器的常量

void       KEY_Init(void);
void       KEY_Tick(void);
key_code_t KEY_Pop(void);

#endif
