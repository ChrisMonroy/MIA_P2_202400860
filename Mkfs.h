#ifndef MKFS_H
#define MKFS_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "utils.h"
#include "globals.h"


// COMANDO MKFS
std::string Mkfs(const std::string& input) {
    try {
        std::string id;
        std::string type = "full";  // Por defecto
        
        // PARSEO DE PARÁMETROS

        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-id=") == 0) {
                id = token.substr(4);
                if (id.size() >= 2 && id.front() == '"' && id.back() == '"') {
                    id = id.substr(1, id.size() - 2);
                }
            }
            else if (token.find("-type=") == 0) {
                type = token.substr(6);
                if (type.size() >= 2 && type.front() == '"' && type.back() == '"') {
                    type = type.substr(1, type.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        // VALIDAR PARÁMETROS

        if (id.empty()) {
            return "Error: -id es obligatorio para mkfs";
        }
        
        std::string formatType = toLowerCase(type);
        if (formatType != "full" && !formatType.empty()) {
            return "Error: type debe ser 'full'";
        }
        
        // BUSCAR PARTICIÓN MONTADA

        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == id) {
                target = &mp;
                break;
            }
        }
        
        if (!target) {
            return "Error: La partición con ID '" + id + "' no está montada";
        }
        
        std::ifstream mbrFile(target->path, std::ios::binary);
        if (!mbrFile.is_open()) {
            return "Error: No se pudo abrir el disco '" + target->path + "'";
        }
        
        MBR mbr;
        mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        mbrFile.close();
        
        // Obtener información de la partición
        Partition& part = mbr.mbr_partitions[target->partition_index];
        long partStart = part.part_start;
        long partSize = part.part_size;
        
        long superSize = sizeof(SuperBloque);
        long inodeSize = sizeof(Inodos);      
        long blockSize = 64;                   
        long bitmapInodeSize = 1;              
        long bitmapBlockSize = 1;              
        

        long denominator = 4 + inodeSize + 3 * blockSize;
        long numerator = partSize - superSize;
        long n = numerator / denominator;
        
        if (n < 2) {
            return "Error: La partición es muy pequeña para crear un sistema de archivos (mínimo 2 inodos)";
        }
        

        // Bitmap se calcula en bits, pero se escribe en bytes
        long bmInodeBytes = (n + 7) / 8;      
        long bmBlockBytes = (3 * n + 7) / 8;   
        
        long bmInodeStart = partStart + superSize;
        long bmBlockStart = bmInodeStart + bmInodeBytes;
        long inodeTableStart = bmBlockStart + bmBlockBytes;
        long blockTableStart = inodeTableStart + (n * inodeSize);
        
        SuperBloque sb;
        memset(&sb, 0, sizeof(SuperBloque));
        
        sb.s_filesystem_type = 2;              
        sb.s_inodes_count = static_cast<int>(n);
        sb.s_blocks_count = static_cast<int>(3 * n);  
        sb.s_free_blocks_count = static_cast<int>(3 * n) - 2;  
        sb.s_free_inodes_count = static_cast<int>(n) - 2;      
        sb.s_mtime = time(nullptr);
        sb.s_umtime = 0;
        sb.s_mnt_count = 1;
        sb.s_magic = 0xEF53;                   
        sb.s_inode_s = static_cast<int>(inodeSize);
        sb.s_block_s = static_cast<int>(blockSize);
        sb.s_firts_ino = 2;                    // Primer inodo libre (0 y 1 ocupados)
        sb.s_first_blo = 2;                    // Primer bloque libre (0 y 1 ocupados)
        sb.s_bm_inode_start = static_cast<int>(bmInodeStart);
        sb.s_bm_block_start = static_cast<int>(bmBlockStart);
        sb.s_inode_start = static_cast<int>(inodeTableStart);
        sb.s_block_start = static_cast<int>(blockTableStart);
        

        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco para escritura";
        }
        
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        char zero = 0;
        
        // Bitmap de inodos
        file.seekp(bmInodeStart, std::ios::beg);
        for (long i = 0; i < bmInodeBytes; i++) {
            file.write(&zero, 1);
        }
        
        // Bitmap de bloques
        file.seekp(bmBlockStart, std::ios::beg);
        for (long i = 0; i < bmBlockBytes; i++) {
            file.write(&zero, 1);
        }
        

        file.seekp(inodeTableStart, std::ios::beg);
        for (long i = 0; i < static_cast<long>(n) * inodeSize; i++) {
            file.write(&zero, 1);
        }

        file.seekp(blockTableStart, std::ios::beg);
        for (long i = 0; i < static_cast<long>(3 * n) * blockSize; i++) {
            file.write(&zero, 1);
        }
        
        Inodos rootInode;
        memset(&rootInode, 0, sizeof(Inodos));
        
        rootInode.i_uid = 1;
        rootInode.i_gid = 1;
        rootInode.i_s = 0;
        rootInode.i_atime = time(nullptr);
        rootInode.i_ctime = time(nullptr);
        rootInode.i_mtime = time(nullptr);
        rootInode.i_type = '1';  
        strncpy(rootInode.i_perm, "664", sizeof(rootInode.i_perm) - 1);
        
        // Inicializar bloques del inodo
        for (int i = 0; i < 15; i++) {
            rootInode.i_block[i] = -1;
        }
        rootInode.i_block[0] = 0; 
        
        file.seekp(inodeTableStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&rootInode), sizeof(Inodos));
        

        BloqueCarpeta rootBlock;
        memset(&rootBlock, 0, sizeof(BloqueCarpeta));
        
        // Entrada (directorio actual)
        strncpy(rootBlock.b_content[0].b_name, ".", 12);
        rootBlock.b_content[0].b_inodo = 0;  // Apunta a sí mismo
        
        // Entrada ".." (directorio padre - en raíz es sí mismo)
        strncpy(rootBlock.b_content[1].b_name, "..", 12);
        rootBlock.b_content[1].b_inodo = 0;
        
        strncpy(rootBlock.b_content[2].b_name, "users.txt", 12);
        rootBlock.b_content[2].b_inodo = 1;  // Apunta al inodo 1
        
        // Entrada 3 vacía
        rootBlock.b_content[3].b_inodo = -1;
        
        file.seekp(blockTableStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&rootBlock), sizeof(BloqueCarpeta));
        
        Inodos usersInode;
        memset(&usersInode, 0, sizeof(Inodos));
        
        const char* usersContent = "1,G,root\n1,U,root,123,root\n";
        int contentLen = strlen(usersContent);
        
        usersInode.i_uid = 1;
        usersInode.i_gid = 1;
        usersInode.i_s = contentLen;
        usersInode.i_atime = time(nullptr);
        usersInode.i_ctime = time(nullptr);
        usersInode.i_mtime = time(nullptr);
        usersInode.i_type = '0'; 
        strncpy(usersInode.i_perm, "664", sizeof(usersInode.i_perm) - 1);
        
        for (int i = 0; i < 15; i++) {
            usersInode.i_block[i] = -1;
        }
        usersInode.i_block[0] = 1;  // Apunta al bloque 1 
        
        file.seekp(inodeTableStart + sizeof(Inodos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        

        BloqueArchivos block;
        memset(&block, 0, sizeof(BloqueArchivos));
        strncpy(block.b_content, usersContent, contentLen);
        
        file.seekp(blockTableStart + sizeof(BloqueCarpeta), std::ios::beg);
        file.write(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
        
        // Bitmap: 0 = libre, 1 = ocupado
        char bitmapByte = 0;
        bitmapByte |= (1 << 0);  // Bit 0
        bitmapByte |= (1 << 1);  // Bit 1
        
        file.seekp(bmInodeStart, std::ios::beg);
        file.write(&bitmapByte, 1);
        
        file.seekp(bmBlockStart, std::ios::beg);
        file.write(&bitmapByte, 1);
        
        file.close();
        
        std::fstream mbrUpdate(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (mbrUpdate.is_open()) {
            MBR mbr2;
            mbrUpdate.read(reinterpret_cast<char*>(&mbr2), sizeof(MBR));
            mbr2.mbr_partitions[target->partition_index].part_status = '1';
            mbrUpdate.seekp(0, std::ios::beg);
            mbrUpdate.write(reinterpret_cast<char*>(&mbr2), sizeof(MBR));
            mbrUpdate.close();
        }
        
        std::ostringstream result;
        result << "----- MKFS -----\n";
        result << "Sistema de archivos EXT2 creado exitosamente\n";
        result << "  ID: " << id << "\n";
        result << "  Disco: " << target->path << "\n";
        result << "  Partición: " << target->name << "\n";
        result << "  Tamaño partición: " << partSize << " bytes\n";
        result << "  Inodos totales: " << n << "\n";
        result << "  Bloques totales: " << (3 * n) << "\n";
        result << "  Inodos libres: " << (n - 2) << "\n";
        result << "  Bloques libres: " << (3 * n - 2) << "\n";
        result << "  Archivo users.txt creado en la raíz";
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en mkfs: ") + e.what();
    }
}

#endif 