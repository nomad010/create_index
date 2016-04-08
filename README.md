create_index - A tool for creating indices of files.

General information
===================

Creates a file of fixed size indices of the positions of target characters. It may useful for repeatedly splitting up a file by arbitrary boundaries. Examples of where it could be useful is for answering aggregation queries on item ranges, and splitting up work from an ASCII file to multiple processors.

The code has been somewhat written for performance and has been clocked at approximately 2.5GB/s on an input file, however the performance is dependent on the average density of target characters, whether the file is in OS cache, the speed of your disk etc. Most of the work is done by a memchr call which is usually implemented to take advantage of CPU features.

Index file format
=================

The index file consists of a fixed size header, followed by a stream of index values. The header consists of: 
* 4-byte magic number(0xba5eba11) for endian checks
* a 1-byte version number(currently 1)
* a 1-byte char for what target was used
* 1-byte padding. 

What follows is a stream of index values listing all the positions of a target in the input file.
    
A fixed size header has a few advantages for instance, the number of occurrences of the target may be easily calculated from the file size and the information stored in the header. 

Issues
======
 * Deal with UTF-8 properly at least, UTF-8 input files are should work, however a UTF-8 target can not be specified.
 * No tools to deal with index files.
 * No info to ensure you use the index on the correct file.
 * Files larger than 2^64 bytes will cause overflow in a 64-bit unsigned integer.
 * main file code is somewhat messy and not terribly extensible.

Enhancements
============
 * Handle target strings via Boyer-Moore and lists of target strings via Aho-Corasick.
 * More tests.
 * Verbosity mode.
