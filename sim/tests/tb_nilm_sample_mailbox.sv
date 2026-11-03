`timescale 1ns/1ps
module tb_nilm_sample_mailbox;
  reg sc=0,ac=0; always #19.531 sc=~sc; always #5 ac=~ac;
  reg sr=1,an=0,sv=0,overrun=0;
  reg [31:0] sid=0; reg [11:0] v=0,i=0;
  wire valid; wire [31:0] data,id; wire [1:0] faults;
  integer received=0;
  nilm_sample_mailbox dut(sc,sr,ac,an,sv,overrun,sid,v,i,valid,data,id,faults);
  always @(posedge ac) if(valid) begin
    received=received+1;
    if(id!=received || data!={4'b0,id[11:0],4'b0,id[11:0]}) $fatal(1,"torn or lost mailbox payload");
  end
  initial begin
    repeat(5) @(negedge sc); sr=0; an=1;
    for(integer n=1;n<=300;n=n+1) begin
      @(negedge sc); sid=n; v=n; i=n; sv=1;
      @(negedge sc); sv=0; repeat(8) @(negedge sc);
    end
    if(received!=300||faults!=0) $fatal(1,"mailbox count");
    overrun=1; repeat(8) @(negedge ac);
    if(faults!=1) $fatal(1,"source fault sync");
    // Two back-to-back source clocks exceed round-trip credit.
    @(negedge sc); sid=301; v=301;i=301;sv=1;
    @(negedge sc);sid=302;v=302;i=302;
    @(negedge sc);sv=0;repeat(10) @(negedge sc);
    if(faults!=3) $fatal(1,"mailbox overflow not reported");
    $display("PASS: mailbox asynchronous phase, atomic payload, overflow and source fault");$finish;
  end
  initial begin #1000000;$fatal(1,"timeout");end
endmodule
