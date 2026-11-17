#include "gate_io.h"

/* ===========================================================================
 * Gate hardware I/O implementation
 *
 * Changes vs original:
 *   - limit levels are debounced (2 equal samples) before being reported;
 *   - encoder keeps a cumulative count plus a per-tick delta, so the motor
 *     layer never touches pins or decides edge polarity;
 *   - no direction / opening / motor state in this module.
 * =========================================================================== */

 /* 上/下限位消抖组 */
static uint8_t  s_open_stable;   /* stable raw level, 1 = released--内部上拉、低电平有效*/
static uint8_t  s_open_cand;     /* 本次采样读到的新电平 */
static uint8_t  s_open_cnt;      /* 电平出现次数 */

static uint8_t  s_close_stable;
static uint8_t  s_close_cand;
static uint8_t  s_close_cnt;

/* 编码器状态组 */
static uint8_t  s_enc_a_prev;    /* 上次 A 通道的电平 */
static uint8_t  s_enc_b_prev;    /* 上次 B 通道的电平 */
static uint16_t s_pulse_count;   /* 累积脉冲数 */
static uint8_t  s_pulse_delta;   /* 每 tick 的脉冲变化量 */

static void GATEIO_Debounce(uint8_t raw, uint8_t *cand, uint8_t *stable, uint8_t *cnt)
{
    if (raw != *cand)
    {
        *cand = raw;             /* changed: this is sample 1 of the window */
        *cnt = 1u;
        return;
    }

    if (*cnt < GATEIO_DEBOUNCE_TICKS)
    {
        (*cnt)++;
        if (*cnt >= GATEIO_DEBOUNCE_TICKS)
        {
            *stable = *cand;     /* two equal samples: commit the new level */
        }
    }
}

void GATEIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

  /* PB3/PB4 are shared with JTAG (TDO / nTRST). Disable JTAG but KEEP SWD
    * (PA13/PA14) so flashing and debugging still work. */
  GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

  GPIO_InitStructure.GPIO_Pin = GATEIO_OPEN_LIMIT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

  /* limit close PB3 + encoder PB4/PB0: pull-up inputs, active low */
	GPIO_InitStructure.GPIO_Pin =  GATEIO_CLOSE_LIMIT_PIN | GATEIO_ENC_A_PIN | GATEIO_ENC_B_PIN;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

  /* seed debounce state with the real pin levels, so a low level present at
    * power-up is not reported as a fresh edge / fresh limit event */
  s_open_stable  = (GATEIO_OPEN_LIMIT_LEVEL()  != 0u) ? 1u : 0u; // 读真实电平，把它归一化成 0/1
  s_open_cand    = s_open_stable;
  s_open_cnt     = 0u;
  s_close_stable = (GATEIO_CLOSE_LIMIT_LEVEL() != 0u) ? 1u : 0u;
  s_close_cand   = s_close_stable;
  s_close_cnt    = 0u;

  s_enc_a_prev  = (GATEIO_ENC_A_LEVEL() != 0u) ? 1u : 0u; // prev 设成上电真实电平
  s_enc_b_prev  = (GATEIO_ENC_B_LEVEL() != 0u) ? 1u : 0u;
  s_pulse_count = 0u;
  s_pulse_delta = 0u;
}

/*	判 PD7/PB3 电平，限位消抖 */
void GATEIO_Tick(void)
{
    uint8_t cur;

    /* ---- limit switches: debounce, then expose as clean levels ---- */
    cur = (GATEIO_OPEN_LIMIT_LEVEL() != 0u) ? 1u : 0u;
    GATEIO_Debounce(cur, &s_open_cand, &s_open_stable, &s_open_cnt);

    cur = (GATEIO_CLOSE_LIMIT_LEVEL() != 0u) ? 1u : 0u;
    GATEIO_Debounce(cur, &s_close_cand, &s_close_stable, &s_close_cnt);

    /* ---- encoder: falling edge on either channel = one pulse ---- */
    cur = (GATEIO_ENC_A_LEVEL() != 0u) ? 1u : 0u;
    if ((s_enc_a_prev != 0u) && (cur == 0u))
    {
        if (s_pulse_count < 0xFFFFu)
            s_pulse_count++;
        if (s_pulse_delta < 0xFFu)
            s_pulse_delta++;
    }
    s_enc_a_prev = cur;

    cur = (GATEIO_ENC_B_LEVEL() != 0u) ? 1u : 0u;
    if ((s_enc_b_prev != 0u) && (cur == 0u))
    {
        if (s_pulse_count < 0xFFFFu)
            s_pulse_count++;
        if (s_pulse_delta < 0xFFu)
            s_pulse_delta++;
    }
    s_enc_b_prev = cur;
}

/* 消抖后的上限位，1=全开到位 */
uint8_t GATEIO_IsOpenLimit(void)
{
    return (s_open_stable == 0u) ? 1u : 0u;
}

/* 消抖后的下限位，1=全关到位 */
uint8_t GATEIO_IsCloseLimit(void)
{
    return (s_close_stable == 0u) ? 1u : 0u;
}

/* 获取编码器脉冲计数 */
uint16_t GATEIO_GetPulseCount(void)
{
    return s_pulse_count;
}

/* 取走并清零上次调用以来新增的边沿数 */
uint8_t GATEIO_PopPulseDelta(void)
{
    uint8_t d = s_pulse_delta;

    s_pulse_delta = 0u;
    return d;
}


