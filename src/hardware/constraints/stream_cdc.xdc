# Related clocks, no clock groups. Bundled payload is held until round-trip ACK.
set stream_sync [get_cells -hier -regexp {.*nilm_stream_axi_0/inst/(mailbox/(ack_sync_reg\[0\]|req_sync_reg\[0\]|fault_meta_reg\[[01]\])|sample_reset_sync_reg\[0\])$}]
set_false_path -to [get_pins -of_objects $stream_sync -filter {REF_PIN_NAME == D}]
set stream_resets [get_cells -hier -regexp {.*nilm_sample_source_0/inst/(reset_pipe_reg\[[0-9]+\]|lock_reset_pipe_reg\[[0-9]+\])$}]
set_false_path -to [get_pins -of_objects $stream_resets -filter {REF_PIN_NAME == PRE}]
# The destination captures only after a three-stage request synchronizer:
# earliest payload capture is > 3 AXI periods after source publication.
# Bound all payload bits to 8 ns (well below 30 ns), and skew to 4 ns.
# This replaces meaningless closest-edge setup/hold analysis for the held bus;
# it is not an unconstrained false path. Padding bits synthesize to constants.
set stream_payload [get_cells -hier -regexp {.*nilm_stream_axi_0/inst/mailbox/payload_reg\[[0-9]+\]$}]
set stream_capture [get_cells -hier -regexp {.*nilm_stream_axi_0/inst/mailbox/(id|data)_reg\[[0-9]+\]$}]
set_max_delay -datapath_only 8.000 -from $stream_payload -to $stream_capture
set_bus_skew 4.000 -from $stream_payload -to $stream_capture
