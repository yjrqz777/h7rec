/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cherryusb_app.h"
#include "SEGGER_RTT.h"
#include "usbd_core.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint32_t Fault_PrintFlag(uint32_t reg, uint32_t mask, const char *name);
static void Fault_PrintCfsr(uint32_t cfsr);
static void Fault_PrintHfsr(uint32_t hfsr);
static void Fault_PrintDfsr(uint32_t dfsr);
static void Fault_PrintShcsr(uint32_t shcsr);
static void Fault_PrintAbfsr(uint32_t abfsr);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint32_t Fault_PrintFlag(uint32_t reg, uint32_t mask, const char *name)
{
  if ((reg & mask) == 0U)
  {
    return 0U;
  }

  SEGGER_RTT_WriteString(0, "[FAULT]   ");
  SEGGER_RTT_WriteString(0, name);
  SEGGER_RTT_WriteString(0, "\r\n");
  return 1U;
}

static void Fault_PrintCfsr(uint32_t cfsr)
{
  uint32_t printed = 0U;

  SEGGER_RTT_WriteString(0, "[FAULT] CFSR decode:\r\n");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_IACCVIOL_Msk, "MMFSR.IACCVIOL: instruction access violation");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_DACCVIOL_Msk, "MMFSR.DACCVIOL: data access violation");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_MUNSTKERR_Msk, "MMFSR.MUNSTKERR: exception return unstacking error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_MSTKERR_Msk, "MMFSR.MSTKERR: exception entry stacking error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_MLSPERR_Msk, "MMFSR.MLSPERR: lazy FP state preservation error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_MMARVALID_Msk, "MMFSR.MMARVALID: MMFAR has valid fault address");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_IBUSERR_Msk, "BFSR.IBUSERR: instruction bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_PRECISERR_Msk, "BFSR.PRECISERR: precise data bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_IMPRECISERR_Msk, "BFSR.IMPRECISERR: imprecise data bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_UNSTKERR_Msk, "BFSR.UNSTKERR: exception return unstacking bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_STKERR_Msk, "BFSR.STKERR: exception entry stacking bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_LSPERR_Msk, "BFSR.LSPERR: lazy FP state preservation bus error");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_BFARVALID_Msk, "BFSR.BFARVALID: BFAR has valid fault address");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_UNDEFINSTR_Msk, "UFSR.UNDEFINSTR: undefined instruction");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_INVSTATE_Msk, "UFSR.INVSTATE: invalid EPSR/T-state");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_INVPC_Msk, "UFSR.INVPC: invalid exception return PC/LR");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_NOCP_Msk, "UFSR.NOCP: coprocessor/FPU access denied");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_UNALIGNED_Msk, "UFSR.UNALIGNED: unaligned access");
  printed += Fault_PrintFlag(cfsr, SCB_CFSR_DIVBYZERO_Msk, "UFSR.DIVBYZERO: divide by zero");

  if (printed == 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT]   none\r\n");
  }
}

static void Fault_PrintHfsr(uint32_t hfsr)
{
  uint32_t printed = 0U;

  SEGGER_RTT_WriteString(0, "[FAULT] HFSR decode:\r\n");
  printed += Fault_PrintFlag(hfsr, SCB_HFSR_VECTTBL_Msk, "VECTTBL: fault during vector table read");
  printed += Fault_PrintFlag(hfsr, SCB_HFSR_FORCED_Msk, "FORCED: configurable fault escalated to HardFault");
  printed += Fault_PrintFlag(hfsr, SCB_HFSR_DEBUGEVT_Msk, "DEBUGEVT: debug event caused HardFault");

  if (printed == 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT]   none\r\n");
  }
}

static void Fault_PrintDfsr(uint32_t dfsr)
{
  uint32_t printed = 0U;

  SEGGER_RTT_WriteString(0, "[FAULT] DFSR decode:\r\n");
  printed += Fault_PrintFlag(dfsr, SCB_DFSR_HALTED_Msk, "HALTED: halt request debug event");
  printed += Fault_PrintFlag(dfsr, SCB_DFSR_BKPT_Msk, "BKPT: breakpoint instruction/debug event");
  printed += Fault_PrintFlag(dfsr, SCB_DFSR_DWTTRAP_Msk, "DWTTRAP: DWT watchpoint/trace event");
  printed += Fault_PrintFlag(dfsr, SCB_DFSR_VCATCH_Msk, "VCATCH: vector catch debug event");
  printed += Fault_PrintFlag(dfsr, SCB_DFSR_EXTERNAL_Msk, "EXTERNAL: external debug request");

  if (printed == 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT]   none\r\n");
  }
}

static void Fault_PrintShcsr(uint32_t shcsr)
{
  uint32_t printed = 0U;

  SEGGER_RTT_WriteString(0, "[FAULT] SHCSR decode:\r\n");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_MEMFAULTACT_Msk, "MEMFAULTACT: MemManage active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_BUSFAULTACT_Msk, "BUSFAULTACT: BusFault active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_USGFAULTACT_Msk, "USGFAULTACT: UsageFault active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_SVCALLACT_Msk, "SVCALLACT: SVCall active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_MONITORACT_Msk, "MONITORACT: DebugMonitor active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_PENDSVACT_Msk, "PENDSVACT: PendSV active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_SYSTICKACT_Msk, "SYSTICKACT: SysTick active");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_USGFAULTPENDED_Msk, "USGFAULTPENDED: UsageFault pending");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_MEMFAULTPENDED_Msk, "MEMFAULTPENDED: MemManage pending");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_BUSFAULTPENDED_Msk, "BUSFAULTPENDED: BusFault pending");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_SVCALLPENDED_Msk, "SVCALLPENDED: SVCall pending");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_MEMFAULTENA_Msk, "MEMFAULTENA: MemManage enabled");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_BUSFAULTENA_Msk, "BUSFAULTENA: BusFault enabled");
  printed += Fault_PrintFlag(shcsr, SCB_SHCSR_USGFAULTENA_Msk, "USGFAULTENA: UsageFault enabled");

  if (printed == 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT]   none\r\n");
  }
}

static void Fault_PrintAbfsr(uint32_t abfsr)
{
  uint32_t printed = 0U;

  SEGGER_RTT_WriteString(0, "[FAULT] ABFSR decode:\r\n");
  printed += Fault_PrintFlag(abfsr, SCB_ABFSR_ITCM_Msk, "ITCM: fault on ITCM interface");
  printed += Fault_PrintFlag(abfsr, SCB_ABFSR_DTCM_Msk, "DTCM: fault on DTCM interface");
  printed += Fault_PrintFlag(abfsr, SCB_ABFSR_AHBP_Msk, "AHBP: fault on AHBP interface");
  printed += Fault_PrintFlag(abfsr, SCB_ABFSR_AXIM_Msk, "AXIM: fault on AXIM interface");
  printed += Fault_PrintFlag(abfsr, SCB_ABFSR_EPPB_Msk, "EPPB: fault on EPPB interface");

  if ((abfsr & SCB_ABFSR_AXIM_Msk) != 0U)
  {
    SEGGER_RTT_printf(0, "[FAULT]   AXIMTYPE=%u\r\n",
                      (abfsr & SCB_ABFSR_AXIMTYPE_Msk) >> SCB_ABFSR_AXIMTYPE_Pos);
  }

  if (printed == 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT]   none\r\n");
  }
}

static void Fault_PrintExceptionFrame(uint32_t *stack, uint32_t exc_return)
{
  if (stack == NULL)
  {
    return;
  }

  SEGGER_RTT_printf(0,
                    "[FAULT] EXC_RETURN=0x%08X SP=0x%08X\r\n",
                    exc_return,
                    (uint32_t)stack);
  SEGGER_RTT_printf(0,
                    "[FAULT] R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X\r\n",
                    stack[0], stack[1], stack[2], stack[3]);
  SEGGER_RTT_printf(0,
                    "[FAULT] R12=0x%08X LR=0x%08X PC=0x%08X xPSR=0x%08X\r\n",
                    stack[4], stack[5], stack[6], stack[7]);
}

void Fault_PrintInfo(const char *handler_name, uint32_t print_stack_regs);

void HardFault_HandlerC(uint32_t *stack, uint32_t exc_return)
{
  Fault_PrintInfo("HardFault_Handler", 1U);
  Fault_PrintExceptionFrame(stack, exc_return);

  while (1)
  {
  }
}

void Fault_PrintInfo(const char *handler_name, uint32_t print_stack_regs)
{
  uint32_t cfsr = SCB->CFSR;
  uint32_t hfsr = SCB->HFSR;
  uint32_t dfsr = SCB->DFSR;
  uint32_t afsr = SCB->AFSR;
  uint32_t abfsr = SCB->ABFSR;
  uint32_t shcsr = SCB->SHCSR;
  uint32_t mmfar = SCB->MMFAR;
  uint32_t bfar = SCB->BFAR;

  SEGGER_RTT_WriteString(0, "\r\n[FAULT] ");
  SEGGER_RTT_WriteString(0, handler_name);
  SEGGER_RTT_WriteString(0, "\r\n");
  SEGGER_RTT_printf(0, "[FAULT] CFSR=0x%08X HFSR=0x%08X DFSR=0x%08X AFSR=0x%08X\r\n",
                    cfsr, hfsr, dfsr, afsr);
  SEGGER_RTT_printf(0, "[FAULT] MMFAR=0x%08X BFAR=0x%08X SHCSR=0x%08X ABFSR=0x%08X\r\n",
                    mmfar, bfar, shcsr, abfsr);

  if (print_stack_regs != 0U)
  {
    SEGGER_RTT_printf(0, "[FAULT] IPSR=%u MSP=0x%08X PSP=0x%08X\r\n",
                      __get_IPSR(), __get_MSP(), __get_PSP());
  }

  if ((cfsr & SCB_CFSR_MMARVALID_Msk) != 0U)
  {
    SEGGER_RTT_printf(0, "[FAULT] MemManage fault address: 0x%08X\r\n", mmfar);
  }

  if ((cfsr & SCB_CFSR_BFARVALID_Msk) != 0U)
  {
    SEGGER_RTT_printf(0, "[FAULT] BusFault fault address: 0x%08X\r\n", bfar);
  }

  if (afsr != 0U)
  {
    SEGGER_RTT_WriteString(0, "[FAULT] AFSR: implementation-defined auxiliary fault bits set\r\n");
  }

  Fault_PrintCfsr(cfsr);
  Fault_PrintHfsr(hfsr);
  Fault_PrintDfsr(dfsr);
  Fault_PrintShcsr(shcsr);
  Fault_PrintAbfsr(abfsr);
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern SD_HandleTypeDef hsd1;
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
extern TIM_HandleTypeDef htim17;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  Fault_PrintInfo("NMI_Handler", 0U);

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile
  (
    "tst lr, #4                             \n"
    "ite eq                                 \n"
    "mrseq r0, msp                          \n"
    "mrsne r0, psp                          \n"
    "mov r1, lr                             \n"
    "b HardFault_HandlerC                   \n"
  );
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  Fault_PrintInfo("MemManage_Handler", 1U);

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  Fault_PrintInfo("BusFault_Handler", 1U);

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  Fault_PrintInfo("UsageFault_Handler", 1U);

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */
  Fault_PrintInfo("DebugMon_Handler", 1U);

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles SDMMC1 global interrupt.
  */
void SDMMC1_IRQHandler(void)
{
  /* USER CODE BEGIN SDMMC1_IRQn 0 */

  /* USER CODE END SDMMC1_IRQn 0 */
  HAL_SD_IRQHandler(&hsd1);
  /* USER CODE BEGIN SDMMC1_IRQn 1 */

  /* USER CODE END SDMMC1_IRQn 1 */
}

/**
  * @brief This function handles USB On The Go FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  /* USER CODE BEGIN OTG_FS_IRQn 0 */
  if (CherryUSB_DeviceIsReady())
  {
    USBD_IRQHandler(0);
  }
  return;

  /* USER CODE END OTG_FS_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
  /* USER CODE BEGIN OTG_FS_IRQn 1 */

  /* USER CODE END OTG_FS_IRQn 1 */
}

/**
  * @brief This function handles TIM17 global interrupt.
  */
void TIM17_IRQHandler(void)
{
  /* USER CODE BEGIN TIM17_IRQn 0 */

  /* USER CODE END TIM17_IRQn 0 */
  HAL_TIM_IRQHandler(&htim17);
  /* USER CODE BEGIN TIM17_IRQn 1 */

  /* USER CODE END TIM17_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
