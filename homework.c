/*
 * file:        homework.c
 * description: skeleton file for CS 5600 system
 *
 * CS 5600, Computer Systems, Northeastern CCIS
 * Peter Desnoyers, November 2023
 */

#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse3/fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include "fs5600.h"

struct fs_super *super;
unsigned char *block_bmp;
unsigned char *inode_bmp;
struct fs_inode *inode_tbl;
uint32_t inode_count;
uint32_t inode_region_blk;

/* disk access. All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 */
extern int block_read(void *buf, int blknum, int nblks);
extern int block_write(void *buf, int blknum, int nblks);

/* how many buckets of size M do you need to hold N items? 
 */
int div_round_up(int n, int m) {
    return (n + m - 1) / m;
}

/* quick and dirty function to split an absolute path (i.e. begins with "/")
 * uses the same interface as the command line parser in Lab 1
 */
int split_path(const char *path, int argc_max, char **argv, char *buf, int buf_len)
{
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
void inode_2_stat(struct stat *sb, struct fs_inode *in)
{
    memset(sb, 0, sizeof(*sb));
    sb->st_mode = in->mode;
    sb->st_nlink = 1;
    sb->st_uid = in->uid;
    sb->st_gid = in->gid;
    sb->st_size = in->size;
    sb->st_blocks = div_round_up(in->size, BLOCK_SIZE);
    sb->st_atime = sb->st_mtime = sb->st_ctime = in->mtime;
}

void* lab3_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    super = malloc(sizeof(*super));
    memset(super, 0, sizeof(*super));
    
    /* Read super block(0) from disk into *super */
    block_read(super, 0, 1);
    
    block_bmp = malloc(sizeof(*block_bmp));
    memset(block_bmp, 0, sizeof(*block_bmp));
    inode_bmp = malloc(sizeof(*inode_bmp));
    memset(inode_bmp, 0, sizeof(*inode_bmp));
    
    /* Read block bitmap into block_bmp */
    block_read(block_bmp, 1, super->blk_map_len);
    
    /* Read inode bitmap into inode_bmp */
    block_read(inode_bmp, 1 + super->blk_map_len, super->in_map_len);
    
    /* Read inode table into inode_tbl */
    inode_region_blk = 1 + super->blk_map_len + super->in_map_len;
    int inodes_in_blk = BLOCK_SIZE / sizeof(struct fs_inode);
    inode_count = super->inodes_len * inodes_in_blk;
    
    inode_tbl = malloc(inode_count * sizeof(struct fs_inode));
    memset(inode_tbl, 0, sizeof(inode_count * sizeof(struct fs_inode)));
    
    block_read(inode_tbl, inode_region_blk, super->inodes_len);
    	
    /*	
    struct fs_inode inode;
    for(int i = 0; i < inode_count; i++) {
    	inode = *(inode_tbl + i);
    	fprintf(stdout, "uid: %d\n", inode.uid);
    	fprintf(stdout, "gid: %d\n", inode.gid);
    	fprintf(stdout, "mode: %o\n", inode.mode);
    	fprintf(stdout, "mtime: %d\n", inode.mtime);
    	fprintf(stdout, "size: %d\n", inode.size);
    	for(int j = 0; j < n_direct; j++) {
    		fprintf(stdout, "ptrs[%d]: %d\n", j, inode.ptrs[j]);
    	}
    	fprintf(stdout, "indir_1: %d\n", inode.indir_1);
    	fprintf(stdout, "indir_2: %d\n", inode.indir_2); 	   
    	fprintf(stdout, "\n"); 	    	    	    	    	
    }
    */
    
    /* for(int i = 0; i < super->blk_map_len * BLOCK_SIZE; i++) {
    	fprintf(stdout, "block bitmap bit %d is %d\n", i, bit_test(block_bmp, i));
    }
    for(int i = 0; i < super->in_map_len * BLOCK_SIZE; i++) {
    	fprintf(stdout, "inode bitmap bit %d is %d\n", i, bit_test(inode_bmp, i));
    }*/
    
    return NULL;
}

int lab3_getattr(const char *path, struct stat *sb, struct fuse_file_info *fi) {

    /* Read tokens from path through parser */
    const int max_tokens = 32;
    char *tokens[max_tokens], linebuf[1024];
    int count = split_path(path, max_tokens, tokens, linebuf, sizeof(linebuf));
    
    
    struct fs_inode inode;
    int dir_in_blk = BLOCK_SIZE / sizeof(struct fs_dirent);
    char *title = "";
    
    /* Brute force search on all directory entries in the file system */
    for(int i = 0; i < count; i++) {
    	title = "";
    	int j = 0;
loop:
    	while(strcmp(title, tokens[i]) && j < inode_count) {
    		inode = *(inode_tbl + j);
    		struct fs_dirent *dir_entries, dir_entry;
    		dir_entries = malloc(dir_in_blk * sizeof(struct fs_dirent));
    		memset(dir_entries, 0, dir_in_blk * sizeof(struct fs_dirent));
    		int type = (inode.mode & 0777000) >> 9;
    		if(bit_test(inode_bmp, j) && type == 0040) {
    			for (int k = 0; k < N_DIRECT; k++) {
    				if(inode.ptrs[k] >= inode_region_blk) {
    					block_read(dir_entries, inode.ptrs[k], 1);
    					for(int l = 0; l < dir_in_blk; l++) {
    						dir_entry = *(dir_entries + l);
    						if(dir_entry.valid && !strcmp(dir_entry.name, tokens[i])) {
    							title = dir_entry.name;
    							goto loop;
    						}
    					}
    				}
    			}
    		}
    		j++;
    	}
    }
    
    if(strcmp(title, "")) {
    	sb = malloc(sizeof(*sb));
    	memset(sb, 0, sizeof(*sb));
    	sb->st_mode = inode.mode;
    	sb->st_nlink = 1;
    	sb->st_uid = inode.uid;
    	sb->st_gid = inode.gid;
    	sb->st_size = inode.size;
    	sb->st_blocks = div_round_up(inode.size, BLOCK_SIZE);
    	sb->st_atime = sb->st_mtime = sb->st_ctime = inode.mtime;
    } else {
    	return -ENOENT;
    }
    return 0;
}

/* for read-only version you need to implement:
 * - lab3_init
 * - lab3_getattr
 * - lab3_readdir
 * - lab3_read
 *
 * for the full version you need to implement:
 * - lab3_create
 * - lab3_mkdir
 * - lab3_unlink
 * - lab3_rmdir
 * - lab3_rename
 * - lab3_chmod
 * - lab3_truncate
 * - lab3_write
 */

/* operations vector. Please don't rename it, or else you'll break things
 * uncomment fields as you implement them.
 */
struct fuse_operations fs_ops = {
    .init = lab3_init,
    .getattr = lab3_getattr,
//    .readdir = lab3_readdir,
//    .read = lab3_read,

//    .create = lab3_create,
//    .mkdir = lab3_mkdir,
//    .unlink = lab3_unlink,
//    .rmdir = lab3_rmdir,
//    .rename = lab3_rename,
//    .chmod = lab3_chmod,
//    .truncate = lab3_truncate,
//    .write = lab3_write,
};

