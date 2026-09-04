----------------------------------------------------------------------------------
-- Target Device : Efinix Trion T55F484
-- Module Name   : fpga_spi_bridge - Behavioral
-- Description   : 1-Byte Header Framed SPI Bridge between Linux Host and
--                 4x ADSP-21489 SHARC DSPs + Internal Core Register Control.
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity fpga_spi_bridge is
    Port (
        -- System Clock & Reset
        clk_i           : in  std_logic;
        rst_n_i         : in  std_logic;

        -- Linux Host SPI Interface (FPGA ist SPI Slave)
        HOST_SCK        : in  std_logic;
        HOST_MOSI       : in  std_logic;
        HOST_MISO       : out std_logic;
        HOST_CS_N       : in  std_logic;

        -- Shared DSP SPI Interface (FPGA ist Master zum DSP)
        DSP_CLK         : out std_logic; -- Pin J2
        DSP_MOSI        : out std_logic; -- Pin J3
        DSP_MISO        : in  std_logic; -- Pin J4

        -- DSP Chip Selects (Active Low)
        DSP_CS1_N       : out std_logic; -- Pin J5
        DSP_CS2_N       : out std_logic; -- Pin L7
        DSP_CS3_N       : out std_logic; -- Pin N2
        DSP_CS4_N       : out std_logic; -- Pin P4

        -- Shared DSP Reset Line (Active Low)
        DSP_RESET_N     : out std_logic  -- Pin B1
    );
end fpga_spi_bridge;

architecture Behavioral of fpga_spi_bridge is

    -- CDC Signals
    signal host_sck_sync  : std_logic_vector(2 downto 0) := (others => '0');
    signal host_mosi_sync : std_logic_vector(2 downto 0) := (others => '0');
    signal host_cs_sync   : std_logic_vector(1 downto 0) := (others => '1');

    signal sck_rising     : std_logic := '0';
    signal sck_falling    : std_logic := '0';
    signal cs_active      : std_logic := '0';

    -- FSM Types & Signals
    type fsm_state_type is (ST_IDLE, ST_READ_HEADER, ST_PASSTHROUGH, ST_INTERNAL_REG);
    signal state          : fsm_state_type := ST_IDLE;

    type reg_fsm_type is (REG_GET_ADDR, REG_DATA_TRANSFER);
    signal reg_state      : reg_fsm_type := REG_GET_ADDR;

    -- Register & Counter Signals
    signal header_shift   : std_logic_vector(7 downto 0) := (others => '0');
    signal bit_counter    : integer range 0 to 15 := 0;
    signal target_dest    : std_logic_vector(7 downto 0) := (others => '0');

    signal reg_addr       : std_logic_vector(7 downto 0) := (others => '0');
    signal reg_tx_shift   : std_logic_vector(7 downto 0) := (others => '0');
    signal reg_rx_shift   : std_logic_vector(7 downto 0) := (others => '0');
    signal int_miso       : std_logic := '0';

    signal reg_dsp_reset  : std_logic := '1';

    -- Signals for CDC parsing
    signal next_bit_mosi  : std_logic := '0';
    signal next_byte      : std_logic_vector(7 downto 0) := (others => '0');

begin

    ------------------------------------------------------------------------------
    -- 1. Clock Domain Crossing (CDC)
    ------------------------------------------------------------------------------
    process(clk_i, rst_n_i)
    begin
        if rst_n_i = '0' then
            host_sck_sync  <= (others => '0');
            host_mosi_sync <= (others => '0');
            host_cs_sync   <= (others => '1');
        elsif rising_edge(clk_i) then
            host_sck_sync  <= host_sck_sync(1 downto 0) & HOST_SCK;
            host_mosi_sync <= host_mosi_sync(1 downto 0) & HOST_MOSI;
            host_cs_sync   <= host_cs_sync(0) & HOST_CS_N;
        end if;
    end process;

    sck_rising    <= '1' when (host_sck_sync(2 downto 1) = "01") else '0';
    sck_falling   <= '1' when (host_sck_sync(2 downto 1) = "10") else '0';
    cs_active     <= '1' when (host_cs_sync(1) = '0') else '0';

    next_bit_mosi <= host_mosi_sync(1);
    next_byte     <= header_shift(6 downto 0) & next_bit_mosi;

    ------------------------------------------------------------------------------
    -- 2. Haupt-FSM: Header-Dekodierung & Protokoll-Steuerung
    ------------------------------------------------------------------------------
    process(clk_i, rst_n_i)
        variable v_rx_byte : std_logic_vector(7 downto 0);
    begin
        if rst_n_i = '0' then
            state         <= ST_IDLE;
            reg_state     <= REG_GET_ADDR;
            header_shift  <= (others => '0');
            bit_counter   <= 0;
            target_dest   <= (others => '0');
            reg_addr      <= (others => '0');
            reg_tx_shift  <= (others => '0');
            reg_rx_shift  <= (others => '0');
            reg_dsp_reset <= '1';
            int_miso      <= '0';
        elsif rising_edge(clk_i) then

            if cs_active = '0' then
                state       <= ST_IDLE;
                reg_state   <= REG_GET_ADDR;
                bit_counter <= 0;
                target_dest <= (others => '0');
                int_miso    <= '0';
            else
                case state is

                    when ST_IDLE =>
                        bit_counter <= 0;
                        state       <= ST_READ_HEADER;

                    when ST_READ_HEADER =>
                        if sck_rising = '1' then
                            header_shift <= next_byte;

                            if bit_counter = 7 then
                                target_dest <= next_byte;
                                if next_byte = x"00" then
                                    state     <= ST_INTERNAL_REG;
                                    reg_state <= REG_GET_ADDR;
                                else
                                    state     <= ST_PASSTHROUGH;
                                end if;
                                bit_counter <= 0;
                            else
                                bit_counter <= bit_counter + 1;
                            end if;
                        end if;

                    when ST_PASSTHROUGH =>
                        -- Passthrough erfolgt nebenläufig

                    when ST_INTERNAL_REG =>
                        case reg_state is

                            when REG_GET_ADDR =>
                                if sck_rising = '1' then
                                    v_rx_byte := reg_rx_shift(6 downto 0) & next_bit_mosi;
                                    reg_rx_shift <= v_rx_byte;

                                    if bit_counter = 7 then
                                        reg_addr    <= v_rx_byte;
                                        reg_state   <= REG_DATA_TRANSFER;
                                        bit_counter <= 0;

                                        case v_rx_byte is
                                            when x"00"  => reg_tx_shift <= x"57"; -- 'W'
                                            when x"01"  => reg_tx_shift <= x"49"; -- 'I'
                                            when x"02"  => reg_tx_shift <= x"4E"; -- 'N'
                                            when x"03"  => reg_tx_shift <= x"47"; -- 'G'
                                            when x"04"  => reg_tx_shift <= "0000000" & reg_dsp_reset;
                                            when others => reg_tx_shift <= x"FF";
                                        end case;
                                    else
                                        bit_counter <= bit_counter + 1;
                                    end if;
                                end if;

                            when REG_DATA_TRANSFER =>
                                if sck_falling = '1' then
                                    int_miso     <= reg_tx_shift(7);
                                    reg_tx_shift <= reg_tx_shift(6 downto 0) & '0';
                                end if;

                                if sck_rising = '1' then
                                    reg_rx_shift <= reg_rx_shift(6 downto 0) & next_bit_mosi;

                                    if bit_counter = 7 then
                                        if reg_addr = x"04" then
                                            reg_dsp_reset <= next_bit_mosi;
                                        end if;
                                        bit_counter <= 0;
                                    else
                                        bit_counter <= bit_counter + 1;
                                    end if;
                                end if;

                        end case;

                end case;
            end if;
        end if;
    end process;

    ------------------------------------------------------------------------------
    -- 3. Concurrent Routing & Multiplexing
    ------------------------------------------------------------------------------
    DSP_RESET_N <= reg_dsp_reset;

    DSP_CLK  <= HOST_SCK  when (state = ST_PASSTHROUGH) else '0';
    DSP_MOSI <= HOST_MOSI when (state = ST_PASSTHROUGH) else '0';

    DSP_CS1_N <= '0' when (state = ST_PASSTHROUGH and (target_dest = x"01" or target_dest = x"0F")) else '1';
    DSP_CS2_N <= '0' when (state = ST_PASSTHROUGH and (target_dest = x"02" or target_dest = x"0F")) else '1';
    DSP_CS3_N <= '0' when (state = ST_PASSTHROUGH and (target_dest = x"03" or target_dest = x"0F")) else '1';
    DSP_CS4_N <= '0' when (state = ST_PASSTHROUGH and (target_dest = x"04" or target_dest = x"0F")) else '1';

    HOST_MISO <= int_miso when (state = ST_INTERNAL_REG) else
                 DSP_MISO when (state = ST_PASSTHROUGH and (target_dest = x"01" or target_dest = x"02" or target_dest = x"03" or target_dest = x"04")) else
                 '0';

end Behavioral;