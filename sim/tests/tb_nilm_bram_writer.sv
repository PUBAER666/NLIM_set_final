`timescale 1ns/1ps
module tb_nilm_bram_writer;
    localparam N = 4096;
    reg sample_clk=0, axi_clk=0, sample_rst=1, axi_resetn=0;
    always #19.53125 sample_clk=~sample_clk;
    initial begin #2.3; forever #5 axi_clk=~axi_clk; end
    reg [31:0] control_gpio=0;
    wire [31:0] status_gpio;
    reg sample_valid=0, source_overrun=0;
    reg [31:0] sample_id=0;
    reg [11:0] voltage_adc=0, current_adc=0;
    wire bram_clk, bram_rst, bram_en;
    wire [3:0] bram_we;
    wire [31:0] bram_addr, bram_wrdata;
    wire [31:0] bram_rddata=0;
    nilm_bram_writer #(.FRAME_SAMPLES(N)) dut (.*);
    reg [31:0] ram [0:N*2-1];
    integer writes=0, frames=0;
    reg [31:0] first_id;
    reg checking=0;
    integer j;

    function automatic [31:0] packed_adc(input [31:0] id);
        packed_adc = {4'b0,id[11:0]^12'ha53,4'b0,~id[11:0]};
    endfunction

    always @(posedge bram_clk) begin
        if (bram_en) begin
            if (bram_rst || bram_we !== 4'hf || bram_addr[1:0] != 0 || bram_addr >= N*8)
                $fatal(1,"Invalid RAM write interface");
            if (checking) begin
                if (bram_addr !== writes*4) $fatal(1,"Address not sequential: %h", bram_addr);
                if (writes%2==0 && bram_wrdata !== packed_adc(first_id+writes/2))
                    $fatal(1,"ADC word mismatch at %0d",writes);
                if (writes%2==1 && bram_wrdata !== first_id+writes/2)
                    $fatal(1,"ID word mismatch at %0d",writes);
            end
            ram[bram_addr/4] = bram_wrdata;
            writes=writes+1;
        end
    end

    task automatic reset_all;
        begin
            sample_rst=1; axi_resetn=0; control_gpio=0; sample_valid=0;
            source_overrun=0; checking=0;
            repeat(5) @(negedge sample_clk);
            sample_rst=0; axi_resetn=1;
            wait(status_gpio[5]); repeat(3) @(negedge axi_clk);
        end
    endtask

    task automatic start_capture;
        begin
            @(negedge axi_clk); control_gpio[0]=~control_gpio[0];
            wait(status_gpio[4] == control_gpio[0] && status_gpio[0] && !status_gpio[1]);
        end
    endtask

    task automatic send_pair(input [31:0] id);
        begin
            @(negedge sample_clk);
            sample_id=id; voltage_adc=id[11:0]^12'ha53; current_adc=~id[11:0];
            sample_valid=1;
            @(negedge sample_clk); sample_valid=0;
            repeat(7) @(negedge sample_clk);
        end
    endtask

    task automatic normal_frame(input [31:0] first);
        integer k, old_writes;
        begin
            first_id=first; writes=0; checking=1;
            start_capture();
            for(k=0;k<N;k=k+1) send_pair(first+k);
            wait(!status_gpio[0] && status_gpio[1]);
            if(writes != 2*N || status_gpio[3:2] != 0) $fatal(1,"Bad completion");
            if(ram[0] !== packed_adc(first) || ram[2*N-1] !== first+N-1)
                $fatal(1,"Frozen boundary words wrong");
            old_writes=writes;
            send_pair(32'h76543210);
            repeat(20) @(negedge sample_clk);
            if(writes != old_writes || !status_gpio[1]) $fatal(1,"Frozen frame overwritten");
            frames=frames+1; checking=0;
        end
    endtask

    initial begin
        reset_all();
        normal_frame(32'hfffffff0); // ID rollover, last BRAM byte address.
        normal_frame(32'd5000);     // Second request toggles back to zero.

        start_capture(); writes=0;
        @(negedge sample_clk); sample_valid=1;
        repeat(4) @(negedge sample_clk);
        sample_valid=0;
        repeat(10) @(negedge axi_clk);
        if(!status_gpio[2]) $fatal(1,"Fast producer drop was not reported");
        source_overrun=1;
        repeat(10) @(negedge axi_clk);
        if(!status_gpio[3]) $fatal(1,"Source overrun was not reported");

        // Coordinated reset during a partial frame must remove stale FULL/ACK.
        reset_all();
        if(status_gpio[4:0] !== 0) $fatal(1,"Reset left stale status");
        normal_frame(32'd9000);
        $display("PASS: writer frames=%0d, 4096 pairs/frame; CDC handshake, wrap, frozen RAM, overflow, reset", frames);
        $finish;
    end
    initial begin #10000000; $fatal(1,"RTL test timeout"); end
endmodule
