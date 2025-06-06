/*
 * Copyright (c) 2022-2024, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  ======== PowerCC27XX.c ========
 */

#include <stdbool.h>

#include <ti/drivers/dpl/HwiP.h>
#include <ti/drivers/dpl/ClockP.h>
#include <ti/drivers/dpl/DebugP.h>

#include <ti/drivers/Power.h>
#include <ti/drivers/Temperature.h>
#include <ti/drivers/GPIO.h>

#include <ti/drivers/utils/Math.h>

#include <ti/log/Log.h>

#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(inc/hw_types.h)
#include DeviceFamily_constructPath(inc/hw_memmap.h)
#include DeviceFamily_constructPath(inc/hw_ints.h)
#include DeviceFamily_constructPath(inc/hw_clkctl.h)
#include DeviceFamily_constructPath(inc/hw_evtsvt.h)
#include DeviceFamily_constructPath(inc/hw_evtull.h)
#include DeviceFamily_constructPath(inc/hw_ckmd.h)
#include DeviceFamily_constructPath(inc/hw_rtc.h)
#include DeviceFamily_constructPath(inc/hw_systim.h)
#include DeviceFamily_constructPath(inc/hw_pmctl.h)
#include DeviceFamily_constructPath(inc/hw_pmud.h)
#include DeviceFamily_constructPath(inc/hw_sys0.h)
#include DeviceFamily_constructPath(inc/hw_ioc.h)
#include DeviceFamily_constructPath(inc/hw_fcfg.h)
#include DeviceFamily_constructPath(driverlib/cpu.h)
#include DeviceFamily_constructPath(driverlib/ckmd.h)
#include DeviceFamily_constructPath(driverlib/hapi.h)
#include DeviceFamily_constructPath(driverlib/gpio.h)
#include DeviceFamily_constructPath(driverlib/lrfd.h)
#include DeviceFamily_constructPath(driverlib/pmctl.h)
#include DeviceFamily_constructPath(cmsis/core/cmsis_compiler.h)

/* Forward declarations */
int_fast16_t PowerCC27XX_notify(uint_fast16_t eventType);
static void PowerCC27XX_oscillatorISR(uintptr_t arg);
static void PowerCC27XX_rtcISR(uintptr_t arg);
static void PowerCC27XX_enterStandby(void);
static void PowerCC27XX_setDependencyCount(Power_Resource resourceId, uint8_t count);
static void PowerCC27XX_startHFXT(void);
bool PowerCC27XX_isValidResourceId(Power_Resource resourceId);

static void PowerCC27XX_hfxtAmpsettledTimeout(uintptr_t arg);
static void PowerCC27XX_initialHfxtAmpCompClockCb(uintptr_t searchDone);
static void PowerCC27XX_forceHfxtFsmToRamp1(void);
static void PowerCC27XX_startContHfxtAmpMeasurements(void);
static void PowerCC27XX_stopContHfxtAmpMeasurements(void);
static uint32_t PowerCC27XX_getHfxtAmpMeasurement(void);

static void PowerCC27XX_hfxtCompensateFxn(int16_t currentTemperature,
                                          int16_t thresholdTemperature,
                                          uintptr_t clientArg,
                                          Temperature_NotifyObj *notifyObject);
static uint32_t PowerCC27XX_temperatureToRatio(int16_t temperature);
static void PowerCC27XX_updateHFXTRatio(uint32_t ratio);

/* Externs */
extern const PowerCC27XX_Config PowerCC27XX_config;
extern const uint_least8_t GPIO_pinLowerBound;
extern const uint_least8_t GPIO_pinUpperBound;
extern const uint_least8_t PowerLPF3_extlfPin;
extern const uint_least8_t PowerLPF3_extlfPinMux;

/* Macro for weak definition of the Power Log module */
Log_MODULE_DEFINE_WEAK(LogModule_Power, {0});

/* Function Macros */
#define IOC_ADDR(index) (IOC_BASE + IOC_O_IOC0 + (sizeof(uint32_t) * index))

/* Macro used to extract the resource group  from a resource ID */
#define RESOURCE_GROUP(resourceId) ((resourceId)&PowerCC27XX_PERIPH_GROUP_M)

/* Macro used to extract the bit index shift encoded from a resource ID */
#define RESOURCE_BIT_INDEX(resourceId) ((resourceId)&PowerCC27XX_PERIPH_BIT_INDEX_M)

#define HFXT_COMP_MAX_TEMP (125)
#define HFXT_COMP_MIN_TEMP (-40)

/* Workaround for missing enums for CKMD_HFTRACKCTL_RATIO */
#define CKMD_HFTRACKCTL_RATIO_REF48M 0x00400000U

/* Workaround for missing enums for CKMD_AFTRACKCTL_RATIO */
#define CKMD_AFTRACKCTL_RATIO__90P3168MHZ 0x0880DEE9U
#define CKMD_AFTRACKCTL_RATIO__98P304MHZ  0x07D00000U

/* Magic VGMCFG register unlock value */
#define SYS0_VGMCFG_KEY_UNLOCK 0x5AU

/* Timeout value used to detect if HFXT FSM is stuck in RAMP0 state */
#define HFXT_AMP_COMP_START_TIMEOUT_US 500

/* Time to wait after changing HFXTTARG.IREF and measuring the resulting
 * HFXT amplitude
 */
#define HFXT_AMP_COMP_MEASUREMENT_US 1000

/* The limits for the allowed target IREF values to be passed to
 * CKMDSetTargetIrefTrim()
 */
#define HFXT_TARGET_IREF_MAX 8
#define HFXT_TARGET_IREF_MIN 3

/* Initial LFINC value for the LFINC filter. The value is representing the
 * nominal LFOSC frequency of 32.768 kHz in us with 16 fractional bits.
 */
#define LFINCCTL_INT_START_VALUE 0x1E8480

/* Type definitions */

/*! Type used for passing configuration information to HFXT temperature
 * notification callback
 */
typedef union
{
    struct
    {
        int16_t delta;
        int16_t threshold;
    } temperature;
    uint32_t value;
} PowerCC27XX_hfxtConfig;

/* Static Globals */

/*! Temperature notification to compensate the HFXT */
static Temperature_NotifyObj PowerCC27XX_hfxtCompNotifyObj = {0};

/* Initialize value to the CKMD:HFTRACKCTL.RATIO field reset value */
static uint32_t hfxtCompRatio = CKMD_HFTRACKCTL_RATIO_REF48M;

/*! Temperature compensation coefficients for HFXT */
static struct
{
    int32_t P0;
    int32_t P1;
    int32_t P2;
    int32_t P3;
    uint8_t shift;
} PowerCC27XX_hfxtCompCoefficients;

/* Global state variable to track if HFXT compensation is enabled or not.
 * It is used to check whether temperature notifications should be re-enabled
 * after an update or not, in case compensation has been asynchronously disabled
 */
static bool PowerCC27XX_hfxtCompEnabled = false;

/* Array to maintain constraint reference counts */
static uint8_t constraintCounts[PowerCC27XX_NUMCONSTRAINTS];

/* Mask of Power constraints for quick access */
static uint32_t constraintMask = 0;

/* Arrays to maintain resource dependency reference counts.
 * Each resource group will have an array associated with it, and the arrays can
 * be indexed using the bit index shift value encoded in the resource ID.
 */
static uint8_t resourceCountsClkctl0[PowerCC27XX_NUMRESOURCES_CLKCTL0];
static uint8_t resourceCountsClkctl1[PowerCC27XX_NUMRESOURCES_CLKCTL1];
static uint8_t resourceCountsLrfd[PowerCC27XX_NUMRESOURCES_LRFD];

/* Keeps track of the configured Power policy. Power_idleFunc() will not run
 * the policy if this is set to NULL
 */
static Power_PolicyFxn policyFxn = NULL;

/* Is the Power policy enabled? */
static bool isPolicyEnabled = false;

/* Has the Power driver been initialized */
static bool isInitialized = false;

/* Power state of the system. Idle, active, standby, etc */
static uint8_t powerState = Power_ACTIVE;

/* Event notification list */
static List_List notifyList;

/* Interrupt for handling clock switching */
static HwiP_Struct ckmHwi;

/* Function to be called to start the initial HFXT Amplitude compensation on the
 * next AMPSETTLED interrupt.
 * Will be NULL if initial compensation should not be
 * be performed. I.e. if it has already been performed or if it is not enabled.
 */
static PowerLPF3_StartInitialHfxtAmpCompFxn startInitialHfxtAmpCompFxn = NULL;

/* Clock object used by HFXT amplitude compensation. It is reused for multiple
 * purposes. The callback function of the ClockP object will be changed
 * dynamically, depending on the use case. The different use cases are described
 * below:
 * - Currently waiting for the AMPSETTLED interrupt, after starting HFXT
 *   - Callback function: PowerCC27XX_hfxtAmpsettledTimeout
 *   - Used for timeout to detect if HFXT FSM gets stuck in RAMP0 state
 * - Currently performing the initial HFXT amplitude compensation, where we are
 *   doing a full search for the optimal HFXTTARG.IREF value,
 *   after the AMPSETTLED signal and before the PowerLPF3_HFXT_AVAILABLE
 *   notification.
 *   - Callback function: PowerCC27XX_initialHfxtAmpCompClockCb
 *   - The clock is used to schedule measurements and evaluations of the HFXT
 *     amplitude.
 * - After PowerLPF3_HFXT_AVAILABLE notification:
 *   - Callback function: NULL
 *   - Currently not used. Could potentially be used to periodically check if
 *     amplitude adjustments are needed.
 *
 * Dynamically changing the callback function is done instead of having a common
 * callback function, to be able to only link the functions that are actually
 * used. For example, if the initial HFXT amplitude compensation at boot is not
 * enabled, then there will be no reference to
 * PowerLPF3_startInitialHfxtAmpComp() which is the only place that references
 * PowerCC27XX_initialHfxtAmpCompClockCb(), and therefore neither functions
 * will be linked in the application.
 */
static ClockP_Struct hfxtAmpCompClock;

/* Non-static Globals */

/* Interrupt for ClockP and Power policy */
HwiP_Struct clockHwi;

/* ****************** Power APIs ******************** */

/*
 *  ======== Power_init ========
 */
int_fast16_t Power_init()
{
    /* If this function has already been called, just return */
    if (isInitialized)
    {
        return Power_SOK;
    }

    isInitialized = true;

    isPolicyEnabled = PowerCC27XX_config.policyFxn != NULL;

    policyFxn = PowerCC27XX_config.policyFxn;

    startInitialHfxtAmpCompFxn = PowerCC27XX_config.startInitialHfxtAmpCompFxn;

    /* Clear VDDIOPGIO to disable pad power. This is a workaround for a
     * CC27xxx10 ROM bug that leaves the bit set rather than cleared. For other
     * devices, this workaround has no effect.
     */
    HWREG(PMCTL_BASE + PMCTL_O_AONRCLR1) |= PMCTL_AONRCLR1_VDDIOPGIO_CLR;

    /* Construct the CKM hwi responsible for oscillator related events.
     * Since there is no dedicated CKM interrupt line, we need to mux one of
     * the configurable lines to the CKM.
     * CPUIRQ3 is dedicated to this purpose.
     */
    HwiP_construct(&ckmHwi, INT_CPUIRQ3, PowerCC27XX_oscillatorISR, NULL);

    HWREG(EVTSVT_BASE + EVTSVT_O_CPUIRQ3SEL) = EVTSVT_CPUIRQ3SEL_PUBID_AON_CKM_COMB;

    /* Enable a selection of CKM signals as interrupt sources. For now,
     * we will stick to AMPSETTLED since it is related to existing notification
     * and HFXTFAULT and TRACKREFLOSS to be able to handle HFXT clock loss.
     * TRACKREFLOSS may occur when entering and exiting fake standby with the
     * debugger attached.
     */
    HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMSET_AMPSETTLED | CKMD_IMSET_HFXTFAULT | CKMD_IMSET_TRACKREFLOSS;

    HwiP_enableInterrupt(INT_CPUIRQ3);

    /* Construct the ClockP hwi responsible for timing service events.
     * This Hwi is time multiplexed between the SysTimer and the RTC for use
     * by the ClockP and Power policy respectively.
     * Since there is no dedicated RTC or SysTimer interrupt line, we need to
     * mux one of the configurable lines to the CKM.
     * CPUIRQ16 is dedicated to this purpose.
     */
    HwiP_construct(&clockHwi, INT_CPUIRQ16, PowerCC27XX_rtcISR, NULL);

    /* Use RTC channel 0 in compare mode. Channel 1 could be used for other
     * purposes.
     */
    HWREG(RTC_BASE + RTC_O_IMSET) = RTC_IMSET_EV0_SET;

    /* Configure RTC to halt when CPU stopped during debug */
    HWREG(RTC_BASE + RTC_O_EMU) = RTC_EMU_HALT_STOP;

    /* Configure SysTimer to halt when CPU stopped during debug. The setting
     * is sync'd from RTC_EMU on each wakeup from standby.
     */
    HWREG(SYSTIM_BASE + SYSTIM_O_EMU) = SYSTIM_EMU_HALT_STOP;

    /* Disable automatic periodic adjustments of HFXT amplitude. It will instead
     * be done in SW.
     * With this setting it is not necessary to restart HFXT after changing
     * IREF.
     */
    HWREG(CKMD_BASE + CKMD_O_AMPCFG1) &= ~CKMD_AMPCFG1_INTERVAL_M;

    /* Set target HFXT IREF to the max value, to ensure it is high enough.
     * It will be gradually updated as needed later, as part of the HFXT
     * amplitude compensation.
     */
    CKMDSetTargetIrefTrim(HFXT_TARGET_IREF_MAX);

    /* Construct HFXT amplitude compensation clock.
     * At boot, the clock is used to detect if the HFXT FSM gets stuck in the
     * RAMP0 state.
     */
    ClockP_construct(&hfxtAmpCompClock,
                     PowerCC27XX_hfxtAmpsettledTimeout,
                     HFXT_AMP_COMP_START_TIMEOUT_US / ClockP_getSystemTickPeriod(),
                     NULL);

    /* Start HFXT */
    PowerCC27XX_startHFXT();

    /* Start timeout clock.
     * Note, interrupts are guaranteed to be disabled during Power_init(), so
     * there is no risk of the AMPSETTLED callback stopping the clock before it
     * is started.
     */
    ClockP_start(&hfxtAmpCompClock);

    /* Enable tracking loop with HFXT as reference. This will automatically
     * calibrate LFOSC against HFXT whenever HFXT is enabled; usually after
     * waking up from standby.
     * This is needed to ensure fast HFXT startup and a reasonably accurate
     * LFOSC frequency.
     */
    HWREG(CKMD_BASE + CKMD_O_HFTRACKCTL) |= CKMD_HFTRACKCTL_EN_M | CKMD_HFTRACKCTL_REFCLK_HFXT;

    /* Enable RTC as a standby wakeup source */
    HWREG(EVTULL_BASE + EVTULL_O_WKUPMASK) = EVTULL_WKUPMASK_AON_RTC_COMB_M;

    return Power_SOK;
}

/*
 *  ======== Power_disablePolicy ========
 *  Do not run the configured policy
 */
bool Power_disablePolicy(void)
{
    bool wasPolicyEnabled = isPolicyEnabled;

    isPolicyEnabled = false;

    return wasPolicyEnabled;
}

/*
 *  ======== Power_enablePolicy ========
 *  Run the configured policy
 */
void Power_enablePolicy(void)
{
    isPolicyEnabled = true;
}

/*
 *  ======== Power_setPolicy ========
 *  Set the Power policy function
 */
void Power_setPolicy(Power_PolicyFxn policy)
{
    policyFxn = policy;
}

/*
 *  ======== Power_getConstraintMask ========
 *  Get a bitmask indicating the constraints that have been registered with
 *  Power.
 */
uint_fast32_t Power_getConstraintMask(void)
{
    return constraintMask;
}

/*
 *  ======== Power_getDependencyCount ========
 *  Get the count of dependencies that are currently declared upon a resource.
 */
int_fast16_t Power_getDependencyCount(Power_Resource resourceId)
{
    DebugP_assert(PowerCC27XX_isValidResourceId(resourceId));

    uint8_t bitIndex    = RESOURCE_BIT_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);
    if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL0)
    {
        return (int_fast16_t)resourceCountsClkctl0[bitIndex];
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL1)
    {
        return (int_fast16_t)resourceCountsClkctl1[bitIndex];
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_LRFD)
    {
        return (int_fast16_t)resourceCountsLrfd[bitIndex];
    }

    return (int_fast16_t)Power_EINVALIDINPUT;
}

/*
 *  ======== Power_getConstraintCount ========
 *  Get the count of constraints that are currently set on a certain
 *  operational transition
 */
int_fast16_t Power_getConstraintCount(uint_fast16_t constraintId)
{
    DebugP_assert(constraintId < PowerCC27XX_NUMCONSTRAINTS);

    if (constraintId < PowerCC27XX_NUMCONSTRAINTS)
    {
        return (int_fast16_t)constraintCounts[constraintId];
    }
    else
    {
        return (int_fast16_t)Power_EINVALIDINPUT;
    }
}

/*
 *  ======== Power_getTransitionLatency ========
 *  Get the transition latency for a sleep state.  The latency is reported
 *  in units of microseconds.
 */
uint_fast32_t Power_getTransitionLatency(uint_fast16_t sleepState, uint_fast16_t type)
{
    /* TODO: Make this a dynamic system based on the dynamically computed delta
     * between RTC timeout and re-enabling interrupts
     */

    uint32_t latency = 0;

    if (type == Power_RESUME)
    {
        if (sleepState == PowerLPF3_STANDBY)
        {
            latency = PowerCC27XX_RESUMETIMESTANDBY;
        }
    }
    else
    {
        if (sleepState == PowerLPF3_STANDBY)
        {
            latency = PowerCC27XX_TOTALTIMESTANDBY;
        }
    }

    return latency;
}

/*
 *  ======== Power_getTransitionState ========
 *  Get the current sleep transition state.
 */
uint_fast16_t Power_getTransitionState(void)
{
    return powerState;
}

/*
 *  ======== Power_idleFunc ========
 *  Function needs to be plugged into the idle loop.
 *  It calls the configured policy function if it is not NULL.
 */
void Power_idleFunc()
{
    if (policyFxn != NULL && isPolicyEnabled == true)
    {
        (*(policyFxn))();
    }
}

/*
 *  ======== Power_registerNotify ========
 *  Register a function to be called on a specific power event.
 */
int_fast16_t Power_registerNotify(Power_NotifyObj *notifyObj,
                                  uint_fast16_t eventTypes,
                                  Power_NotifyFxn notifyFxn,
                                  uintptr_t clientArg)
{
    int_fast16_t status = Power_SOK;

    /* Check for NULL pointers */
    if ((notifyObj == NULL) || (notifyFxn == NULL))
    {
        Log_printf(LogModule_Power,
                   Log_WARNING,
                   "Power_registerNotify: Notify registration failed due to NULL pointer");

        status = Power_EINVALIDPOINTER;
    }
    else
    {
        notifyObj->eventTypes = eventTypes;
        notifyObj->notifyFxn  = notifyFxn;
        notifyObj->clientArg  = clientArg;

        Log_printf(LogModule_Power,
                   Log_INFO,
                   "Power_registerNotify: Register fxn at address 0x%x with event types 0x%x and clientArg 0x%x",
                   notifyFxn,
                   eventTypes,
                   clientArg);

        /* Place notify object on event notification queue. Assume that
         * List_Elem struct member is the first struct member in
         * Power_NotifyObj.
         */
        List_put(&notifyList, (List_Elem *)notifyObj);
    }

    return status;
}

/*
 *  ======== Power_unregisterNotify ========
 *  Unregister for a power notification.
 *
 */
void Power_unregisterNotify(Power_NotifyObj *notifyObj)
{
    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_unregisterNotify: Unregister fxn at address 0x%x with event types 0x%x and clientArg 0x%x",
               notifyObj->notifyFxn,
               notifyObj->eventTypes,
               notifyObj->clientArg);

    /* Remove notify object from its event queue */
    List_remove(&notifyList, (List_Elem *)notifyObj);
}

/*
 *  ======== Power_setConstraint ========
 *  Declare an operational constraint.
 */
int_fast16_t Power_setConstraint(uint_fast16_t constraintId)
{
    uintptr_t key;

    DebugP_assert(constraintId < PowerCC27XX_NUMCONSTRAINTS);

    key = HwiP_disable();

    /* Set the specified constraint in the constraintMask for faster access */
    constraintMask |= 1 << constraintId;

    /* Increment the specified constraint count */
    constraintCounts[constraintId]++;

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_releaseConstraint ========
 *  Release a previously declared constraint.
 */
int_fast16_t Power_releaseConstraint(uint_fast16_t constraintId)
{
    uintptr_t key;

    DebugP_assert(constraintId < PowerCC27XX_NUMCONSTRAINTS);

    key = HwiP_disable();

    DebugP_assert(constraintCounts[constraintId] != 0);

    constraintCounts[constraintId]--;

    /* Only update the constraint mask if we removed the constraint entirely */
    if (constraintCounts[constraintId] == 0)
    {
        constraintMask &= ~(1 << constraintId);
    }

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_setDependency ========
 *  Declare a dependency upon a resource.
 */
int_fast16_t Power_setDependency(Power_Resource resourceId)
{
    uint8_t previousCount;
    uintptr_t key;
    uint8_t bitIndex;

    DebugP_assert(PowerCC27XX_isValidResourceId(resourceId));

    key = HwiP_disable();

    /* Buffer previous reference count */
    previousCount = Power_getDependencyCount(resourceId);

    /* Increment reference count */
    PowerCC27XX_setDependencyCount(resourceId, previousCount + 1);

    /* If the resource was NOT activated previously ... */
    if (previousCount == 0)
    {
        bitIndex = RESOURCE_BIT_INDEX(resourceId);
        /* Turn on the peripheral */
        switch (RESOURCE_GROUP(resourceId))
        {
            case PowerCC27XX_PERIPH_GROUP_CLKCTL0:
                HWREG(CLKCTL_BASE + CLKCTL_O_CLKENSET0) = 1 << bitIndex;
                break;
            case PowerCC27XX_PERIPH_GROUP_CLKCTL1:
                HWREG(CLKCTL_BASE + CLKCTL_O_CLKENSET1) = 1 << bitIndex;
                break;
            case PowerCC27XX_PERIPH_GROUP_LRFD:
                LRFDSetClockDependency(1 << bitIndex, LRFD_CLK_DEP_POWER);
                break;
            default:
                break;
        }
    }

    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_setDependency: Updated resource counter = %d for resource ID = 0x%x",
               Power_getDependencyCount(resourceId), resourceId);

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_releaseDependency ========
 *  Release a previously declared dependency.
 */
int_fast16_t Power_releaseDependency(Power_Resource resourceId)
{
    uint8_t resourceCount;
    uintptr_t key;
    uint8_t bitIndex;

    DebugP_assert(PowerCC27XX_isValidResourceId(resourceId));

    key = HwiP_disable();

    resourceCount = Power_getDependencyCount(resourceId);

    DebugP_assert(resourceCount != 0);

    /* Decrement the reference count */
    resourceCount--;
    PowerCC27XX_setDependencyCount(resourceId, resourceCount);

    /* If this was the last dependency being released.. */
    if (resourceCount == 0)
    {
        bitIndex = RESOURCE_BIT_INDEX(resourceId);
        /* Turn off the peripheral */
        switch (RESOURCE_GROUP(resourceId))
        {
            case PowerCC27XX_PERIPH_GROUP_CLKCTL0:
                HWREG(CLKCTL_BASE + CLKCTL_O_CLKENCLR0) = 1 << bitIndex;
                break;
            case PowerCC27XX_PERIPH_GROUP_CLKCTL1:
                HWREG(CLKCTL_BASE + CLKCTL_O_CLKENCLR1) = 1 << bitIndex;
                break;
            case PowerCC27XX_PERIPH_GROUP_LRFD:
                LRFDReleaseClockDependency(1 << bitIndex, LRFD_CLK_DEP_POWER);
                break;
            default:
                break;
        }
    }

    Log_printf(LogModule_Power,
               Log_INFO,
               "Power_releaseDependency: Updated resource counter = %d for resource ID = 0x%x",
               Power_getDependencyCount(resourceId), resourceId);

    HwiP_restore(key);

    return Power_SOK;
}

/*
 *  ======== Power_shutdown ========
 */
int_fast16_t Power_shutdown(uint_fast16_t shutdownState, uint_fast32_t shutdownTime)
{
    int_fast16_t notifyStatus;
    uint32_t hwiKey;
    uint8_t i;
    bool ioPending = false;

    hwiKey = HwiP_disable();

    /* Check if there is a constraint to prohibit shutdown */
    if (Power_getConstraintMask() & (1 << PowerLPF3_DISALLOW_SHUTDOWN))
    {
        HwiP_restore(hwiKey);
        return Power_ECHANGE_NOT_ALLOWED;
    }

    /* Check whether we were transitioning to another power state */
    if (powerState != Power_ACTIVE)
    {
        HwiP_restore(hwiKey);
        return Power_EBUSY;
    }

    /* Set new transition state to entering shutdown */
    powerState = Power_ENTERING_SHUTDOWN;

    /* Signal all clients registered for pre-shutdown notification */
    notifyStatus = PowerCC27XX_notify(PowerLPF3_ENTERING_SHUTDOWN);

    for (i = GPIO_pinLowerBound; i <= GPIO_pinUpperBound; i++)
    {
        /* Read WUCFGSD field once and check both values */
        uint32_t ioShutdownConfig = HWREG(IOC_ADDR(i)) & IOC_IOC0_WUCFGSD_M;

        if ((ioShutdownConfig == IOC_IOC0_WUCFGSD_WAKE_HIGH || ioShutdownConfig == IOC_IOC0_WUCFGSD_WAKE_LOW) &&
            GPIOGetEventDio(i))
        {
            ioPending = true;
        }
    }

    /* If no IO event is pending on a shutdown wakeup IO, go to shutdown */
    if (ioPending == false && notifyStatus == Power_SOK)
    {
        HWREG(PMCTL_BASE + PMCTL_O_SHTDWN) = PMCTL_SHTDWN_KEY_VALID;
    }

    /* If shutdown succeeded, should never get here */

    powerState = Power_ACTIVE;

    HwiP_restore(hwiKey);

    /* If we get here, failed to shutdown, return error code */
    return Power_EFAIL;
}

/*
 *  ======== Power_sleep ========
 */
int_fast16_t Power_sleep(uint_fast16_t sleepState)
{
    int_fast16_t status = Power_SOK;

    /* Signal all clients registered for pre standby wakeup notification */
    status = PowerCC27XX_notify(PowerLPF3_ENTERING_STANDBY);

    /* Check for any error */
    if (status != Power_SOK)
    {
        Log_printf(LogModule_Power,
                   Log_WARNING,
                   "Power_sleep: Entering standby failed with status = 0x%x",
                   status);

        powerState = Power_ACTIVE;
        return status;
    }

    /* Adjust HFXT amplitude if needed */
    int_fast8_t adjustment = PowerLPF3_getHfxtAmpAdjustment();
    if (adjustment != 0)
    {
        PowerLPF3_adjustHfxtAmp(adjustment);

        /* Explicitly turn off HFXT before entering standby. This is to ensure
         * that the HFXT is actually re-started with the new amplitude. In
         * some cases the HFXT will not be turned off when entering standby. For
         * example when entering fake standby with the debugger attached.
         */
        HWREG(CKMD_BASE + CKMD_O_HFXTCTL) &= ~CKMD_HFXTCTL_EN_M;
    }

    /* Stop continuous measurements of HFXT amplitude before entering standby,
     * since the same hardware is needed to start the HFXT when waking up from
     * standby.
     */
    PowerCC27XX_stopContHfxtAmpMeasurements();

    /* Call wrapper function to ensure that R0-R3 are saved and restored before
     * and after this function call. Otherwise, compilers will attempt to stash
     * values on the stack while on the PSP and then restore them just after
     * HapiEnterStandby() on the MSP. Which will cause wildly unexpected
     * behaviour.
     */
    PowerCC27XX_enterStandby();

    /* Now that we have returned and are executing code from flash again, start
     * up the HFXT. The HFXT might already have been enabled automatically by
     * hardware, but calling below ensures that it has been enabled and that the
     * necessary power constraints and interrupts are set and enabled.
     */
    PowerCC27XX_startHFXT();

    /* Now clear the transition state before re-enabling the scheduler */
    powerState = Power_ACTIVE;

    return status;
}

/*
 *  ======== Power_reset ========
 */
void Power_reset(void)
{
    PMCTLResetSystem();
}

/*
 *  ======== PowerCC27XX_doWFI ========
 */
void PowerCC27XX_doWFI(void)
{
    __WFI();
}

/*
 *  ======== PowerCC27XX_enterStandby ========
 *  Wrapper function to ensure that R0-R3 are saved and restored before and
 *  after this function call. Otherwise, compilers will attempt to stash
 *  values on the stack while on the PSP and then restore them just after
 *  HapiEnterStandby() on the MSP. Which will cause wildly unexpected behaviour.
 */
void PowerCC27XX_enterStandby(void)
{
    /* Read current stack pointer from CONTROL */
    uint32_t controlPreStandby = __get_CONTROL();

    if (controlPreStandby & 0x02)
    {
        /* Currently on PSP stack, switching to MSP.
         * HapiEnterStandby() must execute from MSP since the
         * device reboots into privileged mode on MSP and HapiEnterStandby()
         * assumes it will be called running on MSP.
         */
        __set_CONTROL(0x00);

        /* - Save CPU state on MSP and MSP in CLKCTL_STBYPTR
         * - Enter standby
         * - Exit standby
         * - Restore CPU state from MSP
         * - Apply copyList
         */
        HapiEnterStandby(NULL);

        /* Switch back to PSP stack, where controlPreStandby is stored */
        __set_CONTROL(0x02);

        /* Restore control register */
        __set_CONTROL(controlPreStandby);
    }
    else
    {
        /* Stack is already MSP.
         * - Save CPU state on MSP and MSP in CLKCTL_STBYPTR
         * - Enter standby
         * - Exit standby
         * - Restore CPU state from MSP
         * - Apply copyList
         */
        HapiEnterStandby(NULL);
    }
}

/*
 *  ======== PowerLPF3_selectLFOSC ========
 */
void PowerLPF3_selectLFOSC(void)
{
    /* Select LFOSC */
    HWREG(CKMD_BASE + CKMD_O_LFCLKSEL) = CKMD_LFCLKSEL_MAIN_LFOSC;

    /* Start LFOSC */
    HWREG(CKMD_BASE + CKMD_O_LFOSCCTL) = CKMD_LFOSCCTL_EN;

    /* Initialize LFINCCTL and LFINCCTL2 registers */
    HWREG(CKMD_BASE + CKMD_O_LFINCCTL) = (CKMD_LFINCCTL_PREVENTSTBY_ON | CKMD_LFINCCTL_KEEPHFXTEN_EN |
                                          (LFINCCTL_INT_START_VALUE << CKMD_LFINCCTL_INT_S) |
                                          CKMD_LFINCCTL_STOPGEAR_LOW | CKMD_LFINCCTL_ERRTHR_LARGE |
                                          CKMD_LFINCCTL_GEARRSTRT_TWOTHR | CKMD_LFINCCTL_SOFTRSTRT_ON |
                                          CKMD_LFINCCTL_EN_EN);

    HWREG(CKMD_BASE + CKMD_O_LFINCCTL2) |= CKMD_LFINCCTL2_ADJUSTLFINC;

    /* Enable LFCLKGOOD, TRACKREFLOSS, and TRACKREFOOR. TRACKREFLOSS may occur
     * when entering and exiting fake standby with the debugger attached.
     */
    HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMSET_LFCLKGOOD | CKMD_IMSET_TRACKREFOOR;

    /* Disallow standby until LF clock is running. Otherwise, we will only
     * vector to the ISR after we wake up from standby the next time since the
     * CKM interrupt is purposefully not configured as a wakeup source.
     */
    Power_setConstraint(PowerLPF3_DISALLOW_STANDBY);

    Log_printf(LogModule_Power, Log_INFO, "PowerLPF3_selectLFOSC: LFOSC selected");
}

/*
 *  ======== PowerLPF3_selectLFXT ========
 */
void PowerLPF3_selectLFXT(void)
{
    /* Set LFINC override to 32.768 kHz. Will not impact RTC since the fake LF
     * ticks will have a higher priority than LFINCOVR.
     *
     * The value is calculated as period in microseconds with 16 fractional
     * bits.
     * The LFXT runs at 32.768 kHz -> 1 / 32768 Hz = 30.5176 us.
     * 30.5176 * 2^16 = 2000000 = 0x001E8480
     */
    HWREG(CKMD_BASE + CKMD_O_LFINCOVR) = 0x001E8480 | CKMD_LFINCOVR_OVERRIDE;

    /* Set LFCLK  */
    HWREG(CKMD_BASE + CKMD_O_LFCLKSEL) = CKMD_LFCLKSEL_MAIN_LFXT;

    /* Start LFXT */
    HWREG(CKMD_BASE + CKMD_O_LFXTCTL) = CKMD_LFXTCTL_EN;

    /* Enable LFCLKGOOD */
    HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMASK_LFCLKGOOD;

    /* Disallow standby until LF clock is running. Otherwise, we will only
     * vector to the ISR after we wake up from standby the next time since the
     * CKM interrupt is purposefully not configured as a wakeup source.
     */
    Power_setConstraint(PowerLPF3_DISALLOW_STANDBY);

    Log_printf(LogModule_Power, Log_INFO, "PowerLPF3_selectLFXT: LFXT selected");
}

/*
 *  ======== PowerLPF3_selectEXTLF ========
 */
void PowerLPF3_selectEXTLF(void)
{
    /* Set LFINC override to 31.25 kHz.
     *
     * The value is calculated as period in microseconds with 16 fractional
     * bits.
     * The EXTLF runs at 31.25 kHz -> 1 / 31250 Hz = 32 us.
     * 32 * 2^16 = 2097152 = 0x00200000
     */
    HWREG(CKMD_BASE + CKMD_O_LFINCOVR) = 0x00200000 | CKMD_LFINCOVR_OVERRIDE;

    /* Set LFCLK to EXTLF */
    HWREG(CKMD_BASE + CKMD_O_LFCLKSEL) = CKMD_LFCLKSEL_MAIN_EXTLF;

    /* Configure EXTLF to the right mux */
    GPIO_setConfigAndMux(PowerLPF3_extlfPin, GPIO_CFG_INPUT, PowerLPF3_extlfPinMux);

    /* Enable LFCLKGOOD */
    HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMASK_LFCLKGOOD;

    /* Disallow standby until LF clock is running. Otherwise, we will only
     * vector to the ISR after we wake up from standby the next time since the
     * CKM interrupt is purposefully not configured as a wakeup source.
     */
    Power_setConstraint(PowerLPF3_DISALLOW_STANDBY);

    Log_printf(LogModule_Power, Log_INFO, "PowerLPF3_selectEXTLF: EXTLF selected");
}

/*
 *  ======== PowerCC27XX_oscillatorISR ========
 */
static void PowerCC27XX_oscillatorISR(uintptr_t arg)
{
    uint32_t maskedStatus = HWREG(CKMD_BASE + CKMD_O_MIS);

    /* Manipulating ICLR alone does not actually do anything. The CKM_COMB
     * signals are almost all level values that reset one cycle after writing to
     * ICLR. We need to update the mask instead to avoid looping in the ISR
     */
    HWREG(CKMD_BASE + CKMD_O_ICLR)  = maskedStatus;
    HWREG(CKMD_BASE + CKMD_O_IMCLR) = maskedStatus;

    if (maskedStatus & CKMD_MIS_AMPSETTLED_M)
    {
        /* It has been observed a brief period of ~15us occurring ~130us after
         * starting HFXT where FLTSETTLED pulses high. If the idle loop attempts
         * to enter standby while FLTSETTLED pulses high, it may enter standby
         * before the filter is truly settled. To prevent this, we need to wait
         * for HFXTGOOD and LFTICK to be set before entering standby. This is
         * achieved by clearing LFTICK once AMPSETTLED is set (which occurs
         * after HFXTGOOD is set), and then waiting for LFTICK to be set again
         * before entering standby.
         */
        HWREG(CKMD_BASE + CKMD_O_ICLR) = (CKMD_ICLR_LFTICK);

        /* Stop AMPSETTLED timeout clock and change callback function of clock
         * object (NULL: not used)
         */
        ClockP_stop(&hfxtAmpCompClock);
        ClockP_setFunc(&hfxtAmpCompClock, NULL, 0);

        /* Start continuous measurements of HFXT amplitude */
        PowerCC27XX_startContHfxtAmpMeasurements();

        /* If enabled, start the initial HFXT amplitude compensation */
        if (startInitialHfxtAmpCompFxn != NULL)
        {
            /* Start the asynchronous initial HFXT amplitude compensation.
             * When the HFXT amplitude compensation is done the
             * PowerLPF3_HFXT_AVAILABLE notification will be posted, and the
             * PowerLPF3_DISALLOW_STANDBY constraint will be released.
             */
            startInitialHfxtAmpCompFxn();

            /* Only do the initial HFXT amplitude compensation once */
            startInitialHfxtAmpCompFxn = NULL;
        }
        else
        {
            Log_printf(LogModule_Power, Log_INFO, "PowerCC27XX_oscillatorISR: HFXT is available");

            PowerCC27XX_notify(PowerLPF3_HFXT_AVAILABLE);

            /* Allow standby again now that HFXT has finished starting */
            Power_releaseConstraint(PowerLPF3_DISALLOW_STANDBY);
        }
    }

    if (maskedStatus & (CKMD_MIS_HFXTFAULT_M | CKMD_MIS_TRACKREFLOSS_M))
    {
        Log_printf(LogModule_Power, Log_WARNING, "PowerCC27XX_oscillatorISR: HFXT fault and/or TRACKREFLOSS, restarting HFXT");

        /* If there was a HFXTFAULT or TRACKREFLOSS, restart HFXT. Consider
         * issuing a notification to allow logging. If we keep it like this, we
         * could get stuck in an infinite loop restarting a faulty oscillator.
         * Then again, it is not like there is a great way to recover from that.
         */
        HWREG(CKMD_BASE + CKMD_O_HFXTCTL) &= ~CKMD_HFXTCTL_EN_M;

        /* Stop continuous measurements of HFXT amplitude. The hardware used is
         * needed to start HFXT.
         */
        PowerCC27XX_stopContHfxtAmpMeasurements();

        /* Max out IREF */
        CKMDSetTargetIrefTrim(HFXT_TARGET_IREF_MAX);

        /* Start up the HFXT using the workaround for the HFXT amplitude control ADC
         * bias point
         */
        PowerCC27XX_startHFXT();

        /* Re-enable interrupts */
        HWREG(CKMD_BASE + CKMD_O_IMSET) = maskedStatus & (CKMD_MIS_HFXTFAULT_M | CKMD_MIS_TRACKREFLOSS_M);
    }

    if (maskedStatus & CKMD_MIS_TRACKREFLOSS_M)
    {
        /* Disable interrupts as HFXT SWTCXO may interrupt and modify
         * HFTRACKCTL with a higher priority depending on user interrupt
         * priorities.
         */
        uintptr_t key = HwiP_disable();

        /* Disable tracking */
        HWREG(CKMD_BASE + CKMD_O_HFTRACKCTL) &= ~CKMD_HFTRACKCTL_EN_M;

        /* Re-enable tracking */
        HWREG(CKMD_BASE + CKMD_O_HFTRACKCTL) |= CKMD_HFTRACKCTL_EN_M;

        HwiP_restore(key);

        /* Re-enable TRACKREFLOSS */
        HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMSET_TRACKREFLOSS_M;
    }

    if (maskedStatus & CKMD_MIS_LFCLKGOOD_M)
    {
        Log_printf(LogModule_Power, Log_INFO, "PowerCC27XX_oscillatorISR: LFCLK is ready");

        /* Enable LF clock monitoring */
        HWREG(CKMD_BASE + CKMD_O_LFMONCTL) = CKMD_LFMONCTL_EN;

        /* Enable LF clock loss reset while in standby */
        HWREG(PMCTL_BASE + PMCTL_O_RSTCTL) |= PMCTL_RSTCTL_LFLOSS_ARMED;

        /* Send out notification for LF clock switch */
        PowerCC27XX_notify(PowerLPF3_LFCLK_SWITCHED);

        /* Allow standby again now that we have sent out the notification */
        Power_releaseConstraint(PowerLPF3_DISALLOW_STANDBY);
    }

    if (maskedStatus & CKMD_MIS_TRACKREFOOR_M)
    {
        Log_printf(LogModule_Power, Log_ERROR, "PowerCC27XX_oscillatorISR: TRACKREFOOR, entering endless loop");

        while (1) {}
    }
}

/*
 *  ======== PowerCC27XX_rtcISR ========
 */
static void PowerCC27XX_rtcISR(uintptr_t arg)
{
    /* We should never get here since we will just use the interrupt to wake
     * up the device from standby and then consume the interrupt while
     * interrupts are disabled.
     */
    HWREG(RTC_BASE + RTC_O_ICLR) = HWREG(RTC_BASE + RTC_O_MIS);
}

/*
 *  ======== PowerCC27XX_startHFXT ========
 * It should be ensured that the system does not vector to the CKMD interrupt
 * (PowerCC27XX_oscillatorISR) while this function is executing.
 * Meaning any of the following must be true:
 *  - Interrupts are disabled, or
 *  - This function is called from PowerCC27XX_oscillatorISR(), since nested
 *    vectoring to the same interrupt is not possible.
 */
static void PowerCC27XX_startHFXT(void)
{

    /* Start HFXT and enable automatic enabling after waking up from standby */
    HWREG(CKMD_BASE + CKMD_O_HFXTCTL) |= CKMD_HFXTCTL_EN | CKMD_HFXTCTL_AUTOEN;

    /* Disallow standby until AMPSETTLED is true */
    Power_setConstraint(PowerLPF3_DISALLOW_STANDBY);

    /* Enable the AMPSETTLED interrupt.
     * Since it is a level status signal, it remains asserted when we are
     * running on HFXT and cannot be cleared.
     * The oscillator interrupt removes it from the interrupt mask to prevent
     * repeated vectoring.
     */
    HWREG(CKMD_BASE + CKMD_O_ICLR)  = CKMD_ICLR_AMPSETTLED;
    HWREG(CKMD_BASE + CKMD_O_IMSET) = CKMD_IMSET_AMPSETTLED;
}

/*
 *  ======== PowerLPF3_startAFOSC ========
 */
int_fast16_t PowerLPF3_startAFOSC(PowerLPF3_AfoscFreq frequency)
{
    /* Return failed if AFOSC is already enabled */
    if ((HWREG(CKMD_BASE + CKMD_O_AFOSCCTL) & CKMD_AFOSCCTL_EN_M) == CKMD_AFOSCCTL_EN)
    {
        return Power_EFAIL;
    }

    uint8_t mode;
    uint8_t mid;
    uint8_t coarse;
    uint32_t trackingRatio;

    /* Read AFOSC trims and select tracking loop ratio */
    switch (frequency)
    {
        case PowerLPF3_AFOSC_FREQ_80MHZ:
            mode          = fcfg->appTrims.cc27xx.afosc80MHZ.mode;
            mid           = fcfg->appTrims.cc27xx.afosc80MHZ.mid;
            coarse        = fcfg->appTrims.cc27xx.afosc80MHZ.coarse;
            trackingRatio = CKMD_AFTRACKCTL_RATIO__80MHZ;
            break;
        case PowerLPF3_AFOSC_FREQ_90P3168MHZ:
            mode          = fcfg->appTrims.cc27xx.afosc90MHZ.mode;
            mid           = fcfg->appTrims.cc27xx.afosc90MHZ.mid;
            coarse        = fcfg->appTrims.cc27xx.afosc90MHZ.coarse;
            trackingRatio = CKMD_AFTRACKCTL_RATIO__90P3168MHZ;
            break;
        case PowerLPF3_AFOSC_FREQ_98P304MHZ:
            mode          = fcfg->appTrims.cc27xx.afosc98MHZ.mode;
            mid           = fcfg->appTrims.cc27xx.afosc98MHZ.mid;
            coarse        = fcfg->appTrims.cc27xx.afosc98MHZ.coarse;
            trackingRatio = CKMD_AFTRACKCTL_RATIO__98P304MHZ;
            break;
        default:
            /* Return error */
            Log_printf(LogModule_Power,
                       Log_WARNING,
                       "PowerLPF3_startAFOSC: Invalid frequency input = %d",
                       frequency);

            return Power_EINVALIDINPUT;
            break;
    }

    /* Write AFOSC to temporary variable representing the upper half word
     * (16bits) of CKMD.TRIM0. The CKMD_TRIM0_*_S defines define the bit shift
     * values for fields in the full word (32 bits) CKMD.TRIM0 register. 16 is
     * subtracted from the full word bit shift values to convert them to bit
     * shift values for the upper half word.
     */
    uint16_t upperHalfwordTrim0Tmp = 0;
    upperHalfwordTrim0Tmp |= mode << (CKMD_TRIM0_AFOSC_MODE_S - 16);
    upperHalfwordTrim0Tmp |= mid << (CKMD_TRIM0_AFOSC_MID_S - 16);
    upperHalfwordTrim0Tmp |= coarse << (CKMD_TRIM0_AFOSC_COARSE_S - 16);

    /* Write the AFOSC trims to CKMD with half word access, to prevent writing
     * to the HFOSC trims.
     */
    HWREGH(CKMD_BASE + CKMD_O_TRIM0 + 2) = upperHalfwordTrim0Tmp;

    /* Set tracking ratio.
     * Note, trackingRatio is already bit aligned with the RATIO field.
     */
    uint32_t afTrackCtl = HWREG(CKMD_BASE + CKMD_O_AFTRACKCTL);
    afTrackCtl &= ~CKMD_AFTRACKCTL_RATIO_M;
    afTrackCtl |= trackingRatio;
    HWREG(CKMD_BASE + CKMD_O_AFTRACKCTL) = afTrackCtl;

    /* Enable AFOSC and enable auto-disable on standby entry */
    HWREG(CKMD_BASE + CKMD_O_AFOSCCTL) |= CKMD_AFOSCCTL_EN | CKMD_AFOSCCTL_AUTODIS;

    /* Wait until AFOSC good indication */
    while ((HWREG(CKMD_BASE + CKMD_O_RIS) & CKMD_RIS_AFOSCGOOD_M) != CKMD_RIS_AFOSCGOOD) {}

    /* Enable the AFOSC tracking loop */
    HWREG(CKMD_BASE + CKMD_O_AFTRACKCTL) |= CKMD_AFTRACKCTL_EN;

    Log_printf(LogModule_Power,
               Log_INFO,
               "PowerLPF3_startAFOSC: AFOSC started at RTC count = 0x%x",
               HWREG(RTC_BASE + RTC_O_TIME1U));

    return Power_SOK;
}

/*
 *  ======== PowerLPF3_stopAFOSC ========
 */
void PowerLPF3_stopAFOSC(void)
{
    /* Disable AFOSC tracking loop */
    HWREG(CKMD_BASE + CKMD_O_AFTRACKCTL) &= ~CKMD_AFTRACKCTL_EN_M;

    /* Disable AFOSC */
    HWREG(CKMD_BASE + CKMD_O_AFOSCCTL) &= ~CKMD_AFOSCCTL_EN_M;

    Log_printf(LogModule_Power,
               Log_INFO,
               "PowerLPF3_stopAFOSC: AFOSC stopped at RTC count = 0x%x",
               HWREG(RTC_BASE + RTC_O_TIME1U));
}

/*
 *  ======== notify ========
 *  Send notifications to registered clients.
 *  Note: Task scheduling is disabled when this function is called.
 */
int_fast16_t PowerCC27XX_notify(uint_fast16_t eventType)
{
    int_fast16_t notifyStatus;
    Power_NotifyFxn notifyFxn;
    uintptr_t clientArg;
    List_Elem *elem;

    /* If queue is empty, return immediately */
    if (!List_empty(&notifyList))
    {
        /* Point to first client notify object */
        elem = List_head(&notifyList);

        /* TODO: Change Power_registerNotify() & Power_unregisterNotify() to use
         * index instead of bitmask and then use multiple lists just like in
         * the TI-RTOS scheduler.
         * We currently walk the entire list 4 times when entering/exiting
         * standby + switching HFXT...
         */

        /* Walk the queue and notify each registered client of the event */
        do
        {
            if (((Power_NotifyObj *)elem)->eventTypes & eventType)
            {
                /* Pull params from notify object */
                notifyFxn = ((Power_NotifyObj *)elem)->notifyFxn;
                clientArg = ((Power_NotifyObj *)elem)->clientArg;

                /* Call the client's notification function */
                Log_printf(LogModule_Power,
                           Log_VERBOSE,
                           "PowerCC27XX_notify: Invoking notification fxn at address 0x%x with event type 0x%x and clientArg 0x%x",
                           notifyFxn,
                           eventType,
                           clientArg);

                notifyStatus = (int_fast16_t)(*(Power_NotifyFxn)notifyFxn)(eventType, 0, clientArg);

                /* If client declared error stop all further notifications */
                if (notifyStatus != Power_NOTIFYDONE)
                {
                    Log_printf(LogModule_Power,
                               Log_WARNING,
                               "PowerCC27XX_notify: Notification fxn reported error, fxn at address 0x%x with event type 0x%x and notifyStatus 0x%x",
                               notifyFxn,
                               eventType,
                               notifyStatus);

                    return Power_EFAIL;
                }
            }

            /* Get next element in the notification queue */
            elem = List_next(elem);

        } while (elem != NULL);
    }

    return Power_SOK;
}

/*
 *  ======== PowerCC27XX_setDependencyCount ========
 */
void PowerCC27XX_setDependencyCount(Power_Resource resourceId, uint8_t count)
{
    uint8_t bitIndex    = RESOURCE_BIT_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);

    DebugP_assert(PowerCC27XX_isValidResourceId(resourceId));

    if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL0)
    {
        resourceCountsClkctl0[bitIndex] = count;
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL1)
    {
        resourceCountsClkctl1[bitIndex] = count;
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_LRFD)
    {
        resourceCountsLrfd[bitIndex] = count;
    }
}

/*
 *  ======== PowerCC27XX_isValidResourceId ========
 */
bool PowerCC27XX_isValidResourceId(Power_Resource resourceId)
{
    uint8_t bitIndex    = RESOURCE_BIT_INDEX(resourceId);
    uint_fast16_t group = RESOURCE_GROUP(resourceId);

    if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL0)
    {
        return bitIndex < PowerCC27XX_NUMRESOURCES_CLKCTL0;
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_CLKCTL1)
    {
        return bitIndex < PowerCC27XX_NUMRESOURCES_CLKCTL1;
    }
    else if (group == PowerCC27XX_PERIPH_GROUP_LRFD)
    {
        return bitIndex < PowerCC27XX_NUMRESOURCES_LRFD;
    }
    else
    {
        return false;
    }
}

/*
 *  ======== PowerCC27XX_temperatureToRatio ========
 */
static uint32_t PowerCC27XX_temperatureToRatio(int16_t temperature)
{
    /* Calculate unshifted ppm offset. Fixed-point coefficients are assumed to
     * be set so that this computation does not overflow 32 bits in the -40, 125
     * degC range.
     */
    int32_t hfxtPpmOffset = PowerCC27XX_hfxtCompCoefficients.P3 * temperature * temperature * temperature +
                            PowerCC27XX_hfxtCompCoefficients.P2 * temperature * temperature +
                            PowerCC27XX_hfxtCompCoefficients.P1 * temperature + PowerCC27XX_hfxtCompCoefficients.P0;

    /* Calculate correct frequency offset, using shifted hfxtPpmOffset.
     * Frequency offset = 48000000 Hz * (hfxtPpmOffset >> shift) / 1000000
     *                  = 48 Hz * hfxtPpmOffset >> shift
     * Do 64-bit multiplication, since this will most likely overflow 32 bits.
     * Signed right-shift will result in an arithmetic shift operation.
     */
#if !(defined(__IAR_SYSTEMS_ICC__) || (defined(__clang__) && defined(__ti_version__)) || defined(__GNUC__))
    #warning The following signed right-shift operation is implementation-defined
#endif
    int32_t hfxtFreqOffset = (int32_t)((48LL * (int64_t)hfxtPpmOffset) >> PowerCC27XX_hfxtCompCoefficients.shift);

    /* Calculate temperature dependent ppm offset of the capacitor array on the
     * crystal input pins, modelled as ppm(T) = 0.07 * (T - 25) + 10
     * Input frequency is assumed 48 MHz, and any potential crystal offset is
     * neglected and not factored into the cap array offset calculation.
     * frequency_offset(T) = 48000000 * (0.07 * (T - 25) + 10) / 1000000
     *                     = 3.36 * (T - 25) + 480
     * To avoid floating-point multiplication and integer division, 3.36 is
     * approximated as 3523215 / 2^20 ~ 3.35999966. The error introduced by
     * this approximation is negligable.
     */
#if !(defined(__IAR_SYSTEMS_ICC__) || (defined(__clang__) && defined(__ti_version__)) || defined(__GNUC__))
    #warning The following signed right-shift operation is implementation-defined
#endif
    int32_t capArrayOffset = ((3523215 * (temperature - 25)) >> 20) + 480;

    /* Calculate the actual reference input frequency to the tracking loop,
     * accounting for the HFXT offset and cap array offset
     */
    int32_t refFreq = 48000000 + hfxtFreqOffset + capArrayOffset;

    /* Calculate word to write to HFTRACKCTL.RATIO. Expression taken from
     * register description: ratio = 24MHz / (2 * reference_frequency) * 2^24
     * 64-bit division is required, which is truncated to 32 bit
     */
    uint32_t ratio = (uint32_t)(0xB71B00000000LL / (int64_t)refFreq);

    return ratio;
}

/*
 *  ======== PowerCC27XX_updateHFXTRatio ========
 */
static void PowerCC27XX_updateHFXTRatio(uint32_t ratio)
{
    /* Store ratio so a later forced update has the right value */
    hfxtCompRatio = ratio;
    /* Check if updates are disabled */
    if ((Power_getConstraintMask() & (1 << PowerLPF3_DISALLOW_SWTCXO)) == 0)
    {
        /* Update HFTRACKCTL atomically */
        uintptr_t key = HwiP_disable();
        uint32_t temp = HWREG(CKMD_BASE + CKMD_O_HFTRACKCTL) & ~CKMD_HFTRACKCTL_RATIO_M;
        temp |= ratio & CKMD_HFTRACKCTL_RATIO_M;
        HWREG(CKMD_BASE + CKMD_O_HFTRACKCTL) = temp;
        HwiP_restore(key);
    }
}

/*
 *  ======== PowerCC27XX_hfxtCompensateFxn ========
 */
static void PowerCC27XX_hfxtCompensateFxn(int16_t currentTemperature,
                                          int16_t thresholdTemperature,
                                          uintptr_t clientArg,
                                          Temperature_NotifyObj *notifyObject)
{
    uintptr_t key;
    uint32_t ratio;
    PowerCC27XX_hfxtConfig hfxtConfig;
    hfxtConfig.value = (uint32_t)clientArg;

    /* Sanitize current temperature to fall within valid range. This ensures
     * that 32-bit overflow does not occur in the ppm calculation below.
     */
    if (currentTemperature > HFXT_COMP_MAX_TEMP)
    {
        currentTemperature = HFXT_COMP_MAX_TEMP;
    }

    if (currentTemperature < HFXT_COMP_MIN_TEMP)
    {
        currentTemperature = HFXT_COMP_MIN_TEMP;
    }

    key = HwiP_disable();

    /* If HFXT compensation has been disabled asynchronously during execution of
     * this callback then do not re-register the notification object
     */
    if (PowerCC27XX_hfxtCompEnabled)
    {

        Log_printf(LogModule_Power,
                   Log_INFO,
                   "PowerCC27XX_hfxtCompensateFxn: Registering notification. Temp = %d, Temp threshold = %d",
                   currentTemperature,
                   hfxtConfig.temperature.threshold);

        if (currentTemperature > hfxtConfig.temperature.threshold)
        {
            /* If temperature is above compensation threshold then compute a
             * compensated ratio-value and update the ratio register in the
             * tracking loop
             */
            ratio = PowerCC27XX_temperatureToRatio(currentTemperature);
            PowerCC27XX_updateHFXTRatio(ratio);

            /* Register the notification again with updated thresholds. Notification thresholds must be crossed to
             * trigger, so the upper and lower limits are decreased by 1 to maintain a range of +/- delta.
             */
            Temperature_registerNotifyRange(notifyObject,
                                            currentTemperature + hfxtConfig.temperature.delta - 1,
                                            currentTemperature - hfxtConfig.temperature.delta + 1,
                                            PowerCC27XX_hfxtCompensateFxn,
                                            clientArg);
        }
        else
        {
            /* If temperature is at or below compensation threshold then reset
             * the tracking loop ratio to remove any compensation, and register
             * a new high notification. The new limit should not be lower than
             * the compensation threshold.
             */
            PowerCC27XX_updateHFXTRatio(CKMD_HFTRACKCTL_RATIO_REF48M);

            Temperature_registerNotifyHigh(notifyObject,
                                           Math_MAX(hfxtConfig.temperature.threshold,
                                                    currentTemperature + hfxtConfig.temperature.delta - 1),
                                           PowerCC27XX_hfxtCompensateFxn,
                                           clientArg);
        }
    }

    HwiP_restore(key);
}

/*
 *  ======== PowerLPF3_initHFXTCompensation ========
 */
void PowerLPF3_initHFXTCompensation(int32_t P0,
                                    int32_t P1,
                                    int32_t P2,
                                    int32_t P3,
                                    uint8_t shift,
                                    __attribute__((unused)) bool fcfgInsertion)
{
    PowerCC27XX_hfxtCompCoefficients.P0    = P0;
    PowerCC27XX_hfxtCompCoefficients.P1    = P1;
    PowerCC27XX_hfxtCompCoefficients.P2    = P2;
    PowerCC27XX_hfxtCompCoefficients.P3    = P3;
    PowerCC27XX_hfxtCompCoefficients.shift = shift;

    /* If device offers FCFG insertion data it will be factored in here.
     * Currently no device supports this.
     */
}

/*
 *  ======== PowerLPF3_enableHFXTCompensation ========
 */
void PowerLPF3_enableHFXTCompensation(int16_t tempThreshold, int16_t tempDelta)
{

    if (PowerCC27XX_hfxtCompEnabled == false)
    {
        PowerCC27XX_hfxtCompEnabled = true;

        Temperature_init();

        int16_t currentTemperature = Temperature_getTemperature();

        PowerCC27XX_hfxtConfig config;
        config.temperature.threshold = tempThreshold;
        config.temperature.delta     = tempDelta;

        /* Only perform temperature compensation if the temperature is above the
         * set threshold. If it is not, then register a high notification on the
         * threshold
         */
        if (currentTemperature > tempThreshold)
        {
            PowerCC27XX_hfxtCompensateFxn(currentTemperature,
                                          0,
                                          (uintptr_t)config.value,
                                          &PowerCC27XX_hfxtCompNotifyObj);
        }
        else
        {
            Temperature_registerNotifyHigh(&PowerCC27XX_hfxtCompNotifyObj,
                                           tempThreshold,
                                           PowerCC27XX_hfxtCompensateFxn,
                                           (uintptr_t)config.value);
        }

        Log_printf(LogModule_Power,
                   Log_INFO,
                   "PowerLPF3_enableHFXTCompensation: Compensation enabled. Temp = %d, Temp threshold = %d",
                   currentTemperature,
                   tempThreshold);
    }
}

/*
 *  ======== PowerLPF3_disableHFXTCompensation ========
 */
void PowerLPF3_disableHFXTCompensation(void)
{

    uintptr_t key = HwiP_disable();
    if (PowerCC27XX_hfxtCompEnabled == true)
    {
        PowerCC27XX_hfxtCompEnabled = false;

        Temperature_unregisterNotify(&PowerCC27XX_hfxtCompNotifyObj);

        /* Update HFTRACKCTL.RATIO to reset-value */
        PowerCC27XX_updateHFXTRatio(CKMD_HFTRACKCTL_RATIO_REF48M);
    }
    HwiP_restore(key);

    Log_printf(LogModule_Power,
               Log_INFO,
               "PowerLPF3_disableHFXTCompensation: Compensation disabled");
}

/*
 *  ======== PowerLPF3_disableHFXTCompensation ========
 */
void PowerLPF3_forceHFXTCompensationUpdate(void)
{
    PowerCC27XX_updateHFXTRatio(hfxtCompRatio);
}

/*
 *  ======== PowerLPF3_startInitialHfxtAmpComp ========
 * Function to start the initial HFXT amplitude compensation.
 * If initial HFXT compensation is to be performed, this function is to be
 * called in the interrupt handler for the AMPSETTLED interrupt instead of
 * posting a PowerLPF3_HFXT_AVAILABLE notification and releasing the
 * PowerLPF3_DISALLOW_STANDBY constraint.
 *
 * The initial HFXT amplitude compensation assumes that the HFXTTARG.IREF value
 * is set to the max value (HFXT_TARGET_IREF_MAX) when this function is called.
 * The IREF value will then be updated in steps until the optimal IREF
 * value has been found. This is the steps taken:
 * 1. Wait HFXT_AMP_COMP_MEASUREMENT_US us, to let the HFXT amplitude settle
 *    before measuring it.
 * 2. Measure the HFXT amplitude.
 * 3. If amplitude is above 16, decrement IREF, but never lower than
 *    HFXT_TARGET_IREF_MIN.
 * 4. If an adjustment was made, go to step 1.
 * 5. If amplitude is below 10, increment IREF back to previous value. No new
 *    measurement is required.
 * 6. If an adjustment was made, wait HFXT_AMP_COMP_MEASUREMENT_US us, to let
 *    the HFXT amplitude settle.
 * 7. Post PowerLPF3_HFXT_AVAILABLE notification and release
 *    PowerLPF3_DISALLOW_STANDBY constraint.
 *
 * The waits mentioned in step 1 and 6 will be done asynchronously using
 * hfxtAmpCompClock, and most of above logic is handled in the callback function
 * for the clock (PowerCC27XX_initialHfxtAmpCompClockCb).
 *
 * This function is only responsible for starting the process by starting the
 * clock to perform the first iteration of step 1.
 *
 * In the worst case (in terms of how long the process takes), the IREF value
 * will be decremented to HFXT_TARGET_IREF_MIN and then incremented back to
 * (HFXT_TARGET_IREF_MIN + 1). This means that the IREF will be changed
 * ((HFXT_TARGET_IREF_MAX - HFXT_TARGET_IREF_MIN) + 1) times. After each change
 * we need to wait for HFXT_AMP_COMP_MEASUREMENT_US, and including waiting
 * before the first measurement it means that the initial HFXT amplitude
 * compensation will take up to
 * ((HFXT_TARGET_IREF_MAX - HFXT_TARGET_IREF_MIN) + 2)*HFXT_AMP_COMP_MEASUREMENT_US us
 * plus aditional processing time.
 */
void PowerLPF3_startInitialHfxtAmpComp(void)
{
    /* Update hfxtAmpCompClock callback function */
    ClockP_setFunc(&hfxtAmpCompClock, PowerCC27XX_initialHfxtAmpCompClockCb, 0);

    /* Set timeout for the first HFXT amplitude measurement */
    ClockP_setTimeout(&hfxtAmpCompClock, HFXT_AMP_COMP_MEASUREMENT_US / ClockP_getSystemTickPeriod());

    /* Start clock */
    ClockP_start(&hfxtAmpCompClock);
}

/*
 *  ======== PowerLPF3_getHfxtAmpAdjustment ========
 */
int_fast8_t PowerLPF3_getHfxtAmpAdjustment(void)
{
    uint32_t hfxtAmplitude   = PowerCC27XX_getHfxtAmpMeasurement();
    uint32_t currentIrefTrim = CKMDGetTargetIrefTrim();

    if (hfxtAmplitude < 10 && currentIrefTrim < HFXT_TARGET_IREF_MAX)
    {
        /* Increase amplitude if the amplitude is too small */
        return +1;
    }
    else if (hfxtAmplitude > 16 && currentIrefTrim > HFXT_TARGET_IREF_MIN)
    {
        /* Decrease the amplitude if the amplitude is too big */
        return -1;
    }
    else
    {
        /* No adjustment is needed */
        return 0;
    }
}

/*
 *  ======== PowerLPF3_adjustHfxtAmp ========
 */
void PowerLPF3_adjustHfxtAmp(int_fast8_t adjustment)
{
    uint32_t newTargetIref = CKMDGetTargetIrefTrim() + adjustment;
    CKMDSetTargetIrefTrim(newTargetIref);
}

/*
 *  ======== PowerCC27XX_initialHfxtAmpCompClockCb ========
 * Callback function for the hfxtAmpCompClock object when it is used for the
 * initial HFXT amplitude compensation.
 *
 * params:
 *  - searchDone: If true (non-zero), the search for the optimal IREF value has
 *                already been found.
 */
static void PowerCC27XX_initialHfxtAmpCompClockCb(uintptr_t searchDone)
{

    /* Has the optimal IREF value been found?
     * Might be updated below.
     */
    bool optimalIref = (bool)searchDone;

    /* Has the IREF value been changed?
     * Will be updated below if the IREF value is changed.
     */
    bool irefChanged = false;

    /* Continue search if the optimal IREF has not yet been found */
    if (optimalIref == false)
    {
        /* Measure amplitude */
        uint32_t hfxtAmplitude = PowerCC27XX_getHfxtAmpMeasurement();

        /* Get current IREF value */
        uint32_t currentIrefTrim = CKMDGetTargetIrefTrim();

        /* Determine if IREF needs to be changed.
         * Note, PowerLPF3_getHfxtAmpAdjustment() is not used here, because the
         * amplitude limits used here are different.
         */
        if (hfxtAmplitude < 10 && currentIrefTrim < HFXT_TARGET_IREF_MAX)
        {
            /* Increase amplitude if the amplitude is too small */
            CKMDSetTargetIrefTrim(currentIrefTrim + 1);
            irefChanged = true;

            /* No new measurement is required, because we know
             * that IREF = currentIrefTrim + 1 causes an amplitude above 10,
             * otherwise IREF would not have been decreased.
             */
            optimalIref = true;
        }
        else if (hfxtAmplitude > 16 && currentIrefTrim > HFXT_TARGET_IREF_MIN)
        {
            /* Decrease amplitude if the amplitude is too big */
            CKMDSetTargetIrefTrim(currentIrefTrim - 1);
            irefChanged = true;
        }
        else
        {
            /* IREF is optimal since since the HFXT amplitude is already in
             * the optimal range (10-16)
             */
            optimalIref = true;
        }
    }

    if (irefChanged)
    {
        /* If IREF has changed, start the clock again. If the optimal IREF
         * value has been found, then the search is done, so in the next clock
         * callback no measurements are needed (searchDone is true).
         */
        ClockP_setFunc(&hfxtAmpCompClock, PowerCC27XX_initialHfxtAmpCompClockCb, optimalIref);

        /* Set timeout */
        ClockP_setTimeout(&hfxtAmpCompClock, HFXT_AMP_COMP_MEASUREMENT_US / ClockP_getSystemTickPeriod());

        /* Start clock */
        ClockP_start(&hfxtAmpCompClock);
    }
    else
    {
        /* Update hfxtAmpCompClock callback function (NULL because the clock is
         * no longer used)
         */
        ClockP_setFunc(&hfxtAmpCompClock, NULL, 0);

        Log_printf(LogModule_Power, Log_INFO, "PowerCC27XX_initialHfxtAmpCompClockCb: HFXT is available");

        /* Send HFXT available notification */
        PowerCC27XX_notify(PowerLPF3_HFXT_AVAILABLE);

        /* Allow standby again now that the optimal IREF value has been found */
        Power_releaseConstraint(PowerLPF3_DISALLOW_STANDBY);
    }
}

/*
 *  ======== PowerCC27XX_hfxtAmpsettledTimeout ========
 * Callback function for the hfxtAmpCompClock object when it is used as a
 * timeout for the AMPSETTLED signal. If the timeout occurs (i.e. if this
 * function is called), and if the HFXT FSM is stuck in the RAMP0 state,
 * it will be forced to the RAMP1 state.
 */
static void PowerCC27XX_hfxtAmpsettledTimeout(uintptr_t arg)
{
    /* Disable interrupts */
    uintptr_t key = HwiP_disable();

    /* Timeout occured while waiting for AMPSETTLED */
    if ((HWREG(CKMD_BASE + CKMD_O_AMPSTAT) & CKMD_AMPSTAT_STATE_M) == CKMD_AMPSTAT_STATE_RAMP0)
    {
        PowerCC27XX_forceHfxtFsmToRamp1();
    }

    HwiP_restore(key);
}

/*
 *  ======== PowerCC27XX_forceHfxtFsmToRamp1 ========
 * Force HFXT FSM to RAMP1 state. This function should only be called if the
 * HFXT FSM is stuck in RAMP0 state.
 */
static void PowerCC27XX_forceHfxtFsmToRamp1(void)
{
    /* Max out IREF */
    CKMDSetTargetIrefTrim(HFXT_TARGET_IREF_MAX);

    /* Force a transition to RAMP1 state */
    HWREG(CKMD_BASE + CKMD_O_AMPADCCTL) = (CKMD_AMPADCCTL_SWOVR | CKMD_AMPADCCTL_SRCSEL_PEAK |
                                           CKMD_AMPADCCTL_ADCEN_ENABLE | CKMD_AMPADCCTL_COMPSTRT);

    /* Wait until state changes */
    while ((HWREG(CKMD_BASE + CKMD_O_AMPSTAT) & CKMD_AMPSTAT_STATE_M) == CKMD_AMPSTAT_STATE_RAMP0) {}

    /* Disable AMPADCCTL.SWOVR and let the FSM take control again */
    HWREG(CKMD_BASE + CKMD_O_AMPADCCTL) &= ~CKMD_AMPADCCTL_SWOVR;
}

/*
 *  ======== PowerCC27XX_startContHfxtAmpMeasurements ========
 * Start continuous measurements of the HFXT amplitude.
 * This is done to quickly be able to read the amplitude using
 * PowerCC27XX_getHfxtAmpMeasurement() when a measurement is needed.
 */
static void PowerCC27XX_startContHfxtAmpMeasurements(void)
{
    /* Force amplitude measurement - Set SRCSEL = PEAK */
    HWREG(CKMD_BASE + CKMD_O_AMPADCCTL) = (CKMD_AMPADCCTL_SWOVR | CKMD_AMPADCCTL_PEAKDETEN_ENABLE |
                                           CKMD_AMPADCCTL_ADCEN_ENABLE | CKMD_AMPADCCTL_SRCSEL_PEAK |
                                           CKMD_AMPADCCTL_SARSTRT);
}

/*
 *  ======== PowerCC27XX_stopContHfxtAmpMeasurements ========
 * Stop the continuous HFXT amplitude measurements started by
 * PowerCC27XX_startContHfxtAmpMeasurements().
 */
static void PowerCC27XX_stopContHfxtAmpMeasurements(void)
{
    /* Clear SW override of Amplitude ADC */
    HWREG(CKMD_BASE + CKMD_O_AMPADCCTL) &= ~CKMD_AMPADCCTL_SWOVR;
}

/*
 *  ======== PowerCC27XX_getHfxtAmpMeasurement ========
 * Read the the latest HFXT amplitude measurement.
 * Continuous measurements must have been started using
 * PowerCC27XX_startContHfxtAmpMeasurements().
 */
static uint32_t PowerCC27XX_getHfxtAmpMeasurement(void)
{
    /* Read result in AMPADCSTAT */
    uint32_t ampAdcStat = HWREG(CKMD_BASE + CKMD_O_AMPADCSTAT);
    uint32_t peakRaw    = (ampAdcStat & CKMD_AMPADCSTAT_PEAKRAW_M) >> CKMD_AMPADCSTAT_PEAKRAW_S;
    uint32_t bias       = (ampAdcStat & CKMD_AMPADCSTAT_BIAS_M) >> CKMD_AMPADCSTAT_BIAS_S;

    /* Compute the PEAK value in SW to be able to handle negative values, which
     * can occur for small amplitudes.
     * In case of negative values, the result will be capped at 0.
     * According to the register descriptions PEAK = 2*PEAKRAW - BIAS
     */
    uint32_t result = 2 * peakRaw > bias ? 2 * peakRaw - bias : 0;

    return result;
}
