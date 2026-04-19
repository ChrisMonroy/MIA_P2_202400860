#ifndef MKFILE_H
#define MKFILE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include "Mbr.h"
#include "Mount.h"
#include "Login.h"
#include "globals.h"
#include "BloqueCarpeta.h"
#include "Inodos.h"
#include "SuperBloque.h"
#include "Journaling.h"

// Comando Mkfile
std::string Mkfile(const std::string& input) {
    try {
        // Verificar sesión activa
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa";
        }
        
        std::string path = "", cont = "";
        bool createParents = false;
        int size = 0;

        // Parseo de parámetros
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            std::string tokenLower = toLowerCase(token);
            
            if (tokenLower.find("-path=") == 0) {
                path = token.substr(6);
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
                    path = path.substr(1, path.size() - 2);
                }
            }
            else if (token == "-r") {
                createParents = true;
            }
            else if (tokenLower.find("-size=") == 0) {
                size = std::atoi(token.substr(6).c_str());
            }
            else if (tokenLower.find("-cont=") == 0) {
                cont = token.substr(6);
                if (cont.size() >= 2 && cont.front() == '"' && cont.back() == '"') {
                    cont = cont.substr(1, cont.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        // Validaciones básicas
        if (path.empty()) {
            return "Error: -path es obligatorio";
        }
        
        if (size < 0) {
            return "Error: El tamaño no puede ser negativo";
        }

        // Obtener información de la sesión
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        int sessionUid = getSessionUID();
        int sessionGid = getSessionGID();
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición";
        }

        // Abrir disco
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }

        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

        // Encontrar directorio padre
        std::string fileName = path;
        std::string parentPath = "/";

        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos) {
        fileName = path.substr(lastSlash + 1);
    
        // Determinar ruta padre
        if (lastSlash == 0) {
            parentPath = "/"; 
        } else {
        parentPath = path.substr(0, lastSlash); 
    }
}
        
        int parentInodeIndex = findInodeByPath(file, sb, parentPath);
        
        if (parentInodeIndex == -1) {
            file.close();
            return "Error: El directorio padre '" + parentPath + "' no existe";
        }

        // Verificar espacio libre
        if (sb.s_free_inodes_count <= 0) {
            file.close();
            return "Error: No hay inodos disponibles";
        }
        
        int blocksNeeded = (size + 63) / 64;
        if (blocksNeeded == 0) blocksNeeded = 1;
        
        if (sb.s_free_blocks_count < blocksNeeded) {
            file.close();
            return "Error: No hay bloques disponibles";
        }

        // Encontrar inodo libre
        int freeInodeIndex = -1;
        char bitmapByte;
        
        for (int i = 0; i < sb.s_inodes_count; i++) {
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            file.seekg(sb.s_bm_inode_start + byteIndex, std::ios::beg);
            file.read(&bitmapByte, 1);
            if ((bitmapByte & (1 << bitIndex)) == 0) {
                freeInodeIndex = i;
                bitmapByte |= (1 << bitIndex);
                file.seekp(sb.s_bm_inode_start + byteIndex, std::ios::beg);
                file.write(&bitmapByte, 1);
                break;
            }
        }
        
        if (freeInodeIndex == -1) {
            file.close();
            return "Error: No se encontró inodo libre";
        }

        // Encontrar bloques libres
        std::vector<int> freeBlocks;
        
        for (int i = 0; i < sb.s_blocks_count && (int)freeBlocks.size() < blocksNeeded; i++) {
            int byteIndex = i / 8;
            int bitIndex = i % 8;
            file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
            file.read(&bitmapByte, 1);
            if ((bitmapByte & (1 << bitIndex)) == 0) {
                freeBlocks.push_back(i);
                bitmapByte |= (1 << bitIndex);
                file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                file.write(&bitmapByte, 1);
            }
        }
        
        if ((int)freeBlocks.size() < blocksNeeded) {
            file.close();
            return "Error: No se encontraron bloques libres";
        }

        // Crear inodo del archivo
        Inodos fileInode;
        memset(&fileInode, 0, sizeof(Inodos));
        
        fileInode.i_uid = sessionUid;
        fileInode.i_gid = sessionGid;
        fileInode.i_s = size;
        fileInode.i_atime = time(nullptr);
        fileInode.i_ctime = time(nullptr);
        fileInode.i_mtime = time(nullptr);
        fileInode.i_type = '0'; // Tipo archivo
        strncpy(fileInode.i_perm, "664", sizeof(fileInode.i_perm) - 1);
        
        for (int i = 0; i < 15; i++) {
            fileInode.i_block[i] = -1;
        }
        for (int i = 0; i < 12 && i < (int)freeBlocks.size(); i++) {
            fileInode.i_block[i] = freeBlocks[i];
        }
        
        file.seekp(sb.s_inode_start + freeInodeIndex * sizeof(Inodos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&fileInode), sizeof(Inodos));

        // Escribir contenido en los bloques
        std::string fileContent = cont;
        if (fileContent.empty() && size > 0) {
            for (int i = 0; i < size; i++) {
                fileContent += '0' + (i % 10);
            }
        }
        
        for (size_t i = 0; i < freeBlocks.size() && i < 12; i++) {
            BloqueArchivos block;
            memset(&block, 0, sizeof(BloqueArchivos));
            size_t offset = i * 64;
            size_t chunkSize = std::min(size_t(64), fileContent.length() - offset);
            if (offset < fileContent.length()) {
                strncpy(block.b_content, fileContent.c_str() + offset, chunkSize);
            }
            file.seekp(sb.s_block_start + freeBlocks[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
        }

        // Agregar entrada al directorio padre
        bool added = false;
        
        // Leer inodo del padre
        Inodos parentInode;
        file.seekg(sb.s_inode_start + parentInodeIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&parentInode), sizeof(Inodos));
        
        // Buscar bloque con espacio o crear uno nuevo
        for (int b = 0; b < 12; b++) {
            if (parentInode.i_block[b] == -1) {
                // Crear nuevo bloque
                int newBlockIndex = -1;
                for (int i = 0; i < sb.s_blocks_count; i++) {
                    int byteIndex = i / 8;
                    int bitIndex = i % 8;
                    file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    file.read(&bitmapByte, 1);
                    if ((bitmapByte & (1 << bitIndex)) == 0) {
                        newBlockIndex = i;
                        bitmapByte |= (1 << bitIndex);
                        file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                        file.write(&bitmapByte, 1);
                        break;
                    }
                }
                
                if (newBlockIndex == -1) break;
                
                // Crear bloque de directorio vacío
                BloqueCarpeta newBlock;
                memset(&newBlock, 0, sizeof(BloqueCarpeta));
                for (int j = 0; j < 4; j++) {
                    newBlock.b_content[j].b_inodo = -1;
                }
                
                // Agregar entrada
                strncpy(newBlock.b_content[0].b_name, fileName.c_str(), 12);
                newBlock.b_content[0].b_inodo = freeInodeIndex;
                
                // Escribir bloque
                file.seekp(sb.s_block_start + newBlockIndex * sizeof(BloqueCarpeta), std::ios::beg);
                file.write(reinterpret_cast<char*>(&newBlock), sizeof(BloqueCarpeta));
                
                // Actualizar inodo del padre
                parentInode.i_block[b] = newBlockIndex;
                file.seekp(sb.s_inode_start + parentInodeIndex * sizeof(Inodos), std::ios::beg);
                file.write(reinterpret_cast<char*>(&parentInode), sizeof(Inodos));
                
                added = true;
                sb.s_free_blocks_count--;
                break;
            }
            
            // Bloque existe, verificar si tiene espacio
            BloqueCarpeta block;
            file.seekg(sb.s_block_start + parentInode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
            file.read(reinterpret_cast<char*>(&block), sizeof(BloqueCarpeta));
            
            for (int j = 0; j < 4; j++) {
                if (block.b_content[j].b_inodo == -1) {
                    strncpy(block.b_content[j].b_name, fileName.c_str(), 12);
                    block.b_content[j].b_inodo = freeInodeIndex;
                    
                    file.seekp(sb.s_block_start + parentInode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&block), sizeof(BloqueCarpeta));
                    
                    added = true;
                    break;
                }
            }
            
            if (added) break;
        }
        
        if (!added) {
            file.close();
            return "Error: Directorio padre lleno";
        }

        // Actualizar superbloque
        sb.s_free_inodes_count--;
        sb.s_mtime = time(nullptr);
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.close();

        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
            CommandJournaling::add(
                partitionId,
                "CREATE_FILE",
                path,
                "Size: " + std::to_string(size) + " bytes"
            );
        }
        
        // Mensaje de éxito
        std::ostringstream result;
        result << "----- MKFILE -----\n";
        result << "Archivo creado exitosamente\n";
        result << "  Ruta: " << path << "\n";
        result << "  Tamaño: " << size << " bytes\n";
        result << "  Inodo: " << freeInodeIndex << "\n";
        result << "  Bloques: " << freeBlocks.size();
        
        return result.str();

    } catch (const std::exception& e) {
        return std::string("Error en mkfile: ") + e.what();
    }
}

#endif