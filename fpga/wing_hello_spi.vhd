----------------------------------------------------------------------------------
-- Target Device : Efinix Trion T55F484 / T85F484
-- Module Name   : wing_hello_spi - Behavioral
-- Description   : Efinix Trion FPGA SPI responder for Behringer WING.
--                 Listens on SPI (Mode 0) for 16-bit command 0x1337.
--                 Returns ASCII string "Hello from Wing!" (16 bytes) on MISO.
--
-- Pinout (Bank 1A passive configuration / ECSPI2 pins):
--   HOST_SCK   : Pin W1 (GPIOL_01_CCK)
--   HOST_MOSI  : Pin V2 (GPIOL_08_CDI0)
--   HOST_MISO  : Pin V1 (GPIOL_09_CDI1)
--   HOST_CS_N  : Pin V3 (GPIOL_00_SS_N)
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity wing_hello_spi is
    Port (
        host_sck  : in  std_logic;
        host_mosi : in  std_logic;
        host_miso : out std_logic;
        host_cs_n : in  std_logic
    );
end wing_hello_spi;

architecture Behavioral of wing_hello_spi is

    type rom_type is array (0 to 15) of std_logic_vector(7 downto 0);
    constant RESPONSE_ROM : rom_type := (
        0  => x"48", -- 'H'
        1  => x"65", -- 'e'
        2  => x"6C", -- 'l'
        3  => x"6C", -- 'l'
        4  => x"6F", -- 'o'
        5  => x"20", -- ' '
        6  => x"66", -- 'f'
        7  => x"72", -- 'r'
        8  => x"6F", -- 'o'
        9  => x"6D", -- 'm'
        10 => x"20", -- ' '
        11 => x"57", -- 'W'
        12 => x"69", -- 'i'
        13 => x"6E", -- 'n'
        14 => x"67", -- 'g'
        15 => x"21"  -- '!'
    );

    signal rx_shift           : std_logic_vector(15 downto 0) := (others => '0');
    signal response_requested : std_logic := '0';
    signal tx_bit_count       : integer range 0 to 127 := 0;
    signal host_miso_reg      : std_logic := '0';

    signal byte_idx           : integer range 0 to 15 := 0;
    signal bit_idx            : integer range 0 to 7 := 0;
    signal current_byte       : std_logic_vector(7 downto 0);

begin

    byte_idx     <= tx_bit_count / 8;
    bit_idx      <= tx_bit_count mod 8;
    current_byte <= RESPONSE_ROM(byte_idx);

    -- Sample MOSI on rising edge of SCK (SPI Mode 0)
    process(host_sck, host_cs_n)
        variable next_shift : std_logic_vector(15 downto 0);
    begin
        if host_cs_n = '1' then
            rx_shift           <= (others => '0');
            response_requested <= '0';
            tx_bit_count       <= 0;
        elsif rising_edge(host_sck) then
            if response_requested = '0' then
                next_shift := rx_shift(14 downto 0) & host_mosi;
                rx_shift   <= next_shift;
                if next_shift = x"1337" then
                    response_requested <= '1';
                    tx_bit_count       <= 0;
                end if;
            else
                if tx_bit_count = 127 then
                    response_requested <= '0';
                    rx_shift           <= (others => '0');
                    tx_bit_count       <= 0;
                else
                    tx_bit_count <= tx_bit_count + 1;
                end if;
            end if;
        end if;
    end process;

    -- Update MISO on falling edge of SCK (SPI Mode 0)
    process(host_sck, host_cs_n)
    begin
        if host_cs_n = '1' then
            host_miso_reg <= '0';
        elsif falling_edge(host_sck) then
            if response_requested = '1' then
                host_miso_reg <= current_byte(7 - bit_idx);
            else
                host_miso_reg <= '0';
            end if;
        end if;
    end process;

    host_miso <= host_miso_reg when host_cs_n = '0' else '0';

end Behavioral;
