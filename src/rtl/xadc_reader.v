`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2026/09/28 15:01:23
// Design Name: 
// Module Name: xadc_reader
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module xadc_reader (
    input  wire        clk,           // 100 MHz
    input  wire        rst,           // 高电平复位

    // 来自 XADC Wizard
    input  wire        eoc,           // ADC 转换完成
    input  wire        drdy,          // DRP 读取完成
    input  wire [15:0] do_data,       // DRP 返回数据

    // 发往 XADC Wizard
    output reg  [6:0]  daddr,         // DRP 寄存器地址
    output reg         den,           // DRP 请求，只拉高一周期
    output wire        dwe,           // 写使能，本模块只读
    output wire [15:0] di_data,       // 写数据，本模块不用

    // 交给后续处理模块
    output reg  [11:0] voltage_adc,
    output reg  [11:0] current_adc,
    output reg         sample_valid, // 一对数据有效，拉高一周期
    output reg  [31:0] sample_count, // 成功输出的采样对数量
    output reg         overrun       // 读取未完成就来了新转换，置1
);

    // VAUX2 和 VAUX10 的结果寄存器地址
    localparam [6:0] ADDR_VOLTAGE = 7'h12;
    localparam [6:0] ADDR_CURRENT = 7'h1A;

    localparam [2:0]
        ST_IDLE       = 3'd0,
        ST_REQ_V      = 3'd1,
        ST_WAIT_V     = 3'd2,
        ST_GAP        = 3'd3,
        ST_REQ_I      = 3'd4,
        ST_WAIT_I     = 3'd5,
        ST_FINISH     = 3'd6;

    reg [2:0]  state;
    reg [11:0] voltage_temp;
    reg        drop_pair;

    // 只读取，不写入 XADC 寄存器
    assign dwe     = 1'b0;
    assign di_data = 16'h0000;

    always @(posedge clk) begin
        if (rst) begin
            state         <= ST_IDLE;
            daddr         <= 7'h00;
            den           <= 1'b0;
            voltage_temp  <= 12'd0;
            voltage_adc   <= 12'd0;
            current_adc   <= 12'd0;
            sample_valid  <= 1'b0;
            sample_count  <= 32'd0;
            overrun       <= 1'b0;
            drop_pair     <= 1'b0;
        end else begin
            // 默认拉低，只有指定状态才拉高
            den          <= 1'b0;
            sample_valid <= 1'b0;

            // 当前读取尚未完成，下一次转换已经完成：
            // 丢弃这一对，避免输出不同次转换的数据。
            if (eoc && (state != ST_IDLE)) begin
                overrun   <= 1'b1;
                drop_pair <= 1'b1;
            end

            case (state)
                ST_IDLE: begin
                    if (eoc) begin
                        drop_pair <= 1'b0;
                        state     <= ST_REQ_V;
                    end
                end

                ST_REQ_V: begin
                    daddr <= ADDR_VOLTAGE;
                    den   <= 1'b1;
                    state <= ST_WAIT_V;
                end

                ST_WAIT_V: begin
                    if (drdy) begin
                        voltage_temp <= do_data[15:4];
                        state        <= ST_GAP;
                    end
                end

                ST_GAP: begin
                    // 等前一次读取的 DRDY 回到低电平
                    if (!drdy)
                        state <= ST_REQ_I;
                end

                ST_REQ_I: begin
                    daddr <= ADDR_CURRENT;
                    den   <= 1'b1;
                    state <= ST_WAIT_I;
                end

                ST_WAIT_I: begin
                    if (drdy) begin
                        // eoc 同周期到达时，也不输出这一对
                        if (!drop_pair && !eoc) begin
                            voltage_adc  <= voltage_temp;
                            current_adc  <= do_data[15:4];
                            sample_valid <= 1'b1;
                            sample_count <= sample_count + 32'd1;
                        end
                        state <= ST_FINISH;
                    end
                end

                ST_FINISH: begin
                    if (!drdy)
                        state <= ST_IDLE;
                end

                default: begin
                    state <= ST_IDLE;
                end
            endcase
        end
    end

endmodule
