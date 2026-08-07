#!/usr/bin/env wish

# Omega Virtual Device (OVD) Manager GUI Application
# Built using Tcl/Tk with dark aesthetic theme and TTK widgets

package require Ttk

# Configure Window Properties
wm title . "Omega Virtual Device (OVD) Manager"
wm geometry . 820x560
wm resizable . 1 1

# Configure Custom Styling System
ttk::style theme use clam

# Custom Dark Theme Colors
set bg_dark      "#1E1E2E"
set bg_card      "#2A2A3C"
set fg_light     "#CDD6F4"
set accent_blue  "#89B4FA"
set accent_green "#A6E3A1"
set accent_red   "#F38BA8"

. configure -background $bg_dark

ttk::style configure TFrame -background $bg_dark
ttk::style configure Card.TFrame -background $bg_card -relief flat
ttk::style configure TLabel -background $bg_dark -foreground $fg_light -font {"Helvetica" 11}
ttk::style configure Title.TLabel -background $bg_dark -foreground $accent_blue -font {"Helvetica" 16 "bold"}
ttk::style configure SubTitle.TLabel -background $bg_dark -foreground $fg_light -font {"Helvetica" 10 "italic"}

ttk::style configure TButton -font {"Helvetica" 10 "bold"} -padding 6
ttk::style configure Create.TButton -background $accent_green -foreground "#11111B"
ttk::style configure Run.TButton -background $accent_blue -foreground "#11111B"
ttk::style configure Delete.TButton -background $accent_red -foreground "#11111B"

# Header Banner
ttk::frame .header -padding 15
pack .header -fill x -side top

ttk::label .header.title -text "Omega Virtual Device (OVD) Manager" -style Title.TLabel
pack .header.title -side top -anchor w

ttk::label .header.sub -text "Manage Omega OS virtual devices — x86_64 VGA; ARM/RISC-V SimpleFb with serial fallback" -style SubTitle.TLabel
pack .header.sub -side top -anchor w

# Main Container Frame
ttk::frame .main -padding 15
pack .main -fill both -expand true -side top

# Left Panel: Device List Treeview
ttk::frame .main.left -style Card.TFrame -padding 10
pack .main.left -side left -fill both -expand true -padx {0 10}

ttk::label .main.left.lbl -text "Configured Virtual Devices" -style TLabel -font {"Helvetica" 12 "bold"}
pack .main.left.lbl -side top -anchor w -pady {0 10}

ttk::treeview .main.left.tree -columns {arch ram disk} -show headings -height 15
.main.left.tree heading arch -text "Architecture"
.main.left.tree heading ram -text "RAM (MB)"
.main.left.tree heading disk -text "Disk (MB)"

.main.left.tree column arch -width 120 -anchor center
.main.left.tree column ram -width 100 -anchor center
.main.left.tree column disk -width 100 -anchor center

pack .main.left.tree -fill both -expand true -side top

# Right Panel: Action Controls & Device Creator
ttk::frame .main.right -style Card.TFrame -padding 15
pack .main.right -side right -fill y -width 320

ttk::label .main.right.create_title -text "Create New OVD" -style TLabel -font {"Helvetica" 12 "bold"}
pack .main.right.create_title -side top -anchor w -pady {0 10}

# Device Form
ttk::label .main.right.name_lbl -text "Device Name:" -style TLabel
pack .main.right.name_lbl -side top -anchor w

entry .main.right.name_entry -bg "#313244" -fg "#CDD6F4" -insertbackground "#CDD6F4" -relief flat
pack .main.right.name_entry -side top -fill x -pady {0 10}

ttk::label .main.right.arch_lbl -text "Architecture Target:" -style TLabel
pack .main.right.arch_lbl -side top -anchor w

ttk::combobox .main.right.arch_combo -values {"x86_64" "aarch64" "riscv64"} -state readonly
.main.right.arch_combo set "x86_64"
pack .main.right.arch_combo -side top -fill x -pady {0 10}

ttk::label .main.right.ram_lbl -text "RAM Size (MB):" -style TLabel
pack .main.right.ram_lbl -side top -anchor w

entry .main.right.ram_entry -bg "#313244" -fg "#CDD6F4" -insertbackground "#CDD6F4" -relief flat
.main.right.ram_entry insert 0 "1024"
pack .main.right.ram_entry -side top -fill x -pady {0 10}

ttk::label .main.right.disk_lbl -text "Storage Disk (MB):" -style TLabel
pack .main.right.disk_lbl -side top -anchor w

entry .main.right.disk_entry -bg "#313244" -fg "#CDD6F4" -insertbackground "#CDD6F4" -relief flat
.main.right.disk_entry insert 0 "64"
pack .main.right.disk_entry -side top -fill x -pady {0 15}

# Create Button
button .main.right.btn_create -text "Create Virtual Device" -bg $accent_green -fg "#11111B" -font {"Helvetica" 10 "bold"} -relief flat -command create_device
pack .main.right.btn_create -side top -fill x -pady {0 20}

# Divider
ttk::separator .main.right.sep -orient horizontal
pack .main.right.sep -side top -fill x -pady 10

# Execution Buttons
ttk::label .main.right.run_title -text "Device Execution" -style TLabel -font {"Helvetica" 12 "bold"}
pack .main.right.run_title -side top -anchor w -pady {0 10}

button .main.right.btn_run_gui -text "Launch (GUI — VGA / experimental VirtIO-GPU)" -bg $accent_blue -fg "#11111B" -font {"Helvetica" 10 "bold"} -relief flat -command run_device_gui
pack .main.right.btn_run_gui -side top -fill x -pady {0 8}

button .main.right.btn_run_head -text "Launch (Headless / SimpleFb fallback)" -bg "#89DCEB" -fg "#11111B" -font {"Helvetica" 10 "bold"} -relief flat -command run_device_headless
pack .main.right.btn_run_head -side top -fill x -pady {0 8}

button .main.right.btn_delete -text "Delete Selected Device" -bg $accent_red -fg "#11111B" -font {"Helvetica" 10 "bold"} -relief flat -command delete_device
pack .main.right.btn_delete -side top -fill x

# Procedures
proc refresh_device_list {} {
    .main.left.tree delete [.main.left.tree children {}]
    set script_path "[file dirname [info script]]/ovd_manager.sh"
    if {[catch {exec bash $script_path list} output]} {
        return
    }
    set current_name ""
    set current_arch ""
    set current_ram ""
    set current_disk ""

    foreach line [split $output "\n"] {
        if {[string match "Device: *" $line]} {
            set current_name [string range $line 8 end]
        } elseif {[string match "  ovd.arch=*" $line]} {
            set current_arch [string range $line 11 end]
        } elseif {[string match "  ovd.ram=*" $line]} {
            set current_ram [string range $line 10 end]
        } elseif {[string match "  ovd.disk=*" $line]} {
            set current_disk [string range $line 11 end]
            if {$current_name ne ""} {
                .main.left.tree insert {} end -id $current_name -values [list $current_arch $current_ram $current_disk]
            }
        }
    }
}

proc create_device {} {
    set name [.main.right.name_entry get]
    set arch [.main.right.arch_combo get]
    set ram [.main.right.ram_entry get]
    set disk [.main.right.disk_entry get]

    if {$name eq ""} {
        tk_messageBox -icon error -message "Please enter a valid Device Name."
        return
    }

    set script_path "[file dirname [info script]]/ovd_manager.sh"
    if {[catch {exec bash $script_path create --name $name --arch $arch --ram $ram --disk $disk} res]} {
        tk_messageBox -icon error -message "Failed to create OVD device:\n$res"
    } else {
        refresh_device_list
        .main.right.name_entry delete 0 end
    }
}

proc get_selected_device {} {
    set selected [.main.left.tree selection]
    if {$selected eq ""} {
        tk_messageBox -icon warning -message "Please select a Virtual Device from the list."
        return ""
    }
    return $selected
}

proc run_device_gui {} {
    set dev [get_selected_device]
    if {$dev eq ""} return
    set script_path "[file dirname [info script]]/ovd_run.sh"
    exec bash $script_path run --name $dev --gpu &
}

proc run_device_headless {} {
    set dev [get_selected_device]
    if {$dev eq ""} return
    set script_path "[file dirname [info script]]/ovd_run.sh"
    exec bash $script_path run --name $dev --no-gpu &
}

proc delete_device {} {
    set dev [get_selected_device]
    if {$dev eq ""} return
    set answer [tk_messageBox -message "Are you sure you want to delete '$dev'?" -type yesno -icon question]
    if {$answer eq "yes"} {
        set script_path "[file dirname [info script]]/ovd_manager.sh"
        exec bash $script_path delete --name $dev
        refresh_device_list
    }
}

# Initial List Refresh
refresh_device_list
