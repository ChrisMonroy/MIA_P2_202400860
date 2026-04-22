#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>
#include <vector>
#include <map>
#include "Mbr.h"
#include "SuperBloque.h"
#include "utils.h"
#include "BloqueCarpeta.h"
#include "BloqueArchivos.h"

struct Usuario {
    std::string nombre;
    int uid;
    int gid;
    std::string particionId;
    
    Usuario() : uid(-1), gid(-1) {}
};


inline int session_uid = -1;
inline int session_gid = -1;
inline std::string session_partition_id = "";
inline std::string session_partition_path = "";
inline int session_partition_index = -1;

inline bool is_logged = false;
inline char current_user[16] = {0};
inline Usuario usuarioActual;
inline std::vector<MountedPartition> mounted_list;
inline int mounted_count = 0;
inline char nextDiskLetter = 'A';
inline std::map<std::string, std::pair<char, int>> diskTracking;

//calcular tamaño real de bloque desde superbloque
inline int getBlockSize(const SuperBloque& sb) {
    if (sb.s_block_s <= 2) {
        return 1024 << sb.s_block_s;
    }
    return sb.s_block_s;
}

#endif