#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include "indexer.hpp"

void print_help()
{
    static auto help = R"help(create_index - A tool for creating indices of files.

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
)help";
    printf("%s", help);
}

bool starts_with(std::string in_str, std::string start_string)
{
    return in_str.substr(0, start_string.size()) == start_string;
}

std::map<std::string, std::string> parse_arguments(int argc, char** argv)
{
    std::map<std::string, std::string> read_arguments;
    
    for(int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        
        if(starts_with(arg, "--help"))
        {
            read_arguments["help"] = "true";
        }
        else if(starts_with(arg, "--include-zero"))
        {
            read_arguments["include-zero"] = "true";
        }
        else if(starts_with(arg, "--size="))
        {
            auto size = arg.substr(7);
            if(size == "8" || size == "16" || size == "32" || size == "64")
            {
                read_arguments["size"] = size;
            }
            else
            {
                fprintf(stderr, "Unrecognised index type size.");
                exit(1);
            }
            
        }
        else if(starts_with(arg, "--target="))
        {
            /// Decode escape codes
            auto target = arg.substr(9);
            if(target.size() == 1)
            {
                read_arguments["target"] = target;
            }
            else if(target.size() == 2 && target[0] == '\\')
            {
                std::string chr = "";
                switch(target[1])
                {
                    case '\'':
                        chr = "\x27"; 
                        break;
                    case '"':
                        chr = "\x22";
                        break;
                    case '?':
                        chr = "\x3f";
                        break;
                    case '\\':
                        chr = "\x5c";
                        break;
                    case 'a':
                        chr = "\x07";
                        break;
                    case 'b':
                        chr = "\x08";
                        break;
                    case 'f':
                        chr = "\x0c";
                        break;
                    case 'n':
                        chr = "\x0a";
                        break;
                    case 'r':
                        chr = "\x0d";
                        break;
                    case 't':
                        chr = "\x09";
                        break;
                    default:
                      fprintf(stderr, "Escape sequence not recognized.");
                      exit(1);
                      break;
                }
                
                read_arguments["target"] = chr;
            }
            else
            {
                fprintf(stderr, "Target not recognized.\n");
                exit(1);
            }
        }
        else if(read_arguments.count("input_filename") == 0)
        {
            read_arguments["input_filename"] = arg;
        }
        else if(read_arguments.count("output_filename") == 0)
        {
            read_arguments["output_filename"] = arg;
        }
        else
        {
            std::string msg = "Unknown command: " + arg;
            fprintf(stderr, "%s\n", msg.c_str());
            print_help();
            exit(1);
        }
    }
    
    return read_arguments;
}

int main(int argc, char** argv)
{
    /// Parse arguments into parameters.
    auto args = parse_arguments(argc, argv);
    
    if(args.count("input_filename") == 0 || args.count("output_filename") == 0)
    {
        print_help();
        return 1;
    }
    
    /// Open input for reading.
    int input_fd;
    if(args["input_filename"] == "-")
    {
        input_fd = STDIN_FILENO;
    }
    else
    {
        input_fd = open(args["input_filename"].c_str(), O_RDONLY);
        if(input_fd == -1)
        {
            perror("Error opening input file");
            exit(1);
        }
        /// Tell the OS, we need to read sequentially on the file, if there is an error, well we tried our best.
        posix_fadvise(input_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }
    
    /// Open output for writing.
    int output_fd;
    if(args["output_filename"] == "-")
    {
        output_fd = STDOUT_FILENO;
    }
    else
    {
        output_fd = open(args["output_filename"].c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if(output_fd == -1)
        {
            perror("Error opening output file");
            exit(1);
        }
    }
    
    /// Setup parameters to indexer.
    std::string size = (args.count("size") == 1) ? args["size"] : "32";
    uint8_t target = (args.count("target") == 1) ? args["target"][0] : '\n';
    bool include_zero = args.count("include-zero") == 1;
    
    if(size == "8")
        create_index<uint8_t>(input_fd, output_fd, target, include_zero);
    else if(size == "16")
        create_index<uint16_t>(input_fd, output_fd, target, include_zero);
    else if(size == "32")
        create_index<uint32_t>(input_fd, output_fd, target, include_zero);
    else if(size == "64")
        create_index<uint64_t>(input_fd, output_fd, target, include_zero);
    else
        assert(false);
}