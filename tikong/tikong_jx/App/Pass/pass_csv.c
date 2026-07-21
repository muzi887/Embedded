#include "pass_csv.h"

char s_recv_csv_field[QR_RECV_CSV_MAX][QR_RECV_CSV_FIELD];
int s_recv_csv_count;

// 读头 CSV 整串按 逗号 切成多列，写入本文件静态缓冲
void qr_split_s_received_by_comma(const char *src)
{
	const char *p;
	const char *end;
	int fi;

	s_recv_csv_count = 0;
	if (!src)
		return;
	p = src;
	for (fi = 0; fi < QR_RECV_CSV_MAX && *p; fi++)
	{
		int j = 0;
		while (*p && *p != ',' && j < QR_RECV_CSV_FIELD - 1)
			s_recv_csv_field[fi][j++] = *p++;
		s_recv_csv_field[fi][j] = '\0';
		/* 本列未遇逗号但缓冲已满：跳过后续字符直至逗号，避免同一物理列被拆成多列 */
		if (*p && *p != ',')
		{
			printf("qr csv: field %d truncated (max %d chars)\r\n", fi, QR_RECV_CSV_FIELD - 1);
			while (*p && *p != ',')
				p++;
		}
		s_recv_csv_count++;
		if (*p == ',')
			p++;
	}

	/* 尾部为 ',' 时，补一个空字段（例如 "a,b," => 3 列） */
	end = src;
	while (*end)
		end++;
	if (end > src && *(end - 1) == ',' && s_recv_csv_count < QR_RECV_CSV_MAX)
	{
		s_recv_csv_field[s_recv_csv_count][0] = '\0';
		s_recv_csv_count++;
	}
}
