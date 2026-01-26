

create_clock -period 40.000 -name io_clk25 [get_ports {io_clk25}]
create_clock -period 16.667 -name io_clk60 [get_ports {io_clk60}]
create_clock -period 4.848 -name io_clk_sdctrl [get_ports {io_clk_sdctrl}]
create_clock -period 5.000 -name io_clk_cpu [get_ports {io_clk_cpu}]
create_clock -period 2.500 -name io_ddr_tdqss_clk [get_ports {io_ddr_tdqss_clk}]
create_clock -period 5.000 -name io_ddr_core_clk [get_ports {io_ddr_core_clk}]
create_clock -period 2.500 -name io_ddr_tac_clk [get_ports {io_ddr_tac_clk}]
create_clock -waveform {0.625 1.875} -period 2.500 -name io_ddr_twd_clk [get_ports {io_ddr_twd_clk}]
create_clock -period 20.000 -name io_ddr_feedback_clk [get_ports {io_ddr_feedback_clk}]
create_clock -period 5.000  -name io_dyn_clk0 [get_ports {io_dyn_clk0}]

set_max_delay -from io_clk_cpu -to io_clk60 5.000
set_max_delay -from io_clk60 -to io_clk_cpu 5.000

set_max_delay -from io_clk_cpu -to io_dyn_clk0 10.000
set_max_delay -from io_dyn_clk0 -to io_clk_cpu 10.000

set_max_delay -from io_clk_cpu -to io_clk_sdctrl 3.000
set_max_delay -from io_clk_sdctrl -to io_clk_cpu 3.000

set_max_delay -from io_clk_cpu -to io_clk25 15.000

set_max_delay -from io_clk25 -to io_clk60 15.000
set_max_delay -from io_clk25 -to io_clk_sdctrl 15.000
set_max_delay -from io_clk25 -to io_clk_cpu 15.000
set_max_delay -from io_clk25 -to io_dyn_clk0 15.000
set_max_delay -from io_clk25 -to io_ddr_core_clk 15.000
set_max_delay -from io_clk25 -to io_ddr_tac_clk 15.000
set_max_delay -from io_clk25 -to io_ddr_twd_clk 15.000

set_max_delay -from io_clk_cpu -to io_ddr_core_clk 2.000
set_max_delay -from io_ddr_core_clk -to io_clk_cpu 2.000
set_max_delay -from io_clk_cpu -to io_ddr_tac_clk 2.000
set_max_delay -from io_ddr_tac_clk -to io_clk_cpu 2.000
set_max_delay -from io_clk_cpu -to io_ddr_twd_clk 2.000
set_max_delay -from io_ddr_twd_clk -to io_clk_cpu 2.000
