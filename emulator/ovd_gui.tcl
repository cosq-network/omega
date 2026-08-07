#!/usr/bin/env wish

# Omega Virtual Device Manager GUI.
#
# The GUI is a thin, safe front end to ovd_manager.sh.  The profile catalog
# remains authoritative; Tcl/Tk only selects profiles and presents results.

package require Tk
package require Ttk

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set MANAGER [file join $SCRIPT_DIR ovd_manager.sh]
set PROFILE_TOOL [file join $SCRIPT_DIR profile_catalog.py]
array set PROFILE_META {}

set bg_dark "#1E1E2E"
set bg_card "#2A2A3C"
set fg_light "#CDD6F4"
set accent_blue "#89B4FA"
set accent_green "#A6E3A1"
set accent_red "#F38BA8"

wm title . "Omega Virtual Device Manager"
wm geometry . 1160x700
wm minsize . 980 600
. configure -background $bg_dark
ttk::style theme use clam
ttk::style configure TFrame -background $bg_dark
ttk::style configure Card.TFrame -background $bg_card
ttk::style configure TLabel -background $bg_dark -foreground $fg_light -font {Helvetica 10}
ttk::style configure Title.TLabel -background $bg_dark -foreground $accent_blue -font {Helvetica 16 bold}
ttk::style configure SubTitle.TLabel -background $bg_dark -foreground $fg_light -font {Helvetica 10 italic}
ttk::style configure TButton -padding 6

ttk::frame .header -padding 15
pack .header -fill x
ttk::label .header.title -text "Omega Virtual Device Manager" -style Title.TLabel
pack .header.title -anchor w
ttk::label .header.sub -text "Predefined profiles, ext4 artifact lifecycle, and managed OVD execution"
pack .header.sub -anchor w

ttk::frame .body -padding 12
pack .body -fill both -expand 1

ttk::frame .devices -style Card.TFrame -padding 10
pack .devices -side left -fill both -expand 1 -padx {0 10}
ttk::label .devices.title -text "Managed OVDs" -font {Helvetica 12 bold}
pack .devices.title -anchor w -pady {0 8}
ttk::treeview .devices.tree -columns {profile arch ram disk storage state} -show headings -selectmode browse
foreach {column label width} {profile Profile 175 arch Architecture 90 ram RAM 70 disk Disk 70 storage Storage 85 state State 80} {
    .devices.tree heading $column -text $label
    .devices.tree column $column -width $width -anchor center
}
pack .devices.tree -fill both -expand 1
.devices.tree bind <<TreeviewSelect>> {show_selected_summary}

ttk::frame .controls -style Card.TFrame -padding 12
pack .controls -side right -fill y
ttk::label .controls.title -text "Create or manage" -font {Helvetica 12 bold}
pack .controls.title -anchor w -pady {0 8}

ttk::label .controls.profile_label -text "Predefined profile"
pack .controls.profile_label -anchor w
ttk::combobox .controls.profile -state readonly -width 38
pack .controls.profile -fill x -pady {0 6}
.controls.profile bind <<ComboboxSelected>> {profile_changed}

ttk::label .controls.name_label -text "OVD name"
pack .controls.name_label -anchor w
ttk::entry .controls.name
pack .controls.name -fill x -pady {0 6}

ttk::label .controls.arch_label -text "Architecture"
pack .controls.arch_label -anchor w
ttk::combobox .controls.arch -values {x86_64 aarch64 riscv64} -state readonly
pack .controls.arch -fill x -pady {0 6}

ttk::label .controls.ram_label -text "RAM (MB)"
pack .controls.ram_label -anchor w
ttk::entry .controls.ram
pack .controls.ram -fill x -pady {0 6}

ttk::label .controls.disk_label -text "Disk (MB)"
pack .controls.disk_label -anchor w
ttk::entry .controls.disk
pack .controls.disk -fill x -pady {0 6}

ttk::label .controls.storage_label -text "Generic storage transport"
pack .controls.storage_label -anchor w
ttk::combobox .controls.storage -values {virtio ahci usb sd optical none} -state readonly
pack .controls.storage -fill x -pady {0 6}

ttk::label .controls.network_label -text "Network"
pack .controls.network_label -anchor w
ttk::combobox .controls.network -values {none user socket} -state readonly
pack .controls.network -fill x -pady {0 10}

ttk::button .controls.create_profile -text "Create from selected profile" -command create_from_profile
pack .controls.create_profile -fill x -pady {0 5}
ttk::button .controls.create_generic -text "Create generic OVD" -command create_generic
pack .controls.create_generic -fill x -pady {0 12}

ttk::separator .controls.separator -orient horizontal
pack .controls.separator -fill x -pady 5
ttk::button .controls.inspect_profile -text "Inspect selected profile" -command inspect_profile
pack .controls.inspect_profile -fill x -pady {0 5}
ttk::button .controls.artifacts -text "Check profile artifacts (dry-run)" -command check_profile_artifacts
pack .controls.artifacts -fill x -pady {0 12}

ttk::label .controls.selected -text "No OVD selected" -wraplength 300
pack .controls.selected -anchor w -pady {0 8}
ttk::button .controls.launch_gui -text "Launch selected (GUI)" -command {launch_selected true}
pack .controls.launch_gui -fill x -pady {0 5}
ttk::button .controls.launch_headless -text "Launch selected (headless)" -command {launch_selected false}
pack .controls.launch_headless -fill x -pady {0 5}
ttk::button .controls.stop -text "Stop selected" -command stop_selected
pack .controls.stop -fill x -pady {0 5}
ttk::button .controls.logs -text "View selected logs" -command show_selected_logs
pack .controls.logs -fill x -pady {0 5}
ttk::button .controls.validate -text "Validate selected" -command validate_selected
pack .controls.validate -fill x -pady {0 5}
ttk::button .controls.delete -text "Delete selected" -command delete_selected
pack .controls.delete -fill x

proc run_manager {args} {
    global MANAGER
    if {[catch {exec bash $MANAGER {*}$args} result options]} {
        set detail $result
        if {[dict exists $options -errorinfo]} {set detail [dict get $options -errorinfo]}
        return -code error $detail
    }
    return $result
}

proc refresh_profiles {} {
    global PROFILE_TOOL PROFILE_META
    set previous [.controls.profile get]
    set original $previous
    set names {"(generic OVD)"}
    catch {unset PROFILE_META}
    array set PROFILE_META {}
    if {[catch {exec python3 $PROFILE_TOOL list --tsv} output]} {
        .controls.profile configure -values $names
        .controls.profile set "(generic OVD)"
        profile_changed
        return
    }
    foreach line [split [string trim $output] "\n"] {
        if {$line eq ""} continue
        set fields [split $line "\t"]
        if {[llength $fields] != 8} continue
        lassign $fields profile display arch backend status default_ram image_size native_creation
        lappend names $profile
        set PROFILE_META($profile) [list $display $arch $backend $status $default_ram $image_size $native_creation]
    }
    .controls.profile configure -values $names
    if {[lsearch -exact $names $previous] < 0} {set previous "(generic OVD)"}
    .controls.profile set $previous
    if {$original ne [.controls.profile get]} {profile_changed}
}

proc profile_changed {} {
    global PROFILE_META
    set profile [.controls.profile get]
    if {$profile eq "(generic OVD)" || ![info exists PROFILE_META($profile)]} {
        .controls.create_profile state disabled
        .controls.inspect_profile state disabled
        .controls.artifacts state disabled
        .controls.arch state !disabled
        .controls.ram state !disabled
        .controls.disk state !disabled
        .controls.storage state !disabled
        return
    }
    lassign $PROFILE_META($profile) display arch backend status default_ram image_size native_creation
    if {$native_creation eq "true"} {
        .controls.create_profile state !disabled
    } else {
        .controls.create_profile state disabled
    }
    .controls.inspect_profile state !disabled
    .controls.artifacts state !disabled
    .controls.arch set $arch
    .controls.arch state disabled
    .controls.storage set virtio
    .controls.storage state disabled
    .controls.ram delete 0 end
    .controls.disk delete 0 end
    .controls.ram insert 0 $default_ram
    .controls.disk insert 0 $image_size
    .controls.selected configure -text "Profile: $profile\n$display\nBackend: $backend\nStatus: $status"
}

proc refresh_devices {} {
    .devices.tree delete [.devices.tree children {}]
    if {[catch {run_manager list} output]} return
    set name {}; set profile {}; set arch {}; set ram {}; set disk {}; set storage {}
    foreach line [split $output "\n"] {
        if {[string match "Device: *" $line]} {set name [string range $line 8 end]}
        if {[string match "  ovd.arch=*" $line]} {set arch [string range $line 11 end]}
        if {[string match "  ovd.ram=*" $line]} {set ram [string range $line 10 end]}
        if {[string match "  ovd.disk=*" $line]} {set disk [string range $line 11 end]}
        if {[string match "  ovd.storage=*" $line]} {set storage [string range $line 14 end]}
        if {[string match "  ovd.profile=*" $line]} {set profile [string range $line 14 end]}
        if {[string match "  ovd.state=*" $line]} {
            set state [string range $line 12 end]
            if {$name ne ""} {.devices.tree insert {} end -id $name -values [list $profile $arch $ram $disk $storage $state]}
            set name {}; set profile {}
        }
    }
    show_selected_summary
}

proc selected_device {} {
    set selected [.devices.tree selection]
    if {$selected eq ""} {tk_messageBox -icon warning -message "Select an OVD first."; return ""}
    return [lindex $selected 0]
}

proc show_selected_summary {} {
    set selected [.devices.tree selection]
    if {$selected eq ""} {.controls.selected configure -text "No OVD selected"; return}
    set values [.devices.tree item [lindex $selected 0] -values]
    .controls.selected configure -text "Selected: [lindex $selected 0]\nProfile: [lindex $values 0]\nState: [lindex $values 5]"
}

proc create_from_profile {} {
    set profile [.controls.profile get]
    set name [.controls.name get]
    if {$profile eq "(generic OVD)" || $name eq ""} {tk_messageBox -icon error -message "Select a native profile and enter an OVD name."; return}
    if {[catch {run_manager create-from-profile --profile $profile --name $name --ram [.controls.ram get] --disk [.controls.disk get]} result]} {
        tk_messageBox -icon error -title "Profile creation failed" -message $result
        return
    }
    refresh_devices
    .controls.name delete 0 end
}

proc create_generic {} {
    set name [.controls.name get]
    if {$name eq ""} {tk_messageBox -icon error -message "Enter an OVD name."; return}
    if {[catch {run_manager create --name $name --arch [.controls.arch get] --ram [.controls.ram get] --disk [.controls.disk get] --storage [.controls.storage get] --network [.controls.network get]} result]} {
        tk_messageBox -icon error -title "OVD creation failed" -message $result
        return
    }
    refresh_devices
    .controls.name delete 0 end
}

proc inspect_profile {} {
    set profile [.controls.profile get]
    if {$profile eq "(generic OVD)"} return
    if {[catch {run_manager profiles show --profile $profile --json} result]} {tk_messageBox -icon error -message $result; return}
    tk_messageBox -title "Profile: $profile" -message $result
}

proc check_profile_artifacts {} {
    set profile [.controls.profile get]
    if {$profile eq "(generic OVD)"} return
    if {[catch {run_manager profiles artifacts --profile $profile --dry-run} result]} {tk_messageBox -icon error -message $result; return}
    tk_messageBox -title "Artifacts: $profile" -message $result
}

proc launch_selected {gui} {
    set dev [selected_device]
    if {$dev eq ""} return
    set mode [expr {$gui ? "--gpu" : "--no-gpu"}]
    if {[catch {exec bash [file join [file dirname [info script]] ovd_run.sh] run --name $dev $mode --daemon &} result]} {tk_messageBox -icon error -message $result}
    refresh_devices
}

proc stop_selected {} {
    set dev [selected_device]
    if {$dev eq ""} return
    if {[catch {run_manager stop --name $dev} result]} {tk_messageBox -icon error -message $result}
    refresh_devices
}

proc show_selected_logs {} {
    set dev [selected_device]
    if {$dev eq ""} return
    if {[catch {run_manager logs --name $dev} result]} {tk_messageBox -icon info -message "No emulator log is available yet."; return}
    tk_messageBox -title "OVD Logs: $dev" -message $result
}

proc validate_selected {} {
    set dev [selected_device]
    if {$dev eq ""} return
    if {[catch {run_manager validate --name $dev} result]} {tk_messageBox -icon error -message $result; return}
    tk_messageBox -icon info -title "Validation passed" -message $result
}

proc delete_selected {} {
    set dev [selected_device]
    if {$dev eq ""} return
    if {[tk_messageBox -type yesno -icon question -message "Delete '$dev'?"] ne "yes"} return
    if {[catch {run_manager delete --name $dev} result]} {tk_messageBox -icon error -message $result; return}
    refresh_devices
}

proc schedule_refresh {} {
    refresh_profiles
    refresh_devices
    after 3000 schedule_refresh
}

.controls.arch set x86_64
.controls.ram insert 0 1024
.controls.disk insert 0 64
.controls.storage set virtio
.controls.network set none
refresh_profiles
refresh_devices
after 3000 schedule_refresh
