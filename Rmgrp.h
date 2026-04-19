#ifndef RMGRP_H
#define RMGRP_H

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
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "Login.h"
#include "globals.h"
#include "Journaling.h"

std::string Rmgrp(const std::string& input) {
    try {
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        if (!isRootUser()) {
            return "Error: Solo el usuario root puede eliminar grupos";
        }
        
        std::string name = "";
        
        // Parseo
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-name=") == 0) {
                name = token.substr(6);
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                    name = name.substr(1, name.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        if (name.empty()) {
            return "Error: -name es obligatorio";
        }
        
        if (name == "root") {
            return "Error: No se puede eliminar el grupo root";
        }
        
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición en la sesión";
        }
        
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
        

        std::string usersContent = "";
        Inodos usersInode;
        file.seekg(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        
        std::vector<int> oldBlocks;
        for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
            BloqueArchivos block;
            file.seekg(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            usersContent += std::string(block.b_content);
            oldBlocks.push_back(usersInode.i_block[i]);
        }
        

        std::string newContent = "";
        bool found = false;
        
        std::istringstream stream(usersContent);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lineStream(line);
            
            while (std::getline(lineStream, field, ',')) {
                fields.push_back(field);
            }
            
            if (fields.size() >= 3) {
                std::string type = fields[1];
                std::string groupName = fields[2];
                
                if (type == "G" && groupName == name && fields[0] != "0") {
                    newContent += "0,G," + groupName + "\n";
                    found = true;
                } else {
                    newContent += line + "\n";
                }
            } else {
                newContent += line + "\n";
            }
        }
        
        if (!found) {
            file.close();
            return "Error: No existe el grupo '" + name + "'";
        }
        
        
        // Limpiar bloques anteriores
        for (int blockIndex : oldBlocks) {
            BloqueArchivos emptyBlock;
            memset(&emptyBlock, 0, sizeof(BloqueArchivos));
            file.seekp(sb.s_block_start + blockIndex * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&emptyBlock), sizeof(BloqueArchivos));
        }
        
        // Resetear inodo
        for (int i = 0; i < 15; i++) {
            usersInode.i_block[i] = -1;
        }
        
        // Escribir nuevo contenido
        int blockIndex = 0;
        size_t pos = 0;
        int blocksUsed = 0;
        char bitmapByte;
        
        while (pos < newContent.length() && blockIndex < 12) {
            // Buscar bloque libre
            int freeBlock = -1;
            for (int i = 0; i < sb.s_blocks_count; i++) {
                int byteIndex = i / 8;
                int bitIndex = i % 8;
                file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                file.read(&bitmapByte, 1);
                if ((bitmapByte & (1 << bitIndex)) == 0) {
                    freeBlock = i;
                    bitmapByte |= (1 << bitIndex);
                    file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    file.write(&bitmapByte, 1);
                    break;
                }
            }
            
            if (freeBlock == -1) break;
            
            BloqueArchivos block;
            memset(&block, 0, sizeof(BloqueArchivos));
            
            size_t chunkSize = std::min(size_t(63), newContent.length() - pos);
            strncpy(block.b_content, newContent.c_str() + pos, chunkSize);
            
            usersInode.i_block[blockIndex] = freeBlock;
            
            file.seekp(sb.s_block_start + freeBlock * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            
            pos += chunkSize;
            blockIndex++;
            blocksUsed++;
        }
        
        // Actualizar inodo
        usersInode.i_s = newContent.length();
        usersInode.i_mtime = time(nullptr);
        file.seekp(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        
        // Actualizar superbloque
        sb.s_free_blocks_count -= blocksUsed;
        sb.s_mtime = time(nullptr);
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.close();

        // Después de eliminar el grupo:
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "DELETE_GROUP",
        "/users.txt",
        "Grupo eliminado: " + name
    );
}
        
        std::ostringstream result;
        result << "----- RMGRP ------\n";
        result << "Grupo eliminado exitosamente\n";
        result << "  Nombre: " << name;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en rmgrp: ") + e.what();
    }
}

#endif