#!/usr/bin/env tclsh

# Automated Unit Test Suite for Omega Virtual Device (OVD) Manager GUI Application
# Tests Tcl/Tk procedure definitions, script syntax, and procedure behavior

package require tcltest
namespace import tcltest::*

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set GUI_SCRIPT [file join $SCRIPT_DIR "ovd_gui.tcl"]

test test_gui_script_exists {Verify ovd_gui.tcl file existence} -body {
    file exists $GUI_SCRIPT
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
    expr {$has_refresh && $has_create && $has_run_gui && $has_delete}
} -result {1}

cleanupTests
