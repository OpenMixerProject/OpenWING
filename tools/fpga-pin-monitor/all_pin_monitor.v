// ============================================================================
// Module Name: all_pin_monitor
// Target FPGA: Efinix Trion T55F484 & T85F484 (Behringer WING Console)
//
// Description:
// ------------
// High-performance 252-channel hardware digital logic analyzer and pin
// signature detector implemented in Verilog for the Efinix Trion FPGA.
//
// Hardware Context:
// -----------------
// The Behringer WING mainboard connects the Efinix Trion FPGA to:
//   - 4x Analog Devices ADSP-21489 SHARC DSPs (SPORT audio links, TDM clocks, resets)
//   - Multiple AES50 SuperMAC transceivers (Port A, Port B, Port C)
//   - Internal Expansion Slot & Module connectors
//   - Audio Clock Distribution (Si5351B MCLK, Word Clocks, Bit Clocks)
//   - Control Buses (GPIO, I2C, SPI)
//
// This RTL design simultaneously samples all 252 non-SPI GPIO pins on the chip
// and calculates per-pin telemetry in real time on every clock edge:
//   1. Edge / Transition Counter: 16-bit saturating counter tracking toggles.
//   2. High-State Duration Counter: 16-bit counter tracking duration at HIGH
//      (allows computing exact duty cycle percentage).
//   3. Minimum Observed Pulse Width: 8-bit counter measuring shortest pulse width
//      (useful for detecting glitches or clock frequencies).
//   4. Waveform Snapshot: 32-sample continuous digital history shift register.
//   5. Latch Indicators: Saw-HIGH, Saw-LOW, Active-Toggling, Live DC level.
//   6. Fast Bulk Inquiries: 8x 32-bit Activity bitmasks and 8x 32-bit DC level bitmasks.
//
// SPI Interface Protocol:
// -----------------------
// The module implements a self-synchronizing SPI slave:
//   - Preamble Magic: 16'hA55A
//   - Transaction Frame: 8 bytes total (Mode 0, MSB first)
//       * Byte 0..1: 0xA55A (Preamble word)
//       * Byte 2   : Command & Address (Bit 7: 1=Read, 0=Write; Bits 6..0: RegAddr)
//       * Byte 3..6: 32-bit Data Word (Big-Endian)
//       * Byte 7   : 0x00 (Frame termination trailer)
// ============================================================================

`timescale 1ns / 1ps

module all_pin_monitor #(
    parameter NUM_PINS = 252                    // Total number of monitored candidate GPIO pins
) (
    // ------------------------------------------------------------------------
    // Host CPU SPI Interface (Bank 1A / i.MX6 ECSPI2)
    // ------------------------------------------------------------------------
    input  wire host_sck,       // Package Ball W1 (GPIOL_01_CCK / ECSPI2_SCLK)
    input  wire host_mosi,      // Package Ball V2 (GPIOL_08_CDI0 / ECSPI2_MOSI)
    output wire host_miso,      // Package Ball V1 (GPIOL_09_CDI1 / ECSPI2_MISO)
    input  wire host_cs_n,      // Package Ball V3 (GPIOL_00_SS_N / GPIO2_26)

    // ------------------------------------------------------------------------
    // All 252 Monitored Hardware Probe Pins (Across all I/O Banks)
    // ------------------------------------------------------------------------
    input  wire [NUM_PINS-1:0] probe_in
);

    // ========================================================================
    // Section 1: Synchronizers, Telemetry Counters & Waveform Capture
    // ========================================================================

    // Double-buffering flip-flops to prevent metastability on asynchronous probe inputs
    reg [NUM_PINS-1:0] probe_d1;
    reg [NUM_PINS-1:0] probe_d2;

    // Per-pin telemetry memory arrays
    reg [15:0]         edge_cnt  [NUM_PINS-1:0]; // 16-bit transition count
    reg [15:0]         high_cnt  [NUM_PINS-1:0]; // 16-bit high-duration clock count
    reg [NUM_PINS-1:0] saw_high;                 // Latched 1 if pin ever went HIGH
    reg [NUM_PINS-1:0] saw_low;                  // Latched 1 if pin ever went LOW
    reg [NUM_PINS-1:0] activity;                 // Latched 1 if pin transitioned (toggled)

    reg [7:0]          cur_pulse [NUM_PINS-1:0]; // Current pulse duration counter
    reg [7:0]          min_pulse [NUM_PINS-1:0]; // Shortest observed pulse width
    reg [31:0]         history   [NUM_PINS-1:0]; // 32-sample waveform shift register

    reg [7:0]          sel_pin;                  // Currently selected pin index (0..251)
    reg                rst_counters;             // Synchronous counter reset pulse

    integer j;
    always @(posedge host_sck) begin
        if (rst_counters) begin
            // Synchronously reset all measurement registers
            for (j = 0; j < NUM_PINS; j = j + 1) begin
                edge_cnt[j]  <= 16'd0;
                high_cnt[j]  <= 16'd0;
                saw_high[j]  <= 1'b0;
                saw_low[j]   <= 1'b0;
                activity[j]  <= 1'b0;
                cur_pulse[j] <= 8'd0;
                min_pulse[j] <= 8'hFF;
                history[j]   <= 32'd0;
            end
        end else begin
            // Stage 1 & Stage 2 input synchronizers
            probe_d1 <= probe_in;
            probe_d2 <= probe_d1;

            for (j = 0; j < NUM_PINS; j = j + 1) begin
                // Shift in the latest digital sample to form a live 32-sample waveform
                history[j] <= {history[j][30:0], probe_d1[j]};

                // DC Level and High-Duration Accumulation
                if (probe_d1[j]) begin
                    saw_high[j] <= 1'b1;
                    if (high_cnt[j] != 16'hFFFF) high_cnt[j] <= high_cnt[j] + 16'd1;
                end else begin
                    saw_low[j] <= 1'b1;
                end

                // Edge Detection (Transition between consecutive clock cycles)
                if (probe_d1[j] ^ probe_d2[j]) begin
                    activity[j] <= 1'b1;
                    if (edge_cnt[j] != 16'hFFFF) edge_cnt[j] <= edge_cnt[j] + 16'd1;

                    // Update minimum observed pulse width
                    if (cur_pulse[j] != 8'd0 && cur_pulse[j] < min_pulse[j]) begin
                        min_pulse[j] <= cur_pulse[j];
                    end
                    cur_pulse[j] <= 8'd1;
                end else begin
                    if (cur_pulse[j] != 8'hFF) cur_pulse[j] <= cur_pulse[j] + 8'd1;
                end
            end
        end
    end

    // ========================================================================
    // Section 2: Register Map Multiplexer & Slicing Functions
    // ========================================================================
    localparam [31:0] MAGIC_ID      = 32'h50494E53; // "PINS" ASCII identifier
    localparam [31:0] BUILD_VERSION = 32'h20260908; // YYYYMMDD Version format
    localparam [31:0] PIN_COUNT_REG = NUM_PINS;    // 252 total channels

    // Internal wires for selected pin telemetry
    wire [15:0] cur_edge_cnt  = (sel_pin < NUM_PINS) ? edge_cnt[sel_pin]  : 16'h0000;
    wire [15:0] cur_high_cnt  = (sel_pin < NUM_PINS) ? high_cnt[sel_pin]  : 16'h0000;
    wire [7:0]  cur_min_pulse = (sel_pin < NUM_PINS) ? min_pulse[sel_pin] : 8'h00;
    wire [31:0] cur_history   = (sel_pin < NUM_PINS) ? history[sel_pin]   : 32'h00000000;
    wire        cur_live      = (sel_pin < NUM_PINS) ? probe_d1[sel_pin]  : 1'b0;
    wire        cur_high      = (sel_pin < NUM_PINS) ? saw_high[sel_pin]  : 1'b0;
    wire        cur_low       = (sel_pin < NUM_PINS) ? saw_low[sel_pin]   : 1'b0;
    wire        cur_toggle    = cur_high & cur_low;

    // Helper function: extracts 32-bit slices of the 252-bit activity mask
    function [31:0] get_activity_slice;
        input [2:0] slice_idx;
        integer k;
        begin
            get_activity_slice = 32'd0;
            for (k = 0; k < 32; k = k + 1) begin
                if ((slice_idx * 32 + k) < NUM_PINS)
                    get_activity_slice[k] = activity[slice_idx * 32 + k];
            end
        end
    endfunction

    // Helper function: extracts 32-bit slices of live DC input levels
    function [31:0] get_live_slice;
        input [2:0] slice_idx;
        integer k;
        begin
            get_live_slice = 32'd0;
            for (k = 0; k < 32; k = k + 1) begin
                if ((slice_idx * 32 + k) < NUM_PINS)
                    get_live_slice[k] = probe_d1[slice_idx * 32 + k];
            end
        end
    endfunction

    reg [7:0]  reg_cmd;
    reg [31:0] reg_rdata_comb;

    // Combinatorial Read Multiplexer
    always @(*) begin
        case (reg_cmd[6:0])
            7'h00: reg_rdata_comb = MAGIC_ID;
            7'h01: reg_rdata_comb = BUILD_VERSION;
            7'h02: reg_rdata_comb = PIN_COUNT_REG;
            7'h03: reg_rdata_comb = {24'd0, sel_pin};
            7'h04: reg_rdata_comb = {cur_live, cur_high, cur_low, cur_toggle, 12'd0, cur_edge_cnt};
            7'h05: reg_rdata_comb = {cur_high_cnt, cur_min_pulse, 8'd0};
            7'h06: reg_rdata_comb = cur_history;

            // Bulk Activity Masks (Reg 0x10..0x17): 1 = pin has transitioned
            7'h10: reg_rdata_comb = get_activity_slice(3'd0); // Channels 0..31
            7'h11: reg_rdata_comb = get_activity_slice(3'd1); // Channels 32..63
            7'h12: reg_rdata_comb = get_activity_slice(3'd2); // Channels 64..95
            7'h13: reg_rdata_comb = get_activity_slice(3'd3); // Channels 96..127
            7'h14: reg_rdata_comb = get_activity_slice(3'd4); // Channels 128..159
            7'h15: reg_rdata_comb = get_activity_slice(3'd5); // Channels 160..191
            7'h16: reg_rdata_comb = get_activity_slice(3'd6); // Channels 192..223
            7'h17: reg_rdata_comb = get_activity_slice(3'd7); // Channels 224..251

            // Bulk Live DC Logic Levels (Reg 0x20..0x27): Instantaneous board state
            7'h20: reg_rdata_comb = get_live_slice(3'd0); // Channels 0..31
            7'h21: reg_rdata_comb = get_live_slice(3'd1); // Channels 32..63
            7'h22: reg_rdata_comb = get_live_slice(3'd2); // Channels 64..95
            7'h23: reg_rdata_comb = get_live_slice(3'd3); // Channels 96..127
            7'h24: reg_rdata_comb = get_live_slice(3'd4); // Channels 128..159
            7'h25: reg_rdata_comb = get_live_slice(3'd5); // Channels 160..191
            7'h26: reg_rdata_comb = get_live_slice(3'd6); // Channels 192..223
            7'h27: reg_rdata_comb = get_live_slice(3'd7); // Channels 224..251

            default: reg_rdata_comb = 32'hDEADBEEF;
        endcase
    end

    // ========================================================================
    // Section 3: Self-Synchronizing SPI Framing Engine (0xA55A Preamble)
    // ========================================================================
    reg [15:0] sync_shift = 16'd0;
    reg        frame_active = 1'b0;
    reg [5:0]  frame_bit_cnt = 6'd0;
    reg [31:0] reg_wdata = 32'd0;
    reg [31:0] reg_rdata_shift = 32'd0;

    // Shift in MOSI data on posedge SCK
    always @(posedge host_sck) begin
        if (rst_counters) rst_counters <= 1'b0;

        if (!frame_active) begin
            // Search for 16-bit synchronization sync word 0xA55A
            sync_shift <= {sync_shift[14:0], host_mosi};
            if ({sync_shift[14:0], host_mosi} == 16'hA55A) begin
                frame_active  <= 1'b1;
                frame_bit_cnt <= 6'd0;
                reg_cmd       <= 8'd0;
            end
        end else begin
            // Bits 0..7: Command & Register Address
            if (frame_bit_cnt < 6'd8) begin
                reg_cmd <= {reg_cmd[6:0], host_mosi};
            end
            // Bits 8..39: 32-bit Data Payload
            else if (frame_bit_cnt < 6'd40) begin
                reg_wdata <= {reg_wdata[30:0], host_mosi};

                // Latch write on bit 39 when Write bit (bit 7) is 0
                if (frame_bit_cnt == 6'd39 && reg_cmd[7] == 1'b0) begin
                    case (reg_cmd[6:0])
                        7'h02: rst_counters <= host_mosi;
                        7'h03: sel_pin      <= {reg_wdata[6:0], host_mosi};
                    endcase
                end
            end

            // Frame terminates at bit 47 (6 bytes = 48 bits total following preamble)
            if (frame_bit_cnt == 6'd47) begin
                frame_active  <= 1'b0;
                sync_shift    <= 16'd0;
                frame_bit_cnt <= 6'd0;
            end else begin
                frame_bit_cnt <= frame_bit_cnt + 6'd1;
            end
        end
    end

    // Shift out MISO data on negedge SCK to satisfy host SPI setup/hold timings
    always @(negedge host_sck) begin
        if (frame_active) begin
            if (frame_bit_cnt == 6'd8) begin
                // Latch selected read register value
                reg_rdata_shift <= reg_rdata_comb;
            end else if (frame_bit_cnt > 6'd8 && frame_bit_cnt < 6'd40) begin
                // Shift out MSB first
                reg_rdata_shift <= {reg_rdata_shift[30:0], 1'b0};
            end else if (frame_bit_cnt >= 6'd40) begin
                reg_rdata_shift <= 32'd0;
            end
        end else begin
            reg_rdata_shift <= 32'd0;
        end
    end

    // Assign MISO output: active during data phase (bits 8..39), otherwise idle low
    assign host_miso = (frame_active && frame_bit_cnt >= 6'd8 && frame_bit_cnt <= 6'd39) ? reg_rdata_shift[31] : 1'b0;

endmodule
