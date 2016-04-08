#include <alloca.h>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <type_traits>

/** 
 * ~1 million item buffer sizes, this can have an impact on performance.
 * For the input buffer, this should be about a megabyte.
 * For the output buffer, this depends on the size of the output type.
 */
static const size_t BUFFER_SIZE = 512*1024; 

/**
 * Writes an output buffer to the output file descriptor.
 */
template <typename OutputType>
void write_buffer(OutputType* buffer, ssize_t& buffer_position, int output_fd)
{
    ssize_t remaining_count = sizeof(OutputType)*buffer_position;
    auto write_result = write(output_fd, buffer, remaining_count);
    assert(write_result == remaining_count);
    buffer_position = 0; /// Have to make sure we reset the position on the buffer.
}

/**
 * Gets a reference to the array item, in debug mode we force a range check.
 */
template <typename OutputType>
OutputType& array_item(OutputType* array, size_t index)
{    
#ifdef DEBUG
    assert(index < BUFFER_SIZE);
#endif
    return array[index];
}

/**
 * Writes the 8-byte index file header, the format is as follows:
 ** A 4-byte magic number, 0xba5eba11 to identify endianness.
 ** A 1-byte version number. Currently, it is set 1.
 ** A 1-byte number denoting the size of the index type.
 ** A 2-byte pad.
 */
template <typename OutputType>
void write_header(int output_fd, uint8_t on_chr)
{
    /// This could be made to call write less
    static const uint32_t magic_number = 0xba5eba11;
    static const uint8_t version = 1;
    static const uint8_t index_size = sizeof(OutputType);
    static const uint16_t padding = 0;
    
    assert(write(output_fd, &magic_number, 4) == 4);
    assert(write(output_fd, &version, 1) == 1);
    assert(write(output_fd, &index_size, 1) == 1);
    assert(write(output_fd, &on_chr, 1) == 1);
    assert(write(output_fd, &padding, 1) == 1);
}

/**
 * We would like to prefer stack memory if possible. 
 * This method says whether we need to use the heap, because our buffer size is too large.
 * If 90% of the stack size is smaller than the two buffers, use the heap.
 */
template <typename OutputType>
bool should_use_heap()
{
    static const size_t required_stack = (sizeof(uint8_t) + sizeof(OutputType))*BUFFER_SIZE;
    struct rlimit rlimit_info;
    int result = getrlimit(RLIMIT_STACK, &rlimit_info);
    if (result == 0)
    {
        size_t available_stack = (9*rlimit_info.rlim_cur)/10;
        return available_stack < required_stack;
    }
    else
    {
        /// Failed to get info, use the heap
        return true;
    }
}

/**
 * Performs the innards of the index creation.
 */
template <typename OutputType>
void create_index(int input_fd, int output_fd, uint8_t on_chr, bool include_zero)
{
    /// We don't want to kill the stack, test if we can use it though.
    bool using_heap = should_use_heap<OutputType>();
    uint8_t* input_buffer;
    OutputType* output_buffer;
    
    if(using_heap)
    {
        /// Use malloc to create the buffers on the heap, we do not care about constructor's not being called.
        input_buffer = static_cast<uint8_t*>(malloc(BUFFER_SIZE*sizeof(uint8_t)));
        output_buffer = static_cast<OutputType*>(malloc(BUFFER_SIZE*sizeof(OutputType)));
    }
    else
    {
        /// Use alloca to create the buffers on the stack, we do not care about constructor's not being called. 
        input_buffer = static_cast<uint8_t*>(alloca(BUFFER_SIZE*sizeof(uint8_t)));
        output_buffer = static_cast<OutputType*>(alloca(BUFFER_SIZE*sizeof(OutputType)));
    }
    
    /// Make sure our buffers are created to our satisfaction.
    assert(input_buffer != nullptr);
    assert(output_buffer != nullptr);
    
    ssize_t output_buffer_position = 0; /// Holds the index where we will write into output_buffer
    OutputType current_read_start = 0; /// Will hold the start index of the input chunk in the whole file
    
    write_header<OutputType>(output_fd, on_chr); /// Output the index header.
    if(include_zero)
        output_buffer[output_buffer_position++] = 0; /// If we include zero, we prepend the output with zero
    
    /*
     * Workflow is more or less as follows:
     ** While the file is not complete
     **   Read a chunk from the file
     **   While there are unread target characters in the file
     **     Find the next target in the chunk using memchr
     **     If we couldn't find one, begin the outer loop again on a fresh chunk.
     **     If we could find one, write the global position to the output buffer.
     **     If the output buffer is full, write it out.
     */
    while(true)
    {
        auto bytes_total = read(input_fd, input_buffer, BUFFER_SIZE); /// Bytes total represent's the input chunk size
        auto bytes_left = bytes_total; /// Bytes left is a running count of the remaining unread input chunk.
        
        if(__builtin_expect(bytes_left == -1, 0))
        {
            /// Error with read
            perror("Error reading file: ");
            exit(1);
        }
        else if(__builtin_expect(bytes_left == 0, 0))
        {
            /// No more data
            break;
        }
        else
        {
            /// Successfully read a chunk
            uint8_t* previous_ptr = input_buffer; /// Represents the one past last position where we read a target the beginning of the chunk.
            
            while(true)
            {
                uint8_t* next_ptr = static_cast<uint8_t*>(memchr(previous_ptr, on_chr, bytes_left)); /// Next position of a target.
                if(next_ptr == nullptr)
                    break; /// No more targets, end of input chunk.
                bytes_left -= (std::distance(previous_ptr, next_ptr) + 1); /// We skipped a couple of bytes.
                /// Target global position
                array_item(output_buffer, output_buffer_position++) = std::distance(input_buffer, next_ptr) + current_read_start + 1; 
                previous_ptr = next_ptr + 1; /// Update the search window to start from the last target.
                if(__builtin_expect(output_buffer_position == BUFFER_SIZE, 0))
                    write_buffer(output_buffer, output_buffer_position, output_fd); /// Write the output buffer, if necessary.
            }
            current_read_start += bytes_total; /// We read a chunk, so update our global position.
        }
    }
    if(output_buffer_position != 0)
        write_buffer(output_buffer, output_buffer_position, output_fd); /// Flush the remnants of the output buffer.
     
    /// Release our buffers back to the system if necessary
    if(using_heap)
    {
        /// malloc requires us to free the buffers.
        free(input_buffer);
        free(output_buffer);
    }
    else
    {
        /// alloca does not require any special treatment to free, function end will do that.
    }
}
