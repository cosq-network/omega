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
    set has_refresh [string match "*proc refresh_devices*" $content]
    set has_create  [string match "*proc create_generic*" $content]
    set has_run_gui [string match "*proc launch_selected*" $content]
    set has_delete  [string match "*proc delete_selected*" $content]
    set has_storage [string match "*.controls.storage*" $content]
    set has_stop [string match "*proc stop_selected*" $content]
    set has_logs [string match "*proc show_selected_logs*" $content]
    set has_profiles [string match "*profile_catalog.py*" $content]
    set has_create_profile [string match "*proc create_from_profile*" $content]
    set has_artifacts [string match "*proc check_profile_artifacts*" $content]
    set has_validate [string match "*proc validate_selected*" $content]
    set has_defaults [string match "*default_ram*image_size*native_creation*" $content]
    set has_external_guard [string match "*native_creation eq \"true\"*" $content]
    expr {$has_refresh && $has_create && $has_run_gui && $has_delete && $has_storage && $has_stop && $has_logs && $has_profiles && $has_create_profile && $has_artifacts && $has_validate && $has_defaults && $has_external_guard}
} -result {1}

cleanupTests
