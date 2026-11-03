// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef XNILM_CNN_H
#define XNILM_CNN_H

#ifdef __cplusplus
extern "C" {
#endif

/***************************** Include Files *********************************/
#ifndef __linux__
#include "xil_types.h"
#include "xil_assert.h"
#include "xstatus.h"
#include "xil_io.h"
#else
#include <stdint.h>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stddef.h>
#endif
#include "xnilm_cnn_hw.h"

/**************************** Type Definitions ******************************/
#ifdef __linux__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
typedef struct {
#ifdef SDT
    char *Name;
#else
    u16 DeviceId;
#endif
    u64 Control_BaseAddress;
} XNilm_cnn_Config;
#endif

typedef struct {
    u64 Control_BaseAddress;
    u32 IsReady;
} XNilm_cnn;

typedef u32 word_type;

/***************** Macros (Inline Functions) Definitions *********************/
#ifndef __linux__
#define XNilm_cnn_WriteReg(BaseAddress, RegOffset, Data) \
    Xil_Out32((BaseAddress) + (RegOffset), (u32)(Data))
#define XNilm_cnn_ReadReg(BaseAddress, RegOffset) \
    Xil_In32((BaseAddress) + (RegOffset))
#else
#define XNilm_cnn_WriteReg(BaseAddress, RegOffset, Data) \
    *(volatile u32*)((BaseAddress) + (RegOffset)) = (u32)(Data)
#define XNilm_cnn_ReadReg(BaseAddress, RegOffset) \
    *(volatile u32*)((BaseAddress) + (RegOffset))

#define Xil_AssertVoid(expr)    assert(expr)
#define Xil_AssertNonvoid(expr) assert(expr)

#define XST_SUCCESS             0
#define XST_DEVICE_NOT_FOUND    2
#define XST_OPEN_DEVICE_FAILED  3
#define XIL_COMPONENT_IS_READY  1
#endif

/************************** Function Prototypes *****************************/
#ifndef __linux__
#ifdef SDT
int XNilm_cnn_Initialize(XNilm_cnn *InstancePtr, UINTPTR BaseAddress);
XNilm_cnn_Config* XNilm_cnn_LookupConfig(UINTPTR BaseAddress);
#else
int XNilm_cnn_Initialize(XNilm_cnn *InstancePtr, u16 DeviceId);
XNilm_cnn_Config* XNilm_cnn_LookupConfig(u16 DeviceId);
#endif
int XNilm_cnn_CfgInitialize(XNilm_cnn *InstancePtr, XNilm_cnn_Config *ConfigPtr);
#else
int XNilm_cnn_Initialize(XNilm_cnn *InstancePtr, const char* InstanceName);
int XNilm_cnn_Release(XNilm_cnn *InstancePtr);
#endif

void XNilm_cnn_Start(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_IsDone(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_IsIdle(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_IsReady(XNilm_cnn *InstancePtr);
void XNilm_cnn_EnableAutoRestart(XNilm_cnn *InstancePtr);
void XNilm_cnn_DisableAutoRestart(XNilm_cnn *InstancePtr);

void XNilm_cnn_Set_input_r(XNilm_cnn *InstancePtr, u64 Data);
u64 XNilm_cnn_Get_input_r(XNilm_cnn *InstancePtr);
void XNilm_cnn_Set_output_r(XNilm_cnn *InstancePtr, u64 Data);
u64 XNilm_cnn_Get_output_r(XNilm_cnn *InstancePtr);
void XNilm_cnn_Set_request_abi(XNilm_cnn *InstancePtr, u32 Data);
u32 XNilm_cnn_Get_request_abi(XNilm_cnn *InstancePtr);
void XNilm_cnn_Set_request_model(XNilm_cnn *InstancePtr, u32 Data);
u32 XNilm_cnn_Get_request_model(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_Get_status(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_Get_status_vld(XNilm_cnn *InstancePtr);

void XNilm_cnn_InterruptGlobalEnable(XNilm_cnn *InstancePtr);
void XNilm_cnn_InterruptGlobalDisable(XNilm_cnn *InstancePtr);
void XNilm_cnn_InterruptEnable(XNilm_cnn *InstancePtr, u32 Mask);
void XNilm_cnn_InterruptDisable(XNilm_cnn *InstancePtr, u32 Mask);
void XNilm_cnn_InterruptClear(XNilm_cnn *InstancePtr, u32 Mask);
u32 XNilm_cnn_InterruptGetEnabled(XNilm_cnn *InstancePtr);
u32 XNilm_cnn_InterruptGetStatus(XNilm_cnn *InstancePtr);

#ifdef __cplusplus
}
#endif

#endif
