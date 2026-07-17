#ifndef _SHA1_H_
#define _SHA1_H_

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t Message_Digest[5];   // 5 * 32 = 160 位输出
    uint32_t Length_Low;          // 消息长度（低 32 位，bit）
    uint32_t Length_High;         // 消息长度（高 32 位，bit）
    uint8_t  Message_Block[64];   // 512 位缓冲区
    int      Message_Block_Index; 
    int      Computed;            
    int      Corrupted;           
} SHA1Context;

void SHA1Reset(SHA1Context *context);
int  SHA1Result(SHA1Context *context);
void SHA1Input(SHA1Context *context, const uint8_t *message_array, size_t length);

int sha1_hash(const char *source, char *output_hex); // 直接生成 40字节 HEX 串

#endif
