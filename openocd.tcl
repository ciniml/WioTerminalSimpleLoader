
set CHIPNAME samd51p19a
source [find target/atsame5x.cfg]

adapter speed 3600
gdb_flash_program enable
gdb_breakpoint_override hard

init
targets

# for FT2232D workaround
proc flash_bin_ft {bin_file} {
    reset halt
    set file_size [file size $bin_file]
    set end_addr [expr $file_size + 0x4000]
    for {set addr 0x4000} {$addr < $end_addr} {incr addr 0x2000} {
        flash erase_address $addr 0x2000
        sleep 200
    }
    flash write_image $bin_file 0x4000
    verify_image $bin_file 0x4000 
    echo "flashing $bin_file complete"
    reset halt
}
proc flash_bin {bin_file} {
    reset halt
    flash write_image erase $bin_file 0x4000
    verify_image $bin_file 0x4000 
    echo "flashing $bin_file complete"
    reset halt
}
