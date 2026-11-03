`timescale 1ns/1ps
// One outstanding record. Payload is held until the returned acknowledgement.
// Sample and AXI clocks are related; payload paths remain fully timed.
module nilm_sample_mailbox (
    input wire sample_clk, sample_rst, axi_clk, axi_resetn,
    input wire sample_valid, source_overrun,
    input wire [31:0] sample_id,
    input wire [11:0] voltage_adc, current_adc,
    output reg valid, output reg [31:0] data, id,
    output wire [1:0] faults
);
    reg [63:0] payload;
    reg request, overflow;
    reg acknowledge;
    (* ASYNC_REG="TRUE" *) reg [1:0] ack_sync;
    (* ASYNC_REG="TRUE" *) reg [2:0] req_sync;
    (* ASYNC_REG="TRUE" *) reg [1:0] fault_meta, fault_sync;
    always @(posedge sample_clk) begin
        if (sample_rst) begin
            payload<=0; request<=0; overflow<=0; ack_sync<=0;
        end else begin
            ack_sync<={ack_sync[0],acknowledge};
            if(sample_valid) begin
                if(request==ack_sync[1]) begin
                    payload<={sample_id,4'b0,voltage_adc,4'b0,current_adc};
                    request<=~request;
                end else overflow<=1;
            end
        end
    end
    always @(posedge axi_clk) begin
        if(!axi_resetn) begin
            req_sync<=0; acknowledge<=0; valid<=0; data<=0; id<=0;
            fault_meta<=0; fault_sync<=0;
        end else begin
            req_sync<={req_sync[1:0],request};
            fault_meta<={overflow,source_overrun}; fault_sync<=fault_meta;
            valid<=0;
            if(req_sync[2]!=acknowledge) begin
                {id,data}<=payload;
                valid<=1;
                acknowledge<=req_sync[2];
            end
        end
    end
    assign faults=fault_sync;
endmodule
