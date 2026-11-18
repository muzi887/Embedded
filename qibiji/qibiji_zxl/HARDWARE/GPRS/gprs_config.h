#ifndef __GPRS_CONFIG_H
#define __GPRS_CONFIG_H

#include "stm32f10x.h"
#include "sys.h"

/* ---------------------------------------------------------------------------
 * GPRS 模块全局配置 —— 编译期常量集中地
 *
 * 职责：集中定义设备身份（productKey/deviceKey）、MQTT topic 索引、
 *       AT 命令串、各类超时与节流参数。
 *
 * 备注：身份字段当前是占位符，后续应改为从模块或 EEPROM 读取。
 * --------------------------------------------------------------------------- */

/* ---- device identity (replace with persistent storage later) ---- */
#define GPRS_PRODUCT_KEY   "your_product_key"
#define GPRS_DEVICE_KEY    "your_device_key"
#define GPRS_CLIENT_ID     "your_client_id"

/* ---- MQTT topic slots (module MQTTPUBTP/MQTTSUBTP indices) ---- */
#define TOPIC_SUB           1u
#define TOPIC_PUB           1u
#define TOPIC_PROPERTY_PUB  3u
#define TOPIC_EVENT_PUB     4u
#define TOPIC_SERVICE_REPLY 6u
#define TOPIC_REQUEST_INVOKE 8u

/* ---- AT commands sent to the 4G module ---- */
#define GPRS_AT_ICCID      "usr.cn#AT+ICCID?\r\n"
#define GPRS_AT_ICCID_LEN 18u
#define GPRS_AT_MQTTCID    "usr.cn#AT+MQTTCID?\r\n"
#define GPRS_AT_MQTTCID_LEN 20u
#define GPRS_AT_CSQ        "usr.cn#AT+CSQ?\r\n"
#define GPRS_AT_CSQ_LEN    16u
/* 裸 AT：模块若本来就处于 AT 命令模式，回 OK；用它与带前缀的命令做区分 */
#define GPRS_AT_PING       "AT\r\n"
#define GPRS_AT_PING_LEN   4u

/* ---- log helper: implemented in gprs_port.c (C90-safe, no variadic macro) ---- */
void GPRS_Printf(const char *fmt, ...);

#endif