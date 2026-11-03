`timescale 1ns/1ps
module tb_nilm_stream_axi;
  reg ac=0,sc=0;always #5 ac=~ac;always #19.531 sc=~sc;
  reg resetn=0,sr=1,sv=0;
  reg [31:0] sid=0;
  reg [11:0] awaddr=0,araddr=0;
  reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
  reg [31:0] wd=0;reg [3:0] ws=15;
  wire awready,wready,bvalid,arready,rvalid,be;wire[1:0] bresp,rresp;
  wire [31:0] rd,ba,bd;wire[3:0] bw;wire bc,br;
  reg [31:0] ram[0:8191];integer writes=0;
  nilm_stream_axi dut(
   .s_axi_aclk(ac),.s_axi_aresetn(resetn),.s_axi_awaddr(awaddr),.s_axi_awprot(3'b0),
   .s_axi_awvalid(awvalid),.s_axi_awready(awready),.s_axi_wdata(wd),.s_axi_wstrb(ws),
   .s_axi_wvalid(wvalid),.s_axi_wready(wready),.s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
   .s_axi_araddr(araddr),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
   .s_axi_rdata(rd),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
   .sample_clk(sc),.sample_rst(sr),.sample_valid(sv),.source_overrun(1'b0),
   .sample_id(sid),.voltage_adc(sid[11:0]),.current_adc(sid[11:0]),
   .bram_clk(bc),.bram_rst(br),.bram_en(be),.bram_we(bw),.bram_addr(ba),.bram_wrdata(bd),.bram_rddata(32'b0));
  always @(posedge ac) if(be) begin
    if(dut.engine.ready[ba[14:13]]) $fatal(1,"overwrite owned bank");
    ram[ba>>2]<=bd;writes<=writes+1;
  end
  task address(input [11:0] a);
    @(negedge ac);awaddr=a;awvalid=1;
    do @(posedge ac);while(!awready);
    @(negedge ac);awvalid=0;
  endtask
  task value(input [31:0] d,input [3:0] strobe);
    @(negedge ac);wd=d;ws=strobe;wvalid=1;
    do @(posedge ac);while(!wready);
    @(negedge ac);wvalid=0;
  endtask
  task write_reg(input [11:0] a,input [31:0] d,input integer order,input [3:0] strobe);
    if(order==0) begin address(a);repeat(3)@(negedge ac);value(d,strobe);end
    else begin value(d,strobe);repeat(4)@(negedge ac);address(a);end
    wait(bvalid);repeat(5)begin @(negedge ac);if(!bvalid)$fatal(1,"B response not held");end
    if(bresp!=(strobe==15?0:2)) $fatal(1,"BRESP");
    bready=1;@(negedge ac);bready=0;
  endtask
  task read_reg(input [11:0] a,input [31:0] expected);
    @(negedge ac);araddr=a;arvalid=1;
    do @(posedge ac);while(!arready);
    @(negedge ac);arvalid=0;
    wait(rvalid);
    repeat(5)begin @(negedge ac);if(!rvalid||rd!==expected||rresp!=0)$fatal(1,"R data/hold %h got %h expected %h",a,rd,expected);end
    rready=1;@(negedge ac);rready=0;
  endtask
  initial begin
    repeat(5)@(negedge sc);resetn=1;sr=0;repeat(10)@(negedge ac);
    read_reg(0,32'h4e344231);read_reg(8,1024);
    write_reg('h020,99,0,1);read_reg('h020,0);
    write_reg('h020,1,1,15);write_reg('h034,1,0,15);write_reg('h038,32'h434d4431,1,15);
    read_reg('h040,1);read_reg('h044,0);
    for(integer n=1;n<=4096;n=n+1) begin
      @(negedge sc);sid=n;sv=1;@(negedge sc);sv=0;repeat(8)@(negedge sc);
    end
    repeat(10)@(negedge ac);
    read_reg('h014,15);read_reg('h018,0);
    if(writes!=8192)$fatal(1,"full frame write count");
    for(integer n=0;n<4096;n=n+1)begin
      if(ram[n*2+1]!=n+1)$fatal(1,"full-size address/ID mismatch");
      if(ram[n*2]!=={4'b0,ram[n*2+1][11:0],4'b0,ram[n*2+1][11:0]})$fatal(1,"payload mismatch");
    end
    @(negedge sc);sr=1;repeat(8)@(negedge sc);
    read_reg('h010,0);read_reg('h014,0);read_reg('h00c,0);
    @(negedge sc);sr=0;repeat(8)@(negedge sc);
    read_reg('h014,0);
    $display("PASS: AXI AW/W ordering, response backpressure, partial write rejection; 4 x 1024 atomic mailbox records, RAM boundaries, readable reset epoch");$finish;
  end
  initial begin #10000000;$fatal(1,"AXI timeout");end
endmodule
