#!/bin/sh

xxd -u -a -g 1 -s $2 -l $3 $1


# -u     Use upper-case hex letters. Default is lower-case.

# -a | -autoskip
#     Toggle autoskip: A single '*' replaces NUL-lines.  Default off.

# -g bytes | -groupsize bytes
#         Separate the output of every <bytes> bytes (two hex characters or eight bit digits each) by a whitespace.  Specify -g 0 to suppress grouping.  <Bytes>
#         defaults to 2 in normal mode, 4 in little-endian mode and 1 in bits mode.  Grouping does not apply to PostScript or include style.

# -l len | -len len
#         Stop after writing <len> octets.

# -C | -capitalize
#         Capitalize variable names in C include file style, when using -i.

# -s [+][-]seek
#         Start  at  <seek>  bytes abs. (or rel.) infile offset.  + indicates that the seek is relative to the current stdin file position (meaningless when not
#         reading from stdin).  - indicates that the seek should be that many characters from the end of the input (or if combined with +:  before  the  current
#         stdin file position).  Without -s option, xxd starts at the current file position.
