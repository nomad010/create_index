create_index - A tool for creating indices of files.

Syntax:
    ./create_index [OPTIONS] <input_filename> <output_filename>

Creates an file of fixed size indices of the positions of target characters. It
may useful for repeatedly splitting up a file by arbitrary boundaries. To split 
a file once in a specific boundaries, see the `split' command.

Options:
    --help               Prints this message.
    --include-zero       This enforces the indexer to write out a 0 at the 
                         beginning of the index. By default, this is disabled.
    --size=<size_type>   This sets the index type to use for the output file.
                         The available values are:
                          * 8
                                  Use a 8-bit unsigned integer.
                          * 16
                                  Use a 16-bit unsigned integer.
                          * 32
                                  Use a 32-bit unsigned integer. This is the
                                  default.
                          * 64
                                  Use a 64-bit unsigned integer.
                         All other options are invalid. Overflow is handled by
                         wrapping around to zero.
    --target=<chr>       The character to index on. By default this is a 
                         newline character. Simple escape codes are permitted.
Arguments:
    <input_filename>     The name of the input filename. Input can be read from
                         stdin by specifying -.
    <output_filename>    The name of the output filename. output can be written
                         to stdout by specifying -, but be warned, it is likely
                         to contain arbitrary binary.
                         
Example usage:
  # Search for all newline characters in stdin and write them out on stdout as
    32-bit indices.
  ./create_index - -

  # Search for all tab characters in input.txt and write them out to output.txt
    as 64-bit indices.
  ./create_index --target=\t --size=64 input.txt output.txt

Index file format:
    The index file consists of a fixed size header, followed by a stream of 
    index values. The header consists of a 4-byte magic number(0xba5eba11) for
    endian checks, a 1-byte version number(currently 1), a 1-byte char for what
    target was used and 1-byte padding. What follows is a stream of index 
    values listing all the positions of a target in the input file.