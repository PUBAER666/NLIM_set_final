`timescale 1ns/1ps
module tb_nilm_stream_engine;
    reg clk=0; always #5 clk=~clk;
    reg resetn=0,valid=0,wr=0;
    reg [31:0] data=0,id=0,wd=0;
    reg [1:0] faults=0;
    reg [11:0] addr=0;
    wire [31:0] rd,ba,bd; wire be; wire [3:0] bw;
    reg [31:0] ram[0:8191];
    integer writes=0, command_number=0;
    nilm_stream_engine #(.BLOCK_SAMPLES(8)) dut(clk,resetn,valid,data,id,faults,wr,addr,wd,4'hf,rd,be,bw,ba,bd);
    always @(posedge clk) if(be) begin
        if(dut.ready[ba[14:13]]) $fatal(1,"overwrite READY bank");
        ram[ba>>2]<=bd; writes<=writes+1;
    end
    task write_reg(input [11:0] a,input [31:0] d);
      @(negedge clk); addr=a; wd=d; wr=1;
      @(negedge clk); wr=0;
    endtask
    task command(input [31:0] op,a0,a1,a2,a3);
      write_reg('h020,op); write_reg('h024,a0); write_reg('h028,a1);
      write_reg('h02c,a2); write_reg('h030,a3);
      command_number=command_number+1; write_reg('h034,command_number); write_reg('h038,32'h434d4431);
    endtask
    task sample;
      @(negedge clk); id=id+1; data={4'b0,id[11:0],4'b0,id[11:0]}; valid=1;
      @(negedge clk); valid=0; repeat(5) @(negedge clk);
    endtask
    task samples(input integer n); repeat(n) sample(); endtask
    initial begin
      repeat(4) @(negedge clk); resetn=1;
      command(1,0,0,0,0);
      if(!dut.running||dut.epoch!=1) $fatal(1,"start");
      samples(32);
      if(dut.ready!=15||writes!=64) $fatal(1,"four banks not complete");
      for(integer b=0;b<4;b=b+1) begin
        if(dut.bank_seq[b]!=b||dut.bank_first[b]!=b*8+1||dut.bank_last[b]!=b*8+8) $fatal(1,"descriptor");
        for(integer n=0;n<8;n=n+1) if(ram[b*2048+n*2+1]!=b*8+n+1) $fatal(1,"RAM order");
      end
      samples(3); // full ring: choose DROP for entire block
      command(3,0,99,0,1);
      if(dut.ack_result!=4||dut.ready!=15) $fatal(1,"stale release accepted");
      command(3,0,0,0,1);
      if(dut.ack_result!=0||dut.ready!=14) $fatal(1,"release");
      // Duplicate accepted release must not execute a second time.
      write_reg('h038,32'h434d4431);
      if(dut.ack_result!=0) $fatal(1,"duplicate command");
      samples(5);
      if(dut.dropped_blocks!=1||dut.dropped_records!=8||writes!=64) $fatal(1,"partial dropped block resumed");
      samples(8);
      if(dut.bank_seq[0]!=5||dut.bank_first[0]!=41||dut.ready!=15) $fatal(1,"gap sequence");
      command(3,0,0,0,1);
      if(dut.ack_result!=4||dut.ready!=15) $fatal(1,"old release freed new bank");
      command(4,0,0,0,0);
      if(dut.snap_drops!=8||dut.snap_seq!=6) $fatal(1,"snapshot");
      command(2,0,0,0,0);
      repeat(4) @(negedge clk);
      if(dut.running) $fatal(1,"stop at boundary");
      command(1,0,0,0,0);
      if(dut.ack_result!=1) $fatal(1,"start discarded ready data");
      command(3,0,5,0,1);
      for(integer b=1;b<4;b=b+1) command(3,b,b,0,1);
      command(1,0,0,0,0);
      if(dut.epoch!=2) $fatal(1,"epoch");
      samples(3); command(2,0,0,0,0); samples(5);
      if(dut.running||dut.ready!=1) $fatal(1,"stop partial block");
      command(3,0,0,0,1);
      if(dut.ack_result!=4) $fatal(1,"old epoch release");
      command(3,0,0,0,2); command(1,0,0,0,0);
      samples(2); faults=1; repeat(4) @(negedge clk);
      if(dut.running||dut.ready!=0||dut.faults!=1) $fatal(1,"fault publishes partial data");
      command(1,0,0,0,0);
      if(dut.ack_result!=2) $fatal(1,"restart sticky fault");
      resetn=0; repeat(3) @(negedge clk); faults=0; resetn=1; id=32'hfffffffc;
      command(1,0,0,0,0); samples(8);
      if(dut.faults!=0||dut.ready!=1||dut.bank_last[0]!=4) $fatal(1,"ID wrap");
      command(3,0,0,0,1); samples(1); id=id+1; sample();
      if(dut.running||dut.faults!=8) $fatal(1,"ID gap");
      $display("PASS: four-bank ownership, full-block drop, stale/duplicate/epoch release, snapshots, stop, fault, ID wrap/gap");
      $finish;
    end
    initial begin #1000000; $fatal(1,"timeout"); end
endmodule
