`timescale 1ns/1ps
module nilm_stream_axi (
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aclk, ASSOCIATED_BUSIF S_AXI:BRAM, ASSOCIATED_RESET s_axi_aresetn, FREQ_HZ 100000000" *)
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 s_axi_aclk CLK" *)
    input wire s_axi_aclk,
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aresetn, POLARITY ACTIVE_LOW" *)
    (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 s_axi_aresetn RST" *)
    input wire s_axi_aresetn,
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME S_AXI, PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 12, READ_WRITE_MODE READ_WRITE, NUM_READ_OUTSTANDING 1, NUM_WRITE_OUTSTANDING 1" *)
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWADDR" *) input wire [11:0] s_axi_awaddr,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *) input wire [2:0] s_axi_awprot,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *) input wire s_axi_awvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *) output wire s_axi_awready,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WDATA" *) input wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *) input wire [3:0] s_axi_wstrb,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WVALID" *) input wire s_axi_wvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WREADY" *) output wire s_axi_wready,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BRESP" *) output reg [1:0] s_axi_bresp,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BVALID" *) output reg s_axi_bvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BREADY" *) input wire s_axi_bready,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *) input wire [11:0] s_axi_araddr,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *) input wire [2:0] s_axi_arprot,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *) input wire s_axi_arvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *) output wire s_axi_arready,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RDATA" *) output reg [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RRESP" *) output reg [1:0] s_axi_rresp,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RVALID" *) output reg s_axi_rvalid,
    (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RREADY" *) input wire s_axi_rready,
    input wire sample_clk, sample_rst, sample_valid, source_overrun,
    input wire [31:0] sample_id,
    input wire [11:0] voltage_adc,current_adc,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM CLK" *)
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME BRAM, MASTER_TYPE BRAM_CTRL" *) output wire bram_clk,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM RST" *) output wire bram_rst,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM EN" *) output wire bram_en,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM WE" *) output wire [3:0] bram_we,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM ADDR" *) output wire [31:0] bram_addr,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM DIN" *) output wire [31:0] bram_wrdata,
    (* X_INTERFACE_INFO="xilinx.com:interface:bram:1.0 BRAM DOUT" *) input wire [31:0] bram_rddata
);
    reg aw_hold,w_hold;
    reg [11:0] awaddr;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    // Register the merged sample reset before crossing it: neither the reset
    // combiner nor its separate input synchronizers directly drive an AXI CDC.
    reg sample_reset_registered=1'b1;
    always @(posedge sample_clk) sample_reset_registered<=sample_rst;
    (* ASYNC_REG="TRUE" *) reg [1:0] sample_reset_sync=2'b11;
    always @(posedge s_axi_aclk) begin
        if(!s_axi_aresetn) sample_reset_sync<=2'b11;
        else sample_reset_sync<={sample_reset_sync[0],sample_reset_registered};
    end
    wire engine_resetn=s_axi_aresetn && !sample_reset_sync[1];
    wire [31:0] record_data,record_id;
    wire record_valid;
    wire [1:0] source_faults;
    nilm_sample_mailbox mailbox(
        .sample_clk(sample_clk),.sample_rst(sample_rst),.axi_clk(s_axi_aclk),.axi_resetn(engine_resetn),
        .sample_valid(sample_valid),.source_overrun(source_overrun),.sample_id(sample_id),
        .voltage_adc(voltage_adc),.current_adc(current_adc),
        .valid(record_valid),.data(record_data),.id(record_id),.faults(source_faults)
    );
    wire write_commit=aw_hold && w_hold && !s_axi_bvalid;
    wire [31:0] read_data;
    // Read and write registers share a decoder; arbitration blocks AR only
    // during the commit cycle. AW and W may arrive in either order.
    wire [11:0] reg_addr=write_commit?awaddr:s_axi_araddr;
    nilm_stream_engine engine(
        .clk(s_axi_aclk),.resetn(engine_resetn),.sample_valid(record_valid),
        .sample_data(record_data),.sample_id(record_id),.source_faults(source_faults),
        .wr(write_commit && engine_resetn && awaddr[1:0]==0),.addr(reg_addr),
        .wdata(wdata),.wstrb(wstrb),.rdata(read_data),
        .bram_en(bram_en),.bram_we(bram_we),.bram_addr(bram_addr),.bram_wrdata(bram_wrdata)
    );
    assign bram_clk=s_axi_aclk;
    assign bram_rst=1'b0;
    assign s_axi_awready=!aw_hold && !s_axi_bvalid;
    assign s_axi_wready=!w_hold && !s_axi_bvalid;
    assign s_axi_arready=!s_axi_rvalid && !write_commit;
    always @(posedge s_axi_aclk) begin
        if(!s_axi_aresetn) begin
            aw_hold<=0; w_hold<=0; awaddr<=0; wdata<=0; wstrb<=0;
            s_axi_bvalid<=0; s_axi_bresp<=0; s_axi_rvalid<=0; s_axi_rresp<=0; s_axi_rdata<=0;
        end else begin
            if(s_axi_awvalid && s_axi_awready) begin aw_hold<=1; awaddr<=s_axi_awaddr; end
            if(s_axi_wvalid && s_axi_wready) begin w_hold<=1; wdata<=s_axi_wdata; wstrb<=s_axi_wstrb; end
            if(write_commit) begin
                aw_hold<=0; w_hold<=0; s_axi_bvalid<=1;
                s_axi_bresp<=(!engine_resetn || awaddr[1:0]!=0 || wstrb!=4'hf)?2'b10:0;
            end else if(s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if(s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid<=1; s_axi_rdata<=(!engine_resetn && s_axi_araddr==12'h00c)?0:read_data;
                // Reset epoch/status remains readable so PS can detect a lost
                // stream without turning a status poll into a CPU data abort.
                s_axi_rresp<=(s_axi_araddr[1:0]!=0)?2'b10:0;
            end else if(s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
        end
    end
endmodule
