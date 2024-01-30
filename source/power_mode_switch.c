/*
 * Copyright 2022-2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */



#include "fsl_cmc.h"
#include "fsl_spc.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "power_mode_switch.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_lpuart.h"
#include "fsl_lptmr.h"
#include "fsl_wuu.h"
#include "fsl_vbat.h"
#include "fsl_gpio.h"
#include "fsl_cache_lpcac.h"
#include "fsl_port.h"

#include "fsl_wwdt.h"
#include <stdbool.h>


/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_POWER_MODE_NAME                                          \
    {                                                                \
        "Active", "Sleep", "DeepSleep", "PowerDown" \
    }

#define APP_POWER_MODE_DESC                                                                                         \
    {                                                                                                               \
        "Acitve: Core clock is 48MHz, power consumption is about 7.8 mA.  ",                                        \
            "Sleep: CPU0 clock is off, System and Bus clock remain ON, power consumption is about 5.85 mA. ",       \
            "Deep Sleep: Core/System/Bus clock are gated off. ",                                                    \
            "Power Down: Core/System/Bus clock are gated off, both CORE_MAIN and CORE_WAKE power domains are put "  \
            "into state retention mode."                                                                           \
    }

#define APP_CMC           CMC0
#define APP_RAM_ARRAYS_DS (0x3F0077FE)
#define APP_RAM_ARRAYS_PD (0x3F0077FE)


#define APP_SPC                           SPC0
#define APP_SPC_LPTMR_LPISO_VALUE         (0x1EU) /* VDD_USB, VDD_P2, VDD_P3, VDD_P4. */
#define APP_SPC_LPTMR_ISO_DOMAINS         "VDD_USB, VDD_P2, VDD_P3, VDD_P4"
#define APP_SPC_MAIN_POWER_DOMAIN         (kSPC_PowerDomain0)
#define APP_SPC_WAKE_POWER_DOMAIN         (kSPC_PowerDomain1)

#define APP_VBAT             VBAT0
#define APP_VBAT_IRQN        VBAT0_IRQn
#define APP_VBAT_IRQ_HANDLER VBAT0_IRQHandler

/* TO UPDATE */
#define APP_DEBUG_CONSOLE_RX_PORT   PORT1
#define APP_DEBUG_CONSOLE_RX_GPIO   GPIO1
#define APP_DEBUG_CONSOLE_RX_PIN    8U
#define APP_DEBUG_CONSOLE_RX_PINMUX kPORT_MuxAlt2
/* TO UPDATE */
#define APP_DEBUG_CONSOLE_TX_PORT   PORT1
#define APP_DEBUG_CONSOLE_TX_GPIO   GPIO1
#define APP_DEBUG_CONSOLE_TX_PIN    9U
#define APP_DEBUG_CONSOLE_TX_PINMUX kPORT_MuxAlt2

#define WWDT                WWDT0
#define APP_WDT_IRQn        WWDT0_IRQn
#define APP_WDT_IRQ_HANDLER WWDT0_IRQHandler
#define WDT_CLK_FREQ        CLOCK_GetWdtClkFreq(0)
#define IS_WWDT_RESET       (0 != (CMC0->SRS & CMC_SRS_WWDT0_MASK))


/*******************************************************************************
 * Prototypes
 ******************************************************************************/
void APP_InitDebugConsole(void);
void APP_DeinitDebugConsole(void);
static void APP_SetSPCConfiguration(void);
static void APP_SetVBATConfiguration(void);
static void APP_SetCMCConfiguration(void);

static void APP_PowerPostSwitchHook(void);

static void APP_EnterSleepMode(void);
static void APP_EnterDeepSleepMode(void);
static void APP_EnterPowerDownMode(void);
static void APP_PowerModeSwitch(app_power_mode_t targetPowerMode);
static app_power_mode_t APP_GetTargetPowerMode(void);

 void APP_WDT_IRQ_HANDLER(void);
 void delayWwdtWindow(void);

 void init_Wwd(void);

/*******************************************************************************
 * Variables
 ******************************************************************************/

char *const g_modeNameArray[] = APP_POWER_MODE_NAME;
char *const g_modeDescArray[] = APP_POWER_MODE_DESC;

 wwdt_config_t config;

/*******************************************************************************
 * Code
 ******************************************************************************/
 void APP_WDT_IRQ_HANDLER(void)
{
    uint32_t wdtStatus = WWDT_GetStatusFlags(WWDT);

    /* The chip will reset before this happens */
    if (wdtStatus & kWWDT_TimeoutFlag)
    {
        WWDT_ClearStatusFlags(WWDT, kWWDT_TimeoutFlag);
    }

    /* Handle warning interrupt */
    if (wdtStatus & kWWDT_WarningFlag)
    {
        /* A watchdog feed didn't occur prior to warning timeout */
        WWDT_ClearStatusFlags(WWDT, kWWDT_WarningFlag);
        /* User code. User can do urgent case before timeout reset.
         * IE. user can backup the ram data or ram log to flash.
         * the period is set by config.warningValue, user need to
         * check the period between warning interrupt and timeout.
         */
    }

#if (defined(LPC55S36_WORKAROUND) && LPC55S36_WORKAROUND)
    /* Set PMC register value that could run with GDET enable */
    PMC->LDOPMU   = 0x0109CF18;
    PMC->DCDC0    = 0x010A767E;
    PMC->LDOCORE0 = 0x2801006B;
#endif
    SDK_ISR_EXIT_BARRIER;
}

void delayWwdtWindow(void)
{
    /* For the TV counter register value will decrease after feed watch dog,
     * we can use it to as delay. But in user scene, user need feed watch dog
     * in the time period after enter Window but before warning intterupt.
     */
    while (WWDT->TV > WWDT->WINDOW)
    {
        __NOP();
    }
}

 void init_Wwd(void)
 {
    uint32_t wdtFreq;
    bool timeOutResetEnable;

    /* Enable the WWDT time out to reset the CPU. */
    timeOutResetEnable = true;

    /* Check if reset is due to Watchdog */
#ifdef IS_WWDT_RESET
    if (IS_WWDT_RESET)
#else
    if (WWDT_GetStatusFlags(WWDT) & kWWDT_TimeoutFlag)
#endif
    {
        PRINTF("Watchdog reset occurred\r\n");
        timeOutResetEnable = false;
    /* The timeout flag can only clear when and after wwdt intial. */
    }

     /* wdog refresh test in window mode/timeout reset */
    PRINTF("\r\n--- %s test start ---\r\n", (timeOutResetEnable) ? "Time out reset" : "Window mode refresh");

    /* The WDT divides the input frequency into it by 4 */
    wdtFreq = WDT_CLK_FREQ / 4;

    WWDT_GetDefaultConfig(&config);


    config.timeoutValue = wdtFreq * 4;
    config.warningValue = 512;
    config.windowValue  = wdtFreq * 1;
    /* Configure WWDT to reset on timeout */
    config.enableWatchdogReset = true;
    /* Setup watchdog clock frequency(Hz). */
    config.clockFreq_Hz = WDT_CLK_FREQ;
    WWDT_Init(WWDT, &config);

    NVIC_EnableIRQ(APP_WDT_IRQn);
 }

void APP_InitDebugConsole(void)
{
    /*
     * Debug console RX pin is set to disable for current leakage, need to re-configure pinmux.
     * Debug console TX pin: Don't need to change.
     */
    BOARD_InitPins();
    BOARD_BootClockFROHF48M();
    BOARD_InitDebugConsole();
}

void APP_DeinitDebugConsole(void)
{
    DbgConsole_Deinit();
    PORT_SetPinMux(APP_DEBUG_CONSOLE_RX_PORT, APP_DEBUG_CONSOLE_RX_PIN, kPORT_PinDisabledOrAnalog);
    PORT_SetPinMux(APP_DEBUG_CONSOLE_TX_PORT, APP_DEBUG_CONSOLE_TX_PIN, kPORT_PinDisabledOrAnalog);
}

void APP_VBAT_IRQ_HANDLER(void)
{
    if (VBAT_GetStatusFlags(APP_VBAT) & kVBAT_StatusFlagWakeupPin)
    {
        VBAT_ClearStatusFlags(APP_VBAT, kVBAT_StatusFlagWakeupPin);
    }
}


void main(void)
{
    app_power_mode_t targetPowerMode;

    BOARD_InitPins();
    BOARD_BootClockFROHF48M();
    BOARD_InitDebugConsole();

    /* Set clock divider for WWDT clock source. */
    CLOCK_SetClkDiv(kCLOCK_DivWdt0Clk, 1U);

    /* Enable FRO 1M clock for WWDT module. */
    SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_FRO1MHZ_CLK_ENA_MASK;

    /* Release the I/O pads and certain peripherals to normal run mode state, for in Power Down mode
     * they will be in a latched state. */
    if ((CMC_GetSystemResetStatus(APP_CMC) & kCMC_WakeUpReset) != 0UL)
    {
        SPC_ClearPeriphIOIsolationFlag(APP_SPC);
    }

    APP_SetVBATConfiguration();
    APP_SetSPCConfiguration();

    PRINTF("\r\nNormal Boot.\r\n");

    while (1)
    {
        if ((CMC_GetSystemResetStatus(APP_CMC) & kCMC_WakeUpReset) != 0UL)
        {
            /* Close ISO flags. */
            SPC_ClearPeriphIOIsolationFlag(APP_SPC);
        }

        /* Clear CORE_MAIN power domain's low power request flag. */
        SPC_ClearPowerDomainLowPowerRequestFlag(APP_SPC, APP_SPC_MAIN_POWER_DOMAIN);
        /* Clear CORE_WAKE power domain's low power request flag. */
        SPC_ClearPowerDomainLowPowerRequestFlag(APP_SPC, APP_SPC_WAKE_POWER_DOMAIN);
        SPC_ClearLowPowerRequest(APP_SPC);

        /* Normal start. */
        APP_SetCMCConfiguration();

        PRINTF("\r\n###########################    Power Mode Switch Demo    ###########################\r\n");
        PRINTF("    Power mode: Active\r\n");
        targetPowerMode = APP_GetTargetPowerMode();

            APP_PowerModeSwitch(targetPowerMode);
            APP_PowerPostSwitchHook();

        PRINTF("\r\nNext loop.\r\n");
    }
}

static void APP_SetSPCConfiguration(void)
{
    status_t status;

    spc_active_mode_regulators_config_t activeModeRegulatorOption;

    /* Disable all modules that controlled by SPC in active mode.. */
    SPC_DisableActiveModeAnalogModules(APP_SPC, kSPC_controlAllModules);

    /* Disable LVDs and HVDs */
    SPC_EnableActiveModeCoreHighVoltageDetect(APP_SPC, false);
    SPC_EnableActiveModeCoreLowVoltageDetect(APP_SPC, false);
    SPC_EnableActiveModeSystemHighVoltageDetect(APP_SPC, false);
    SPC_EnableActiveModeSystemLowVoltageDetect(APP_SPC, false);
    SPC_EnableActiveModeIOHighVoltageDetect(APP_SPC, false);
    SPC_EnableActiveModeIOLowVoltageDetect(APP_SPC, false);

    activeModeRegulatorOption.bandgapMode = kSPC_BandgapEnabledBufferDisabled;
    activeModeRegulatorOption.lpBuff      = false;
    /* DCDC output voltage is 1.1V in active mode. */
    activeModeRegulatorOption.DCDCOption.DCDCVoltage           = kSPC_DCDC_NormalVoltage;
    activeModeRegulatorOption.DCDCOption.DCDCDriveStrength     = kSPC_DCDC_NormalDriveStrength;
    activeModeRegulatorOption.SysLDOOption.SysLDOVoltage       = kSPC_SysLDO_NormalVoltage;
    activeModeRegulatorOption.SysLDOOption.SysLDODriveStrength = kSPC_SysLDO_LowDriveStrength;
    activeModeRegulatorOption.CoreLDOOption.CoreLDOVoltage     = kSPC_CoreLDO_NormalVoltage;
#if defined(FSL_FEATURE_SPC_HAS_CORELDO_VDD_DS) && FSL_FEATURE_SPC_HAS_CORELDO_VDD_DS
    activeModeRegulatorOption.CoreLDOOption.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength;
#endif /* FSL_FEATURE_SPC_HAS_CORELDO_VDD_DS */

    status = SPC_SetActiveModeRegulatorsConfig(APP_SPC, &activeModeRegulatorOption);
    /* Disable Vdd Core Glitch detector in active mode. */
    SPC_DisableActiveModeVddCoreGlitchDetect(APP_SPC, true);
    if (status != kStatus_Success)
    {
        PRINTF("Fail to set regulators in Active mode.");
        return;
    }
    while (SPC_GetBusyStatusFlag(APP_SPC))
        ;

    SPC_DisableLowPowerModeAnalogModules(APP_SPC, kSPC_controlAllModules);
    SPC_SetLowPowerWakeUpDelay(APP_SPC, 0xFF);
    spc_lowpower_mode_regulators_config_t lowPowerRegulatorOption;

    lowPowerRegulatorOption.lpIREF      = false;
    lowPowerRegulatorOption.bandgapMode = kSPC_BandgapDisabled;
    lowPowerRegulatorOption.lpBuff      = false;
    /* Enable Core IVS, which is only useful in power down mode.  */
    lowPowerRegulatorOption.CoreIVS = true;
    /* DCDC output voltage is 1.0V in some low power mode(Deep sleep, Power Down). DCDC is disabled in Deep Power Down.
     */
    lowPowerRegulatorOption.DCDCOption.DCDCVoltage             = kSPC_DCDC_MidVoltage;
    lowPowerRegulatorOption.DCDCOption.DCDCDriveStrength       = kSPC_DCDC_LowDriveStrength;
    lowPowerRegulatorOption.SysLDOOption.SysLDODriveStrength   = kSPC_SysLDO_LowDriveStrength;
    lowPowerRegulatorOption.CoreLDOOption.CoreLDOVoltage       = kSPC_CoreLDO_MidDriveVoltage;
    lowPowerRegulatorOption.CoreLDOOption.CoreLDODriveStrength = kSPC_CoreLDO_LowDriveStrength;

    status = SPC_SetLowPowerModeRegulatorsConfig(APP_SPC, &lowPowerRegulatorOption);
    /* Disable Vdd Core Glitch detector in low power mode. */
    SPC_DisableLowPowerModeVddCoreGlitchDetect(APP_SPC, true);
    if (status != kStatus_Success)
    {
        PRINTF("Fail to set regulators in Low Power Mode.");
        return;
    }
    while (SPC_GetBusyStatusFlag(APP_SPC))
        ;

    /* Disable LDO_CORE since it is bypassed. */
    SPC_EnableCoreLDORegulator(APP_SPC, false);

    /* Enable Low power request output to observe the entry/exit of
     * low power modes(including: deep sleep mode, power down mode, and deep power down mode).
     */
    spc_lowpower_request_config_t lpReqConfig = {
        .enable   = true,
        .override = kSPC_LowPowerRequestNotForced,
        .polarity = kSPC_LowTruePolarity,
    };

    SPC_SetLowPowerRequestConfig(APP_SPC, &lpReqConfig);
}

static void APP_SetVBATConfiguration(void)
{
    if (VBAT_CheckFRO16kEnabled(APP_VBAT) == false)
    {
        /* In case of FRO16K is not enabled, enable it firstly. */
        VBAT_EnableFRO16k(APP_VBAT, true);
    }
    VBAT_UngateFRO16k(APP_VBAT, kVBAT_EnableClockToVddSys);

    /* Disable Bandgap to save current consumption. */
    if (VBAT_CheckBandgapEnabled(APP_VBAT))
    {
        VBAT_EnableBandgapRefreshMode(APP_VBAT, false);
        VBAT_EnableBandgap(APP_VBAT, false);
    }
}

static void APP_SetCMCConfiguration(void)
{
    /* Disable low power debug. */
    CMC_EnableDebugOperation(APP_CMC, false);
    /* Allow all power mode */
    CMC_SetPowerModeProtection(APP_CMC, kCMC_AllowAllLowPowerModes);

    /* Disable flash memory accesses and place flash memory in low-power state whenever the core clock
       is gated. And an attempt to access the flash memory will cause the flash memory to exit low-power
       state for the duration of the flash memory access. */
    CMC_ConfigFlashMode(APP_CMC, true, true, false);
}

static app_power_mode_t APP_GetTargetPowerMode(void)
{
    uint8_t ch;

    app_power_mode_t inputPowerMode;

    do
    {
        PRINTF("\r\nSelect the desired operation \n\r\n");
        for (app_power_mode_t modeIndex = kAPP_PowerModeActive; modeIndex <= kAPP_PowerModePowerDown; modeIndex++)
        {
            PRINTF("\tPress %c to enter: %s mode\r\n", modeIndex,
                   g_modeNameArray[(uint8_t)(modeIndex - kAPP_PowerModeActive)]);
        }

        PRINTF("\r\nWaiting for power mode select...\r\n\r\n");

        ch = GETCHAR();

        if ((ch >= 'a') && (ch <= 'z'))
        {
            ch -= 'a' - 'A';
        }
        inputPowerMode = (app_power_mode_t)ch;

        if ((inputPowerMode > kAPP_PowerModePowerDown) || (inputPowerMode < kAPP_PowerModeActive))
        {
            PRINTF("Wrong Input!");
        }
    } while (inputPowerMode > kAPP_PowerModePowerDown);

    PRINTF("\t%s\r\n", g_modeDescArray[(uint8_t)(inputPowerMode - kAPP_PowerModeActive)]);

    return inputPowerMode;
}

static void APP_PowerPostSwitchHook(void)
{
    APP_InitDebugConsole();
}

static void APP_PowerModeSwitch(app_power_mode_t targetPowerMode)
{
    if (targetPowerMode != kAPP_PowerModeActive)
    {
        switch (targetPowerMode)
        {
            case kAPP_PowerModeSleep:
                init_Wwd();
                APP_EnterSleepMode();
                break;
            case kAPP_PowerModeDeepSleep:
                init_Wwd();
                APP_EnterDeepSleepMode();
                break;
            case kAPP_PowerModePowerDown:
                init_Wwd();
                APP_EnterPowerDownMode();
                break;
            default:
                assert(false);
                break;
        }
    }
}

static void APP_EnterSleepMode(void)
{
    cmc_power_domain_config_t config;

    config.clock_mode  = kCMC_GateCoreClock;
    config.main_domain = kCMC_ActiveOrSleepMode;
    config.wake_domain = kCMC_ActiveOrSleepMode;

    /* Switch as ROSC before entering into sleep mode, and disable other clock sources to decrease current consumption.
     */
    CMC_EnterLowPowerMode(APP_CMC, &config);
}

static void APP_EnterDeepSleepMode(void)
{
    cmc_power_domain_config_t config;

    config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
    config.main_domain = kCMC_DeepSleepMode;
    config.wake_domain = kCMC_ActiveOrSleepMode;

    /* Power off some RAM blocks.  */
    CMC_PowerOffSRAMLowPowerOnly(APP_CMC, APP_RAM_ARRAYS_DS);

    CMC_EnterLowPowerMode(APP_CMC, &config);
}

static void APP_EnterPowerDownMode(void)
{
    cmc_power_domain_config_t config;

    config.clock_mode  = kCMC_GateAllSystemClocksEnterLowPowerMode;
    config.main_domain = kCMC_PowerDownMode;
    config.wake_domain = kCMC_ActiveOrSleepMode;

    /* Power off some RAM blocks. */
    CMC_PowerOffSRAMLowPowerOnly(APP_CMC, APP_RAM_ARRAYS_PD);
    L1CACHE_InvalidateCodeCache();
    /* Enable CORE VDD Internal Voltage scaling to decrease current consumption in power down mode. */
    SPC_EnableLowPowerModeCoreVDDInternalVoltageScaling(APP_SPC, true);
    CMC_EnterLowPowerMode(APP_CMC, &config);
}

