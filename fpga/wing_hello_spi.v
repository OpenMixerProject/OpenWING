`timescale 1ns / 1ps

// ============================================================================
// Module Name: wing_hello_spi
// Description: Efinix Trion T55 FPGA SPI responder for Behringer WING.
//              Listens on SPI (Mode 0) for 16-bit command 0x1337.
//              Returns ASCII string "Hello from Wing!" (16 bytes) on MISO.
//
// SPI Transaction Framing (18 bytes total):
//   MOSI: 13 37 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
//         [CMD] <---------------- 16 dummy clocks -------------->
//   MISO: 00 00 48 65 6c 6c 6f 20 66 72 6f 6d 20 57 69 6e 67 21
//         [---]  H  e  l  l  o     f  r  o  m     W  i  n  g  !
// ============================================================================

module wing_hello_spi (
    input  wire host_sck,
    input  wire host_mosi,
    output wire host_miso,
    input  wire host_cs_n
);

    // 16-byte ASCII response: "Hello from Wing!"
    function [7:0] response_byte;
        input [3:0] index;
        begin
            case (index)
                4'd0:  response_byte = 8'h48; // 'H'
                4'd1:  response_byte = 8'h65; // 'e'
                4'd2:  response_byte = 8'h6C; // 'l'
                4'd3:  response_byte = 8'h6C; // 'l'
                4'd4:  response_byte = 8'h6F; // 'o'
                4'd5:  response_byte = 8'h20; // ' '
                4'd6:  response_byte = 8'h66; // 'f'
                4'd7:  response_byte = 8'h72; // 'r'
                4'd8:  response_byte = 8'h6F; // 'o'
                4'd9:  response_byte = 8'h6D; // 'm'
                4'd10: response_byte = 8'h20; // ' '
                4'd11: response_byte = 8'h57; // 'W'
                4'd12: response_byte = 8'h69; // 'i'
                4'd13: response_byte = 8'h6E; // 'n'
                4'd14: response_byte = 8'h67; // 'g'
                4'd15: response_byte = 8'h21; // '!'
                default: response_byte = 8'h00;
            endcase
        end
    endfunction

    reg [15:0] rx_shift           = 16'h0000;
    reg        response_requested = 1'b0;
    reg [6:0]  tx_bit_count       = 7'd0; // 0..127 (128 bits = 16 bytes)

    wire [7:0] current_byte = response_byte(tx_bit_count[6:3]);

    // Sample MOSI on rising edges of SCK (SPI Mode 0).
    // Matches command 0x1337 continuously via shift register.
    // Automatically resets when the full 16-byte response is finished.
    always @(posedge host_sck or posedge host_cs_n) begin
        if (host_cs_n) begin
            rx_shift           <= 16'h0000;
            response_requested <= 1'b0;
            tx_bit_count       <= 7'd0;
        end else if (!response_requested) begin
            rx_shift <= {rx_shift[14:0], host_mosi};
            if ({rx_shift[14:0], host_mosi} == 16'h1337) begin
                response_requested <= 1'b1;
                tx_bit_count       <= 7'd0;
            end
        end else begin
            if (tx_bit_count == 7'd127) begin
                response_requested <= 1'b0;
                rx_shift           <= 16'h0000;
                tx_bit_count       <= 7'd0;
            end else begin
                tx_bit_count <= tx_bit_count + 7'd1;
            end
        end
    end

    reg host_miso_reg = 1'b0;

    // Update MISO on falling edges of SCK (SPI Mode 0).
    always @(negedge host_sck or posedge host_cs_n) begin
        if (host_cs_n) begin
            host_miso_reg <= 1'b0;
        end else if (response_requested) begin
            host_miso_reg <= current_byte[3'd7 - tx_bit_count[2:0]];
        end else begin
            host_miso_reg <= 1'b0;
        end
    end

    assign host_miso = (!host_cs_n) ? host_miso_reg : 1'b0;

endmodule
