# System Clock (clk_i auf A10) - z.B. 50 MHz (Period: 20 ns)
create_clock -period 20.000 -name clk_i [get_ports {clk_i}]

# Host SPI Clock (HOST_SCK) - z.B. 25 MHz (Period: 40 ns)
create_clock -period 40.000 -name HOST_SCK [get_ports {HOST_SCK}]

# Clock Groups (Asynchroner Übergang zwischen Host SPI und FPGA Systemtakt)
set_clock_groups -asynchronous -group [get_clocks {clk_i}] -group [get_clocks {HOST_SCK}]
