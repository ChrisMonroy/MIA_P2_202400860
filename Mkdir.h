#ifndef MKDIR_H
#define MKDIR_H

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
#include "utils.h"

// Buscar entrada libre en un bloque de directorio
int findFreeSlot(BloqueCarpeta& block) {
    for (int i = 0; i < 4; i++) {
        if (block.b_content[i].b_inodo == -1) {
            return i;
        }
    }
    return -1;
}

std::string Mkdir(const std::string& input) {
    try {
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }

        std::string path = "";
        bool createParents = false;

        // Parseo
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
            else if (token == "-p") {
                createParents = true;
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }

        if (path.empty()) {
            return "Error: -path es obligatorio para mkdir";
        }

        // Obtener info de sesión
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        int sessionUid = getSessionUID();
        int sessionGid = getSessionGID();

        if (partitionPath.empty()) {
            return "Error: No hay información de partición en la sesión";
        }

        // Abrir disco
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }

        // Leer MBR y SuperBloque
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

        // Parsear ruta en partes
        std::string cleanPath = path;
        if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
        }

        std::vector<std::string> parts;
        std::string partName;
        std::istringstream iss(cleanPath);

        while (std::getline(iss, partName, '/')) {
            if (!partName.empty()) {
            parts.push_back(partName); 
        }
    }

        if (parts.empty()) {
            file.close();
            return "Error: Ruta inválida";
        }

        //Creamos la ruta
        int currentInodeIndex = 0;  // Empezar desde raíz
        int lastCreatedInode = -1;
        int lastCreatedBlock = -1;
        char bitmapByte;

        for (size_t i = 0; i < parts.size(); i++) {
            //Leemos el direcotrio acual
            Inodos currentInode;
            file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inodos));
            
            if (currentInode.i_type != '1') {
                file.close();
                return "Error: '" + parts[i] + "' no es un directorio";
            }
            
            //Busca si existe
            int existingInode = -1;
            
            // Buscar en todos los bloques del directorio
            for (int b = 0; b < 12 && currentInode.i_block[b] != -1; b++) {
                BloqueCarpeta dirBlock;
                file.seekg(sb.s_block_start + currentInode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
                file.read(reinterpret_cast<char*>(&dirBlock), sizeof(BloqueCarpeta));
                
                for (int j = 0; j < 4; j++) {
                    if (dirBlock.b_content[j].b_inodo == -1) continue;
                    
                    std::string entryName = dirBlock.b_content[j].b_name;
                    entryName.erase(std::remove(entryName.begin(), entryName.end(), '\0'), entryName.end());
                    
                    if (entryName == parts[i]) {
                        existingInode = dirBlock.b_content[j].b_inodo;
                        break;
                    }
                }
                
                if (existingInode != -1) break;
            }
            
            if (existingInode != -1) {
                // La entrada ya existe, verificar que sea directorio
                Inodos existingInodeData;
                file.seekg(sb.s_inode_start + existingInode * sizeof(Inodos), std::ios::beg);
                file.read(reinterpret_cast<char*>(&existingInodeData), sizeof(Inodos));
                
                if (existingInodeData.i_type != '1') {
                    file.close();
                    return "Error: '" + parts[i] + "' ya existe pero no es un directorio";
                }
                
                currentInodeIndex = existingInode;
                continue;  // Pasar a la siguiente parte
            }
            
            //Crea un nuevo directorio
            if (!createParents && i > 0) {
                file.close();
                return "Error: No existen las carpetas padres. Use -p para crearlas";
            }

            // Verificar espacio libre
            if (sb.s_free_inodes_count <= 0) {
                file.close();
                return "Error: No hay inodos disponibles";
            }
            
            if (sb.s_free_blocks_count <= 0) {
                file.close();
                return "Error: No hay bloques disponibles";
            }

            //Encontrar el Inodo Libre
            int freeInodeIndex = -1;
            
            for (int inode = 0; inode < sb.s_inodes_count; inode++) {
                int byteIndex = inode / 8;
                int bitIndex = inode % 8;
                
                file.seekg(sb.s_bm_inode_start + byteIndex, std::ios::beg);
                file.read(&bitmapByte, 1);
                
                if ((bitmapByte & (1 << bitIndex)) == 0) {
                    freeInodeIndex = inode;
                    // Marcar como ocupado
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

            //Buscamos el Bloque Libre
            int freeBlockIndex = -1;
            
            for (int block = 0; block < sb.s_blocks_count; block++) {
                int byteIndex = block / 8;
                int bitIndex = block % 8;
                
                file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                file.read(&bitmapByte, 1);
                
                if ((bitmapByte & (1 << bitIndex)) == 0) {
                    freeBlockIndex = block;
                    // Marcar como ocupado
                    bitmapByte |= (1 << bitIndex);
                    file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    file.write(&bitmapByte, 1);
                    break;
                }
            }

            if (freeBlockIndex == -1) {
                file.close();
                return "Error: No se encontró bloque libre";
            }

            //Creacion del Inodo
            Inodos newInode;
            memset(&newInode, 0, sizeof(Inodos));
            newInode.i_uid = sessionUid;
            newInode.i_gid = sessionGid;
            newInode.i_s = 0;
            newInode.i_atime = time(nullptr);
            newInode.i_ctime = time(nullptr);
            newInode.i_mtime = time(nullptr);
            newInode.i_type = '1';  // Directorio
            strncpy(newInode.i_perm, "664", sizeof(newInode.i_perm) - 1);
            
            for (int j = 0; j < 15; j++) {
                newInode.i_block[j] = -1;
            }
            newInode.i_block[0] = freeBlockIndex;

            // Escribir inodo
            file.seekp(sb.s_inode_start + freeInodeIndex * sizeof(Inodos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&newInode), sizeof(Inodos));

            //Crea el Bloque
            BloqueCarpeta newDirBlock;
            memset(&newDirBlock, 0, sizeof(BloqueCarpeta));
            
            for (int j = 0; j < 4; j++) {
                newDirBlock.b_content[j].b_inodo = -1;
            }
            
            // Entrada "." (apunta a sí misma)
            strncpy(newDirBlock.b_content[0].b_name, ".", 12);
            newDirBlock.b_content[0].b_inodo = freeInodeIndex;
            
            // Entrada ".." (apunta al padre)
            strncpy(newDirBlock.b_content[1].b_name, "..", 12);
            newDirBlock.b_content[1].b_inodo = currentInodeIndex;
            
            // Entradas vacías
            newDirBlock.b_content[2].b_inodo = -1;
            newDirBlock.b_content[3].b_inodo = -1;

            // Escribir bloque del nuevo directorio
            file.seekp(sb.s_block_start + freeBlockIndex * sizeof(BloqueCarpeta), std::ios::beg);
            file.write(reinterpret_cast<char*>(&newDirBlock), sizeof(BloqueCarpeta));


            if (!addEntryToDirectory(file, sb, currentInodeIndex, parts[i], freeInodeIndex, bitmapByte)) {
                file.close();
                return "Error: No se pudo agregar entrada al directorio padre";
            }

            //Actualiza el super bloque (Sino se queda y no aumenta el contador)
            sb.s_free_inodes_count--;
            sb.s_free_blocks_count--;
            sb.s_mtime = time(nullptr);
            file.seekp(partStart, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

            lastCreatedInode = freeInodeIndex;
            lastCreatedBlock = freeBlockIndex;
            currentInodeIndex = freeInodeIndex;  // Moverse al nuevo directorio
        }

        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "CREATE_DIR",
        path,
        "Directorio creado"
        );
        }

        file.close();

        std::ostringstream result;
        result << "----- MKDIR -----\n";
        result << "Carpeta creada exitosamente\n";
        result << "  Ruta: " << path << "\n";
        result << "  Inodo: " << lastCreatedInode << "\n";
        result << "  Bloque: " << lastCreatedBlock;

        return result.str();

    } catch (const std::exception& e) {
        return std::string("Error en mkdir: ") + e.what();
    }
}

#endif