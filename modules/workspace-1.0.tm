# workspace-1.0.tm
# Workspace management for stim2 development frontend
#
# Usage:
#   package require workspace
#   set ::workspace::system_examples_path /path/to/examples
#   workspace::init
#
# New setup/adjuster system:
#   workspace::reset
#   workspace::setup proc_name {param specs...} -adjusters {adjuster_names...}
#   workspace::adjuster name {param specs...} -target obj_name -proc proc_name
#
# Multiple setups per file supported - each with its own adjusters.
#

package require yajltcl

namespace eval ::workspace {
    variable version 1.0
    
    # State
    variable initialized 0
    variable workspaces {}
    
    # System examples path - set by stim2 startup before init
    variable system_examples_path ""
    
    # Legacy export tracking (for backwards compatibility)
    variable exports {}
    variable current_file ""
    
    # New setup/adjuster system
    variable setups {}          ;# dict: setup_name -> {proc params adjusters file}
    variable adjusters {}       ;# dict: adjuster_name -> {proc target params file setup}
    variable active_setup ""    ;# currently active setup name
    
    # Notification callback
    variable notify_callback ""
}

#
# JSON Encoding Helpers
#
proc ::workspace::encode_param {yh param} {
    $yh map_open
    
    dict for {key val} $param {
        $yh string $key
        
        switch $key {
            name - type - label - unit {
                $yh string $val
            }
            default {
                set ptype [dict get $param type]
                if {$ptype in {int float}} {
                    if {[string is integer -strict $val]} {
                        $yh integer $val
                    } elseif {[string is double -strict $val]} {
                        $yh double $val
                    } else {
                        $yh string $val
                    }
                } else {
                    $yh string $val
                }
            }
            min - max - step {
                if {[string is integer -strict $val]} {
                    $yh integer $val
                } else {
                    $yh double $val
                }
            }
            choices {
                $yh array_open
                foreach choice $val {
                    $yh string $choice
                }
                $yh array_close
            }
        }
    }
    
    $yh map_close
}

proc ::workspace::encode_export_info {yh info} {
    $yh map_open
    
    $yh string "proc"
    $yh string [dict get $info proc]
    
    $yh string "params"
    $yh array_open
    foreach param [dict get $info params] {
        encode_param $yh $param
    }
    $yh array_close
    
    $yh map_close
}

proc ::workspace::encode_setup_info {yh name info} {
    $yh map_open
    
    $yh string "name"
    $yh string $name
    
    $yh string "proc"
    $yh string [dict get $info proc]
    
    $yh string "params"
    $yh array_open
    foreach param [dict get $info params] {
        encode_param $yh $param
    }
    $yh array_close
    
    $yh string "adjusters"
    $yh array_open
    foreach adj [dict get $info adjusters] {
        $yh string $adj
    }
    $yh array_close
    
    $yh map_close
}

proc ::workspace::encode_adjuster_info {yh name info} {
    $yh map_open
    
    $yh string "name"
    $yh string $name
    
    $yh string "proc"
    $yh string [dict get $info proc]
    
    $yh string "target"
    $yh string [dict get $info target]
    
    # Include uniform name if this is a shader uniform adjuster
    if {[dict exists $info uniform]} {
        $yh string "uniform"
        $yh string [dict get $info uniform]
    }
    
    $yh string "setup"
    $yh string [dict get $info setup]
    
    # Flag if auto-generated
    if {[dict exists $info auto_generated] && [dict get $info auto_generated]} {
        $yh string "auto_generated"
        $yh bool 1
    }
    
    $yh string "params"
    $yh array_open
    foreach param [dict get $info params] {
        encode_param $yh $param
    }
    $yh array_close
    
    $yh map_close
}

proc ::workspace::encode_workspace {yh ws} {
    $yh map_open
    
    foreach key {id name path icon} {
        if {[dict exists $ws $key]} {
            $yh string $key
            $yh string [dict get $ws $key]
        }
    }
    
    foreach key {builtin default_save exists writable} {
        if {[dict exists $ws $key]} {
            $yh string $key
            $yh bool [dict get $ws $key]
        }
    }
    
    $yh map_close
}

proc ::workspace::encode_file_item {yh item} {
    $yh map_open
    
    $yh string "type"
    $yh string [dict get $item type]
    
    $yh string "name"
    $yh string [dict get $item name]
    
    if {[dict exists $item title]} {
        $yh string "title"
        $yh string [dict get $item title]
    }
    
    $yh string "path"
    $yh string [dict get $item path]
    
    if {[dict get $item type] eq "directory" && [dict exists $item children]} {
        $yh string "children"
        $yh array_open
        foreach child [dict get $item children] {
            encode_file_item $yh $child
        }
        $yh array_close
    }
    
    $yh map_close
}

#
# Initialization
#
proc ::workspace::init {} {
    variable initialized
    variable workspaces
    variable system_examples_path
    
    if {$initialized} return
    
    set ws_list {}
    
    # Built-in examples (set by stim2 startup)
    if {$system_examples_path ne "" && [file isdirectory $system_examples_path]} {
        lappend ws_list [dict create \
            id "stim2-examples" \
            name "stim2 Examples" \
            path $system_examples_path \
            icon "package" \
            builtin 1]
    }
    
    # Additional workspaces from environment variable
    if {[info exists ::env(STIM2_WORKSPACES)]} {
        set idx 0
        foreach p [split $::env(STIM2_WORKSPACES) ":"] {
            set p [string trim $p]
            if {$p ne "" && [file isdirectory $p]} {
                lappend ws_list [dict create \
                    id "env-$idx" \
                    name [file tail $p] \
                    path $p \
                    icon "folder" \
                    builtin 0]
                incr idx
            }
        }
    }
    
    set workspaces [dict create workspaces $ws_list active "stim2-examples"]
    set initialized 1
}

#
# Workspace Management
#
proc ::workspace::list_workspaces {} {
    variable initialized
    variable workspaces
    
    if {!$initialized} { init }
    
    set yh [yajl create #auto]
    
    $yh map_open
    $yh string "workspaces"
    $yh array_open
    
    foreach ws [dict get $workspaces workspaces] {
        set path [file normalize [dict get $ws path]]
        set ws_copy $ws
        dict set ws_copy path $path
        dict set ws_copy exists [file isdirectory $path]
        dict set ws_copy writable [expr {[file isdirectory $path] && [file writable $path]}]
        encode_workspace $yh $ws_copy
    }
    
    $yh array_close
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

proc ::workspace::add {name path} {
    variable initialized
    variable workspaces
    
    if {!$initialized} { init }
    
    set path [file normalize $path]
    
    if {![file isdirectory $path]} {
        error "Path does not exist or is not a directory: $path"
    }
    
    set id [string tolower [regsub -all {[^a-zA-Z0-9]} $name "-"]]
    set base_id $id
    set n 1
    while {[find_workspace_index $id] >= 0} {
        set id "${base_id}-$n"
        incr n
    }
    
    set ws [dict create \
        id $id \
        name $name \
        path $path \
        icon "folder" \
        builtin 0]
    
    dict lappend workspaces workspaces $ws
    
    set yh [yajl create #auto]
    encode_workspace $yh $ws
    set json [$yh get]
    $yh delete
    
    return $json
}

proc ::workspace::remove {workspace_id} {
    variable initialized
    variable workspaces
    
    if {!$initialized} { init }
    
    set idx [find_workspace_index $workspace_id]
    if {$idx < 0} {
        error "Workspace not found: $workspace_id"
    }
    
    set ws [lindex [dict get $workspaces workspaces] $idx]
    if {[dict exists $ws builtin] && [dict get $ws builtin]} {
        error "Cannot remove built-in workspace"
    }
    
    set ws_list [dict get $workspaces workspaces]
    set ws_list [lreplace $ws_list $idx $idx]
    dict set workspaces workspaces $ws_list
    
    set yh [yajl create #auto]
    $yh map_open
    $yh string "removed"
    $yh string $workspace_id
    $yh map_close
    set json [$yh get]
    $yh delete
    
    return $json
}

proc ::workspace::find_workspace_index {workspace_id} {
    variable workspaces
    
    set idx 0
    foreach ws [dict get $workspaces workspaces] {
        if {[dict get $ws id] eq $workspace_id} {
            return $idx
        }
        incr idx
    }
    return -1
}

proc ::workspace::get_workspace {workspace_id} {
    variable workspaces
    
    foreach ws [dict get $workspaces workspaces] {
        if {[dict get $ws id] eq $workspace_id} {
            return $ws
        }
    }
    error "Workspace not found: $workspace_id"
}

#
# File Operations
#
proc ::workspace::scan {workspace_id} {
    variable initialized
    
    if {!$initialized} { init }
    
    set ws [get_workspace $workspace_id]
    set base_path [file normalize [dict get $ws path]]
    
    set yh [yajl create #auto]
    $yh map_open
    
    $yh string "workspace"
    $yh string $workspace_id
    
    $yh string "path"
    $yh string $base_path
    
    if {![file isdirectory $base_path]} {
        $yh string "error"
        $yh string "Workspace path does not exist"
        $yh string "items"
        $yh array_open
        $yh array_close
    } else {
        $yh string "writable"
        $yh bool [file writable $base_path]
        
        $yh string "items"
        $yh array_open
        foreach item [scan_dir $base_path ""] {
            encode_file_item $yh $item
        }
        $yh array_close
    }
    
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

proc ::workspace::parse_index {dir_path} {
    set index_file [file join $dir_path INDEX]
    set result {}
    
    if {![file exists $index_file]} {
        return $result
    }
    
    set f [open $index_file r]
    while {[gets $f line] >= 0} {
        set line [string trim $line]
        # Skip empty lines and comments
        if {$line eq "" || [string index $line 0] eq "#"} continue
        
        # Parse: filename "Title String"
        if {[regexp {^(\S+)\s+"([^"]+)"} $line -> name title]} {
            dict set result $name $title
        } elseif {[regexp {^(\S+)} $line -> name]} {
            # No title, just filename
            dict set result $name ""
        }
    }
    close $f
    
    return $result
}

proc ::workspace::scan_dir {base_path rel_path} {
    if {$rel_path eq ""} {
        set full_path $base_path
    } else {
        set full_path [file join $base_path $rel_path]
    }
    
    # Parse INDEX file if present
    set index_info [parse_index $full_path]
    set has_index [expr {[dict size $index_info] > 0}]
    
    # Collect all items first
    set file_items {}
    set dir_items {}
    
    foreach entry [glob -nocomplain -directory $full_path *] {
        set name [file tail $entry]
        
        # Skip hidden files and INDEX file
        if {[string index $name 0] eq "."} continue
        if {$name eq "INDEX"} continue
        
        if {$rel_path eq ""} {
            set entry_rel $name
        } else {
            set entry_rel [file join $rel_path $name]
        }
        
        if {[file isdirectory $entry]} {
            set children [scan_dir $base_path $entry_rel]
            if {[llength $children] > 0} {
                set dir_item [dict create \
                    type "directory" \
                    name $name \
                    path $entry_rel \
                    children $children]
                
                # Add title from INDEX if available (for directories too)
                if {$has_index && [dict exists $index_info $name]} {
                    set title [dict get $index_info $name]
                    if {$title ne ""} {
                        dict set dir_item title $title
                    }
                }
                
                lappend dir_items $dir_item
            }
        } elseif {[string match "*.tcl" $name]} {
            set basename [file rootname $name]
            set item [dict create \
                type "file" \
                name $name \
                path $entry_rel]
            
            # Add title from INDEX if available
            if {$has_index && [dict exists $index_info $basename]} {
                set title [dict get $index_info $basename]
                if {$title ne ""} {
                    dict set item title $title
                }
            }
            
            lappend file_items $item
        }
    }
    
    # Order directories according to INDEX if present
    if {$has_index} {
        set ordered_dirs {}
        set indexed_dir_names {}
        
        # First add directories in INDEX order
        dict for {idx_name title} $index_info {
            foreach item $dir_items {
                if {[dict get $item name] eq $idx_name} {
                    lappend ordered_dirs $item
                    lappend indexed_dir_names $idx_name
                    break
                }
            }
        }
        
        # Then add any directories not in INDEX (alphabetically)
        foreach item [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $dir_items] {
            if {[dict get $item name] ni $indexed_dir_names} {
                lappend ordered_dirs $item
            }
        }
        
        set dir_items $ordered_dirs
    } else {
        # No INDEX - sort directories alphabetically
        set dir_items [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $dir_items]
    }
    
    # Order files according to INDEX if present
    if {$has_index} {
        set ordered_files {}
        set indexed_names {}
        
        # First add files in INDEX order
        dict for {basename title} $index_info {
            set tcl_name "${basename}.tcl"
            foreach item $file_items {
                if {[dict get $item name] eq $tcl_name} {
                    lappend ordered_files $item
                    lappend indexed_names $tcl_name
                    break
                }
            }
        }
        
        # Then add any files not in INDEX (alphabetically)
        foreach item [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $file_items] {
            if {[dict get $item name] ni $indexed_names} {
                lappend ordered_files $item
            }
        }
        
        set file_items $ordered_files
    } else {
        # No INDEX - sort alphabetically
        set file_items [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $file_items]
    }
    
    # Return directories first, then files
    return [concat $dir_items $file_items]
}

proc ::workspace::load {workspace_id filepath} {
    variable initialized
    
    if {!$initialized} { init }
    
    set ws [get_workspace $workspace_id]
    set base_path [file normalize [dict get $ws path]]
    set full_path [file join $base_path $filepath]
    
    set norm_full [file normalize $full_path]
    if {![string match "${base_path}/*" $norm_full]} {
        error "Invalid path: $filepath"
    }
    
    if {![file exists $full_path]} {
        error "File not found: $filepath"
    }
    
    set f [open $full_path r]
    set content [read $f]
    close $f
    
    return $content
}

proc ::workspace::save {workspace_id filepath content} {
    variable initialized
    
    if {!$initialized} { init }
    
    set ws [get_workspace $workspace_id]
    set base_path [file normalize [dict get $ws path]]
    
    if {![file writable $base_path]} {
        error "Workspace is read-only"
    }
    
    set full_path [file join $base_path $filepath]
    
    set norm_full [file normalize $full_path]
    if {![string match "${base_path}/*" $norm_full]} {
        error "Invalid path: $filepath"
    }
    
    file mkdir [file dirname $full_path]
    
    set f [open $full_path w]
    puts -nonewline $f $content
    close $f
    
    set yh [yajl create #auto]
    $yh map_open
    $yh string "saved"
    $yh string $filepath
    $yh map_close
    set json [$yh get]
    $yh delete
    
    return $json
}

#
# Reset - clears exports/setups/adjusters for current file
#
proc ::workspace::reset {} {
    variable initialized
    variable exports
    variable setups
    variable adjusters
    variable active_setup
    variable current_file
    
    if {!$initialized} return
    
    if {$current_file eq ""} {
        # Clear everything
        set exports {}
        set setups {}
        set adjusters {}
        set active_setup ""
    } else {
        # Clear only items from current file
        set new_exports {}
        dict for {procname info} $exports {
            if {[dict get $info file] ne $current_file} {
                dict set new_exports $procname $info
            }
        }
        set exports $new_exports
        
        set new_setups {}
        dict for {name info} $setups {
            if {[dict get $info file] ne $current_file} {
                dict set new_setups $name $info
            }
        }
        set setups $new_setups
        
        set new_adjusters {}
        dict for {name info} $adjusters {
            if {[dict get $info file] ne $current_file} {
                dict set new_adjusters $name $info
            }
        }
        set adjusters $new_adjusters
        
        # Clear active_setup if it was from this file
        if {$active_setup ne "" && [dict exists $setups $active_setup]} {
            if {[dict get [dict get $setups $active_setup] file] eq $current_file} {
                set active_setup ""
            }
        }
    }
}

#
# New Setup/Adjuster System
#

# workspace::setup proc_name {params...} ?-adjusters {adj_list}?
proc ::workspace::setup {procname params args} {
    variable initialized
    variable setups
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set adjuster_list {}
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -adjusters {
                incr i
                set adjuster_list [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::setup: $opt"
            }
        }
        incr i
    }
    
    # Parse parameters
    set parsed_params [parse_verbose_params $params]
    
    # Store setup info
    dict set setups $procname [dict create \
        proc $procname \
        params $parsed_params \
        adjusters $adjuster_list \
        file $current_file]
    
    # Track current setup for adjuster registration
    set current_setup $procname
    
    notify "setup" [dict create proc $procname file $current_file]
}

# workspace::adjuster name {params...} -target obj_name -proc proc_name
proc ::workspace::adjuster {name params args} {
    variable initialized
    variable adjusters
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set target ""
    set proc_name ""
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -target {
                incr i
                set target [lindex $args $i]
            }
            -proc {
                incr i
                set proc_name [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::adjuster: $opt"
            }
        }
        incr i
    }
    
    if {$proc_name eq ""} {
        error "workspace::adjuster requires -proc option"
    }
    
    # Parse parameters
    set parsed_params [parse_verbose_params $params]
    
    # Determine which setup this adjuster belongs to
    set setup_name ""
    if {[info exists current_setup] && $current_setup ne ""} {
        set setup_name $current_setup
    }
    
    # Store adjuster info
    dict set adjusters $name [dict create \
        proc $proc_name \
        target $target \
        params $parsed_params \
        file $current_file \
        setup $setup_name]
    
    notify "adjuster" [dict create name $name proc $proc_name file $current_file]
}

# Invoke a setup proc with given arguments
proc ::workspace::invoke_setup {setup_name args} {
    variable setups
    variable active_setup
    
    if {![dict exists $setups $setup_name]} {
        error "Unknown setup: $setup_name"
    }
    
    set info [dict get $setups $setup_name]
    set procname [dict get $info proc]
    
    # Call the setup proc
    set result [uplevel #0 [list $procname {*}$args]]
    
    # Mark this setup as active
    set active_setup $setup_name
    
    return $result
}

# Invoke an adjuster - prepends target to args
proc ::workspace::invoke_adjuster {adjuster_name args} {
    variable adjusters
    
    if {![dict exists $adjusters $adjuster_name]} {
        error "Unknown adjuster: $adjuster_name"
    }
    
    set info [dict get $adjusters $adjuster_name]
    set procname [dict get $info proc]
    set target [dict get $info target]
    
    # Build argument list: target first (if specified), then the rest
    if {$target ne ""} {
        set call_args [list $target {*}$args]
    } else {
        set call_args $args
    }
    
    # Call the proc
    return [uplevel #0 [list $procname {*}$call_args]]
}

#
# Legacy Export System (backwards compatibility)
#
proc ::workspace::export {procname args} {
    variable initialized
    variable exports
    variable current_file
    
    if {!$initialized} return
    
    if {[llength $args] == 0} {
        set params {}
    } elseif {[llength $args] == 1} {
        set first [lindex $args 0]
        if {[string first ":" $first] >= 0} {
            set params [parse_compact_params [::list $first]]
        } else {
            set params [parse_verbose_params $first]
        }
    } else {
        if {[string first ":" [lindex $args 0]] >= 0} {
            set params [parse_compact_params $args]
        } else {
            set params [parse_verbose_params $args]
        }
    }
    
    dict set exports $procname [dict create \
        proc $procname \
        params $params \
        file $current_file]
    
    notify "export" [dict create proc $procname file $current_file]
}

proc ::workspace::parse_compact_params {specs} {
    set params {}
    
    foreach spec $specs {
        set parts [split $spec ":"]
        set name [lindex $parts 0]
        set type [lindex $parts 1]
        
        set p [dict create name $name type $type]
        
        switch $type {
            int - float {
                if {[llength $parts] > 2} { dict set p min [lindex $parts 2] }
                if {[llength $parts] > 3} { dict set p max [lindex $parts 3] }
                if {[llength $parts] > 4} { dict set p step [lindex $parts 4] }
                if {[llength $parts] > 5} { dict set p default [lindex $parts 5] }
            }
            bool {
                if {[llength $parts] > 2} { dict set p default [lindex $parts 2] }
            }
            string {
                if {[llength $parts] > 2} { dict set p default [lindex $parts 2] }
            }
            choice {
                if {[llength $parts] > 2} { 
                    dict set p choices [split [lindex $parts 2] ","]
                }
                if {[llength $parts] > 3} { dict set p default [lindex $parts 3] }
            }
        }
        
        lappend params $p
    }
    
    return $params
}

proc ::workspace::parse_verbose_params {spec_list} {
    set params {}
    
    foreach {name spec} $spec_list {
        set type [lindex $spec 0]
        set p [dict create name $name type $type]
        
        switch $type {
            int - float {
                if {[llength $spec] > 1} { dict set p min [lindex $spec 1] }
                if {[llength $spec] > 2} { dict set p max [lindex $spec 2] }
                if {[llength $spec] > 3} { dict set p step [lindex $spec 3] }
                if {[llength $spec] > 4} { dict set p default [lindex $spec 4] }
                if {[llength $spec] > 5} { dict set p label [lindex $spec 5] }
                if {[llength $spec] > 6} { dict set p unit [lindex $spec 6] }
            }
            bool {
                if {[llength $spec] > 1} { dict set p default [lindex $spec 1] }
                if {[llength $spec] > 2} { dict set p label [lindex $spec 2] }
            }
            string {
                if {[llength $spec] > 1} { dict set p default [lindex $spec 1] }
                if {[llength $spec] > 2} { dict set p label [lindex $spec 2] }
            }
            choice {
                if {[llength $spec] > 1} { dict set p choices [lindex $spec 1] }
                if {[llength $spec] > 2} { dict set p default [lindex $spec 2] }
                if {[llength $spec] > 3} { dict set p label [lindex $spec 3] }
            }
        }
        
        lappend params $p
    }
    
    return $params
}

#
# File Activation
#
proc ::workspace::activate {workspace_id filepath} {
    variable initialized
    variable current_file
    variable current_setup
    
    if {!$initialized} { init }
    
    set ws [get_workspace $workspace_id]
    set base_path [file normalize [dict get $ws path]]
    set full_path [file join $base_path $filepath]
    
    set norm_full [file normalize $full_path]
    if {![string match "${base_path}/*" $norm_full]} {
        error "Invalid path: $filepath"
    }
    
    if {![file exists $full_path]} {
        error "File not found: $filepath"
    }
    
    set current_file $filepath
    set current_setup ""
    
    if {[catch {uplevel #0 [::list source $full_path]} err]} {
        set current_file ""
        set current_setup ""
        error "Source error in $filepath: $err"
    }
    
    set current_file ""
    set current_setup ""
    
    return [get_file_info $filepath]
}

#
# Query Functions
#

# Get all exports (legacy)
proc ::workspace::get_exports {} {
    variable exports
    
    set yh [yajl create #auto]
    $yh map_open
    
    dict for {procname info} $exports {
        $yh string $procname
        encode_export_info $yh $info
    }
    
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

# Get exports for a specific file (legacy)
proc ::workspace::get_file_exports {filepath} {
    variable exports
    
    set yh [yajl create #auto]
    $yh map_open
    
    dict for {procname info} $exports {
        if {[dict get $info file] eq $filepath} {
            $yh string $procname
            encode_export_info $yh $info
        }
    }
    
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

# Get complete file info (setups, adjusters, and legacy exports)
proc ::workspace::get_file_info {filepath} {
    variable exports
    variable setups
    variable adjusters
    variable active_setup
    
    set yh [yajl create #auto]
    $yh map_open
    
    # Setups
    $yh string "setups"
    $yh map_open
    dict for {name info} $setups {
        if {[dict get $info file] eq $filepath} {
            $yh string $name
            encode_setup_info $yh $name $info
        }
    }
    $yh map_close
    
    # Adjusters
    $yh string "adjusters"
    $yh map_open
    dict for {name info} $adjusters {
        if {[dict get $info file] eq $filepath} {
            $yh string $name
            encode_adjuster_info $yh $name $info
        }
    }
    $yh map_close
    
    # Legacy exports
    $yh string "exports"
    $yh map_open
    dict for {procname info} $exports {
        if {[dict get $info file] eq $filepath} {
            $yh string $procname
            encode_export_info $yh $info
        }
    }
    $yh map_close
    
    # Active setup
    $yh string "active_setup"
    if {$active_setup ne ""} {
        $yh string $active_setup
    } else {
        $yh null
    }
    
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

# Get current active setup
proc ::workspace::get_active_setup {} {
    variable active_setup
    return $active_setup
}

# Set active setup (for frontend to track)
proc ::workspace::set_active_setup {setup_name} {
    variable setups
    variable active_setup
    
    if {$setup_name ne "" && ![dict exists $setups $setup_name]} {
        error "Unknown setup: $setup_name"
    }
    
    set active_setup $setup_name
}

#
# Notifications
#
proc ::workspace::set_notify_callback {callback} {
    variable notify_callback
    set notify_callback $callback
}

proc ::workspace::notify {event data} {
    variable notify_callback
    
    if {$notify_callback ne ""} {
        catch {{*}$notify_callback $event $data}
    }
}

#
# Shader Uniform Auto-Discovery
#
# Generate adjusters automatically from shader object uniforms
# Usage: workspace::shader_adjusters $obj_name ?-prefix prefix? ?-exclude {list}?
#
proc ::workspace::shader_adjusters {obj_name args} {
    variable initialized
    variable adjusters
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set prefix "${obj_name}_"
    set exclude {time resolution projMat modelviewMat}  ;# Common auto-set uniforms
    set include_samplers 0
    
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -prefix {
                incr i
                set prefix [lindex $args $i]
            }
            -exclude {
                incr i
                set exclude [concat $exclude [lindex $args $i]]
            }
            -include {
                incr i
                # Only include these uniforms
                set include_only [lindex $args $i]
            }
            -samplers {
                set include_samplers 1
            }
            default {
                error "Unknown option to workspace::shader_adjusters: $opt"
            }
        }
        incr i
    }
    
    # Get uniform names from the shader object
    if {[catch {shaderObjUniformNames $obj_name} uniform_names]} {
        error "Failed to get uniforms for '$obj_name': $uniform_names"
    }
    
    # Get default values if available (need to get shader program name first)
    set defaults {}
    if {[catch {
        # Try to get defaults - this may not work for all objects
        set shader_prog [shaderObjProgram $obj_name]
        if {$shader_prog ne ""} {
            set defaults [shaderDefaultSettings $shader_prog]
        }
    }]} {
        # Ignore errors - defaults are optional
    }
    
    set generated {}
    
    foreach uname $uniform_names {
        # Skip excluded uniforms
        if {$uname in $exclude} continue
        
        # Skip if include_only is set and this isn't in it
        if {[info exists include_only] && $uname ni $include_only} continue
        
        # Skip samplers unless requested
        if {!$include_samplers && [string match "tex*" $uname]} continue
        if {!$include_samplers && [string match "*sampler*" $uname]} continue
        
        # Create adjuster name
        set adj_name "${prefix}${uname}"
        
        # Get default value if available
        set default_val 0.0
        if {[dict exists $defaults $uname]} {
            set default_val [dict get $defaults $uname]
        }
        
        # Create parameter spec - assume float for now
        # Could be enhanced to detect type from uniform info
        set param_spec [list value [list float -10.0 10.0 0.01 $default_val $uname]]
        
        # Parse and store
        set parsed_params [parse_verbose_params $param_spec]
        
        # Determine setup
        set setup_name ""
        if {[info exists current_setup] && $current_setup ne ""} {
            set setup_name $current_setup
        }
        
        # Store adjuster
        dict set adjusters $adj_name [dict create \
            proc "shaderObjSetUniform" \
            target $obj_name \
            uniform $uname \
            params $parsed_params \
            file $current_file \
            setup $setup_name \
            auto_generated 1]
        
        lappend generated $adj_name
    }
    
    return $generated
}

# Modified invoke_adjuster to handle shader uniforms specially
proc ::workspace::invoke_adjuster {adjuster_name args} {
    variable adjusters
    
    if {![dict exists $adjusters $adjuster_name]} {
        error "Unknown adjuster: $adjuster_name"
    }
    
    set info [dict get $adjusters $adjuster_name]
    set procname [dict get $info proc]
    set target [dict get $info target]
    
    # Special handling for auto-generated shader uniform adjusters
    if {[dict exists $info uniform]} {
        set uniform [dict get $info uniform]
        # Call: shaderObjSetUniform $target $uniform $value
        return [uplevel #0 [list $procname $target $uniform {*}$args]]
    }
    
    # Standard adjuster: prepend target to args
    if {$target ne ""} {
        set call_args [list $target {*}$args]
    } else {
        set call_args $args
    }
    
    return [uplevel #0 [list $procname {*}$call_args]]
}

package provide workspace $::workspace::version
