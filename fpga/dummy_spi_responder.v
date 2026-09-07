`timescale 1ns / 1ps

// Minimal SPI mode-0 responder for the Efinix Trion T55F484.
//
// Keep chip select low and transmit:
//   50 49 4e 47 00 00 00 00 00 00 00 00 00 00 00 00
//   P  I  N  G  <----------- 12 dummy bytes ---------->
//
// MISO is zero during the four command bytes, then returns "TRION READY\n".
module dummy_spi_responder (
    input  wire host_sck,
    input  wire host_mosi,
    output wire host_miso,
    input  wire host_cs_n
);

    localparam [2:0] LAST_COMMAND_BYTE = 3;
    localparam [3:0] LAST_RESPONSE_BYTE = 11;

    reg [7:0] rx_shift = 8'h00;
    reg       command_matches = 1'b1;
    reg       response_requested = 1'b0;
    reg [2:0] rx_bit_index = 3'd0;
    reg [2:0] rx_byte_index = 3'd0;

    function [7:0] command_byte;
        input [2:0] index;
        begin
            case (index)
                3'd0: command_byte = 8'h50; // P
                3'd1: command_byte = 8'h49; // I
                3'd2: command_byte = 8'h4e; // N
                3'd3: command_byte = 8'h47; // G
                default: command_byte = 8'h00;
            endcase
        end
    endfunction

    // SPI mode 0: sample MOSI on rising edges. A mismatch suppresses the
    // response until CS rises.
    always @(posedge host_sck or posedge host_cs_n) begin
        if (host_cs_n) begin
            rx_shift           <= 8'h00;
            rx_bit_index       <= 3'd0;
            rx_byte_index      <= 3'd0;
            command_matches    <= 1'b1;
            response_requested <= 1'b0;
        end else if (!response_requested) begin
            rx_shift <= {rx_shift[6:0], host_mosi};
            if (rx_bit_index == 3'd7) begin
                rx_bit_index <= 3'd0;
                if ({rx_shift[6:0], host_mosi} != command_byte(rx_byte_index))
                    command_matches <= 1'b0;

                if (rx_byte_index == LAST_COMMAND_BYTE) begin
                    if (command_matches &&
                        ({rx_shift[6:0], host_mosi} == command_byte(rx_byte_index)))
                        response_requested <= 1'b1;
                end else begin
                    rx_byte_index <= rx_byte_index + 3'd1;
                end
            end else begin
                rx_bit_index <= rx_bit_index + 3'd1;
            end
        end
    end

    function [7:0] response_byte;
        input [3:0] index;
        begin
            case (index)
                4'd0:  response_byte = 8'h54; // T
                4'd1:  response_byte = 8'h52; // R
                4'd2:  response_byte = 8'h49; // I
                4'd3:  response_byte = 8'h4f; // O
                4'd4:  response_byte = 8'h4e; // N
                4'd5:  response_byte = 8'h20; // space
                4'd6:  response_byte = 8'h52; // R
                4'd7:  response_byte = 8'h45; // E
                4'd8:  response_byte = 8'h41; // A
                4'd9:  response_byte = 8'h44; // D
                4'd10: response_byte = 8'h59; // Y
                4'd11: response_byte = 8'h0a; // newline
                default: response_byte = 8'h00;
            endcase
        end
    endfunction

    reg       tx_active = 1'b0;
    reg [2:0] tx_bit_index = 3'd0;
    reg [3:0] tx_byte_index = 4'd0;
    wire [7:0] current_response_byte = response_byte(tx_byte_index);

    // SPI mode 0: update MISO on falling edges.
    always @(negedge host_sck or posedge host_cs_n) begin
        if (host_cs_n) begin
            tx_active     <= 1'b0;
            tx_bit_index  <= 3'd0;
            tx_byte_index <= 4'd0;
        end else if (!tx_active && response_requested) begin
            tx_active     <= 1'b1;
            tx_bit_index  <= 3'd0;
            tx_byte_index <= 4'd0;
        end else if (tx_active) begin
            if (tx_bit_index == 3'd7) begin
                tx_bit_index <= 3'd0;
                if (tx_byte_index == LAST_RESPONSE_BYTE)
                    tx_active <= 1'b0;
                else
                    tx_byte_index <= tx_byte_index + 4'd1;
            end else begin
                tx_bit_index <= tx_bit_index + 3'd1;
            end
        end
    end

    assign host_miso = (!host_cs_n && tx_active)
                     ? current_response_byte[3'd7 - tx_bit_index]
                     : 1'b0;

endmodule
