from ctypes import *

MAGIC = 0x37363030

class dirent(Structure):
    _fields_ = [("valid", c_uint, 1),
                ("inode", c_uint, 31),
                ("name", c_char * 28)]
        
class super(Structure):
    _fields_ = [("magic", c_int),
                ("disk_size", c_int),
                ("blk_map_len", c_int),
                ("in_map_len", c_int),
                ("inodes_len", c_int),
                ("_pad", c_char * 1004)]

class inode(Structure):
    _fields_ = [("uid", c_short),
                ("gid", c_short),
                ("mode", c_int),
                ("mtime", c_int),
                ("size", c_int),
                ("ptrs", c_int * 6),
                ("indir_1", c_int),
                ("indir_2", c_int),
                ("_pad", c_int * 4)]

# one block worth of bitmap
#
class bitmap(Structure):
    _fields_ = [("vals", c_uint * 256)]
    def get(self, i):
        n = self.vals[i // 32]
        mask = 1 << (i % 32)
        return (n & mask) != 0
    def set(self, i, val):
        mask = 1 << (i % 32)
        n = self.vals[i // 32]
        if val:
            n = n | mask
        else:
            n = n & (mask ^ 0xffffffff)
        self.vals[i // 32] = n

class indirect(Structure):
    _fields_ = [("ptrs", c_uint * 256)]
    def pointers(self, n):
        ptrs = []
        for i in range(n):
            ptrs.append(self.ptrs[i])
        return ptrs

S_IFMT  = 0o0170000  # bit mask for the file type bit field
S_IFREG = 0o0100000  # regular file
S_IFDIR = 0o0040000  # directory

def S_ISREG(mode):
    return (mode & S_IFMT) == S_IFREG

def S_ISDIR(mode):
    return (mode & S_IFMT) == S_IFDIR
