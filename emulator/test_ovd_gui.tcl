#!/usr/bin/env tclsh

# Automated Unit Test Suite for Omega Virtual Device (OVD) Manager GUI Application
# Tests Tcl/Tk procedure definitions, script syntax, and procedure behavior

package require tcltest
namespace import tcltest::*

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set GUI_SCRIPT [file join $SCRIPT_DIR "ovd_gui.tcl"]
set RUN_SCRIPT [file join $SCRIPT_DIR "ovd_run.sh"]

test test_gui_script_exists {Verify ovd_gui.tcl file existence} -body {
    file exists $GUI_SCRIPT
} -result {1}

test test_ovd_run_vga_config {Verify ovd_run.sh configures Standard VGA for x86_64} -body {
    set fp [open $RUN_SCRIPT r]
    set content [read $fp]
    close $fp
    string match {*-vga std*} $content
} -result {1}

test test_ovd_run_non_x86_display_config {Verify non-x86 display fallback and experimental GPU paths} -body {
    set fp [open $RUN_SCRIPT r]
    set content [read $fp]
    close $fp
    set has_simplefb [string match "*SimpleFb*" $content]
    set has_virtio_gpu [string match "*-device virtio-gpu-pci*" $content]
    expr {$has_simplefb && $has_virtio_gpu}
} -result {1}

test test_gui_script_syntax {Verify ovd_gui.tcl syntax correctness} -body {
    set fp [open $GUI_SCRIPT r]
    set content [read $fp]
    close $fp
    # Check key procedure declarations in Tcl script
    set has_refresh [string match "*proc refresh_device_list*" $content]
    set has_create  [string match "*proc create_device*" $content]
    set has_run_gui [string match "*proc run_device_gui*" $content]
    set has_delete  [string match "*proc delete_device*" $content]
    set has_storage [string match "*storage_combo*" $content]
    set has_stop [string match "*proc stop_device*" $content]
    set has_logs [string match "*proc show_device_logs*" $content]
    expr {$has_refresh && $has_create && $has_run_gui && $has_delete && $has_storage && $has_stop && $has_logs}
} -result {1}

cleanupTests
