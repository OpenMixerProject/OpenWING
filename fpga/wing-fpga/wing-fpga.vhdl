--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity wing_fpga is
    port (
        HOST_SPI_CLK    : in  std_logic;
        HOST_SPI_MISO   : out  std_logic;
        HOST_SPI_MOSI   : in  std_logic;
        
        HOST_DSP1_RESET_N : in std_logic;
        
        DSP1_RESET_N    : out std_logic;
        DSP1_SPI_CLK    : out std_logic;
        DSP1_SPI_MISO   : in std_logic;
        DSP1_SPI_MOSI   : out std_logic
    );
end entity wing_fpga;

architecture rtl of wing_fpga is

begin

    DSP1_SPI_CLK <= HOST_SPI_CLK;
    HOST_SPI_MISO <= DSP1_SPI_MISO;
    DSP1_SPI_MOSI <= HOST_SPI_MOSI;
    DSP1_RESET_N <= HOST_DSP1_RESET_N;

end architecture rtl;