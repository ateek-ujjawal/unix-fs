#!/usr/bin/python3

import sys
import lab3
import ctypes

def div_round_up(n, m):
    return (n + m - 1) // m

# list of blocks, only handles 1 level of of indirect
#
def get_blocks(inum):
    b = []
    _in = inodes[inum]
    for i in range(6):
        n = _in.ptrs[i]
        if n == 0:
            return b
        b.append(n)
    ind = lab3.indirect.from_buffer(bytearray(blocks[_in.indir_1]))
    for i in range(256):
        n = ind.ptrs[i]
        if n == 0:
            return b
        b.append(n)
    return b
            
def bad_inode(inum):
    _in = inodes[inum]
    if lab3.S_ISDIR(_in.mode) == False and lab3.S_ISREG(_in.mode) == False:
        print('ERROR: inode %d mode 0o%o' % (inum, _in.mode))
        return True
    if lab3.S_ISDIR(_in.mode) and _in.size == 0:
        printf('ERROR: inode %d: zero-len directory' % inum)
        return True
    return False

all_files = []
leaves = dict()
all_dirs = []
file_blocks = dict()

def map_file(path, inum):
    my_inode_map[inum] = True
    all_files.append(path)
    file_blocks[path] = []
    _in = inodes[inum]
    for b in [_in.ptrs[i] for i in range(6)]:
        if b < 0 or b >= n_blks:
            print('ERROR: inode %d: block %d' % (inum, b))
        elif b == 0:
            break
        else:
            my_block_map[b] = True
            file_blocks[path].append(b)
    i1 = _in.indir_1
    if i1 != 0:
        if i1 < 0 or i1 >= n_blks:
            print('ERROR: inode %d: indirect block %d' % (inum, i1))
        else:
            my_block_map[i1] = True
            file_blocks[path].append([i1])
            ind = lab3.indirect.from_buffer(bytearray(blocks[i1]))
            for b in [ind.ptrs[i] for i in range(256)]:
                if b < 0 or b >= n_blks:
                    print('ERROR: inode %d: block %d' % (inum, b))
                elif b == 0:
                    break
                else:
                    my_block_map[b] = True
                    file_blocks[path].append(b)

dir_blocks = dict()
path_2_inode = dict()

def map_dir(path, inum):
    leaves[path] = []
    dir_blocks[path] = []
    all_dirs.append(path)
    my_inode_map[inum] = True
    _in = inodes[inum]
    assert(lab3.S_ISDIR(_in.mode))
    _blks = get_blocks(inum)
    blks = []
    for b in _blks:
        if b < 0 or b >= n_blks:
            print('ERROR: inode %d: block %d' % (inum, b))
        else:
            dir_blocks[path].append(b)
            my_block_map[b] = True
            blks.append(b)
    for b in blks:
        des = (lab3.dirent * 32).from_buffer(bytearray(blocks[b]))
        for i in range(32):
            if des[i].valid == 0:
                continue
            slash = b'' if path == b'/' else b'/'
            name = path + slash + des[i].name
            n = des[i].inode
            if n < 1 or n >= n_inodes:
                print('ERROR: %s: %s: bad inum %d' %
                          (path, des[i].name, n))
            elif bad_inode(n):
                pass
            else:
                leaves[path].append(name)
                inode_2_name[n] = name
                path_2_inode[name] = n
                if lab3.S_ISREG(inodes[n].mode):
                    map_file(name, n)
                else:
                    map_dir(name, n)

fp = open(sys.argv[1], 'rb')
disk = fp.read()
fp.close()
n_blks = len(disk) // 1024
if n_blks*1024 != len(disk):
    print('ERROR: file length %d not a multiple of 1024' % len(disk))

blocks = [disk[i*1024:(i+1)*1024] for i in range(n_blks)]
super = lab3.super.from_buffer(bytearray(blocks[0]))
if n_blks != super.disk_size:
    print('ERROR: super.disk_size=%d file=%d blocks' %
              (super.disk_size, n_blks))
bm_start = 1
bm_len = super.blk_map_len
if bm_len != div_round_up(n_blks, 8*1024):
    print('ERROR: super.blk_map_len %d (should be %d)' %
              (bm_len, div_round_up(n_blks, 8*1024)))
blk_bitmap = bytearray(b''.join(blocks[bm_start:bm_start+bm_len]))
blk_bitmap2 = [lab3.bitmap.from_buffer(bytearray(blocks[i]))
                   for i in range(bm_start, bm_start+bm_len)]

def get2(b_list, i):
    j = i // 8192
    i = i % 8192
    return b_list[j].get(i)

im_start = bm_start + bm_len
im_len = super.in_map_len
if im_len < 1 or im_len > bm_len:
    print('ERROR: bad super.in_map_len %d' % im_len)
in_bitmap = bytearray(b''.join(blocks[im_start:im_start+im_len]))
in_bitmap2 = [lab3.bitmap.from_buffer(bytearray(blocks[i]))
                   for i in range(im_start, im_start+im_len)]

in_start = im_start + im_len
in_len = super.inodes_len
if in_len < 1 or in_len > n_blks//2:
    print('ERROR: bad super.inodes_len %d' % in_len)
n_inodes = in_len * 16
if im_len != div_round_up(n_inodes, 8*1024):
    print('ERROR: bad inode map len %d (should be %d)' %
              (im_len, div_round_up(n_inodes, 8*1024)))
inodes = (lab3.inode * n_inodes).from_buffer(bytearray(b''.join(
    blocks[in_start:in_start+in_len])))

my_block_map = [False] * n_blks
my_block_map[0:2] = [True,True]
my_inode_map = [False] * n_inodes
my_inode_map[0] = True

for i in range(in_start+in_len):
    my_block_map[i] = True
    
inode_2_name = dict()

path_2_inode[b'/'] = 1
map_dir(b'/', 1)

def get_bit(buf, i):
    j = i // 8
    return (buf[j] & (1 << (i%8))) != 0
    
print('disk size:', super.disk_size)
print('block map:', super.blk_map_len)


print('                            1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3') 
print('        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1');
for i in range(0, n_blks, 32):
    print(' %.4d: ' % i, end='')
    for j in range(i, i+32):
        print(' %d' % get2(blk_bitmap2, j), end='')
    print('')

print('inode map:', super.in_map_len)

print('                            1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3') 
print('        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1');
for i in range(0, n_inodes, 32):
    print(' %.4d: ' % i, end='')
    for j in range(i, i+32):
        print(' %d' % get2(in_bitmap2, j), end='')
    print('')

def xbytes(s):
    return s.decode('UTF-8')

print('directories:')
for d in all_dirs:
    print(xbytes(d), 'inode', path_2_inode[d], 'blocks:', dir_blocks[d])
    for l in leaves[d]:
        print(' ', xbytes(l), path_2_inode[l])

print('files:')
for f in all_files:
    print(xbytes(f), 'inode', path_2_inode[f], 'blocks:', file_blocks[f])

for i in range(n_blks):
    if get2(blk_bitmap2, i) != my_block_map[i]:
        print('ERROR: blkmap[%d] = %d, mymap[%d]=%d' %
                  (i, get2(blk_bitmap2,i), i, my_block_map[i]))

for i in range(n_inodes):
    if get2(in_bitmap2, i) != my_inode_map[i]:
        print('ERROR: inode_map[%d] = %d, mymap[%d]=%d' %
                  (i, get2(in_bitmap2,i), i, my_inode_map[i]))
