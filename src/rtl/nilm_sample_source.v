`timescale 1ns/1ps
module nilm_sample_source (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_100m CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME clk_100m, FREQ_HZ 100000000, ASSOCIATED_RESET resetn" *)
    input wire clk_100m,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 resetn RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME resetn, POLARITY ACTIVE_LOW" *)
    input wire resetn,
    input wire vauxp2,
    input wire vauxn2,
    input wire vauxp10,
    input wire vauxn10,
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 sample_clk CLK" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME sample_clk, FREQ_HZ 25600000, ASSOCIATED_RESET sample_rst" *)
    output wire sample_clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 sample_rst RST" *)
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME sample_rst, POLARITY ACTIVE_HIGH" *)
    output wire sample_rst,
    output wire [11:0] voltage_adc,
    output wire [11:0] current_adc,
    output wire sample_valid,
    output wire [31:0] sample_id,
    output wire source_overrun
);
    wire locked;
    clk_wiz_sample u_clock (
        .clk_in1(clk_100m), .reset(~resetn),
        .clk_out1(sample_clk), .locked(locked)
    );
    // Synchronize each asynchronous reset source independently. Combining
    // sources before a synchronizer can create a short deassertion glitch.
    (* ASYNC_REG = "TRUE" *) reg [15:0] reset_pipe = 16'hffff;
    always @(posedge sample_clk or negedge resetn) begin
        if(!resetn) reset_pipe <= 16'hffff;
        else reset_pipe <= {reset_pipe[14:0],1'b0};
    end
    (* ASYNC_REG = "TRUE" *) reg [3:0] lock_reset_pipe = 4'hf;
    always @(posedge sample_clk or negedge locked) begin
        if(!locked) lock_reset_pipe <= 4'hf;
        else lock_reset_pipe <= {lock_reset_pipe[2:0],1'b0};
    end
    // Both releases now occur on sample_clk; consumers use synchronous reset.
    assign sample_rst = reset_pipe[15] | lock_reset_pipe[3];
    nilm_xadc_top u_core (
        .clk(sample_clk), .rst(sample_rst),
        .vauxp2(vauxp2), .vauxn2(vauxn2),
        .vauxp10(vauxp10), .vauxn10(vauxn10),
        .voltage_adc(voltage_adc), .current_adc(current_adc),
        .sample_valid(sample_valid), .sample_count(sample_id),
        .overrun(source_overrun)
    );
endmodule
