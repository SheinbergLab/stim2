# workspace-2.0.tm
# Workspace management for stim2 development frontend
#
# ============================================================
# QUICK REFERENCE FOR DEMO SCRIPTS
# ============================================================
#
# BASIC STRUCTURE:
#   1. Define your setup proc(s) that create graphics objects
#   2. Call workspace::reset
#   3. Register setup with workspace::setup
#   4. Register adjusters with workspace::adjuster (or use -template)
#
# EXAMPLE:
#   proc my_setup {size color_name} {
#       glistInit 1
#       resetObjList
#       set p [polygon]
#       objName $p shape
#       scaleObj $p $size $size
#       ...
#       glistAddObject $p 0
#       glistSetVisible 1
#       redraw
#   }
#   
#   workspace::reset
#   workspace::setup my_setup {
#       size       {float 1.0 10.0 0.5 5.0 "Size"}
#       color_name {choice {red green blue} red "Color"}
#   } -adjusters {shape_scale shape_color} -label "My Demo"
#   
#   workspace::adjuster shape_scale -template scale -target shape
#   workspace::adjuster shape_color -template color -target shape
#
# ============================================================
# SETUP PARAMETERS - Types and Format
# ============================================================
#   {param_name {type args... default label ?unit?}}
#
#   Types:
#     float  {float min max step default label ?unit?}
#     int    {int min max step default label}
#     bool   {bool default label}
#     choice {choice {opt1 opt2 ...} default label}
#     string {string default label}
#
# ============================================================
# AVAILABLE TEMPLATES (-template option)
# ============================================================
#   scale        - Uniform scale slider (works with scaleObj)
#   size2d       - Width/height sliders (works with scaleObj)
#   rotation     - Rotation angle in degrees (works with rotateObj)
#   position     - X/Y position sliders (works with translateObj)
#   color        - RGB color picker (works with polycolor, etc.)
#   color_alpha  - RGBA color picker with alpha slider
#   pointsize    - Point size slider (for point-based objects)
#
#   Usage: workspace::adjuster name -template TEMPLATE -target OBJ_NAME
#   With defaults: workspace::adjuster name -template scale -target obj -defaults {scale 2.0}
#
# ============================================================
# CUSTOM ADJUSTERS
# ============================================================
#   workspace::adjuster name {params...} -target obj -proc setter -getter getter
#
#   Setter proc signature: proc setter {target_name param1 param2 ...}
#     - First arg is target (from -target), unless -target {} (empty)
#     - Remaining args are param values IN ORDER of definition
#     - Must call redraw at end
#
#   Getter proc signature: proc getter {target_name} -> dict
#     - Returns dict with keys EXACTLY matching param names
#     - Example: dict create r 1.0 g 0.5 b 0.0
#
# ============================================================
# ACTION BUTTONS (stateless triggers)
# ============================================================
#   workspace::adjuster name {
#       action1 {action "Button Label"}
#       action2 {action "Another Button"}
#   } -target obj -proc trigger_proc -label "Actions"
#
#   Proc signature: proc trigger_proc {target action_name}
#     - First arg is target (from -target), unless -target {} (empty)
#     - Second arg is the action name (e.g., "action1", "action2")
#     - No return value needed
#     - No getter needed (stateless)
#
#   Example:
#     workspace::adjuster player_actions {
#         jump  {action "Jump"}
#         shoot {action "Shoot"}
#         duck  {action "Duck"}
#     } -target player -proc trigger_action -label "Actions"
#
#     proc trigger_action {target action} {
#         switch $action {
#             jump  { sp::setAnimationByName $target jump 0 0 }
#             shoot { sp::setAnimationByName $target shoot 0 0 }
#             duck  { sp::setAnimationByName $target duck 0 0 }
#         }
#         return
#     }
#
#   Use action buttons for:
#     - One-shot animations (jump, shoot, death)
#     - Discrete events (reset, randomize, save)
#     - State transitions (start, stop, toggle direction)
#
# ============================================================
# GETTER PROC SIGNATURE
# ============================================================
#   Getter procs take an optional target parameter:
#
#     proc my_getter {{target {}}} {
#         dict create param1 $value1 param2 $value2
#     }
#
#   The target arg may be empty string if -target {} was used.
#   Return a dict with keys matching your parameter names exactly.
#
#   For best results, query actual object state rather than
#   cached namespace variables:
#
#     # Preferred - reads actual state:
#     proc get_scale {{target {}}} {
#         set s [scaleObj $target]
#         dict create scale [lindex $s 0]
#     }
#
#     # Acceptable for complex state not easily queried:
#     proc get_animation {{target {}}} {
#         dict create anim $myns::current_anim
#     }
#
# ============================================================
# STORING STATE WITH setObjProp
# ============================================================
#   Use setObjProp to attach custom state to objects rather than
#   namespace variables. State stays with the object and survives
#   re-sourcing the demo file (unless resetObjList is called).
#
#   Syntax: setObjProp objname key ?value?
#     - With value: sets the property
#     - Without value: gets the property
#
#   Example - tracking direction:
#     # In setup:
#     setObjProp $obj facing_right 1
#
#     # In setter:
#     proc set_direction {target direction} {
#         if {$direction eq "left"} {
#             scaleObj $target -1.0 1.0
#             setObjProp $target facing_right 0
#         } else {
#             scaleObj $target 1.0 1.0
#             setObjProp $target facing_right 1
#         }
#         return
#     }
#
#     # In getter:
#     proc get_direction {{target {}}} {
#         dict create facing_right [setObjProp $target facing_right]
#     }
#
#   Benefits over namespace variables:
#     - State tied to object lifetime
#     - Works with multiple instances
#     - Survives script re-sourcing
#     - Cleaner separation of concerns
#
# ============================================================
# SETTER PROC GUIDELINES  
# ============================================================
#   - Always end with: return
#     (prevents command output from being logged)
#   
#   - Call redraw only if needed for immediate visual update
#     (animated objects update automatically)
#
#   - Example:
#     proc set_opacity {target value} {
#         svgOpacity $target $value
#         redraw
#         return
#     }
# ============================================================
# COLOR PICKER UI (-colorpicker flag)
# ============================================================
#   Add -colorpicker flag to get color picker UI for r/g/b/a params:
#
#   workspace::adjuster my_color {
#       r {float 0 1 0.01 1.0 "Red"}
#       g {float 0 1 0.01 1.0 "Green"}
#       b {float 0 1 0.01 1.0 "Blue"}
#   } -target obj -proc set_color -colorpicker
#
#   Without -colorpicker: renders as individual sliders
#   With -colorpicker: renders as color picker widget
#   
#   Templates (-template color, -template color_alpha) automatically
#   include -colorpicker behavior.
#
#   You can mix -colorpicker with extra params (rendered before picker):
#   workspace::adjuster tint {
#       mode {choice {0 1 2} 0 "Mode"}
#       r {float 0 1 0.01 1.0 "R"}
#       g {float 0 1 0.01 1.0 "G"}
#       b {float 0 1 0.01 1.0 "B"}
#   } -target obj -proc set_tint -colorpicker
#
# ============================================================
# VARIANTS (multiple entry points, same proc)
# ============================================================
#   workspace::variant name {params...} ?-proc proc_name? ?-adjusters {...}? ?-label "..."?
#
#   If -proc is omitted, defaults to the proc from the most recent workspace::setup
#   Useful for preset configurations:
#
#   workspace::setup my_setup {shape {choice {circle square} circle "Shape"}} ...
#   workspace::variant circle_preset {shape circle} -label "Circle"
#   workspace::variant square_preset {shape square} -label "Square"
#
# ============================================================
# METAGROUP PATTERN (recommended)
# ============================================================
#   Separate shape definition from display transforms:
#
#   set p [polygon]
#   objName $p my_shape        ;# shape params (color, vertices)
#   
#   set mg [metagroup]
#   metagroupAdd $mg $p
#   objName $mg my_group       ;# transform params (scale, rotation, position)
#   
#   Then use adjusters on each:
#   workspace::adjuster shape_color -template color -target my_shape
#   workspace::adjuster group_scale -template scale -target my_group
#
# ============================================================
# HISTORY
# ============================================================
# Key changes from 1.0:
#   - Retired workspace::export (use setup/adjuster exclusively)
#   - Added workspace::template for reusable adjuster patterns
#   - Added workspace::variant for multiple setups from same proc
#   - Enhanced workspace::shader_adjusters with specs dict
#   - Built-in helper procs for common operations
#   - Added -colorpicker flag for explicit color picker UI
#
# Key changes in 2.0:
#   - Auto-generated adjusters (from shader_adjusters) are cleared
#     when invoke_setup runs, preventing accumulation across variants
#
# Usage:
#   package require workspace
#   set ::workspace::system_examples_path /path/to/examples
#   workspace::init
#

package require yajltcl

namespace eval ::workspace {
    variable version 2.0
    
    # State
    variable initialized 0
    variable workspaces {}
    
    # System examples path - set by stim2 startup before init
    variable system_examples_path ""
    
    # Current file tracking
    variable current_file ""
    
    # Setup/Adjuster system
    variable setups {}          ;# dict: setup_name -> {proc params adjusters file label}
    variable adjusters {}       ;# dict: adjuster_name -> {proc target params file setup uniform colorpicker}
    variable templates {}       ;# dict: template_name -> {params proc getter ?colorpicker?}
    variable active_setup ""    ;# currently active setup name
    
    # Notification callback
    variable notify_callback ""
}

# ============================================================
# BUILT-IN HELPER PROCS
# ============================================================
# These are standard operations demos can reference without 
# defining their own wrapper procs

namespace eval ::workspace::helpers {
    # Transform helpers
    proc scale2d {name w h} {
        scaleObj $name $w $h
        redraw
    }
    
    proc scale_uniform {name s} {
        scaleObj $name $s $s
        redraw
    }
    
    proc rotate_z {name angle} {
        rotateObj $name $angle 0 0 1
        redraw
    }
    
    proc translate {name x y {z 0}} {
        translateObj $name $x $y $z
        redraw
    }
    
    # Color helpers
    proc color_rgb {name r g b} {
        polycolor $name $r $g $b
        redraw
    }
    
    proc color_rgba {name r g b a} {
        polycolor $name $r $g $b $a
        redraw
    }
    
    # ---- GETTER HELPERS ----
    # These return current values as dicts matching param names
    
    proc get_scale2d {name} {
        set vals [scaleObj $name]
        dict create width [lindex $vals 0] height [lindex $vals 1]
    }
    
    proc get_scale {name} {
        set vals [scaleObj $name]
        dict create scale [lindex $vals 0]
    }
    
    proc get_rotation {name} {
        set vals [rotateObj $name]
        # rotateObj returns angle x y z - we just want angle
        dict create angle [lindex $vals 0]
    }
    
    proc get_position {name} {
        set vals [translateObj $name]
        dict create x [lindex $vals 0] y [lindex $vals 1]
    }
    
    proc get_color {name} {
        set vals [polycolor $name]
        dict create r [lindex $vals 0] g [lindex $vals 1] b [lindex $vals 2]
    }
    
    proc get_color_alpha {name} {
        set vals [polycolor $name]
        dict create r [lindex $vals 0] g [lindex $vals 1] b [lindex $vals 2] a [lindex $vals 3]
    }
    
    proc get_pointsize {name} {
        dict create size [polypointsize $name]
    }
    
    proc get_linewidth {name} {
        dict create width [polylinewidth $name]
    }
    
    # Shader uniform helpers - single value
    proc uniform_float {name uniform val} {
        shaderObjSetUniform $name $uniform $val
        redraw
    }
    
    proc uniform_int {name uniform val} {
        shaderObjSetUniform $name $uniform [expr {int($val)}]
        redraw
    }
    
    proc uniform_bool {name uniform val} {
        shaderObjSetUniform $name $uniform [expr {$val ? 1 : 0}]
        redraw
    }
    
    # Shader uniform helpers - multiple values (e.g., color)
    proc uniform_rgb {name uniform_r uniform_g uniform_b r g b} {
        shaderObjSetUniform $name $uniform_r $r
        shaderObjSetUniform $name $uniform_g $g
        shaderObjSetUniform $name $uniform_b $b
        redraw
    }
    
    # Point/line helpers
    proc set_pointsize {name size} {
        polypointsize $name [expr {double($size)}]
        redraw
    }
    
    proc set_linewidth {name width} {
        polylinewidth $name [expr {double($width)}]
        redraw
    }
}

# ============================================================
# BUILT-IN TEMPLATES
# ============================================================
# Registered after init - can be used with workspace::adjuster -template

proc ::workspace::register_builtin_templates {} {
    variable templates
    
    # Transform templates
    dict set templates size2d [dict create \
        params {
            width  {float 0.2 10.0 0.1 4.0 "Width" deg}
            height {float 0.2 10.0 0.1 4.0 "Height" deg}
        } \
        proc ::workspace::helpers::scale2d \
        getter ::workspace::helpers::get_scale2d]
    
    dict set templates scale [dict create \
        params {
            scale {float 0.1 10.0 0.1 1.0 "Scale"}
        } \
        proc ::workspace::helpers::scale_uniform \
        getter ::workspace::helpers::get_scale]
    
    dict set templates rotation [dict create \
        params {
            angle {float 0 360 1 0 "Angle" deg}
        } \
        proc ::workspace::helpers::rotate_z \
        getter ::workspace::helpers::get_rotation]
    
    dict set templates position [dict create \
        params {
            x {float -10 10 0.1 0 "X" deg}
            y {float -10 10 0.1 0 "Y" deg}
        } \
        proc ::workspace::helpers::translate \
        getter ::workspace::helpers::get_position]
    
    # Color templates
    dict set templates color [dict create \
        params {
            r {float 0 1 0.05 1.0 "Red"}
            g {float 0 1 0.05 1.0 "Green"}
            b {float 0 1 0.05 1.0 "Blue"}
        } \
        proc ::workspace::helpers::color_rgb \
        getter ::workspace::helpers::get_color \
        colorpicker 1]
    
    dict set templates color_alpha [dict create \
        params {
            r {float 0 1 0.05 1.0 "Red"}
            g {float 0 1 0.05 1.0 "Green"}
            b {float 0 1 0.05 1.0 "Blue"}
            a {float 0 1 0.05 1.0 "Alpha"}
        } \
        proc ::workspace::helpers::color_rgba \
        getter ::workspace::helpers::get_color_alpha \
        colorpicker 1]
    
    # Point/line templates
    dict set templates pointsize [dict create \
        params {
            size {float 1.0 20.0 0.5 5.0 "Point Size" px}
        } \
        proc ::workspace::helpers::set_pointsize \
        getter ::workspace::helpers::get_pointsize]
    
    dict set templates linewidth [dict create \
        params {
            width {float 0.5 10.0 0.5 1.0 "Line Width" px}
        } \
        proc ::workspace::helpers::set_linewidth \
        getter ::workspace::helpers::get_linewidth]
}

# ============================================================
# JSON ENCODING HELPERS
# ============================================================

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
                } elseif {$ptype eq "action"} {
		   # Actions have no default value
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
		    if {[llength $choice] == 2} {
			# Paired: {value label}
			$yh map_open
			$yh string "value"
			$yh string [lindex $choice 0]
			$yh string "label"
			$yh string [lindex $choice 1]
			$yh map_close
		    } else {
			# Simple string
			$yh string $choice
		    }
		}
		$yh array_close
	    }
        }
    }
    
    $yh map_close
}

proc ::workspace::encode_setup_info {yh name info} {
    $yh map_open
    
    $yh string "name"
    $yh string $name
    
    $yh string "proc"
    $yh string [dict get $info proc]
    
    if {[dict exists $info label]} {
        $yh string "label"
        $yh string [dict get $info label]
    }
    
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
    
    if {[dict exists $info uniform]} {
        $yh string "uniform"
        $yh string [dict get $info uniform]
    }
    
    if {[dict exists $info label]} {
        $yh string "label"
        $yh string [dict get $info label]
    }
    
    if {[dict exists $info colorpicker] && [dict get $info colorpicker]} {
        $yh string "colorpicker"
        $yh bool 1
    }
    
    $yh string "setup"
    $yh string [dict get $info setup]
    
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

# ============================================================
# INITIALIZATION
# ============================================================

proc ::workspace::init {} {
    variable initialized
    variable workspaces
    variable system_examples_path
    
    if {$initialized} return
    
    set ws_list {}
    
    # Built-in examples
    if {$system_examples_path ne "" && [file isdirectory $system_examples_path]} {
        lappend ws_list [dict create \
            id "stim2-examples" \
            name "stim2 Examples" \
            path $system_examples_path \
            icon "package" \
            builtin 1]
    }
    
    # Additional workspaces from environment
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
    
    # Register built-in templates
    register_builtin_templates
    
    set initialized 1
}

# ============================================================
# WORKSPACE MANAGEMENT (abbreviated - same as 1.0)
# ============================================================

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

proc ::workspace::get_workspace {workspace_id} {
    variable workspaces
    
    foreach ws [dict get $workspaces workspaces] {
        if {[dict get $ws id] eq $workspace_id} {
            return $ws
        }
    }
    error "Workspace not found: $workspace_id"
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

# ============================================================
# FILE OPERATIONS
# ============================================================

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
                
                # Add title from INDEX if available
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
        
        dict for {idx_name title} $index_info {
            foreach item $dir_items {
                if {[dict get $item name] eq $idx_name} {
                    lappend ordered_dirs $item
                    lappend indexed_dir_names $idx_name
                    break
                }
            }
        }
        
        foreach item [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $dir_items] {
            if {[dict get $item name] ni $indexed_dir_names} {
                lappend ordered_dirs $item
            }
        }
        
        set dir_items $ordered_dirs
    } else {
        set dir_items [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $dir_items]
    }
    
    # Order files according to INDEX if present
    if {$has_index} {
        set ordered_files {}
        set indexed_names {}
        
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
        
        foreach item [lsort -command {apply {{a b} {
            string compare [dict get $a name] [dict get $b name]
        }}} $file_items] {
            if {[dict get $item name] ni $indexed_names} {
                lappend ordered_files $item
            }
        }
        
        set file_items $ordered_files
    } else {
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

# ============================================================
# RESET
# ============================================================

proc ::workspace::reset {} {
    variable initialized
    variable setups
    variable adjusters
    variable active_setup
    variable current_file
    
    if {!$initialized} return
    
    if {$current_file eq ""} {
        set setups {}
        set adjusters {}
        set active_setup ""
    } else {
        # Clear only items from current file
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
        
        if {$active_setup ne "" && [dict exists $setups $active_setup]} {
            if {[dict get [dict get $setups $active_setup] file] eq $current_file} {
                set active_setup ""
            }
        }
    }
}

# ============================================================
# SETUP - Primary demo entry point
# ============================================================
# workspace::setup proc_name {params...} ?-adjusters {list}? ?-label "Display Name"?

proc ::workspace::setup {procname params args} {
    variable initialized
    variable setups
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set adjuster_list {}
    set label ""
    
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -adjusters {
                incr i
                set adjuster_list [lindex $args $i]
            }
            -label {
                incr i
                set label [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::setup: $opt"
            }
        }
        incr i
    }
    
    # Parse parameters
    set parsed_params [parse_params $params]
    
    # Build setup info
    set info [dict create \
        proc $procname \
        params $parsed_params \
        adjusters $adjuster_list \
        file $current_file]
    
    if {$label ne ""} {
        dict set info label $label
    }
    
    dict set setups $procname $info
    
    # Track current setup for adjuster registration
    set current_setup $procname
    
    notify "setup" [dict create proc $procname file $current_file]
}

# ============================================================
# VARIANT - Multiple entry points sharing implementation
# ============================================================
# workspace::variant name {params...} ?-proc impl_proc? ?-adjusters {list}? ?-label "Name"?
# Creates a setup that calls impl_proc but with different default params
# If -proc is omitted, defaults to the proc from the most recent workspace::setup

proc ::workspace::variant {name params args} {
    variable initialized
    variable setups
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set impl_proc ""
    set adjuster_list {}
    set label ""
    
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -proc {
                incr i
                set impl_proc [lindex $args $i]
            }
            -adjusters {
                incr i
                set adjuster_list [lindex $args $i]
            }
            -label {
                incr i
                set label [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::variant: $opt"
            }
        }
        incr i
    }
    
    # Default to the main setup's proc if not specified
    if {$impl_proc eq ""} {
        if {$current_setup ne "" && [dict exists $setups $current_setup]} {
            set impl_proc [dict get $setups $current_setup proc]
        } else {
            error "workspace::variant requires -proc option (no prior workspace::setup found)"
        }
    }
    
    set parsed_params [parse_params $params]
    
    set info [dict create \
        proc $impl_proc \
        params $parsed_params \
        adjusters $adjuster_list \
        file $current_file \
        variant 1]
    
    if {$label ne ""} {
        dict set info label $label
    }
    
    dict set setups $name $info
    set current_setup $name
    
    notify "setup" [dict create name $name proc $impl_proc file $current_file variant 1]
}

# ============================================================
# ADJUSTER - Runtime parameter modification
# ============================================================
# workspace::adjuster name {params...} -target obj -proc proc_name
# workspace::adjuster name -template tpl_name -target obj ?-defaults {key val...}?
# Use -colorpicker to explicitly request color picker UI for r/g/b/a params

proc ::workspace::adjuster {name args} {
    variable initialized
    variable adjusters
    variable templates
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Check for template form vs explicit form
    set use_template ""
    set params {}
    set target ""
    set proc_name ""
    set getter ""
    set label ""
    set defaults {}
    set colorpicker 0
    
    # First arg might be params dict or -template
    set i 0
    if {[llength $args] > 0 && [string index [lindex $args 0] 0] ne "-"} {
        # First arg is params
        set params [lindex $args 0]
        incr i
    }
    
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -template {
                incr i
                set use_template [lindex $args $i]
            }
            -target {
                incr i
                set target [lindex $args $i]
            }
            -proc {
                incr i
                set proc_name [lindex $args $i]
            }
            -getter {
                incr i
                set getter [lindex $args $i]
            }
            -label {
                incr i
                set label [lindex $args $i]
            }
            -defaults {
                incr i
                set defaults [lindex $args $i]
            }
            -colorpicker {
                set colorpicker 1
            }
            default {
                error "Unknown option to workspace::adjuster: $opt"
            }
        }
        incr i
    }
    
    # Resolve template if specified
    if {$use_template ne ""} {
        if {![dict exists $templates $use_template]} {
            error "Unknown template: $use_template"
        }
        set tpl [dict get $templates $use_template]
        set params [dict get $tpl params]
        set proc_name [dict get $tpl proc]
        
        # Get getter from template if not explicitly provided
        if {$getter eq "" && [dict exists $tpl getter]} {
            set getter [dict get $tpl getter]
        }
        
        # Get colorpicker from template if not explicitly set
        if {!$colorpicker && [dict exists $tpl colorpicker]} {
            set colorpicker [dict get $tpl colorpicker]
        }
        
        # Apply defaults overrides
        if {[llength $defaults] > 0} {
            set new_params {}
            foreach {pname pspec} $params {
                if {[dict exists $defaults $pname]} {
                    # Override default value (index 4 in spec)
                    set pspec [lreplace $pspec 4 4 [dict get $defaults $pname]]
                }
                lappend new_params $pname $pspec
            }
            set params $new_params
        }
    }
    
    if {$proc_name eq ""} {
        error "workspace::adjuster requires -proc option or -template"
    }
    
    set parsed_params [parse_params $params]
    
    set setup_name ""
    if {[info exists current_setup] && $current_setup ne ""} {
        set setup_name $current_setup
    }
    
    set info [dict create \
        proc $proc_name \
        target $target \
        params $parsed_params \
        file $current_file \
        setup $setup_name \
        colorpicker $colorpicker]
    
    if {$getter ne ""} {
        dict set info getter $getter
    }
    
    if {$label ne ""} {
        dict set info label $label
    }
    
    dict set adjusters $name $info
    
    notify "adjuster" [dict create name $name proc $proc_name file $current_file]
}

# ============================================================
# TEMPLATE - Define reusable adjuster patterns
# ============================================================
# workspace::template name {params...} -proc proc_name

proc ::workspace::template {name params args} {
    variable templates
    
    set proc_name ""
    
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -proc {
                incr i
                set proc_name [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::template: $opt"
            }
        }
        incr i
    }
    
    if {$proc_name eq ""} {
        error "workspace::template requires -proc option"
    }
    
    dict set templates $name [dict create params $params proc $proc_name]
}

# ============================================================
# SHADER UNIFORM AUTO-DISCOVERY
# ============================================================
# workspace::shader_adjusters obj_name ?-specs {uniform_specs}? ?-prefix pfx? ?-exclude {list}?
#
# Specs dict format:
#   uniform_name {type min max step default ?label? ?unit?}
# or for bool:
#   uniform_name {bool default ?label?}

proc ::workspace::shader_adjusters {obj_name args} {
    variable initialized
    variable adjusters
    variable current_file
    variable current_setup
    
    if {!$initialized} return
    
    # Parse options
    set prefix "${obj_name}_"
    set specs {}
    set exclude {time resolution projMat modelviewMat}
    set include_only {}
    
    set i 0
    while {$i < [llength $args]} {
        set opt [lindex $args $i]
        switch -- $opt {
            -prefix {
                incr i
                set prefix [lindex $args $i]
            }
            -specs {
                incr i
                set specs [lindex $args $i]
            }
            -exclude {
                incr i
                set exclude [concat $exclude [lindex $args $i]]
            }
            -include {
                incr i
                set include_only [lindex $args $i]
            }
            default {
                error "Unknown option to workspace::shader_adjusters: $opt"
            }
        }
        incr i
    }
    
    # Get uniform names from shader object
    if {[catch {shaderObjUniformNames $obj_name} uniform_names]} {
        error "Failed to get uniforms for '$obj_name': $uniform_names"
    }
    
    # Get default values from shader
    set shader_defaults {}
    if {[catch {
        set shader_prog [shaderObjProgram $obj_name]
        if {$shader_prog ne ""} {
            set shader_defaults [shaderDefaultSettings $shader_prog]
        }
    }]} {
        # Ignore - defaults optional
    }
    
    set generated {}
    
    foreach uname $uniform_names {
        # Skip excluded
        if {$uname in $exclude} continue
        
        # Skip if include_only set and not in it
        if {[llength $include_only] > 0 && $uname ni $include_only} continue
        
        # Skip samplers
        if {[string match "tex*" $uname] || [string match "*sampler*" $uname]} continue
        
        set adj_name "${prefix}${uname}"
        
        # Get spec from provided specs, or generate default
        if {[dict exists $specs $uname]} {
            set spec [dict get $specs $uname]
        } else {
            # Default spec - float with reasonable range
            set default_val 0.0
            if {[dict exists $shader_defaults $uname]} {
                set default_val [dict get $shader_defaults $uname]
            }
            set spec [list float -10.0 10.0 0.01 $default_val $uname]
        }
        
        # Determine proc and parse params based on type
        set spec_type [lindex $spec 0]
        
        switch $spec_type {
            float {
                set proc_name ::workspace::helpers::uniform_float
                set parsed_params [list [dict create \
                    name value \
                    type float \
                    min [lindex $spec 1] \
                    max [lindex $spec 2] \
                    step [lindex $spec 3] \
                    default [lindex $spec 4]]]
                if {[llength $spec] > 5} {
                    lset parsed_params 0 [dict merge [lindex $parsed_params 0] [dict create label [lindex $spec 5]]]
                }
                if {[llength $spec] > 6} {
                    lset parsed_params 0 [dict merge [lindex $parsed_params 0] [dict create unit [lindex $spec 6]]]
                }
            }
            int {
                set proc_name ::workspace::helpers::uniform_int
                set parsed_params [list [dict create \
                    name value \
                    type int \
                    min [lindex $spec 1] \
                    max [lindex $spec 2] \
                    step [lindex $spec 3] \
                    default [lindex $spec 4]]]
                if {[llength $spec] > 5} {
                    lset parsed_params 0 [dict merge [lindex $parsed_params 0] [dict create label [lindex $spec 5]]]
                }
            }
            bool {
                set proc_name ::workspace::helpers::uniform_bool
                set parsed_params [list [dict create \
                    name value \
                    type bool \
                    default [lindex $spec 1]]]
                if {[llength $spec] > 2} {
                    lset parsed_params 0 [dict merge [lindex $parsed_params 0] [dict create label [lindex $spec 2]]]
                }
            }
            default {
                # Skip unknown types
                continue
            }
        }
        
        set setup_name ""
        if {[info exists current_setup] && $current_setup ne ""} {
            set setup_name $current_setup
        }
        
        dict set adjusters $adj_name [dict create \
            proc $proc_name \
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

# ============================================================
# INVOKE FUNCTIONS
# ============================================================

# Clear auto-generated adjusters (e.g., shader uniforms from previous setup)
# Called automatically before each setup runs
proc ::workspace::clear_auto_adjusters {} {
    variable adjusters
    
    set new_adjusters {}
    dict for {name info} $adjusters {
        if {![dict exists $info auto_generated] || ![dict get $info auto_generated]} {
            dict set new_adjusters $name $info
        }
    }
    set adjusters $new_adjusters
}

proc ::workspace::invoke_setup {setup_name args} {
    variable setups
    variable active_setup
    
    if {![dict exists $setups $setup_name]} {
        error "Unknown setup: $setup_name"
    }
    
    # Clear auto-generated adjusters from previous setup
    clear_auto_adjusters
    
    set info [dict get $setups $setup_name]
    set procname [dict get $info proc]
    
    set result [uplevel #0 [list $procname {*}$args]]
    set active_setup $setup_name
    
    return $result
}

proc ::workspace::invoke_adjuster {adjuster_name args} {
    variable adjusters
    
    if {![dict exists $adjusters $adjuster_name]} {
        error "Unknown adjuster: $adjuster_name"
    }
    
    set info [dict get $adjusters $adjuster_name]
    set procname [dict get $info proc]
    set target [dict get $info target]
    
    # Special handling for shader uniforms
    if {[dict exists $info uniform]} {
        set uniform [dict get $info uniform]
        return [uplevel #0 [list $procname $target $uniform {*}$args]]
    }
    
    # Standard: prepend target
    if {$target ne ""} {
        set call_args [list $target {*}$args]
    } else {
        set call_args $args
    }
    
    return [uplevel #0 [list $procname {*}$call_args]]
}

# Invoke an action (stateless button press)
proc ::workspace::invoke_action {adjuster_name action_name} {
    variable adjusters
    
    if {![dict exists $adjusters $adjuster_name]} {
        error "Unknown adjuster: $adjuster_name"
    }
    
    set info [dict get $adjusters $adjuster_name]
    set procname [dict get $info proc]
    set target [dict get $info target]
    
    # For actions, call proc with target (if any) and action name
    if {$target ne ""} {
        return [uplevel #0 [list $procname $target $action_name]]
    } else {
        return [uplevel #0 [list $procname $action_name]]
    }
}

# ============================================================
# QUERY FUNCTIONS
# ============================================================

proc ::workspace::get_file_info {filepath} {
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

proc ::workspace::get_active_setup {} {
    variable active_setup
    return $active_setup
}

proc ::workspace::set_active_setup {setup_name} {
    variable setups
    variable active_setup
    
    if {$setup_name ne "" && ![dict exists $setups $setup_name]} {
        error "Unknown setup: $setup_name"
    }
    
    set active_setup $setup_name
}

proc ::workspace::get_templates {} {
    variable templates
    return $templates
}

# Get current values for a list of adjusters by calling their getters
# Returns JSON: {"adjuster_name": {param: value, ...}, ...}
proc ::workspace::get_adjuster_values {adjuster_names} {
    variable adjusters
    
    set yh [yajl create #auto]
    $yh map_open
    
    foreach adj_name $adjuster_names {
        if {![dict exists $adjusters $adj_name]} continue
        
        set info [dict get $adjusters $adj_name]
        
        # Skip if no getter defined
        if {![dict exists $info getter]} continue
        
        set getter [dict get $info getter]
        set target [dict get $info target]
        
        # Call the getter with target
        if {[catch {$getter $target} values]} {
            # Getter failed - skip this adjuster
            continue
        }
        
        # values should be a dict like {r 1.0 g 0.5 b 0.0}
        $yh string $adj_name
        $yh map_open
        dict for {param_name param_val} $values {
            $yh string $param_name
            if {[string is double -strict $param_val]} {
                $yh double $param_val
            } elseif {[string is integer -strict $param_val]} {
                $yh integer $param_val
            } else {
                $yh string $param_val
            }
        }
        $yh map_close
    }
    
    $yh map_close
    
    set json [$yh get]
    $yh delete
    
    return $json
}

# Convenience: get values for all adjusters of the active setup
proc ::workspace::get_active_adjuster_values {} {
    variable setups
    variable active_setup
    
    if {$active_setup eq "" || ![dict exists $setups $active_setup]} {
        return "{}"
    }
    
    set setup_info [dict get $setups $active_setup]
    set adjuster_names [dict get $setup_info adjusters]
    
    return [get_adjuster_values $adjuster_names]
}

# ============================================================
# PARAMETER PARSING
# ============================================================

proc ::workspace::parse_params {spec_list} {
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
            action {
                # Action buttons just have a label
                if {[llength $spec] > 1} { dict set p label [lindex $spec 1] }
            }	    
        }
        
        lappend params $p
    }
    
    return $params
}

# ============================================================
# NOTIFICATIONS
# ============================================================

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

# ============================================================
# FILE ACTIVATION (simplified - add full scan/load from 1.0 as needed)
# ============================================================

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

package provide workspace $::workspace::version
