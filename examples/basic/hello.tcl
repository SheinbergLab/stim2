# examples/basic/hello.tcl
workspace::reset

proc hello {name} {
    puts "hello, $name!"
}

workspace::export hello {
    name {string "world" "Name"}
}
