# EES331: copied from the already tested nilm_xadc_board.xdc.
# Analog inputs: do not apply 3.3 V because of the IOSTANDARD label.
set_property PACKAGE_PIN C15 [get_ports vauxp2]
set_property PACKAGE_PIN B15 [get_ports vauxn2]
set_property PACKAGE_PIN B16 [get_ports vauxp10]
set_property PACKAGE_PIN B17 [get_ports vauxn10]
set_property IOSTANDARD LVCMOS33 [get_ports {vauxp2 vauxn2 vauxp10 vauxn10}]

# PS FCLK and Clocking Wizard provide the clock constraints in this design.
# There is no external PL clk/rst_n port in this top level.
