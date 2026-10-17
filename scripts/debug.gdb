set pagination off
set debuginfod enabled off
set print frame-arguments none
set print entry-values no
set breakpoint pending on
handle SIGPIPE nostop noprint pass

python
import gdb

class AllocationFailure(gdb.Breakpoint):
    """Catch bad_alloc even when libstdc++ lacks exception SDT probes."""

    def stop(self):
        # Both supported Linux targets use the Itanium C++ ABI: __cxa_throw's
        # second argument is type_info, whose second pointer is its type name.
        # Inspect only that metadata; never print exception objects or locals.
        try:
            arch = gdb.newest_frame().architecture().name()
            if "x86-64" in arch:
                register = "$rsi"
            elif "aarch64" in arch:
                register = "$x1"
            else:
                return True
            pointers = gdb.lookup_type("char").pointer().pointer()
            info = gdb.parse_and_eval(register).cast(pointers)
            name = info[1].string().lstrip("*")
            return name == "St9bad_alloc"
        except (gdb.error, UnicodeError):
            # If type metadata is unavailable, stop and retain a useful trace.
            return True

AllocationFailure("__cxa_throw", internal=True)
end
