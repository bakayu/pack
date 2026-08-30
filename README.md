# pack

A recursive file/directory archiver with a Huffman compression layer, written
from scratch in C with no external libraries. Educational project.

## Build

```sh
make        # or just use just (justfile included)
make test
```

## Usage

```sh
pack -c <input> <archive.pack>    # archive and compress a file or directory
pack -x <archive.pack> <dest>     # decompress and extract into dest
pack --debug -c src/ out.pack     # same, plus the internals (see below)
pack --help
```

`--debug` prints the archive entries as they are walked, the byte frequency
table, the Huffman tree, the code table, and the resulting compression
ratio. On extraction it prints the tree rebuilt from the archive and the
entries as they are recreated. It changes nothing about the bytes written.

## File format

```
"PACK"          4 bytes magic
version         u8
orig_size       u64, big-endian, uncompressed archive stream length
tree            pre-order bit stream: 1 = leaf + 8-bit value, 0 = internal
body            one Huffman code per byte, zero-padded to a byte boundary
```

Inside the archive stream, one entry per file or directory:

```
type      u8    0 = dir, 1 = regular file
mode      u32   permission bits
path_len  u16
path      path_len bytes
size      u64   0 for dirs
payload   size bytes (files only)
```
