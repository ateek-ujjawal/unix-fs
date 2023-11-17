/*
 * file:        fsx600.h
 * description: Data structures for CS 5600/7600 file system.
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers,  November 2023
 */
#ifndef __CSX600_H__
#define __CSX600_H__

#define BLOCK_SIZE 1024
#define FS_MAGIC 0x37363030

/* Entry in a directory
 */
struct fs_dirent {
    uint32_t valid : 1;
    uint32_t inode : 31;
    char name[28];              /* with trailing NUL */
};

/* Superblock - holds file system parameters. 
 * All lengths are in blocks; root directory is inode 1
 */
struct fs_super {
    int32_t magic;             /* 0x37363030 ('5600') */
    int32_t disk_size;
    int32_t blk_map_len;
    int32_t in_map_len;
    int32_t inodes_len;

    /* pad out to an entire block */
    char pad[1004];
};

#define N_DIRECT 6
struct fs_inode {
    int16_t uid;      /* file owner */
    int16_t gid;      /* group */
    int32_t mode;     /* type + permissions */
    int32_t mtime;    /* modification time */
    int32_t size;     /* size in bytes */
    int32_t ptrs[N_DIRECT];  /* first 6 blocks of file */
    int32_t indir_1;  /* block holding next 256 blocks */
    int32_t indir_2;  /* double indirect block */
    int32_t pad[4];   /* to make it 64 bytes */
};

#endif
