#
# check_polls_common.tcl --
#
#     Shared helpers for the TH8K-009 loop tools: check_polls.tcl (the
#     enforcement gate) and discover_loops.tcl (the inventory sweep).
#     strip_literals blanks C string/char literals and // comments so braces
#     inside them are not counted as scope braces; extract_body returns a
#     TH8-style function definition's brace-balanced body.
#

# ---------------------------------------------------------------------------
# strip_literals --
#     Blank out C string ("...") and character ('...') literals and //
#     line-comments in a line so that braces inside them (e.g. the `'{'`
#     char literal in th8ParseCommand) are not counted as scope braces.
# ---------------------------------------------------------------------------
proc strip_literals {line} {
    set out ""
    set n [string length $line]
    set i 0
    while {$i < $n} {
        set ch [string index $line $i]
        if {$ch eq "/" && [string index $line [expr {$i + 1}]] eq "/"} {
            break
        }
        if {$ch eq "\"" || $ch eq "'"} {
            set q $ch
            incr i
            while {$i < $n} {
                set c [string index $line $i]
                if {$c eq "\\"} {
                    incr i 2
                    continue
                }
                incr i
                if {$c eq $q} break
            }
            append out " "
        } else {
            append out $ch
            incr i
        }
    }
    return $out
}

# ---------------------------------------------------------------------------
# extract_body --
#     Given the full text of a C file and a function NAME, return the text
#     of that function's body (between the opening and matching closing
#     brace of its definition), or "" if not found.  A definition is
#     recognized TH8-style: a line that begins at column 0 with the function
#     name followed by an open paren; the body is the brace-balanced region
#     that follows the parameter list.  Braces inside string/char literals
#     are ignored (via strip_literals).
# ---------------------------------------------------------------------------
proc extract_body {text name} {
    set lines [split $text \n]
    set n [llength $lines]
    set defpat "${name}\("
    for {set i 0} {$i < $n} {incr i} {
        set line [lindex $lines $i]
        set c0 [string index $line 0]
        if {$c0 eq " " || $c0 eq "\t"} {
            continue
        }
        if {![string match "${defpat}*" $line]} {
            continue
        }
        set depth 0
        set started 0
        set body ""
        for {set j $i} {$j < $n} {incr j} {
            set l [lindex $lines $j]
            foreach ch [split [strip_literals $l] ""] {
                if {$ch eq "\{"} {
                    incr depth
                    set started 1
                } elseif {$ch eq "\}"} {
                    incr depth -1
                    if {$started && $depth == 0} {
                        return $body
                    }
                }
            }
            if {$started} {
                append body $l "\n"
            }
        }
        return ""
    }
    return ""
}
