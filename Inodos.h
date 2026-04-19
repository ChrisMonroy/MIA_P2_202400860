#ifndef INODOS_H
#define INODOS_H
#include <ctime>
#include <iostream>
#include <fstream>

struct Inodos{
    int i_uid;
    int i_gid;
    int i_s;
    time_t i_atime;
    time_t i_ctime;
    time_t i_mtime;
    int i_block[15];
    char i_type;
    char i_perm[3];
};
#endif