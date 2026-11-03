// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
#ifndef __linux__

#include "xstatus.h"
#ifdef SDT
#include "xparameters.h"
#endif
#include "xnilm_cnn.h"

extern XNilm_cnn_Config XNilm_cnn_ConfigTable[];

#ifdef SDT
XNilm_cnn_Config *XNilm_cnn_LookupConfig(UINTPTR BaseAddress) {
	XNilm_cnn_Config *ConfigPtr = NULL;

	int Index;

	for (Index = (u32)0x0; XNilm_cnn_ConfigTable[Index].Name != NULL; Index++) {
		if (!BaseAddress || XNilm_cnn_ConfigTable[Index].Control_BaseAddress == BaseAddress) {
			ConfigPtr = &XNilm_cnn_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XNilm_cnn_Initialize(XNilm_cnn *InstancePtr, UINTPTR BaseAddress) {
	XNilm_cnn_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XNilm_cnn_LookupConfig(BaseAddress);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XNilm_cnn_CfgInitialize(InstancePtr, ConfigPtr);
}
#else
XNilm_cnn_Config *XNilm_cnn_LookupConfig(u16 DeviceId) {
	XNilm_cnn_Config *ConfigPtr = NULL;

	int Index;

	for (Index = 0; Index < XPAR_XNILM_CNN_NUM_INSTANCES; Index++) {
		if (XNilm_cnn_ConfigTable[Index].DeviceId == DeviceId) {
			ConfigPtr = &XNilm_cnn_ConfigTable[Index];
			break;
		}
	}

	return ConfigPtr;
}

int XNilm_cnn_Initialize(XNilm_cnn *InstancePtr, u16 DeviceId) {
	XNilm_cnn_Config *ConfigPtr;

	Xil_AssertNonvoid(InstancePtr != NULL);

	ConfigPtr = XNilm_cnn_LookupConfig(DeviceId);
	if (ConfigPtr == NULL) {
		InstancePtr->IsReady = 0;
		return (XST_DEVICE_NOT_FOUND);
	}

	return XNilm_cnn_CfgInitialize(InstancePtr, ConfigPtr);
}
#endif

#endif

