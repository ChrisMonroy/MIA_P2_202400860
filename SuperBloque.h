#ifndef SUPERBLOQUE_H
#define SUPERBLOQUE_H
#include <ctime>
#include <iostream>
#include <fstream>
#include <ctime>

struct SuperBloque {
    int s_filesystem_type; //El 2 EXT2 y 3 EXT3
    int s_inodes_count;
    int s_blocks_count;
    int s_free_blocks_count;
    int s_free_inodes_count;
    time_t s_mtime;
    time_t s_umtime;
    int s_mnt_count;
    int s_magic;
    int s_inode_s;
    int s_block_s;
    int s_firts_ino;
    int s_first_blo;
    int s_bm_inode_start;
    int s_bm_block_start;
    int s_inode_start;
    int s_block_start;
    int s_journal_start;
    int s_journal_count;  
};
#endif