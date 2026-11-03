`timescale 1ns / 1ps

module nilm_xadc_top (
    input  wire        clk,           // 25.6 MHz
    input  wire        rst,           // 高电平复位

    // 板上的两路模拟输入
    input  wire        vauxp2,
    input  wire        vauxn2,
    input  wire        vauxp10,
    input  wire        vauxn10,

    // 采样结果
    output wire [11:0] voltage_adc,
    output wire [11:0] current_adc,
    output wire        sample_valid,
    output wire [31:0] sample_count,
    output wire        overrun
);

    // 两个模块之间的 DRP 连线
    wire [6:0]  drp_addr;
    wire        drp_en;
    wire        drp_we;
    wire [15:0] drp_write_data;
    wire [15:0] drp_read_data;
    wire        drp_ready;

    // XADC 转换完成信号
    wire        adc_eoc;

    // One CONVST pulse every 1000 cycles: 25.6 MHz / 1000 = 25.6 kSPS.
    // First trigger is delayed by 1000 clocks after reset release.
    reg [9:0] sample_timer;
    reg sample_trigger;
    always @(posedge clk) begin
        if (rst) begin
            sample_timer <= 10'd0;
            sample_trigger <= 1'b0;
        end else begin
            sample_trigger <= 1'b0;
            if (sample_timer == 10'd999) begin
                sample_timer <= 10'd0;
                sample_trigger <= 1'b1;
            end else begin
                sample_timer <= sample_timer + 1'b1;
            end
        end
    end
    // XADC IP：完成模拟量到数字量的转换
    xadc_wiz_0 u_xadc (
        .dclk_in              (clk),
        .reset_in             (rst),
        .convst_in            (sample_trigger),

        .daddr_in             (drp_addr),
        .den_in               (drp_en),
        .dwe_in               (drp_we),
        .di_in                (drp_write_data),
        .do_out               (drp_read_data),
        .drdy_out             (drp_ready),

        .vauxp2               (vauxp2),
        .vauxn2               (vauxn2),
        .vauxp10              (vauxp10),
        .vauxn10              (vauxn10),

        // 当前不使用专用 VP/VN 通道
        .vp_in                (1'b0),
        .vn_in                (1'b0),

        .eoc_out              (adc_eoc),

        // 当前不使用的输出端口
        .busy_out             (),
        .channel_out          (),
        .eos_out              (),
        .ot_out               (),
        .vccddro_alarm_out     (),
        .vccpaux_alarm_out     (),
        .vccpint_alarm_out     (),
        .vccaux_alarm_out      (),
        .vccint_alarm_out      (),
        .user_temp_alarm_out  (),
        .alarm_out            ()
    );

    // 读取模块：读出两路结果，并组成一对数据
    xadc_reader u_reader (
        .clk          (clk),
        .rst          (rst),

        .eoc          (adc_eoc),
        .drdy         (drp_ready),
        .do_data      (drp_read_data),

        .daddr        (drp_addr),
        .den          (drp_en),
        .dwe          (drp_we),
        .di_data      (drp_write_data),

        .voltage_adc  (voltage_adc),
        .current_adc  (current_adc),
        .sample_valid (sample_valid),
        .sample_count (sample_count),
        .overrun      (overrun)
    );
    // 观察采样结果，使用同一个 25.6 MHz 时钟
    ila_0 u_ila (
        .clk    (clk),
        .probe0 (voltage_adc),
        .probe1 (current_adc),
        .probe2 (sample_valid),
        .probe3 (sample_count),
        .probe4 (overrun),
        .probe5 (adc_eoc)
    );
endmodule
