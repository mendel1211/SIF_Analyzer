/**
*****************************************************************************************
*     Copyright(c) 2025, 黑龙江天有为电子股份有限公司. All rights reserved.
*****************************************************************************************
* @file      sif.c
* @brief     一线通(SIF)接收与解析模块实现
*            提供一线通数据接收、解析、校验等功能
* @author    shu.wang
* @date      2026-01-26
 * @version   v1.2 (边沿去抖+同步窗放宽，改善实车一线通接收)
**************************************************************************************
 * @attention
* <h2><center>&copy; COPYRIGHT 2025 黑龙江天有为电子股份有限公司</center></h2>
*
* 实现说明:
* - 使用定时器扫描方式接收一线通数据(50us周期)
* - 状态机解析数据帧
* - 自动校验和验证
* - 数据接收完成回调通知
*
 * 修复记录 v1.1:
 * - 修复了bit接收后的左移逻辑，确保最后一个bit不会被错误左移
 * - 修复了导致最后两个字节数据错误的问题(0D 0A -> 56)
 * v1.2:
 * - 引脚边沿去抖(SIF_PIN_EDGE_DEBOUNCE)，抑制LA可见的亚100µs毛刺
 * - 放宽同步高容许范围与 SYNC_H 等待超时，匹配~0.5ms同步高/~1.5ms数据位周期
**************************************************************************************
*/

/*===========================================================================*/
/* Includes                                                                  */
/*===========================================================================*/
#include "sif.h"
#include "../../Device/GPIO/GPIO.h"
#include "../../System/TickTimer/TickTimer.h"
#include "../standard_types.h"
#include "../rtt/SEGGER_RTT.h"
#include "sif/sif_protocol.h"

/*===========================================================================*/
/* Local Macros and Constants                                               */
/*===========================================================================*/

/** @brief 一线通数据接收引脚: PC6 */
#define SIF_RX_PIN                    (GPIO_PORTC_PIN06)

/** @brief 低电平值 */
#define SIF_LEVEL_LOW                 (GPIO_PIN_RESET)

/** @brief 高电平值 */
#define SIF_LEVEL_HIGH                (GPIO_PIN_SET)

/** @brief 定时器扫描周期(50us) */
#define SIF_TIMER_INTERVAL            (1U)

/*===========================================================================*/
/* Local Type Definitions                                                    */
/*===========================================================================*/

/**
 * @brief 一线通接收控制块
 */
typedef struct {
    SifReceiveState_e state;              /**< 接收状态 */
    uint8_t receiveBitNum;               /**< 接收的bit位个数 */
    uint8_t receiveDataNum;              /**< 接收的数据个数 */
    uint16_t levelTimeCnt;               /**< 高低电平时间计数 */
    uint16_t lowPulseWidth;              /**< 当前逻辑周期的低电平脉宽 */
    uint16_t highPulseWidth;             /**< 当前逻辑周期的高电平脉宽 */
    boolean lastPinLevel;                /**< 上一次的引脚电平 */
    boolean startTimingFlag;             /**< 开始高低电平计时标记 */
    boolean hasReadBit;                  /**< 是否已读取一个bit位 */
    boolean dataReady;                   /**< 数据就绪标志 */
    boolean checkOk;                     /**< 校验和正确标志 */
    uint8_t receiveDataBuf[SIF_RX_DATA_NUM]; /**< 接收数据缓存数组 */
    /** 提交时刻快照：避免 ISR 在 INITIAL->SYNC_L 清零 receiveDataNum 后主循环仍见 len=0 或缓冲被下一帧覆盖 */
    uint8_t readyDataNum;
    uint8_t readyDataBuf[SIF_RX_DATA_NUM];
    SifCallback_t callback;              /**< 数据接收完成回调函数 */
} SifControlBlock_t;

/*===========================================================================*/
/* Local Variables                                                           */
/*===========================================================================*/

/** @brief 一线通接收控制块 */
static SifControlBlock_t s_SifCb = {
    .state = SIF_STATE_INITIAL,
    .receiveBitNum = 0U,
    .receiveDataNum = 0U,
    .levelTimeCnt = 0U,
    .lowPulseWidth = 0U,
    .highPulseWidth = 0U,
    .lastPinLevel = FALSE,
    .startTimingFlag = FALSE,
    .hasReadBit = FALSE,
    .dataReady = FALSE,
    .checkOk = FALSE,
    .receiveDataBuf = {0U},
    .readyDataNum = 0U,
    .readyDataBuf = {0U},
    .callback = NULL_PTR
};

/** @brief 调试日志计数器，限制输出次数 */
static uint8_t s_DebugLogCounter = 0U;

/** @brief 忽略“持续低触发的帧结束”直到总线变高（防止未满帧就 commit） */
static boolean s_SifIgnorePolledEndUntilHigh = FALSE;

/** @brief 经过去抖后的电平：TRUE=低 */
static boolean s_SifPinLowStable = FALSE;
static uint8_t s_SifPinDebounceCnt = 0U;

#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
static uint32_t s_SifRttIsrTick;
static uint32_t s_SifRttPinEdgesWindow;
static boolean s_SifRttLastPinSample;
static boolean s_SifRttPinSampleValid;
#endif

/*===========================================================================*/
/* Local Function Prototypes                                                */
/*===========================================================================*/

static void sif_TimerCallback(void);
static void sif_ReceiveDataHandle(void);
static void sif_CheckSumHandle(void);
static boolean sif_IsPinLow(void);
static boolean sif_PinDebounceUpdate(boolean rawLow);
static boolean sif_ShouldCommitFrame(void);
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
static void sif_RttPutU32(char *msg, uint8_t *pPos, uint8_t maxLen, uint32_t val);
static void sif_RttPeriodicStats(void);
#endif

/*===========================================================================*/
/* Local Function Implementations                                           */
/*===========================================================================*/

/**
 * @brief     检查接收引脚是否为低电平
 * 
 * @return    TRUE   引脚为低电平
 * @return    FALSE  引脚为高电平
 */
static boolean sif_IsPinLow(void)
{
    uint32_t pinLevel;
    boolean isLow;
    
    pinLevel = GPIO_Get_inputLevel(SIF_RX_PIN);
    
    if (pinLevel == SIF_LEVEL_LOW) {
        isLow = TRUE;
    } else {
        isLow = FALSE;
    }
    
    return isLow;
}

/**
 * @brief 50us 定时器上的边沿去抖：抑制单次采样毛刺，再交给状态机
 */
static boolean sif_PinDebounceUpdate(boolean rawLow)
{
    uint8_t thresh;

    thresh = SIF_PIN_EDGE_DEBOUNCE;
    if (thresh < 1U) {
        thresh = 1U;
    }
    if (rawLow == s_SifPinLowStable) {
        s_SifPinDebounceCnt = 0U;
    } else {
        s_SifPinDebounceCnt++;
        if (s_SifPinDebounceCnt >= thresh) {
            s_SifPinLowStable = rawLow;
            s_SifPinDebounceCnt = 0U;
        }
    }
    return s_SifPinLowStable;
}

/**
 * @brief 是否允许按“结束条件”提交当前帧
 * @details 头已是 D0+设备号+长度时，必须收满 3+dataLen+1，否则易将位间长低误判为帧尾（如 len=11 却声明 0x14）
 */
static boolean sif_ShouldCommitFrame(void)
{
    uint16_t need;
    if (s_SifCb.receiveDataNum < SIF_PROTOCOL_HEADER_LEN) {
        return TRUE;
    }
    if ((s_SifCb.receiveDataBuf[0U] != SIF_PROTOCOL_NUMBER) ||
        (s_SifCb.receiveDataBuf[1U] != SIF_DEVICE_NUMBER)) {
        return TRUE;
    }
    need = (uint16_t)SIF_PROTOCOL_HEADER_LEN + (uint16_t)s_SifCb.receiveDataBuf[2U] +
           (uint16_t)SIF_CHECKSUM_LEN;
    if (need > (uint16_t)SIF_RX_DATA_NUM) {
        return TRUE;
    }
    if (s_SifCb.receiveDataNum < need) {
        return FALSE;
    }
    return TRUE;
}

/** @brief 校验完成后锁存一帧供 Sif_Service / 回调读取，再置 dataReady */
static void sif_LatchReadyFrame(void)
{
    uint8_t i;
    uint8_t n = s_SifCb.receiveDataNum;

    if (n > SIF_RX_DATA_NUM) {
        n = SIF_RX_DATA_NUM;
    }
    for (i = 0U; i < n; i++) {
        s_SifCb.readyDataBuf[i] = s_SifCb.receiveDataBuf[i];
    }
    for (; i < SIF_RX_DATA_NUM; i++) {
        s_SifCb.readyDataBuf[i] = 0U;
    }
    s_SifCb.readyDataNum = n;
    s_SifCb.dataReady = TRUE;
}

#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
/** @brief ISR 内使用：往 msg 追加无符号十进制 */
static void sif_RttPutU32(char *msg, uint8_t *pPos, uint8_t maxLen, uint32_t val)
{
    char digits[11];
    uint8_t nd = 0U;
    uint8_t i;

    if (val == 0U) {
        digits[nd++] = '0';
    } else {
        while ((val > 0U) && (nd < 10U)) {
            digits[nd++] = (char)('0' + (val % 10U));
            val /= 10U;
        }
    }
    for (i = nd; (i > 0U) && (*pPos < (uint8_t)(maxLen - 1U)); i--) {
        msg[*pPos] = digits[i - 1U];
        (*pPos)++;
    }
}

/** @brief 周期性心跳：ISR 计数、当前引脚、状态机、TickTimer 使能、窗口内边沿数 */
static void sif_RttPeriodicStats(void)
{
    char msg[100];
    uint8_t pos = 0U;
    uint32_t edges = s_SifRttPinEdgesWindow;
    const char *pref = "[SIF] hb isr=";
    uint8_t i;
    boolean pinLow;
    const char *s;

    s_SifRttPinEdgesWindow = 0U;

    for (i = 0U; (pref[i] != '\0') && (pos < 95U); i++) {
        msg[pos++] = pref[i];
    }
    sif_RttPutU32(msg, &pos, 100U, s_SifRttIsrTick);

    s = " pin=";
    for (i = 0U; (s[i] != '\0') && (pos < 95U); i++) {
        msg[pos++] = s[i];
    }
    pinLow = sif_IsPinLow();
    msg[pos++] = (pinLow == TRUE) ? (char)'L' : (char)'H';

    s = " st=";
    for (i = 0U; (s[i] != '\0') && (pos < 95U); i++) {
        msg[pos++] = s[i];
    }
    sif_RttPutU32(msg, &pos, 100U, (uint32_t)s_SifCb.state);

    s = " tt=";
    for (i = 0U; (s[i] != '\0') && (pos < 95U); i++) {
        msg[pos++] = s[i];
    }
    sif_RttPutU32(msg, &pos, 100U, (uint32_t)TickTimer_getState());

    s = " edge=";
    for (i = 0U; (s[i] != '\0') && (pos < 95U); i++) {
        msg[pos++] = s[i];
    }
    sif_RttPutU32(msg, &pos, 100U, edges);

    if (pos < 98U) {
        msg[pos++] = '\r';
        msg[pos++] = '\n';
    }
    (void)SEGGER_RTT_WriteSkipNoLock(0, msg, (unsigned)pos);
}
#endif

/**
 * @brief     定时器回调函数
 * 
 * @details   每50us调用一次，用于扫描GPIO电平并处理接收数据
 */
static void sif_TimerCallback(void)
{
    if (s_SifCb.startTimingFlag == TRUE) {
        s_SifCb.levelTimeCnt++;
    }
    
    sif_ReceiveDataHandle();

#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
    s_SifRttIsrTick++;
    if ((s_SifRttIsrTick % SIF_RTT_ISR_LOG_INTERVAL) == 0U) {
        sif_RttPeriodicStats();
    }
#endif
}

/**
 * @brief     接收数据处理
 * 
 * @details   根据当前状态处理接收数据，实现状态机解析
 */
static void sif_ReceiveDataHandle(void)
{
    boolean pinLevel;
    boolean pinRaw;
    uint16_t timeCnt;
    
    timeCnt = s_SifCb.levelTimeCnt;
    pinRaw = sif_IsPinLow();
    pinLevel = sif_PinDebounceUpdate(pinRaw);

#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
    if (s_SifRttPinSampleValid == FALSE) {
        s_SifRttLastPinSample = pinRaw;
        s_SifRttPinSampleValid = TRUE;
    } else if (pinRaw != s_SifRttLastPinSample) {
        s_SifRttPinEdgesWindow++;
        s_SifRttLastPinSample = pinRaw;
    }
#endif
    
    switch (s_SifCb.state) {
        case SIF_STATE_INITIAL: {
            /* 初始状态，未接收到同步信息，进行同步判断 */
            if (pinLevel == TRUE) {
                /* pinLevel==TRUE 表示低电平 */
                /* 开始计时，等待低电平持续时间达到同步信号要求 */
                if (s_SifCb.startTimingFlag == FALSE) {
                    /* 刚检测到低电平，开始计时 */
                    s_SifCb.levelTimeCnt = 0U;
                    s_SifCb.startTimingFlag = TRUE;
                } else {
                    /* 已经在计时，检查低电平持续时间 */
                    if (timeCnt >= SIF_SYNC_L_TIME_NUM) {
                        /* 低电平持续时间>=10ms，确认为同步信号 */
                        s_SifCb.receiveBitNum = 0U;
                        s_SifCb.receiveDataNum = 0U;
                        s_SifCb.state = SIF_STATE_SYNC_L;
                        /* 只输出前3次 */
                        if (s_DebugLogCounter < 3U) {
#if (SIF_RTT_LOG != 0U)
                            SEGGER_RTT_WriteString(0, "[SIF] INITIAL->SYNC_L\r\n");
#endif
                            s_DebugLogCounter++;
                        }
                    }
                }
            } else {
                /* pinLevel==FALSE 表示高电平，重置计时 */
                s_SifCb.startTimingFlag = FALSE;
                s_SifCb.levelTimeCnt = 0U;
            }
            break;
        }
        
        case SIF_STATE_SYNC_L: {
            /* 在读取同步低电平信号期间 */
            /* 注意：pinLevel==TRUE表示低电平，pinLevel==FALSE表示高电平 */
            if (pinLevel == FALSE) {
                /* 检测到高电平，说明低电平结束 */
                if (timeCnt >= SIF_SYNC_L_TIME_NUM) {
                    /* 同步信号低电平时间要>=10ms */
                    s_SifCb.levelTimeCnt = 0U;
                    s_SifCb.state = SIF_STATE_SYNC_H;
                    /* 关闭状态转换日志 */
                } else {
                    /* 低电平时间不够，重新接收 */
                    s_SifCb.state = SIF_STATE_RESTART;
                    /* 简化输出：只输出实际时间和需要的时间 */
#if (SIF_RTT_LOG != 0U)
                    {
                        char msg[64];
                        uint8_t pos = 0U;
                        uint32_t num;
                        uint8_t i;
                        
                        /* 构建消息 "[SIF] LOW time=XXX, need>=200\r\n" */
                        const char* prefix = "[SIF] LOW time=";
                        for (i = 0U; prefix[i] != '\0' && pos < 63U; i++) {
                            msg[pos++] = prefix[i];
                        }
                        
                        /* 输出实际时间 */
                        num = timeCnt;
                        if (num == 0U) {
                            if (pos < 63U) msg[pos++] = '0';
                        } else {
                            char numStr[8];
                            uint8_t numLen = 0U;
                            while (num > 0U && numLen < 7U) {
                                numStr[numLen++] = (char)('0' + (num % 10U));
                                num /= 10U;
                            }
                            /* 反转并复制 */
                            for (i = numLen; i > 0U && pos < 63U; i--) {
                                msg[pos++] = numStr[i - 1U];
                            }
                        }
                        
                        /* 添加 ", need>=200" */
                        const char* suffix = ", need>=200\r\n";
                        for (i = 0U; suffix[i] != '\0' && pos < 63U; i++) {
                            msg[pos++] = suffix[i];
                        }
                        msg[pos] = '\0';
                        
                        SEGGER_RTT_WriteString(0, msg);
                    }
#endif
                }
            } else {
                /* pinLevel==TRUE，还是低电平，继续等待 */
                /* 检查是否超时（增加到50ms，即1000个周期） */
                if (timeCnt >= SIF_SYNC_L_MAX_LOW_TICKS) {
                    /* 低电平时间过长，可能是信号异常或非标准空闲态被当成同步 */
                    s_SifCb.state = SIF_STATE_RESTART;
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
                    (void)SEGGER_RTT_WriteSkipNoLock(0,
                        "[SIF] SYNC_L RESTART: low too long (SIF_SYNC_L_MAX_LOW_TICKS)\r\n", 65U);
#endif
                }
                /* 否则继续等待低电平结束 */
            }
            break;
        }
        
        case SIF_STATE_SYNC_H: {
            /* 在读取同步信号高电平期间 */
            /* 注意：pinLevel==TRUE表示低电平，pinLevel==FALSE表示高电平 */
            if (pinLevel == TRUE) {
                /* pinLevel==TRUE表示低电平，说明高电平结束 */
                /* 判断同步信号高电平时间是否在范围内 */
                if ((timeCnt >= SIF_SYNC_H_TIME_NUM_MIN) && 
                    (timeCnt <= SIF_SYNC_H_TIME_NUM_MAX)) {
                    /* 检测到低电平，说明同步高电平结束，第一个数据bit的低电平开始 */
                    /* 重置levelTimeCnt为0，从这个低电平开始计时 */
                    s_SifCb.levelTimeCnt = 0U;
                    s_SifCb.lowPulseWidth = 0U;
                    s_SifCb.highPulseWidth = 0U;
                    s_SifCb.lastPinLevel = TRUE;  /* 当前是低电平 */
                    s_SifCb.state = SIF_STATE_DATA_RX;
                    /* 数据区从干净位状态开始，避免上一帧残留导致首字节偏一位 */
                    s_SifCb.hasReadBit = FALSE;
                    s_SifCb.receiveBitNum = 0U;
                    /* 初始化第一个字节为0x00 */
                    s_SifCb.receiveDataBuf[0U] = 0x00U;
#if (SIF_RTT_LOG != 0U)
                    SEGGER_RTT_WriteString(0, "[SIF] Start RX\r\n");
#endif
                } else {
                    s_SifCb.state = SIF_STATE_RESTART;
                    /* 输出实际高电平时间 */
#if (SIF_RTT_LOG != 0U)
                    {
                        char msg[64];
                        uint8_t pos = 0U;
                        uint32_t num = timeCnt;
                        uint8_t i;
                        
                        const char* prefix = "[SIF] HIGH time=";
                        for (i = 0U; prefix[i] != '\0' && pos < 63U; i++) {
                            msg[pos++] = prefix[i];
                        }
                        
                        if (num == 0U) {
                            if (pos < 63U) msg[pos++] = '0';
                        } else {
                            char numStr[8];
                            uint8_t numLen = 0U;
                            while (num > 0U && numLen < 7U) {
                                numStr[numLen++] = (char)('0' + (num % 10U));
                                num /= 10U;
                            }
                            for (i = numLen; i > 0U && pos < 63U; i--) {
                                msg[pos++] = numStr[i - 1U];
                            }
                        }
                        
                        const char* suffix = ", need min-max sync-H\r\n";
                        for (i = 0U; suffix[i] != '\0' && pos < 63U; i++) {
                            msg[pos++] = suffix[i];
                        }
                        msg[pos] = '\0';
                        
                        SEGGER_RTT_WriteString(0, msg);
                    }
#endif
                }
            } else {
                /* pinLevel==FALSE，还是高电平，继续等待 */
                /* 同步高持续过长（电平未回到数据起始低），放宽等待见 sif.h SIF_SYNC_H_TIMEOUT_TICKS */
                if (timeCnt >= SIF_SYNC_H_TIMEOUT_TICKS) {
                    s_SifCb.state = SIF_STATE_RESTART;
#if (SIF_RTT_LOG != 0U)
                    SEGGER_RTT_WriteString(0, "[SIF] State: SYNC_H -> RESTART (timeout)\r\n");
#endif
                }
            }
            break;
        }
        
        case SIF_STATE_DATA_RX: {
            /* 在读取数据码电平期间 */
            /* 逻辑"0"为 长低电平 + 短高电平 */
            /* 逻辑"1"为 短低电平 + 长高电平 */
            /* 通过检测电平变化，记录低电平和高电平的脉宽，然后比较 */

            if (pinLevel == FALSE) {
                /* 总线为高：可重新允许“持续低”型帧结束检测 */
                s_SifIgnorePolledEndUntilHigh = FALSE;
            }
            
            /* 检测电平变化 */
            if (pinLevel != s_SifCb.lastPinLevel) {
                /* 电平发生变化 */
                if (s_SifCb.lastPinLevel == TRUE) {
                    /* 从低电平变为高电平，记录低电平脉宽 */
                    s_SifCb.lowPulseWidth = timeCnt;
                    s_SifCb.levelTimeCnt = 0U;
                    
                    /* 检查低电平脉宽是否过长，可能是结束信号 */
                    /* 只有在完整字节接收后（receiveBitNum==0）才能判断为结束信号 */
                    if ((s_SifCb.lowPulseWidth >= SIF_END_SIGNAL_TIME_NUM) && 
                        (s_SifCb.receiveBitNum == 0U) &&
                        (s_SifCb.hasReadBit == FALSE)) {
                        /* 低电平持续2ms以上，且是在完整字节之后，可能是结束信号 */
                        if (s_SifCb.receiveDataNum > 0U) {
                            if (sif_ShouldCommitFrame() == TRUE) {
                            /* 停止计时，完成数据接收 */
                            s_SifCb.startTimingFlag = FALSE;
                            s_SifCb.levelTimeCnt = 0U;
                            s_SifCb.state = SIF_STATE_INITIAL;
                            s_SifIgnorePolledEndUntilHigh = FALSE;
                            s_SifPinLowStable = sif_IsPinLow();
                            s_SifPinDebounceCnt = 0U;
                            
                            /* 进行校验和验证 */
                            sif_CheckSumHandle();
                            
                            /* 锁存本帧并置就绪（无论校验是否通过） */
                            sif_LatchReadyFrame();
                            
                            if (s_SifCb.callback != NULL_PTR && s_SifCb.checkOk == TRUE) {
                                s_SifCb.callback();
                            }
                            }
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
                            else {
                                (void)SEGGER_RTT_WriteSkipNoLock(0,
                                    "[SIF] skip end: need more bytes per header\r\n", 46U);
                            }
#endif
                        }
                        s_SifCb.lastPinLevel = pinLevel;
                        break;
                    }
                } else {
                    /* 从高电平变为低电平，记录高电平脉宽 */
                    s_SifCb.highPulseWidth = timeCnt;
                    s_SifCb.levelTimeCnt = 0U;
                    
                    /* 一个完整的逻辑周期结束（低+高），判断bit值 */
                    if ((s_SifCb.lowPulseWidth > 0U) && (s_SifCb.highPulseWidth > 0U)) {
                        /* 比较脉宽：哪个更长 */
                        uint8_t bitValue;
                        if (s_SifCb.highPulseWidth > s_SifCb.lowPulseWidth) {
                            /* 高电平更长，逻辑"1" */
                            s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum] |= 0x01U;
                            bitValue = 1U;
                        } else {
                            /* 低电平更长，逻辑"0"，不设置bit（保持0） */
                            bitValue = 0U;
                        }
                        
                        /* 关闭bit级调试输出 */
                        if (0) {  /* 禁用 */
                            SEGGER_RTT_WriteString(0, "[Byte");
                            {
                                char numStr[4];
                                uint32_t num = s_SifCb.receiveDataNum;
                                uint8_t i = 0U;
                                while (num > 0U && i < 3U) {
                                    numStr[i++] = (char)('0' + (num % 10U));
                                    num /= 10U;
                                }
                                if (i == 0U) numStr[i++] = '0';
                                numStr[i] = '\0';
                                {
                                    uint8_t j;
                                    for (j = 0U; j < i / 2U; j++) {
                                        char temp = numStr[j];
                                        numStr[j] = numStr[i - 1U - j];
                                        numStr[i - 1U - j] = temp;
                                    }
                                }
                                SEGGER_RTT_WriteString(0, numStr);
                            }
                            SEGGER_RTT_WriteString(0, ",Bit");
                            {
                                char numStr[2];
                                numStr[0] = (char)('0' + s_SifCb.receiveBitNum);
                                numStr[1] = '\0';
                                SEGGER_RTT_WriteString(0, numStr);
                            }
                            SEGGER_RTT_WriteString(0, "]=");
                            SEGGER_RTT_WriteString(0, (bitValue == 1U) ? "1" : "0");
                            SEGGER_RTT_WriteString(0, ", LOW=");
                            {
                                char numStr[5];
                                uint32_t num = s_SifCb.lowPulseWidth;
                                uint8_t i = 0U;
                                while (num > 0U && i < 4U) {
                                    numStr[i++] = (char)('0' + (num % 10U));
                                    num /= 10U;
                                }
                                if (i == 0U) numStr[i++] = '0';
                                numStr[i] = '\0';
                                {
                                    uint8_t j;
                                    for (j = 0U; j < i / 2U; j++) {
                                        char temp = numStr[j];
                                        numStr[j] = numStr[i - 1U - j];
                                        numStr[i - 1U - j] = temp;
                                    }
                                }
                                SEGGER_RTT_WriteString(0, numStr);
                            }
                            SEGGER_RTT_WriteString(0, ", HIGH=");
                            {
                                char numStr[5];
                                uint32_t num = s_SifCb.highPulseWidth;
                                uint8_t i = 0U;
                                while (num > 0U && i < 4U) {
                                    numStr[i++] = (char)('0' + (num % 10U));
                                    num /= 10U;
                                }
                                if (i == 0U) numStr[i++] = '0';
                                numStr[i] = '\0';
                                {
                                    uint8_t j;
                                    for (j = 0U; j < i / 2U; j++) {
                                        char temp = numStr[j];
                                        numStr[j] = numStr[i - 1U - j];
                                        numStr[i - 1U - j] = temp;
                                    }
                                }
                                SEGGER_RTT_WriteString(0, numStr);
                            }
                            SEGGER_RTT_WriteString(0, "\r\n");
                        }
                        
                        s_SifCb.hasReadBit = TRUE;
                        s_SifCb.lowPulseWidth = 0U;
                        s_SifCb.highPulseWidth = 0U;
                    }
                }
                s_SifCb.lastPinLevel = pinLevel;
            }
            
            /* 处理已读取的bit */
            if (s_SifCb.hasReadBit == TRUE) {
                s_SifCb.hasReadBit = FALSE;
                s_SifCb.receiveBitNum++;
                
                if (s_SifCb.receiveBitNum == SIF_RX_BIT_NUM) {
                    /* 如果一个字节8个bit位接收完成 */
                    /* 不需要反转bit，直接使用接收到的值 */
                    
                    /* 关闭字节级调试输出 */
                    if (0) {  /* 禁用 */
                        uint8_t receivedByte = s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum];
                        SEGGER_RTT_WriteString(0, "[Byte");
                        {
                            char numStr[4];
                            uint32_t num = s_SifCb.receiveDataNum;
                            uint8_t i = 0U;
                            if (num == 0U) {
                                numStr[i++] = '0';
                            } else {
                                while (num > 0U && i < 3U) {
                                    numStr[i++] = (char)('0' + (num % 10U));
                                    num /= 10U;
                                }
                            }
                            numStr[i] = '\0';
                            {
                                uint8_t j;
                                for (j = 0U; j < i / 2U; j++) {
                                    char temp = numStr[j];
                                    numStr[j] = numStr[i - 1U - j];
                                    numStr[i - 1U - j] = temp;
                                }
                            }
                            SEGGER_RTT_WriteString(0, numStr);
                        }
                        SEGGER_RTT_WriteString(0, "] Received=0x");
                        {
                            char hexStr[3];
                            hexStr[0] = (char)((receivedByte >> 4) < 10U ? '0' + (receivedByte >> 4) : 'A' + (receivedByte >> 4) - 10U);
                            hexStr[1] = (char)((receivedByte & 0x0FU) < 10U ? '0' + (receivedByte & 0x0FU) : 'A' + (receivedByte & 0x0FU) - 10U);
                            hexStr[2] = '\0';
                            SEGGER_RTT_WriteString(0, hexStr);
                        }
                        SEGGER_RTT_WriteString(0, "\r\n");
                    }
                    
                    s_SifCb.receiveDataNum++;
                    s_SifCb.receiveBitNum = 0U;
                    
                    /* 关闭进度输出 */
                    if (0) {  /* 禁用 */
                        SEGGER_RTT_WriteString(0, "[SIF] Received ");
                        {
                            char numStr[8];
                            uint32_t num = s_SifCb.receiveDataNum;
                            uint8_t i = 0U;
                            if (num == 0U) {
                                numStr[i++] = '0';
                            } else {
                                while (num > 0U && i < 7U) {
                                    numStr[i++] = (char)('0' + (num % 10U));
                                    num /= 10U;
                                }
                            }
                            numStr[i] = '\0';
                            /* 反转字符串 */
                            {
                                uint8_t j;
                                for (j = 0U; j < i / 2U; j++) {
                                    char temp = numStr[j];
                                    numStr[j] = numStr[i - 1U - j];
                                    numStr[i - 1U - j] = temp;
                                }
                            }
                            SEGGER_RTT_WriteString(0, numStr);
                        }
                        SEGGER_RTT_WriteString(0, " bytes\r\n");
                    }
                    
                    if (s_SifCb.receiveDataNum == SIF_RX_DATA_NUM) {
                        /* 如果数据采集完毕 */
                        s_SifCb.state = SIF_STATE_END_SIGNAL;
                        /* 关闭状态转换日志 */
                    } else {
                        /* 初始化下一个字节为0 */
                        s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum] = 0x00U;
                    }
                } else {
                    /* 【修复点】一个字节还没接收完，左移当前字节为下一个bit腾出空间 */
                    /* 逻辑：第1个bit左移7次到bit7，第2个bit左移6次...第8个bit不左移留在bit0 */
                    /* 这里在receiveBitNum递增后左移，所以receiveBitNum=1-7时左移，receiveBitNum=8时不执行此分支 */
                    s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum] = 
                        s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum] << 1U;
                }
            }
            
            /* 检测结束信号：当前是低电平，持续2ms以上，且在字节边界 */
            if ((pinLevel == TRUE) && (s_SifIgnorePolledEndUntilHigh == FALSE) &&
                (timeCnt >= SIF_END_SIGNAL_TIME_NUM) && 
                (s_SifCb.receiveBitNum == 0U) && (s_SifCb.receiveDataNum > 0U)) {
                if (sif_ShouldCommitFrame() == TRUE) {
                /* 完整字节接收完成，检测到结束信号 */
                s_SifCb.startTimingFlag = FALSE;
                s_SifCb.levelTimeCnt = 0U;
                s_SifCb.state = SIF_STATE_INITIAL;
                s_SifIgnorePolledEndUntilHigh = FALSE;
                s_SifPinLowStable = sif_IsPinLow();
                s_SifPinDebounceCnt = 0U;
                
                /* 进行校验和验证 */
                sif_CheckSumHandle();
                
                /* 锁存本帧并置就绪 */
                sif_LatchReadyFrame();
                
               // SEGGER_RTT_WriteString(0, "[SIF] Frame END\r\n");
                
                if (s_SifCb.callback != NULL_PTR && s_SifCb.checkOk == TRUE) {
                    s_SifCb.callback();
                }
                } else {
                    s_SifIgnorePolledEndUntilHigh = TRUE;
                    s_SifCb.levelTimeCnt = 0U;
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
                    (void)SEGGER_RTT_WriteSkipNoLock(0,
                        "[SIF] ignore polled end: short frame vs header\r\n", 50U);
#endif
                }
            }
            break;
        }
        
        case SIF_STATE_END_SIGNAL: {
            /* END_SIGNAL状态已在脉宽检测中处理，这里直接返回INITIAL */
            s_SifIgnorePolledEndUntilHigh = FALSE;
            s_SifPinLowStable = sif_IsPinLow();
            s_SifPinDebounceCnt = 0U;
            s_SifCb.state = SIF_STATE_INITIAL;
            break;
        }
        
        case SIF_STATE_RESTART: {
            /* 重新接收数据状态 */
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
            (void)SEGGER_RTT_WriteSkipNoLock(0, "[SIF] RESTART->INIT\r\n", 21U);
#endif
            s_SifCb.startTimingFlag = FALSE;
            s_SifCb.levelTimeCnt = 0U;
            s_SifIgnorePolledEndUntilHigh = FALSE;
            s_SifPinLowStable = sif_IsPinLow();
            s_SifPinDebounceCnt = 0U;
            s_SifCb.state = SIF_STATE_INITIAL;
            break;
        }
        
        default: {
            /* 未知状态，重置为初始状态 */
            s_SifIgnorePolledEndUntilHigh = FALSE;
            s_SifPinLowStable = sif_IsPinLow();
            s_SifPinDebounceCnt = 0U;
            s_SifCb.state = SIF_STATE_INITIAL;
            break;
        }
    }
}

/**
 * @brief     校验和处理
 * 
 * @details   计算接收数据的校验和，验证数据正确性
 */
static void sif_CheckSumHandle(void)
{
    uint8_t i;
    uint8_t checkByte;
    uint32_t checkSum;
    
    checkSum = 0U;
    
    for (i = 0U; i < (s_SifCb.receiveDataNum - 1U); i++) {
        checkSum += (uint32_t)s_SifCb.receiveDataBuf[i];
    }
    
    checkByte = (uint8_t)(checkSum & 0xFFU);
    
    if (checkByte == s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum - 1U]) {
        /* 校验和正确 */
        s_SifCb.checkOk = TRUE;
    } else {
        /* 校验和失败，输出调试信息 */
        s_SifCb.checkOk = FALSE;
#if (SIF_RTT_LOG != 0U)
        SEGGER_RTT_WriteString(0, "[SIF] Checksum calc=0x");
        {
            char hexStr[3];
            hexStr[0] = (char)((checkByte >> 4) < 10U ? '0' + (checkByte >> 4) : 'A' + (checkByte >> 4) - 10U);
            hexStr[1] = (char)((checkByte & 0x0FU) < 10U ? '0' + (checkByte & 0x0FU) : 'A' + (checkByte & 0x0FU) - 10U);
            hexStr[2] = '\0';
            SEGGER_RTT_WriteString(0, hexStr);
        }
        SEGGER_RTT_WriteString(0, ", received=0x");
        {
            char hexStr[3];
            uint8_t recvChecksum = s_SifCb.receiveDataBuf[s_SifCb.receiveDataNum - 1U];
            hexStr[0] = (char)((recvChecksum >> 4) < 10U ? '0' + (recvChecksum >> 4) : 'A' + (recvChecksum >> 4) - 10U);
            hexStr[1] = (char)((recvChecksum & 0x0FU) < 10U ? '0' + (recvChecksum & 0x0FU) : 'A' + (recvChecksum & 0x0FU) - 10U);
            hexStr[2] = '\0';
            SEGGER_RTT_WriteString(0, hexStr);
        }
        SEGGER_RTT_WriteString(0, "\r\n");
#endif
    }
}

/*===========================================================================*/
/* Public Function Implementations                                          */
/*===========================================================================*/

/**
 * @brief     初始化一线通接收模块
 */
Std_ReturnType Sif_Init(void)
{
    Std_ReturnType retVal;
    int8_t gpioRet;
    
    /* 配置PC6引脚为输入模式 */
    /* 使用GPIO_PARA_INPUT配置（输入+无上下拉+开漏），适合一线通接收 */
    gpioRet = GPIO_config(SIF_RX_PIN, GPIO_PARA_INPUT);
    
    /* 调试：输出GPIO配置结果 */
#if (SIF_RTT_LOG != 0U)
    if (gpioRet == GPIO_OK) {
        SEGGER_RTT_WriteString(0, "[SIF] PC6 GPIO config OK\r\n");
    } else {
        SEGGER_RTT_WriteString(0, "[SIF] PC6 GPIO config FAILED!\r\n");
    }
    
    /* 验证引脚配置：读取初始引脚电平 */
    {
        uint32_t pinLevel;
        pinLevel = GPIO_Get_inputLevel(SIF_RX_PIN);
        SEGGER_RTT_WriteString(0, "[SIF] PC6 initial level: ");
        if (pinLevel == GPIO_PIN_RESET) {
            SEGGER_RTT_WriteString(0, "LOW (with pull-up, should be HIGH if no signal)\r\n");
        } else {
            SEGGER_RTT_WriteString(0, "HIGH (OK)\r\n");
        }
    }
#else
    (void)gpioRet;
#endif
    
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
    s_SifRttIsrTick = 0U;
    s_SifRttPinEdgesWindow = 0U;
    s_SifRttPinSampleValid = FALSE;
#endif

    /* 初始化控制块 */
    s_SifCb.state = SIF_STATE_INITIAL;
    s_SifCb.receiveBitNum = 0U;
    s_SifCb.receiveDataNum = 0U;
    s_SifCb.levelTimeCnt = 0U;
    s_SifCb.startTimingFlag = FALSE;
    s_SifCb.hasReadBit = FALSE;
    s_SifCb.dataReady = FALSE;
    s_SifCb.checkOk = FALSE;
    s_SifCb.readyDataNum = 0U;
    s_SifCb.callback = NULL_PTR;
    s_SifIgnorePolledEndUntilHigh = FALSE;
    s_SifPinLowStable = sif_IsPinLow();
    s_SifPinDebounceCnt = 0U;
    
    /* 初始化接收数据缓冲区与就绪快照 */
    {
        uint8_t i;
        for (i = 0U; i < SIF_RX_DATA_NUM; i++) {
            s_SifCb.receiveDataBuf[i] = 0U;
            s_SifCb.readyDataBuf[i] = 0U;
        }
    }
    
    /* 注意：TickTimer需要在外部初始化，使用sif_TimerCallback作为回调 */
    /* 例如：TickTimer_Init(1, sif_TimerCallback); */
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
    (void)SEGGER_RTT_WriteSkipNoLock(0,
        "[SIF] Init done: app must TickTimer_Init(1, Sif_GetTimerCallback) 50us\r\n", 72U);
#endif
    
    retVal = E_OK;
    
    return retVal;
}

/**
 * @brief     反初始化一线通接收模块
 */
Std_ReturnType Sif_DeInit(void)
{
    Std_ReturnType retVal;
    
    /* 停止定时器 */
    TickTimer_DeInit();
    
    /* 清除控制块 */
    s_SifCb.state = SIF_STATE_INITIAL;
    s_SifCb.callback = NULL_PTR;
    s_SifCb.dataReady = FALSE;
    s_SifCb.readyDataNum = 0U;
    
    retVal = E_OK;
    
    return retVal;
}

/**
 * @brief     注册数据接收完成回调函数
 */
Std_ReturnType Sif_RegisterCallback(SifCallback_t callback)
{
    Std_ReturnType retVal;
    
    if (callback == NULL_PTR) {
        retVal = E_NOT_OK;
    } else {
        s_SifCb.callback = callback;
        retVal = E_OK;
    }
    
    return retVal;
}

/**
 * @brief     获取接收到的数据
 */
Std_ReturnType Sif_GetReceivedData(uint8_t* const buffer, uint16_t size)
{
    Std_ReturnType retVal;
    uint8_t i;
    
    if (buffer == NULL_PTR) {
        retVal = E_NOT_OK;
    } else if (size < SIF_RX_DATA_NUM) {
        retVal = E_NOT_OK;
    } else {
        /* 复制数据：就绪时读快照，避免与下一帧接收竞态 */
        if (s_SifCb.dataReady == TRUE) {
            for (i = 0U; i < SIF_RX_DATA_NUM; i++) {
                buffer[i] = s_SifCb.readyDataBuf[i];
            }
        } else {
            for (i = 0U; i < SIF_RX_DATA_NUM; i++) {
                buffer[i] = s_SifCb.receiveDataBuf[i];
            }
        }
        
        retVal = E_OK;
    }
    
    return retVal;
}

/**
 * @brief     检查是否有新数据接收完成
 */
boolean Sif_IsDataReady(void)
{
    return s_SifCb.dataReady;
}

/**
 * @brief     清除数据就绪标志
 */
Std_ReturnType Sif_ClearDataReady(void)
{
    Std_ReturnType retVal;
    
    s_SifCb.dataReady = FALSE;
    s_SifCb.readyDataNum = 0U;
    retVal = E_OK;
    
    return retVal;
}

/**
 * @brief     获取实际接收到的数据长度
 */
uint8_t Sif_GetReceivedDataLength(void)
{
    if (s_SifCb.dataReady == TRUE) {
        return s_SifCb.readyDataNum;
    }
    return s_SifCb.receiveDataNum;
}

/**
 * @brief     获取当前接收状态
 */
SifReceiveState_e Sif_GetReceiveState(void)
{
    return s_SifCb.state;
}

/**
 * @brief     获取当前接收到的数据字节数
 */
uint8_t Sif_GetCurrentReceiveCount(void)
{
    return s_SifCb.receiveDataNum;
}

/**
 * @brief     获取定时器回调函数指针
 */
TickTimerCallback_t Sif_GetTimerCallback(void)
{
    return sif_TimerCallback;
}

void Sif_Service(void)
{
    /* 检查一线通数据是否接收完成 */
    if (Sif_IsDataReady() == TRUE)
    {
        uint8_t sifData[SIF_RX_DATA_NUM];
        uint8_t actualDataLen;
        
        /* 获取实际接收到的数据长度 */
        actualDataLen = Sif_GetReceivedDataLength();
        
        /* 获取接收到的数据 */
        if (Sif_GetReceivedData(sifData, SIF_RX_DATA_NUM) == E_OK)
        {
            SifProtocolPacket_t packet;
            Std_ReturnType pr;

#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
            {
                char line[112];
                uint8_t p = 0U;
                uint8_t j;
                uint8_t n;
                uint8_t b;
                const char *hdr = "[SIF] SVC raw len=";
                const char *sep = " B, data:";
                for (j = 0U; (hdr[j] != '\0') && (p < 92U); j++) {
                    line[p++] = hdr[j];
                }
                sif_RttPutU32(line, &p, 112U, (uint32_t)actualDataLen);
                for (j = 0U; (sep[j] != '\0') && (p < 100U); j++) {
                    line[p++] = sep[j];
                }
                if (p < 100U) {
                    line[p++] = ' ';
                }
                n = actualDataLen;
                if (n > 8U) {
                    n = 8U;
                }
                for (j = 0U; (j < n) && (p < 104U); j++) {
                    b = sifData[j];
                    line[p++] = (char)(((b >> 4U) < 10U) ? ('0' + (b >> 4U)) : ('A' + (b >> 4U) - 10U));
                    line[p++] = (char)(((b & 0x0FU) < 10U) ? ('0' + (b & 0x0FU)) : ('A' + (b & 0x0FU) - 10U));
                    line[p++] = (char)' ';
                }
                line[p++] = '\r';
                line[p++] = '\n';
                (void)SEGGER_RTT_WriteSkipNoLock(0, line, (unsigned)p);
            }
#endif
            
            /* 解析协议数据包 - 使用实际接收到的数据长度 */
            pr = SifProtocol_Parse(sifData, actualDataLen, &packet);
            if (pr == E_OK) {
                /* 根据解析结果更新各 TAG 全局数据，供其他模块通过 Get 接口获取 */
                SifProtocol_UpdateFromPacket(&packet);
#if (SIF_RTT_LOG != 0U)
                SifProtocol_RttLogParsedPacket(&packet);
#endif
            }
#if ((SIF_RTT_LOG != 0U) && (SIF_RTT_DEBUG != 0U))
            else {
                (void)SEGGER_RTT_WriteSkipNoLock(0, "[SIF] SVC SifProtocol_Parse fail\r\n", 36U);
            }
#endif
            
            /* 清除数据就绪标志，准备接收下一帧 */
            Sif_ClearDataReady();
        }
    }
}