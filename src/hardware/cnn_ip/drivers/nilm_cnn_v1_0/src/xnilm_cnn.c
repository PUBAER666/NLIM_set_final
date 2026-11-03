// ==============================================================
// Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2025.2 (64-bit)
// Tool Version Limit: 2025.11
// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// Copyright 2022-2025 Advanced Micro Devices, Inc. All Rights Reserved.
// 
// ==============================================================
/***************************** Include Files *********************************/
#include "xnilm_cnn.h"

/************************** Function Implementation *************************/
#ifndef __linux__
int XNilm_cnn_CfgInitialize(XNilm_cnn *InstancePtr, XNilm_cnn_Config *ConfigPtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(ConfigPtr != NULL);

    InstancePtr->Control_BaseAddress = ConfigPtr->Control_BaseAddress;
    InstancePtr->IsReady = XIL_COMPONENT_IS_READY;

    return XST_SUCCESS;
}
#endif

void XNilm_cnn_Start(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL) & 0x80;
    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL, Data | 0x01);
}

u32 XNilm_cnn_IsDone(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL);
    return (Data >> 1) & 0x1;
}

u32 XNilm_cnn_IsIdle(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL);
    return (Data >> 2) & 0x1;
}

u32 XNilm_cnn_IsReady(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL);
    // check ap_start to see if the pcore is ready for next input
    return !(Data & 0x1);
}

void XNilm_cnn_EnableAutoRestart(XNilm_cnn *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL, 0x80);
}

void XNilm_cnn_DisableAutoRestart(XNilm_cnn *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_AP_CTRL, 0);
}

void XNilm_cnn_Set_input_r(XNilm_cnn *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_INPUT_R_DATA, (u32)(Data));
    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_INPUT_R_DATA + 4, (u32)(Data >> 32));
}

u64 XNilm_cnn_Get_input_r(XNilm_cnn *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_INPUT_R_DATA);
    Data += (u64)XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_INPUT_R_DATA + 4) << 32;
    return Data;
}

void XNilm_cnn_Set_output_r(XNilm_cnn *InstancePtr, u64 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_OUTPUT_R_DATA, (u32)(Data));
    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_OUTPUT_R_DATA + 4, (u32)(Data >> 32));
}

u64 XNilm_cnn_Get_output_r(XNilm_cnn *InstancePtr) {
    u64 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_OUTPUT_R_DATA);
    Data += (u64)XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_OUTPUT_R_DATA + 4) << 32;
    return Data;
}

void XNilm_cnn_Set_request_abi(XNilm_cnn *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_REQUEST_ABI_DATA, Data);
}

u32 XNilm_cnn_Get_request_abi(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_REQUEST_ABI_DATA);
    return Data;
}

void XNilm_cnn_Set_request_model(XNilm_cnn *InstancePtr, u32 Data) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_REQUEST_MODEL_DATA, Data);
}

u32 XNilm_cnn_Get_request_model(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_REQUEST_MODEL_DATA);
    return Data;
}

u32 XNilm_cnn_Get_status(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_STATUS_DATA);
    return Data;
}

u32 XNilm_cnn_Get_status_vld(XNilm_cnn *InstancePtr) {
    u32 Data;

    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Data = XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_STATUS_CTRL);
    return Data & 0x1;
}

void XNilm_cnn_InterruptGlobalEnable(XNilm_cnn *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_GIE, 1);
}

void XNilm_cnn_InterruptGlobalDisable(XNilm_cnn *InstancePtr) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_GIE, 0);
}

void XNilm_cnn_InterruptEnable(XNilm_cnn *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_IER);
    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_IER, Register | Mask);
}

void XNilm_cnn_InterruptDisable(XNilm_cnn *InstancePtr, u32 Mask) {
    u32 Register;

    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    Register =  XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_IER);
    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_IER, Register & (~Mask));
}

void XNilm_cnn_InterruptClear(XNilm_cnn *InstancePtr, u32 Mask) {
    Xil_AssertVoid(InstancePtr != NULL);
    Xil_AssertVoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    XNilm_cnn_WriteReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_ISR, Mask);
}

u32 XNilm_cnn_InterruptGetEnabled(XNilm_cnn *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_IER);
}

u32 XNilm_cnn_InterruptGetStatus(XNilm_cnn *InstancePtr) {
    Xil_AssertNonvoid(InstancePtr != NULL);
    Xil_AssertNonvoid(InstancePtr->IsReady == XIL_COMPONENT_IS_READY);

    return XNilm_cnn_ReadReg(InstancePtr->Control_BaseAddress, XNILM_CNN_CONTROL_ADDR_ISR);
}

