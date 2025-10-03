# pts-opack-port: a port of the old pack compressor and its decompressor by Steve Zucker to modern C compilers

pack does Huffman compression. There are the old and the new file formats
and algorithms. This is the implementation of the old algorithm, by Steve
Zucker no later than 1977-07-13. The new one is written by Thomas G.
Szymanski no later than 1980-04-11.

Initial port of pack.c was done by Leo Broukhis. More porting work and
porting of unpack.c was done by pts.

More details of the old pack file format:

* Old PACKED signature (16-bit PDP-11 word): 0x1f 0x1f.
* Number of chars in expanded file (32-bit PDP-11 float (not supported here) or 32-bit PDP-11 word).
* Number of words in expanded tree (16-bit PDP-11 word).
* Tree in 'compressed' form:
  * If 0<=byte<=0376, expand by zero padding to left.
  * If byte=0377, next two bytes for one word (16-bit PDP-11 word).
  * Nodes:
    * Terminal nodes: First word is zero; second is character.
    * Non-terminal nodes: Incremental 0/1 pointers.
* Huffman code strings for the bytes.

More info about pack: https://en.wikipedia.org/wiki/Pack_(software)

More info about pack: http://fileformats.archiveteam.org/wiki/Pack_(Unix)

More info about pack: https://retrocomputing.stackexchange.com/q/32120

Please note that the deecompressor implementation doesn't check for
valid input, It may do out-of-bound memory reads and writes or fall to an
infinite loop on invalid input.
