# workspace-1.0.tm
# Workspace management for stim2 development frontend
#
# Usage:
#   package require workspace
#   set ::workspace::system_examples_path /path/to/examples
#   workspace::init
#

package require yajltcl

namespace eval ::workspace {
    variable version 1.0
    
    # State
    variable initialized 0
    variable workspaces {}
    
    # System examples path - set by stim2 startup before init
    variable system_examples_path ""
    
    # Export tracking
    variable exports {}
    variable current_file ""
    
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

proc ::workspace::scan_dir {base_path rel_path} {
    if {$rel_path eq ""} {
        set full_path $base_path
    } else {
        set full_path [file join $base_path $rel_path]
    }
    
    set items {}
    
    foreach entry [lsort [glob -nocomplain -directory $full_path *]] {
        set name [file tail $entry]
        
        if {[string index $name 0] eq "."} continue
        
        if {$rel_path eq ""} {
            set entry_rel $name
        } else {
            set entry_rel [file join $rel_path $name]
        }
        
        if {[file isdirectory $entry]} {
            set children [scan_dir $base_path $entry_rel]
            if {[llength $children] > 0} {
                lappend items [dict create \
                    type "directory" \
                    name $name \
                    path $entry_rel \
                    children $children]
            }
        } elseif {[string match "*.tcl" $name]} {
            lappend items [dict create \
                type "file" \
                name $name \
                path $entry_rel]
        }
    }
    
    return $items
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
# Export System
#
proc ::workspace::reset {} {
    variable initialized
    variable exports
    variable current_file
    
    if {!$initialized} return
    
    if {$current_file eq ""} {
        set exports {}
    } else {
        set new_exports {}
        dict for {procname info} $exports {
            if {[dict get $info file] ne $current_file} {
                dict set new_exports $procname $info
            }
        }
        set exports $new_exports
    }
}

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

proc ::workspace::activate {workspace_id filepath} {
    variable initialized
    variable current_file
    
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
    
    if {[catch {uplevel #0 [::list source $full_path]} err]} {
        set current_file ""
        error "Source error in $filepath: $err"
    }
    
    set current_file ""
    
    return [get_file_exports $filepath]
}

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

package provide workspace $::workspace::version
