`timescale 1ns/1ps
// Behavioral stubs isolate reset sequencing, not analog/XADC behavior.
module clk_wiz_sample(input clk_in1, input reset, output reg clk_out1=0,
                      output reg locked=0);
    always #19.53125 clk_out1=~clk_out1;
endmodule
module nilm_xadc_top(input clk,rst,vauxp2,vauxn2,vauxp10,vauxn10,
    output [11:0] voltage_adc,current_adc, output sample_valid,
    output [31:0] sample_count, output overrun);
    assign voltage_adc=0, current_adc=0, sample_valid=0, sample_count=0, overrun=0;
endmodule
module tb_nilm_sample_reset;
    reg clk_100m=0, resetn=0;
    always #5 clk_100m=~clk_100m;
    wire sample_clk,sample_rst,sample_valid,source_overrun;
    wire [11:0] voltage_adc,current_adc;
    wire [31:0] sample_id;
    nilm_sample_source dut(.clk_100m(clk_100m),.resetn(resetn),
        .vauxp2(1'b0),.vauxn2(1'b0),.vauxp10(1'b0),.vauxn10(1'b0),
        .sample_clk(sample_clk),.sample_rst(sample_rst),.sample_valid(sample_valid),
        .source_overrun(source_overrun),.voltage_adc(voltage_adc),
        .current_adc(current_adc),.sample_id(sample_id));
    integer k;
    initial begin
        #100; resetn=1;
        repeat(20) @(negedge sample_clk);
        if(!sample_rst) $fatal(1,"Released without clock lock");
        dut.u_clock.locked=1;
        for(k=1;k<=4;k=k+1) begin
            @(posedge sample_clk); #1;
            if(sample_rst !== (k<4)) $fatal(1,"Lock release latency");
        end
        #3; resetn=0; #1;
        if(!sample_rst) $fatal(1,"PS reset did not assert asynchronously");
        @(negedge sample_clk); resetn=1;
        for(k=1;k<=16;k=k+1) begin
            @(posedge sample_clk); #1;
            if(sample_rst !== (k<16)) $fatal(1,"PS release latency");
        end
        #3; dut.u_clock.locked=0; #1;
        if(!sample_rst) $fatal(1,"Lock loss did not assert asynchronously");
        @(negedge sample_clk); resetn=0; dut.u_clock.locked=1;
        repeat(8) @(negedge sample_clk);
        if(!sample_rst) $fatal(1,"Lock recovery bypassed PS reset");
        resetn=1; repeat(16) @(posedge sample_clk); #1;
        if(sample_rst) $fatal(1,"Did not recover");
        $display("PASS: independent PS/lock reset assertion, release, and overlap");
        $finish;
    end
    initial begin #10000; $fatal(1,"Reset test timeout"); end
endmodule
