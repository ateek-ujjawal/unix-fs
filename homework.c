/*
 * file:        homework.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern Khoury College
 * Ateek Ujjawal, November 2023
 */

#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "fs5600.h"

struct fs_super *super;
unsigned char *block_bmp;
unsigned char *inode_bmp;
struct fs_inode *inode_tbl;
uint32_t inode_count;
uint32_t inode_region_blk;
int32_t data_blk;
uint32_t disk_size;

#define MAX_ENTRIES (BLOCK_SIZE / sizeof(struct fs_dirent))
#define MAX_BLKS_IN_BLK (BLOCK_SIZE / sizeof(int32_t))
#define MAX_TOKENS 32

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int blknum, int nblks);
extern int block_write(void *buf, int blknum, int nblks);

/* how many buckets of size M do you need to hold N items?
 */
int div_round_up(int n, int m) { return (n + m - 1) / m; }

/* quick and dirty function to split an absolute path (i.e. begins with "/")
 * uses the same interface as the command line parser in Lab 1
 */
int split_path(const char *path, int argc_max, char **argv, char *buf,
               int buf_len) {
    int i = 0, c = 1;
    char *end = buf + buf_len;

    if (*path++ != '/' || *path == 0)
        return 0;

    while (c != 0 && i < argc_max && buf < end) {
        argv[i++] = buf;
        while ((c = *path++) && (c != '/') && buf < end)
            *buf++ = c;
        *buf++ = 0;
    }
    return i;
}

/* I'll give you this function for free, to help
 */
void inode_2_stat(struct stat *sb, struct fs_inode *in) {
    memset(sb, 0, sizeof(*sb));
    sb->st_mode = in->mode;
    sb->st_nlink = 1;
    sb->st_uid = in->uid;
    sb->st_gid = in->gid;
    sb->st_size = in->size;
    sb->st_blocks = div_round_up(in->size, BLOCK_SIZE);
    sb->st_atime = sb->st_mtime = sb->st_ctime = in->mtime;
}

unsigned long get_usecs(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int write_block_bmp_back() {
    return block_write(block_bmp, 1, super->blk_map_len);
}

int write_inode_bmp_back() {
    return block_write(inode_bmp, 1 + super->blk_map_len, super->in_map_len);
}

int write_inode_tbl_back() {
    return block_write(inode_tbl, inode_region_blk, super->inodes_len);
}

// check the block is located at data blocks region and in use
int check_data_blk(int32_t blk) {
    if (blk >= data_blk && blk < disk_size && bit_test(block_bmp, blk)) {
        return 1;
    }

    return 0;
}

int32_t allocate_inode() {
    /* Loop through bitmap to check for available inodes, and allocate if free
     */
    for (int i = 2; i < inode_count; i++) {
        bool test = bit_test(inode_bmp, i);
        if (!test) {
            bit_set(inode_bmp, i);
            write_inode_bmp_back();
            return i;
        }
    }
    return -ENOSPC;
}

int32_t allocate_data_blk() {
    int data_blk_start =
        1 + super->blk_map_len + super->in_map_len + super->inodes_len;
    /*Loop throught bitmap to check for available data blocks, and allocate if
     * free */
    for (int i = data_blk_start; i < disk_size; i++) {
        bool test = bit_test(block_bmp, i);
        if (!test) {
            bit_set(block_bmp, i);
            char *data = calloc(BLOCK_SIZE, sizeof(char));
            block_write(data, i, 1);
            write_block_bmp_back();
            return i;
        }
    }
    return -ENOSPC;
}

void write_dir_to_blk(uint32_t block) {
    /* Write MAX_ENTRIES * sizeof(struct fs_dirent) to the given block */
    struct fs_dirent *dirent = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_write(dirent, block, 1);
}

void create_inode(mode_t mode, uint32_t inode_no) {
    assert(inode_no > 1 && inode_no < inode_count);
    struct fs_inode *inode = calloc(1, sizeof(struct fs_inode));
    inode->uid = 0;
    inode->gid = 0;
    inode->mode = mode;
    inode->mtime = get_usecs();
    inode->size = 0;
    struct fs_inode *location = (inode_tbl + inode_no);
    memcpy(location, inode, sizeof(struct fs_inode));
    write_inode_tbl_back();
}

int allocate_dirent() {
    /* Allocates a new directory entry in a new data block */
    int data_blk = allocate_data_blk();
    if (data_blk < 0)
        return data_blk;
    write_dir_to_blk(data_blk);
    return data_blk;
}

uint32_t search_dir(const char *dir_name, int32_t block) {
    if (!check_data_blk(block)) {
        return 0;
    }

    struct fs_dirent *dirs = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_read(dirs, block, 1);
    for (int j = 0; j < MAX_ENTRIES; j++) {
        struct fs_dirent *dir_entry = dirs + j;
        if (dir_entry->valid && !strcmp(dir_name, dir_entry->name)) {
            return dir_entry->inode;
        }
    }

    return 0;
}

int32_t remove_dir(const char *dir_name, int32_t block) {
    if (!check_data_blk(block)) {
        return 0;
    }

    struct fs_dirent *dirs = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_read(dirs, block, 1);
    for (int j = 0; j < MAX_ENTRIES; j++) {
        struct fs_dirent *dir_entry = dirs + j;
        if (dir_entry->valid && !strcmp(dir_name, dir_entry->name)) {
            memset(dir_entry, 0, sizeof(struct fs_dirent));
            block_write(dirs, block, 1);

            for (int k = 0; k < MAX_ENTRIES; k++) {
                dir_entry = dirs + k;
                if (dir_entry->valid)
                    return 1;
            }

            return -1;
        }
    }

    return 0;
}

int _getinodeno(int argc, char **argv, uint32_t *inode_no) {
    for (int i = 0; i < argc; i++) {
        // illegal inode_no
        assert(*inode_no >= 1 && *inode_no < inode_count);

        struct fs_inode *inode = inode_tbl + *inode_no; // get inode

        // if current inode is for a file, it's a wrong path
        if (!S_ISDIR(inode->mode)) {
            return -ENOTDIR;
        }

        // search in direct pointers
        int is_find = 0;
        for (int j = 0; j < N_DIRECT; j++) {
            uint32_t search_inode = search_dir(argv[i], inode->ptrs[j]);
            if (search_inode) {
                *inode_no = search_inode;
                is_find = 1;
                break;
            }
        }

        /*
         search in indirect pointer
         entries number in current directory is more than 6 * (BLOCK_SIZE /
         sizeof(struct fs_dirent))
        */
        if (!is_find && check_data_blk(inode->indir_1)) {
            int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
            block_read(blks, inode->indir_1, 1);
            for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
                uint32_t search_inode = search_dir(argv[i], *(blks + j));
                if (search_inode) {
                    *inode_no = search_inode;
                    is_find = 1;
                    break;
                }
            }
        }

        // search in double indirect pointer
        if (!is_find && check_data_blk(inode->indir_2)) {
            int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
            block_read(blks_1, inode->indir_2, 1);
            for (int j = 0; j < MAX_BLKS_IN_BLK && !is_find; j++) {
                int32_t blks_2 = *(blks_1 + j);
                if (check_data_blk(blks_2)) {
                    int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                    block_read(blks, blks_2, 1);
                    for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
                        uint32_t search_inode =
                            search_dir(argv[i], *(blks + k));
                        if (search_inode) {
                            *inode_no = search_inode;
                            is_find = 1;
                            break;
                        }
                    }
                }
            }
        }

        if (!is_find) {
            return -ENOENT;
        }
    }

    return 0;
}

void *lab3_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {

    super = calloc(1, sizeof(struct fs_super));

    /* Read super block(0) from disk into *super */
    block_read(super, 0, 1);

    block_bmp = calloc(BLOCK_SIZE * super->blk_map_len, sizeof(unsigned char));

    /* Read block bitmap into block_bmp */
    block_read(block_bmp, 1, super->blk_map_len);

    inode_bmp = calloc(BLOCK_SIZE * super->in_map_len, sizeof(unsigned char));

    /* Read inode bitmap into inode_bmp */
    block_read(inode_bmp, 1 + super->blk_map_len, super->in_map_len);

    /* Read inode table into inode_tbl */
    inode_region_blk = 1 + super->blk_map_len + super->in_map_len;
    int inodes_in_blk = BLOCK_SIZE / sizeof(struct fs_inode);
    inode_count = super->inodes_len * inodes_in_blk;

    inode_tbl = calloc(inode_count, sizeof(struct fs_inode));

    block_read(inode_tbl, inode_region_blk, super->inodes_len);

    data_blk = inode_region_blk + super->inodes_len;
    disk_size = super->disk_size;

    return NULL;
}

int lab3_getattr(const char *path, struct stat *sb, struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir

    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;
    inode_2_stat(sb, inode);
    return 0;
}

typedef int (*fuse_fill_dir_t)(void *ptr, const char *name,
                               const struct stat *stbuf, off_t off,
                               enum fuse_fill_dir_flags flags);

void *read_blk_dir(void *ptr, fuse_fill_dir_t filler, int32_t block) {
    if (!check_data_blk(block)) {
        return NULL;
    }

    struct fs_dirent *dirs = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_read(dirs, block, 1);
    for (int j = 0; j < MAX_ENTRIES; j++) {
        struct fs_dirent *dir_entry = dirs + j;
        if (dir_entry->valid) {
            filler(ptr, dir_entry->name, NULL, 0, 0);
        }
    }

    return NULL;
}

int lab3_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi,
                 enum fuse_readdir_flags flags) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;
    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR;
    }

    // read direct pointers
    for (int i = 0; i < N_DIRECT; i++) {
        read_blk_dir(ptr, filler, inode->ptrs[i]);
    }

    // read indirect pointer
    if (check_data_blk(inode->indir_1)) {
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks, inode->indir_1, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            read_blk_dir(ptr, filler, *(blks + j));
        }
    }

    // read double indirect pointer
    if (check_data_blk(inode->indir_2)) {
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_1, inode->indir_2, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            int32_t blks_2 = *(blks_1 + j);
            if (check_data_blk(blks_2)) {
                int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                block_read(blks, blks_2, 1);
                for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
                    read_blk_dir(ptr, filler, *(blks + k));
                }
            }
        }
    }

    return 0;
}

char *read_blk_file(int32_t blk) {
    if (!check_data_blk(blk)) {
        return NULL;
    }

    char *buf = calloc(BLOCK_SIZE, sizeof(*buf));
    block_read(buf, blk, 1);

    return buf;
}

char *get_file(struct fs_inode *inode, off_t offset, uint32_t bytes_to_copy) {
    char *start, *buffer, *blk;

    buffer = calloc(BLOCK_SIZE, inode->size);
    start = buffer;
    uint32_t count = 0;
    uint32_t start_block = offset / BLOCK_SIZE;
    uint32_t end_block = (offset + bytes_to_copy) / BLOCK_SIZE;

    // Read direct pointer block data into buffer
    for (int i = 0; i < N_DIRECT; i++) {
        if (count >= start_block && count <= end_block) {
            blk = read_blk_file(inode->ptrs[i]);
            if (blk != NULL) {
                memset(buffer, 0, BLOCK_SIZE);
                memcpy(buffer, blk, BLOCK_SIZE);
                buffer = buffer + BLOCK_SIZE;
            }
        }

        if (count > end_block)
            return start;

        count++;
    }

    // Read indirect pointer block data into buffer
    if (check_data_blk(inode->indir_1) && count <= end_block) {
        uint32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(*blks));
        block_read(blks, inode->indir_1, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            if (count >= start_block && count <= end_block) {
                blk = read_blk_file(*(blks + j));
                if (blk != NULL) {
                    memset(buffer, 0, BLOCK_SIZE);
                    memcpy(buffer, blk, BLOCK_SIZE);
                    buffer = buffer + BLOCK_SIZE;
                }
            }

            if (count > end_block)
                return start;
            count++;
        }
    }

    // Read double indirect pointer block data into buffer;
    if (check_data_blk(inode->indir_2) && count <= end_block) {
        uint32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(*blks_1));
        block_read(blks_1, inode->indir_2, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            uint32_t blks_2 = *(blks_1 + j);
            if (check_data_blk(blks_2)) {
                uint32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(*blks));
                block_read(blks, blks_2, 1);
                for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
                    if (count >= start_block && count <= end_block) {
                        blk = read_blk_file(*(blks + k));
                        if (blk != NULL) {
                            memset(buffer, 0, BLOCK_SIZE);
                            memcpy(buffer, blk, BLOCK_SIZE);
                            buffer = buffer + BLOCK_SIZE;
                        }
                    }

                    if (count > end_block)
                        return start;
                    count++;
                }
            }
        }
    }

    return start;
}

int lab3_read(const char *path, char *buf, size_t len, off_t offset,
              struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;
    if (S_ISDIR(inode->mode)) {
        return -EISDIR;
    }

    if (offset >= inode->size) {
        return 0;
    }

    /* Read len bytes from offset, if offset + len is less than file size,
     * otherwise read till end of file */
    uint32_t bytes_to_copy = inode->size - offset;
    if (offset + len < inode->size) {
        bytes_to_copy = len;
    }

    /* Read part of file into file_bytes */
    char *file_bytes = get_file(inode, offset, bytes_to_copy);

    uint32_t start = (offset % BLOCK_SIZE);

    memcpy(buf, file_bytes + start, bytes_to_copy);

    return bytes_to_copy;
}

int32_t write_to_dirent(const char *dir_name, mode_t mode, int32_t block) {
    if (!check_data_blk(block)) {
        return 0;
    }
    int allocated_inode;

    struct fs_dirent *dirs = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_read(dirs, block, 1);
    for (int i = 0; i < MAX_ENTRIES; i++) {
        struct fs_dirent *dir_entry = dirs + i;
        if (!dir_entry->valid) {
            allocated_inode = allocate_inode();

            if (allocated_inode < 0)
                return allocated_inode;

            create_inode(mode, allocated_inode);
            dir_entry->valid = 1;
            dir_entry->inode = allocated_inode;
            int j = 0;
            while (*(dir_name + j) != '\0') {
                dir_entry->name[j] = *(dir_name + j);
                j++;
            }
            dir_entry->name[j] = '\0';
            block_write(dirs, block, 1);
            return allocated_inode;
        }
    }

    return 0;
}

int create_dir_file(uint32_t inode_no, char *dir_name, mode_t mode) {
    assert(inode_no >= 1 && inode_no < inode_count);

    struct fs_inode *inode = inode_tbl + inode_no;
    uint32_t dir_inode = 0, data_blk;

    /* Write to free directory entry in direct pointers */
    for (int i = 0; i < N_DIRECT; i++) {
        /* If dirent not allocated, or full, allocate a new dirent */
        if (inode->ptrs[i] == 0) {
            data_blk = allocate_dirent();
            if (data_blk < 0) {
                return data_blk;
            }
            inode->size += BLOCK_SIZE;
            inode->ptrs[i] = data_blk;
            write_inode_tbl_back();
        }

        /* Write directory to dirent */
        dir_inode = write_to_dirent(dir_name, mode, inode->ptrs[i]);

        /*Return inode of directory */
        if (dir_inode)
            return 0;
    }

    /* Write to free directory entry in indirect pointers */
    // if indir_1 not exists, create first
    if (!check_data_blk(inode->indir_1)) {
        int blk_no = allocate_data_blk();
        if (blk_no < 0) {
            return blk_no;
        }
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_write(blks, blk_no, 1);
        inode->indir_1 = blk_no;
        write_inode_tbl_back();
    }
    int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
    block_read(blks, inode->indir_1, 1);
    for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
        if (*(blks + i) == 0) {
            data_blk = allocate_dirent();
            if (data_blk < 0) {
                return data_blk;
            }
            inode->size += BLOCK_SIZE;
            write_inode_tbl_back();
            *(blks + i) = data_blk;
        }

        dir_inode = write_to_dirent(dir_name, mode, *(blks + i));

        if (dir_inode) {
            block_write(blks, inode->indir_1, 1);
            return 0;
        }
    }

    /* Write to free directory entry in double indirect pointers */
    // if indir_2 not exists, create first
    if (!check_data_blk(inode->indir_2)) {
        int32_t blk1_no = allocate_data_blk();
        if (blk1_no < 0) {
            return blk1_no;
        }
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_write(blks_1, blk1_no, 1);
        inode->indir_2 = blk1_no;
        write_inode_tbl_back();
    }
    int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
    block_read(blks_1, inode->indir_2, 1);
    for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
        // if *(blks_1 + j) not exists, create first
        if (!check_data_blk(*(blks_1 + j))) {
            int32_t blk2_no = allocate_data_blk();
            if (blk2_no < 0) {
                return blk2_no;
            }
            int32_t *blks_2 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
            block_write(blks_2, blk2_no, 1);
            *(blks_1 + j) = blk2_no;
        }
        int32_t *blks_2 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_2, *(blks_1 + j), 1);
        for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
            if (*(blks_2 + k) == 0) {
                data_blk = allocate_dirent();
                if (data_blk < 0) {
                    return data_blk;
                }
                inode->size += BLOCK_SIZE;
                write_inode_tbl_back();
                *(blks_2 + k) = data_blk;
            }

            dir_inode = write_to_dirent(dir_name, mode, *(blks_2 + k));

            if (dir_inode) {
                block_write(blks_2, *(blks_1 + j), 1);
                return 0;
            }
        }
    }

    return -ENOSPC;
}

int lab3_mkdir(const char *path, mode_t mode) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    if (strlen(tokens[n_tokens - 1]) > 27) {
        return -ENAMETOOLONG;
    }

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res == 0) {
        return -EEXIST;
    }

    *inode_no = 1;
    n_tokens--;
    res = _getinodeno(n_tokens, tokens, inode_no);

    if (res < 0) {
        return res;
    }

    res = create_dir_file(*inode_no, tokens[n_tokens], mode | S_IFDIR);

    if (res < 0) {
        return res;
    }

    return res;
}

int check_if_empty(uint32_t inode_no) {
    struct fs_inode *inode = inode_tbl + inode_no;

    for (int i = 0; i < N_DIRECT; i++) {
        if (check_data_blk(inode->ptrs[i]))
            return -ENOTEMPTY;
    }

    if (check_data_blk(inode->indir_1)) {
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks, inode->indir_1, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            if (check_data_blk(*(blks + i)))
                return -ENOTEMPTY;
        }
    }

    if (check_data_blk(inode->indir_2)) {
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_1, inode->indir_2, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            int32_t blks_2 = *(blks_1 + j);
            if (check_data_blk(blks_2)) {
                int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                block_read(blks, blks_2, 1);
                for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
                    if (check_data_blk(*(blks + k)))
                        return -ENOTEMPTY;
                }
            }
        }
    }

    return 0;
}

int remove_from_dirent(uint32_t inode_no, char *name) {
    struct fs_inode *inode = inode_tbl + inode_no;
    int success = 0;

    for (int i = 0; i < N_DIRECT; i++) {
        success = remove_dir(name, inode->ptrs[i]);
        if (success) {
            if (success == -1) {
                bit_clear(block_bmp, inode->ptrs[i]);
                inode->ptrs[i] = 0;
                inode->size -= BLOCK_SIZE;
                write_inode_tbl_back();
                write_block_bmp_back();
                success = 1;
            }
            return success;
        }
    }

    if (check_data_blk(inode->indir_1)) {
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks, inode->indir_1, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            success = remove_dir(name, *(blks + i));
            if (success) {
                if (success == -1) {
                    bit_clear(block_bmp, *(blks + i));
                    *(blks + i) = 0;
                    inode->size -= BLOCK_SIZE;
                    write_inode_tbl_back();
                    write_block_bmp_back();
                    success = 1;
                }
                return success;
            }
        }
    }

    if (check_data_blk(inode->indir_2)) {
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_1, inode->indir_2, 1);
        for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
            int32_t blks_2 = *(blks_1 + j);
            if (check_data_blk(blks_2)) {
                int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                block_read(blks, blks_2, 1);
                for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
                    success = remove_dir(name, *(blks + k));
                    if (success) {
                        if (success == -1) {
                            bit_clear(block_bmp, *(blks + k));
                            *(blks + k) = 0;
                            inode->size -= BLOCK_SIZE;
                            write_inode_tbl_back();
                            write_block_bmp_back();
                            success = 1;
                        }
                        return success;
                    }
                }
            }
        }
    }

    return 0;
}

int lab3_rmdir(const char *path) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    if (strlen(tokens[n_tokens - 1]) > 27) {
        return -ENAMETOOLONG;
    }

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0)
        return res;

    struct fs_inode *inode = inode_tbl + *inode_no;
    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR;
    }

    res = check_if_empty(*inode_no);
    if (res < 0)
        return res;

    if (*inode_no != 1) {
        bit_clear(inode_bmp, *inode_no);
        write_inode_bmp_back();
    }

    *inode_no = 1;
    n_tokens--;
    res = _getinodeno(n_tokens, tokens, inode_no);

    res = remove_from_dirent(*inode_no, tokens[n_tokens]);
    assert(res != 0);
    return 0;
}

int lab3_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    if (strlen(tokens[n_tokens - 1]) > 27) {
        return -ENAMETOOLONG;
    }

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res == 0) {
        return -EEXIST;
    }

    *inode_no = 1;
    n_tokens--;
    res = _getinodeno(n_tokens, tokens, inode_no);

    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;
    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR;
    }

    return create_dir_file(*inode_no, tokens[n_tokens], mode | S_IFREG);
}

int lab3_utimens(const char *path, const struct timespec tv[2],
                 struct fuse_file_info *fi) {
    return 0;
}

int lab3_chmod(const char *path, mode_t new_mode, struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;

    inode->mode = (inode->mode & S_IFMT) | new_mode;
    write_inode_tbl_back();

    return 0;
}

int free_file_blk(uint32_t inode_no) {
    struct fs_inode *inode = inode_tbl + inode_no;

    // free direct pointers block
    for (int i = 0; i < N_DIRECT; i++) {
        if (check_data_blk(inode->ptrs[i])) {
            inode->ptrs[i] = 0;
            bit_clear(block_bmp, inode->ptrs[i]);
        }
    }

    // free indirect pointer block
    if (check_data_blk(inode->indir_1)) {
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks, inode->indir_1, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            if (check_data_blk(*(blks + i))) {
                bit_clear(block_bmp, *(blks + i));
            }
        }
        inode->indir_1 = 0;
        bit_clear(block_bmp, inode->indir_1);
    }

    // free double indirect pointer block
    if (check_data_blk(inode->indir_2)) {
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_1, inode->indir_2, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            int32_t blks_2 = *(blks_1 + i);
            if (check_data_blk(blks_2)) {
                int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                block_read(blks, blks_2, 1);
                for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
                    if (check_data_blk(*(blks + j))) {
                        bit_clear(block_bmp, *(blks + j));
                    }
                }
                bit_clear(block_bmp, blks_2);
            }
        }
        inode->indir_2 = 0;
        bit_clear(block_bmp, inode->indir_2);
    }

    inode->size = 0;
    write_inode_tbl_back();
    write_block_bmp_back();

    return 0;
}

int lab3_unlink(const char *path) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;

    if (S_ISDIR(inode->mode)) {
        return -EISDIR;
    }

    free_file_blk(*inode_no);

    // free inode no
    bit_clear(inode_bmp, *inode_no);
    write_inode_bmp_back();

    // remove from parent dirent
    *inode_no = 1;
    n_tokens--;
    _getinodeno(n_tokens, tokens, inode_no);
    res = remove_from_dirent(*inode_no, tokens[n_tokens]);
    assert(res != 0);
    return 0;
}

int search_rename(const char *src_name, const char *dst_name, int32_t block) {
    if (!check_data_blk(block)) {
        return 0;
    }

    struct fs_dirent *dirs = calloc(MAX_ENTRIES, sizeof(struct fs_dirent));
    block_read(dirs, block, 1);
    for (int i = 0; i < MAX_ENTRIES; i++) {
        struct fs_dirent *dir_entry = dirs + i;
        if (dir_entry->valid && !strcmp(dir_entry->name, src_name)) {
            memset(dir_entry->name, 0, sizeof(dir_entry));
            strcpy(dir_entry->name, dst_name);
            block_write(dirs, block, 1);
            return 1;
        }
    }

    return 0;
}

int rename_dir(const char *src_name, const char *dst_name, uint32_t inode_no) {
    assert(inode_no >= 1 && inode_no < inode_count);
    struct fs_inode *inode = inode_tbl + inode_no;
    for (int i = 0; i < N_DIRECT; i++) {
        int search_res = search_rename(src_name, dst_name, inode->ptrs[i]);
        if (search_res) {
            return 0;
        }
    }

    if (check_data_blk(inode->indir_1)) {
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks, inode->indir_1, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            int search_res = search_rename(src_name, dst_name, *(blks + i));
            if (search_res) {
                return 0;
            }
        }
    }

    if (check_data_blk(inode->indir_2)) {
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_1, inode->indir_2, 1);
        for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
            int32_t blks_2 = *(blks_1 + i);
            if (check_data_blk(blks_2)) {
                int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
                block_read(blks, blks_2, 1);
                for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
                    int search_res =
                        search_rename(src_name, dst_name, *(blks + j));
                    if (search_res) {
                        return 0;
                    }
                }
            }
        }
    }

    return -ENOENT;
}

int lab3_rename(const char *src_path, const char *dst_path,
                unsigned int flags) {
    uint32_t *inode_no_src = malloc(sizeof(uint32_t));
    *inode_no_src = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens_src[MAX_TOKENS], linebuf_src[1024];
    int n_tokens_src = split_path(src_path, MAX_TOKENS, tokens_src, linebuf_src,
                                  sizeof(linebuf_src));

    int res_src = _getinodeno(n_tokens_src, tokens_src, inode_no_src);
    if (res_src < 0) {
        return res_src;
    }

    uint32_t *inode_no_dst = malloc(sizeof(uint32_t));
    *inode_no_dst = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens_dst[MAX_TOKENS], linebuf_dst[1024];
    int n_tokens_dst = split_path(dst_path, MAX_TOKENS, tokens_dst, linebuf_dst,
                                  sizeof(linebuf_dst));

    int res_dst = _getinodeno(n_tokens_dst, tokens_dst, inode_no_dst);
    if (res_dst < 0) {
        return res_dst;
    }

    if (n_tokens_src != n_tokens_dst) {
        return -EINVAL;
    }

    for (int i = 0; i < n_tokens_src - 1; i++) {
        if (strcmp(tokens_src[i], tokens_dst[i])) {
            return -EINVAL;
        }
    }

    if (!strcmp(tokens_src[n_tokens_src - 1], tokens_dst[n_tokens_src - 1])) {
        return 0;
    }

    struct fs_inode *inode_dst = inode_tbl + *inode_no_dst;
    if (S_ISDIR(inode_dst->mode)) {
        int check_empty_res = check_if_empty(*inode_no_dst);
        if (check_empty_res < 0) {
            return check_empty_res;
        }
        lab3_rmdir(dst_path);
    } else {
        // if dst is a file
        lab3_unlink(dst_path);
    }

    // change the name of parent dir entry
    uint32_t *inode_p = malloc(sizeof(uint32_t));
    *inode_p = 1;
    _getinodeno(n_tokens_src - 1, tokens_src, inode_p);

    return rename_dir(tokens_src[n_tokens_src - 1],
                      tokens_dst[n_tokens_src - 1], *inode_p);
}

int lab3_truncate(const char *path, off_t new_len, struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;

    if (S_ISDIR(inode->mode)) {
        return -EISDIR;
    }

    if (new_len != 0) {
        return -EINVAL;
    }

    free_file_blk(*inode_no);

    return 0;
}

void *write_blk_file(uint32_t blk, char *buf) {
    if (!check_data_blk(blk)) {
        return NULL;
    }

    block_write(buf, blk, 1);
    return 0;
}

int write_file(struct fs_inode *inode, const char *buf, off_t offset,
               size_t len) {
    char *out_buffer = calloc(BLOCK_SIZE, 1);

    uint32_t count = 0, start_block, end_block, bytes_written, off_start;
    int32_t allocated_data_blk;
    bytes_written = len;
    off_start = (offset % BLOCK_SIZE);
    start_block = offset / BLOCK_SIZE;
    end_block = (offset + len) / BLOCK_SIZE;
    bool read_done = false;

    // Read direct pointer block data into buffer
    for (int i = 0; i < N_DIRECT; i++) {
        if (count >= start_block && count <= end_block) {

            if (inode->ptrs[i] == 0) {
                allocated_data_blk = allocate_data_blk();
                if (allocated_data_blk < 0)
                    return allocated_data_blk;
                inode->ptrs[i] = allocated_data_blk;
                write_inode_tbl_back();
                read_done = true;
            }
            char *read_buffer;
            if (!read_done) {
                read_buffer = read_blk_file(inode->ptrs[i]);
                memcpy(out_buffer, read_buffer, off_start);
                if (len < BLOCK_SIZE - off_start) {
                    memcpy(out_buffer + off_start, buf + bytes_written - len,
                           len);
                    memcpy(out_buffer + off_start + len,
                           read_buffer + off_start + len,
                           BLOCK_SIZE - off_start - len);
                    len -= len;
                } else {
                    memcpy(out_buffer + off_start, buf + bytes_written - len,
                           BLOCK_SIZE - off_start);
                    len -= BLOCK_SIZE - off_start;
                }
                read_done = true;
            } else {
                if (count == end_block) {
                    read_buffer = read_blk_file(inode->ptrs[i]);
                }
                if (len >= 0 && len < BLOCK_SIZE) {
                    memcpy(out_buffer, buf + bytes_written - len, len);
                    memcpy(out_buffer + len, read_buffer + len,
                           BLOCK_SIZE - len);
                    len -= len;
                } else {
                    memcpy(out_buffer, buf + bytes_written - len, BLOCK_SIZE);
                    len -= BLOCK_SIZE;
                }
            }

            write_blk_file(inode->ptrs[i], out_buffer);
            memset(out_buffer, 0, BLOCK_SIZE);
        }

        if (count > end_block) {
            if (offset + bytes_written > inode->size)
                inode->size = offset + bytes_written;
            write_inode_tbl_back();
            return bytes_written;
        }

        count++;
    }

    /* Write to free directory entry in indirect pointers */
    // if indir_1 not exists, create first

    if (!check_data_blk(inode->indir_1)) {
        int blk_no = allocate_data_blk();
        if (blk_no < 0) {
            return blk_no;
        }
        int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_write(blks, blk_no, 1);
        inode->indir_1 = blk_no;
        write_inode_tbl_back();
    }
    int32_t *blks = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
    block_read(blks, inode->indir_1, 1);
    for (int i = 0; i < MAX_BLKS_IN_BLK; i++) {
        if (count >= start_block && count <= end_block) {

            if (*(blks + i) == 0) {
                allocated_data_blk = allocate_data_blk();
                if (allocated_data_blk < 0)
                    return allocated_data_blk;
                *(blks + i) = allocated_data_blk;
                block_write(blks, inode->indir_1, 1);
                read_done = true;
            }
            char *read_buffer;
            if (!read_done) {
                read_buffer = read_blk_file(*(blks + i));
                memcpy(out_buffer, read_buffer, off_start);
                if (len < BLOCK_SIZE - off_start) {
                    memcpy(out_buffer + off_start, buf + bytes_written - len,
                           len);
                    memcpy(out_buffer + off_start + len,
                           read_buffer + off_start + len,
                           BLOCK_SIZE - off_start - len);
                    len -= len;
                } else {
                    memcpy(out_buffer + off_start, buf + bytes_written - len,
                           BLOCK_SIZE - off_start);
                    len -= BLOCK_SIZE - off_start;
                }
                read_done = true;
            } else {
                if (count == end_block) {
                    read_buffer = read_blk_file(*(blks + i));
                }
                if (len >= 0 && len < BLOCK_SIZE) {
                    memcpy(out_buffer, buf + bytes_written - len, len);
                    memcpy(out_buffer + len, read_buffer + len,
                           BLOCK_SIZE - len);
                    len -= len;
                } else {
                    memcpy(out_buffer, buf + bytes_written - len, BLOCK_SIZE);
                    len -= BLOCK_SIZE;
                }
            }

            write_blk_file(*(blks + i), out_buffer);
            memset(out_buffer, 0, BLOCK_SIZE);
        }

        if (count > end_block) {
            if (offset + bytes_written > inode->size)
                inode->size = offset + bytes_written;
            write_inode_tbl_back();
            return bytes_written;
        }

        count++;
    }
    block_write(blks, inode->indir_1, 1);

    /* Write to free directory entry in double indirect pointers */
    // if indir_2 not exists, create first
    if (!check_data_blk(inode->indir_2) && count < end_block) {
        int32_t blk1_no = allocate_data_blk();
        if (blk1_no < 0) {
            return blk1_no;
        }
        int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_write(blks_1, blk1_no, 1);
        inode->indir_2 = blk1_no;
        write_inode_tbl_back();
    }
    int32_t *blks_1 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
    block_read(blks_1, inode->indir_2, 1);
    for (int j = 0; j < MAX_BLKS_IN_BLK; j++) {
        // if *(blks_1 + j) not exists, create first
        if (!check_data_blk(*(blks_1 + j))) {
            int32_t blk2_no = allocate_data_blk();
            if (blk2_no < 0) {
                return blk2_no;
            }
            int32_t *blks_2 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
            block_write(blks_2, blk2_no, 1);
            *(blks_1 + j) = blk2_no;
            block_write(blks_1, inode->indir_2, 1);
        }
        int32_t *blks_2 = calloc(MAX_BLKS_IN_BLK, sizeof(int32_t));
        block_read(blks_2, *(blks_1 + j), 1);
        for (int k = 0; k < MAX_BLKS_IN_BLK; k++) {
            if (count >= start_block && count <= end_block) {

                if (*(blks_2 + k) == 0) {
                    allocated_data_blk = allocate_data_blk();
                    if (allocated_data_blk < 0)
                        return allocated_data_blk;
                    *(blks_2 + k) = allocated_data_blk;
                    block_write(blks_2, *(blks_1 + j), 1);
                    read_done = true;
                }
                char *read_buffer;
                if (!read_done) {
                    read_buffer = read_blk_file(*(blks_2 + k));
                    memcpy(out_buffer, read_buffer, off_start);
                    if (len < BLOCK_SIZE - off_start) {
                        memcpy(out_buffer + off_start,
                               buf + bytes_written - len, len);
                        memcpy(out_buffer + off_start + len,
                               read_buffer + off_start + len,
                               BLOCK_SIZE - off_start - len);
                        len -= len;
                    } else {
                        memcpy(out_buffer + off_start,
                               buf + bytes_written - len,
                               BLOCK_SIZE - off_start);
                        len -= BLOCK_SIZE - off_start;
                    }
                    read_done = true;
                } else {
                    if (count == end_block) {
                        read_buffer = read_blk_file(*(blks_2 + k));
                    }
                    if (len >= 0 && len < BLOCK_SIZE) {
                        memcpy(out_buffer, buf + bytes_written - len, len);
                        memcpy(out_buffer + len, read_buffer + len,
                               BLOCK_SIZE - len);
                        len -= len;
                    } else {
                        memcpy(out_buffer, buf + bytes_written - len,
                               BLOCK_SIZE);
                        len -= BLOCK_SIZE;
                    }
                }

                write_blk_file(*(blks_2 + k), out_buffer);
                memset(out_buffer, 0, BLOCK_SIZE);
            }

            if (count > end_block) {
                if (offset + bytes_written > inode->size)
                    inode->size = offset + bytes_written;
                write_inode_tbl_back();
                return bytes_written;
            }

            count++;
        }
        block_write(blks_2, *(blks_1 + j), 1);
    }

    return -ENOSPC;
}

int lab3_write(const char *path, const char *buf, size_t len, off_t offset,
               struct fuse_file_info *fi) {
    uint32_t *inode_no = malloc(sizeof(uint32_t));
    *inode_no = 1; // initially, starts from root dir
    /* Read tokens from path through parser */
    char *tokens[MAX_TOKENS], linebuf[1024];
    int n_tokens =
        split_path(path, MAX_TOKENS, tokens, linebuf, sizeof(linebuf));

    int res = _getinodeno(n_tokens, tokens, inode_no);
    if (res < 0) {
        return res;
    }

    struct fs_inode *inode = inode_tbl + *inode_no;

    if (offset > inode->size)
        return -EINVAL;

    res = write_file(inode, buf, offset, len);

    return res;
}

struct fuse_operations fs_ops = {
    .init = lab3_init,
    .getattr = lab3_getattr,
    .readdir = lab3_readdir,
    .read = lab3_read,
    .create = lab3_create,
    .mkdir = lab3_mkdir,
    .unlink = lab3_unlink,
    .rmdir = lab3_rmdir,
    .rename = lab3_rename,
    .chmod = lab3_chmod,
    .truncate = lab3_truncate,
    .write = lab3_write,
    .utimens = lab3_utimens,
};
