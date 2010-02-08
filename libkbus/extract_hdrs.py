#!/usr/bin/env python

"""extract_hdrs.py -- Extract "extern" function headers from a .c file to a .h file

This is intended for use with libkbus, and has not been generalised for any other
purpose - maybe later on.
"""

import os
import re
import sys

pattern = r"""\
(?P<header>                     # start of header group
     \s* / \* .* \n             # start of header comment
    (\s*   \* .* \n)*           # 0 or more comment lines
     \s*   \* /  \n             # end of header comment
     \s* extern
        [\n\s\*\w]*             # crudely allow for type info
        (?P<name>
            [\n\s]+ \w+         # name of function
        )
     \(  ([^)]|\n)* \)          # crudely match arguments
)                               # end of header group
\s* \n
\s* {
"""

start_delimiter = "// -------- TEXT AFTER THIS AUTOGENERATED - DO NOT EDIT --------\n"
end_delimiter = "// -------- TEXT BEFORE THIS AUTOGENERATED - DO NOT EDIT --------\n"

def do_stuff(args):

    c_file = 'libkbus.c'
    h_file = 'kbus.h'
    temp_file = h_file + '.new'
    save_file = h_file + '.bak'

    with open(c_file,'r') as file:
        data = file.read()

        parts = re.finditer(pattern, data, re.VERBOSE)

        with open(h_file, 'r') as original:

            data = original.read()

            delimiter1_posn = data.find(start_delimiter)
            delimiter2_posn = data.find(end_delimiter)

            if delimiter1_posn == -1 or delimiter2_posn == -1:
                print "Couldn't find start or end 'AUTOGENERATED' line in file"
                return

            print '-----------------------------'
            print data[:delimiter1_posn]
            print '-----------------------------'
            print data[delimiter2_posn:]
            print '-----------------------------'

            with open(temp_file, 'w') as output:

                output.write(data[:delimiter1_posn])
                # The start delimiter is in the text we decided to throw
                # away, so we need to write it out again
                output.write(start_delimiter)

                for part in parts:
                    print 'Found ',part.group('name')
                    output.write(part.group('header'))
                    output.write(';\n')

                # The end delimiter is in the text we decided to keep,
                # so we don't need to write it out again
                output.write('\n')
                output.write(data[delimiter2_posn:])

        os.rename(h_file, save_file)
        os.rename(temp_file, h_file)


if __name__ == '__main__':
    do_stuff(sys.argv[1:])

# vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab: