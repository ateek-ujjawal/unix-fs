#!/usr/bin/python3
#

import sys
import argparse
import lab3
import time

parser = argparse.ArgumentParser(description='format a fs5600 disk image')
parser.add_argument('--size', help='disk size (k/m/g)', required=True)
parser.add_argument('file', nargs=1, help='disk image name')
args = parser.parse_args()


# based on https://stackoverflow.com/a/42865957/2002471
import re
units = {"B": 1, "KB": 2**10, "MB": 2**20, "GB": 2**30, "TB": 2**40}
def parse_size(size):
    size = re.sub(r'(\d+)([KMGT]?)', r'\1 \2B', size.upper())
    number, unit = [string.strip() for string in size.split()]
    return int(float(number)*units[unit])

def div_round_up(n, m):
    return int((n + m - 1) / m)

size = parse_size(args.size)
blocks = int(size / 1024)
block_map_blks = div_round_up(blocks, 1024*8)
n_inodes = blocks // 2
inodes_per_blk = 1024 // 64
inode_blks = div_round_up(n_inodes, inodes_per_blk)
n_inodes = inode_blks * inodes_per_blk
inode_map_blks = div_round_up(n_inodes, 1024*8)

first_data_blk = 1 + block_map_blks + inode_map_blks + inode_blks

super = lab3.super()
super.magic = lab3.MAGIC
super.disk_size = int(size / 1024)
super.blk_map_len = block_map_blks
super.in_map_len = inode_map_blks
super.inodes_len = inode_blks

block_map = (lab3.bitmap * block_map_blks)()
inode_map = (lab3.bitmap * inode_map_blks)()

# inode 0 is reserved, 1 is root directory
for i in range(2):
    inode_map[0].set(i, 1)

# we'll use the first data block for the root directory
for i in range(first_data_blk+1):
    block_map[0].set(i, 1)

inodes = (lab3.inode * n_inodes)()
inodes[1].mode = lab3.S_IFDIR | 0o777
inodes[1].mtime = int(time.time())
inodes[1].size = 1024
inodes[1].ptrs[0] = first_data_blk

fp = open(args.file[0], 'wb')
fp.write(bytearray(super))
fp.write(bytearray(block_map))
fp.write(bytearray(inode_map))
fp.write(bytearray(inodes))

blk0 = bytearray(1024)
for i in range(blocks - first_data_blk):
    fp.write(blk0)
fp.close()
