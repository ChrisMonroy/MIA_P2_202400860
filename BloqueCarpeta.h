#ifndef BLOQUECARPETA_H
#define BLOQUECARPETA_H
#include <iostream>
#include <fstream>

#pragma pack(push, 1)
struct B_content{
    char b_name[12];
    int b_inodo;
};

struct BloqueCarpeta{
    B_content b_content[4];
};
#pragma pack(pop)

#endif