`timescale 1ns/1ps
// RAW1 four-bank engine. All ownership, registers and RAM writes use axi_clk.
// Faults are sticky until coordinated reset. No READY bank is overwritten.
module nilm_stream_engine #(parameter BLOCK_SAMPLES=1024)(
    input wire clk, resetn,
    input wire sample_valid, input wire [31:0] sample_data, sample_id,
    input wire [1:0] source_faults,
    input wire wr, input wire [11:0] addr, input wire [31:0] wdata,
    input wire [3:0] wstrb, output reg [31:0] rdata,
    output wire bram_en, output wire [3:0] bram_we,
    output wire [31:0] bram_addr, bram_wrdata
);
    localparam IDLE=0, LOW=1, HIGH=2;
    reg [1:0] state, bank;
    reg [3:0] ready;
    reg running, stopping, dropping;
    reg [9:0] index;
    reg [31:0] held_data, held_id, first_id;
    reg [63:0] block_seq;
    reg [31:0] epoch, faults, dropped_blocks;
    reg [63:0] dropped_records;
    reg [63:0] bank_seq[0:3];
    reg [31:0] bank_first[0:3], bank_last[0:3], bank_epoch[0:3];
    reg [31:0] opcode, arg0, arg1, arg2, arg3, cmd_seq;
    reg [31:0] ack_seq, ack_result;
    reg have_ack;
    reg [191:0] last_command;
    wire [191:0] command={cmd_seq,opcode,arg0,arg1,arg2,arg3};
    reg [63:0] snap_seq, snap_drops;
    reg [31:0] snap_blocks, snap_faults, snap_number;
    reg seen_id; reg [31:0] previous_id;
    wire id_bad=sample_valid && seen_id && sample_id!=previous_id+1;
    wire writer_bad=sample_valid && state!=IDLE;
    wire input_fault=(|source_faults)||id_bad||writer_bad;
    integer i;
    reg found; reg [1:0] free_bank;
    always @* begin
        found=0; free_bank=0;
        for(integer j=0;j<4;j=j+1) if(!ready[j]&&!found) begin found=1; free_bank=j; end
    end
    assign bram_en=(state==LOW || state==HIGH);
    assign bram_we=bram_en?4'hf:0;
    assign bram_addr={17'b0,bank,index,3'b0} + (state==HIGH?32'd4:0);
    assign bram_wrdata=state==HIGH?held_id:held_data;
    always @* begin
        rdata=0;
        case(addr)
          'h000:rdata=32'h4e344231; // N4B1
          'h004:rdata=32'h00010000;
          'h008:rdata=BLOCK_SAMPLES;
          'h00c:rdata={26'b0,stopping,(|faults),ready!=0,1'b1,dropping,running};
          'h010:rdata=epoch;
          'h014:rdata={28'b0,ready};
          'h018:rdata=faults;
          'h020:rdata=opcode;
          'h024:rdata=arg0;
          'h028:rdata=arg1;
          'h02c:rdata=arg2;
          'h030:rdata=arg3;
          'h034:rdata=cmd_seq;
          'h040:rdata=ack_seq;
          'h044:rdata=ack_result;
          'h048:rdata={31'b0,have_ack};
          'h060:rdata=snap_seq[31:0];
          'h064:rdata=snap_seq[63:32];
          'h068:rdata=snap_drops[31:0];
          'h06c:rdata=snap_drops[63:32];
          'h070:rdata=snap_blocks;
          'h074:rdata=snap_faults;
          'h078:rdata=snap_number;
          default: begin
            if(addr>='h100 && addr<'h200) begin
                case(addr[5:0])
                  'h00:rdata={31'b0,ready[addr[7:6]]};
                  'h04:rdata=bank_epoch[addr[7:6]];
                  'h08:rdata=bank_seq[addr[7:6]][31:0];
                  'h0c:rdata=bank_seq[addr[7:6]][63:32];
                  'h10:rdata=bank_first[addr[7:6]];
                  'h14:rdata=bank_last[addr[7:6]];
                  'h18:rdata=BLOCK_SAMPLES;
                  'h1c:rdata=1; // RAW1; IDs are success counters, not time slots
                  default:rdata=0;
                endcase
            end
          end
        endcase
    end
    always @(posedge clk) begin
      if(!resetn) begin
        state<=IDLE; bank<=0; ready<=0; running<=0; stopping<=0; dropping<=0;
        index<=0; held_data<=0; held_id<=0; first_id<=0; block_seq<=0;
        epoch<=0; faults<=0; dropped_blocks<=0; dropped_records<=0;
        opcode<=0; arg0<=0; arg1<=0; arg2<=0; arg3<=0; cmd_seq<=0;
        ack_seq<=0; ack_result<=0; have_ack<=0; last_command<=0;
        snap_seq<=0; snap_drops<=0; snap_blocks<=0; snap_faults<=0; snap_number<=0;
        seen_id<=0; previous_id<=0;
        for(i=0;i<4;i=i+1) begin bank_seq[i]<=0; bank_first[i]<=0; bank_last[i]<=0; bank_epoch[i]<=0; end
      end else begin
        if(stopping && index==0 && state==IDLE && !sample_valid) begin running<=0; stopping<=0; end
        if(sample_valid) begin seen_id<=1; previous_id<=sample_id; end
        if(input_fault) faults<=faults | {28'b0,id_bad,writer_bad,source_faults};
        if(state==LOW) state<=HIGH;
        if(state==HIGH) begin
            state<=IDLE;
            if(index==BLOCK_SAMPLES-1) begin
                ready[bank]<=1; bank_seq[bank]<=block_seq; bank_first[bank]<=first_id;
                bank_last[bank]<=held_id; bank_epoch[bank]<=epoch;
                block_seq<=block_seq+1; index<=0;
                if(stopping) begin running<=0; stopping<=0; end
            end else index<=index+1;
        end
        if(running && sample_valid && state==IDLE && !input_fault) begin
            if((index==0&&!found)||dropping) begin
                dropping<=1;
                if(dropped_records!=64'hffffffffffffffff) dropped_records<=dropped_records+1;
                if(index==0 && dropped_blocks!=32'hffffffff) dropped_blocks<=dropped_blocks+1;
                if(index==BLOCK_SAMPLES-1) begin
                    index<=0; dropping<=0; block_seq<=block_seq+1;
                    if(stopping) begin running<=0; stopping<=0; end
                end else index<=index+1;
            end else begin
                if(index==0) begin bank<=free_bank; first_id<=sample_id; end
                held_data<=sample_data; held_id<=sample_id; state<=LOW;
            end
        end
        // Full-word register writes only; AXI wrapper reports partial writes SLVERR.
        if(wr && wstrb==4'hf) begin
            case(addr)
              'h020:opcode<=wdata;
              'h024:arg0<=wdata;
              'h028:arg1<=wdata;
              'h02c:arg2<=wdata;
              'h030:arg3<=wdata;
              'h034:cmd_seq<=wdata;
              'h038:if(wdata==32'h434d4431) begin
                if(have_ack && cmd_seq==ack_seq) begin
                    if(command!=last_command) ack_result<=5;
                end else begin
                    have_ack<=1; ack_seq<=cmd_seq; last_command<=command; ack_result<=0;
                    case(opcode)
                      1:if(running) ack_result<=0;
                        else if(ready!=0 || stopping) ack_result<=1;
                        else if(faults!=0 || input_fault) ack_result<=2;
                        else begin
                            running<=1; index<=0; dropping<=0; block_seq<=0;
                            epoch<=epoch+1; dropped_records<=0; dropped_blocks<=0;
                        end
                      2:if(running) begin
                          if(index==0 && state==IDLE && !dropping && !sample_valid) begin running<=0; stopping<=0; end
                          else stopping<=1;
                        end
                      3:if(arg0>3) ack_result<=3;
                        else if(!ready[arg0[1:0]] || bank_seq[arg0[1:0]]!={arg2,arg1} || bank_epoch[arg0[1:0]]!=arg3) ack_result<=4;
                        else ready[arg0[1:0]]<=0;
                      4:begin
                          snap_seq<=block_seq; snap_drops<=dropped_records;
                          snap_blocks<=dropped_blocks; snap_faults<=faults; snap_number<=snap_number+1;
                        end
                      default:ack_result<=6;
                    endcase
                end
              end
              default:begin end
            endcase
        end
        // An ADC/transport fault invalidates the in-progress block. Completed
        // banks remain readable, but STATUS.FAULT prevents claiming continuity.
        if(input_fault && running) begin
            running<=0; stopping<=0; state<=IDLE; index<=0; dropping<=0;
            if(state==HIGH && index==BLOCK_SAMPLES-1) ready[bank]<=0;
        end
      end
    end
endmodule
