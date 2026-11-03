`timescale 1ns/1ps
// GPIO channel 1 bit 0: request toggle (NOT a pulse).
// GPIO channel 2: [0] busy, [1] full, [2] sample dropped while writing,
// [3] XADC overrun, [4] acknowledged request toggle, [5] sample domain ready.
// Software waits for ready; toggles request once while idle; first observes
// ACK equal to request AND busy AND !full, then waits for !busy AND full.
// Never start while reading RAM. Poll the busy acknowledgement promptly.
// Both clock domains must be reset together at system initialization.
module nilm_bram_writer #(
    parameter integer FRAME_SAMPLES = 4096
) (
    input wire sample_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 sample_rst RST" *)
(* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME sample_rst, POLARITY ACTIVE_HIGH" *)
    input wire sample_rst,
    input wire axi_clk,
    input wire axi_resetn,
    input wire [31:0] control_gpio,
    output wire [31:0] status_gpio,
    input wire sample_valid,
    input wire [31:0] sample_id,
    input wire [11:0] voltage_adc,
    input wire [11:0] current_adc,
    input wire source_overrun,
    // BRAM Controller mode uses BYTE addresses, including on port B.
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME BRAM, MASTER_TYPE BRAM_CTRL" *)
    output wire bram_clk,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM RST" *)
    output wire bram_rst,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM EN" *)
    output wire bram_en,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM WE" *)
    output wire [3:0] bram_we,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM ADDR" *)
    output wire [31:0] bram_addr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM DIN" *)
    output wire [31:0] bram_wrdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:bram:1.0 BRAM DOUT" *)
    input wire [31:0] bram_rddata
);
    localparam IDLE=2'd0, CAPTURE=2'd1, WRITE_LOW=2'd2, WRITE_HIGH=2'd3;
    reg [1:0] state;
    reg [12:0] pair_index;
    reg [31:0] held_data, held_id;
    reg full, dropped, acknowledged, ready, busy;
    (* ASYNC_REG="TRUE" *) reg [1:0] request_sync;
    wire new_request = request_sync[1] != acknowledged;
    always @(posedge sample_clk) begin
        if (sample_rst) request_sync <= 0;
        else request_sync <= {request_sync[0], control_gpio[0]};
    end
    always @(posedge sample_clk) begin
        if (sample_rst) begin
            state<=IDLE; pair_index<=0; full<=0; dropped<=0; busy<=0;
            acknowledged<=0; ready<=0; held_data<=0; held_id<=0;
        end else begin
            ready<=1;
            // Requests during capture are deliberately ignored. Software must
            // wait until idle before changing the toggle again.
            if (new_request) acknowledged <= request_sync[1];
            if (new_request && !busy) begin
                state<=CAPTURE; pair_index<=0; full<=0; dropped<=0; busy<=1;
            end else begin
                case (state)
                    CAPTURE: if (sample_valid) begin
                        held_data <= {4'b0,voltage_adc,4'b0,current_adc};
                        held_id <= sample_id;
                        state <= WRITE_LOW;
                    end
                    WRITE_LOW: begin
                        if(sample_valid) dropped<=1;
                        state<=WRITE_HIGH;
                    end
                    WRITE_HIGH: begin
                        if(sample_valid) dropped<=1;
                        // At this edge the RAM accepts the last word.
                        if(pair_index == FRAME_SAMPLES-1) begin
                            full<=1; state<=IDLE; busy<=0;
                        end else begin
                            pair_index<=pair_index+1'b1; state<=CAPTURE;
                        end
                    end
                    default: begin state<=IDLE; busy<=0; end
                endcase
            end
        end
    end
    assign bram_clk=sample_clk;
    // Port B is write-only; its output register does not need a reset.
    // RAM control must depend only on synchronously clocked state. An
    // asynchronous sample_rst must not glitch EN/WE. At the reset edge a
    // pending write may finish; that frame is invalidated and the next
    // capture overwrites every word before FULL can be published again.
    assign bram_rst=1'b0;
    assign bram_en=(state==WRITE_LOW)||(state==WRITE_HIGH);
    assign bram_we=bram_en ? 4'hf : 4'h0;
    assign bram_addr={16'b0,pair_index,3'b000} + ((state==WRITE_HIGH)?32'd4:32'd0);
    assign bram_wrdata=(state==WRITE_HIGH)?held_id:held_data;
    // Independent stable status flags, not a multi-bit data CDC. Software
    // polls ACK+full and rejects overflow flags; this is not an atomic bus.
    wire [5:0] flags={ready,acknowledged,source_overrun,dropped,full,busy};
    (* ASYNC_REG="TRUE" *) reg [5:0] flags_meta,flags_sync;
    always @(posedge axi_clk) begin
        if(!axi_resetn) begin flags_meta<=0; flags_sync<=0; end
        else begin flags_meta<=flags; flags_sync<=flags_meta; end
    end
    assign status_gpio={26'b0,flags_sync};
endmodule
