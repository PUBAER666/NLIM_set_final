set root [file normalize [file join [file dirname [info script]] ..]]
set work [file join $root work hardware]
file mkdir $work
create_project -force nilm_final $work -part xc7z020clg484-1
set repo [file join $root src hardware cnn_ip]
if {[file exists [file join $root work hls hls impl ip component.xml]]} {set repo [file join $root work hls hls impl ip]}
set_property ip_repo_paths [list $repo] [current_project]
update_ip_catalog
add_files [glob [file join $root src rtl *.v]]
foreach name {ila_0 xadc_wiz_0 clk_wiz_sample} {
    import_ip -files [file join $root src hardware ip $name ${name}.xci]
}
add_files -fileset constrs_1 [glob [file join $root src hardware constraints *.xdc]]
source [file join $root src hardware system_bd.tcl]
validate_bd_design
save_bd_design
set bd [get_files */system.bd]
make_wrapper -files $bd -top
add_files [file join $work nilm_final.gen sources_1 bd system hdl system_wrapper.v]
set_property top system_wrapper [current_fileset]
update_compile_order -fileset sources_1
puts "PASS_RECREATED_BD: source and IP resolved; not a routed-design result"
if {[lsearch -exact $argv --implement]>=0} {
    generate_target all $bd
    launch_runs synth_1 -jobs 4
    wait_on_run synth_1
    if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Synthesis failed"}
    launch_runs impl_1 -to_step write_bitstream -jobs 4
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Implementation failed"}
    open_run impl_1
    foreach {cmd name} {report_utilization utilization report_timing_summary timing_summary report_cdc cdc report_drc drc report_bus_skew bus_skew} {
        $cmd -file [file join $work ${name}.rpt]
    }
    if {[get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]<0 || [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]<0} {error "Timing failed"}
    write_hw_platform -fixed -include_bit -force -file [file join $work nilm_board.xsa]
    file copy -force [file join [get_property DIRECTORY [get_runs impl_1]] system_wrapper.bit] [file join $work system_wrapper.bit]
}
close_project
