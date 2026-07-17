#include "blackList.h"

// 全局结构体数组
DataEntry dataArray[MAX_ARRAY_SIZE];

// 初始化内存中的黑名单数组（当前未使用）
void initBlacklist()
{
	int i;
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		dataArray[i].isEmpty = 1; // 初始化时所有数据都为空
	}
}

// 添加数据到内存和 EEPROM
int addDataToBlacklist(uint8_t *data)
{
	int i, j;
	uint16_t eeAddress;

	// 检查数据是否已经在黑名单中
	for (i = 0; i < MAX_ARRAY_SIZE; i++) //
	{
		if (memcmp(dataArray[i].data, data, 10) == 0 && dataArray[i].isEmpty == 0)
		{
			return -1; // 数据已存在
		}
	}

	// 找到第一个空位置
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		if (dataArray[i].isEmpty == 1)
		{
			printf("add = %d\n", i);
			// 复制数据
			for (j = 0; j < DATA_SIZE - 1; j++)
			{
				dataArray[i].data[j] = data[j];
			}
			dataArray[i].data[DATA_SIZE - 1] = 0;
			dataArray[i].isEmpty = 0;
			// 写入到 EEPROM
			eeAddress = i * DATA_SIZE;

			for (j = 0; j < DATA_SIZE; j++)
			{
				EEPROM_WriteByte(eeAddress + j, dataArray[i].data[j]);
			}

			return 0;
		}
	}
	return -2; // 空间已满
}

// 删除黑名单数据
int deleteDataFromBlacklist(uint8_t *data)
{
	int i;
	uint16_t eeAddress;

	// 查找数据
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		if (memcmp(dataArray[i].data, data, 10) == 0 && dataArray[i].isEmpty == 0)
		{
			dataArray[i].isEmpty = 1; // 标记为空
			// 清除 EEPROM 中的数据
			eeAddress = (i + 1) * DATA_SIZE - 1;
			EEPROM_WriteByte(eeAddress, 0x01); // 写入 0x01 标记为空
			return 0;						   // 成功
		}
	}
	return -1; // 未找到数据
}

// 清空从 EEPROM 地址 0 开始到 PUBLICKEY_ADR 地址之前的 EEPROM 空间，标记为空
void clearPublicKeySpace(void)
{
	uint16_t eeAddress = 0; // 从 EEPROM 地址起始地址开始
	uint16_t i = 0;

	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		dataArray[i].isEmpty = 0x01;
	}

	// 从地址 0 开始到 PUBLICKEY_ADR 地址之前的 EEPROM 空间，标记为空
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		eeAddress = (i + 1) * DATA_SIZE - 1;
		EEPROM_WriteByte(eeAddress, 0x01); // 写入 0x01 标记为空
	}

	//
	//	for (i = 0; i < 2200; i++)
	//	{
	//		EEPROM_WriteByte(0+i , 0x02);  // 写入 0xFF 标记为空
	//	}

	printf("eprom data is empty!\n");
}

// 从 EEPROM 中读取数据并初始化内存
void loadDataFromEEPROM(void)
{
	int i, j;
	uint16_t eeAddress;
	uint8_t data[DATA_SIZE];
	printf("\nblacklist:\n");
	for (i = 0; i < MAX_ARRAY_SIZE; i++)
	{
		eeAddress = i * DATA_SIZE;
		EEPROM_ReadBuffer(eeAddress, data, DATA_SIZE); // 从 EEPROM 读取 11 字节数据

		//		for (j = 0; j < DATA_SIZE; j++)
		//		{
		//			printf("%c ",data[j]);
		//		}
		//		printf("\n");

		if (data[10] == 0x00) // 检查数据标记位是否为非空
		{
			dataArray[i].isEmpty = 0x00;
			for (j = 0; j < DATA_SIZE; j++)
			{
				dataArray[i].data[j] = data[j];
				printf("%c ", data[j]);
			}
			printf("\n");
		}
		else
		{
			dataArray[i].isEmpty = 0x01;
		}
	}
}
