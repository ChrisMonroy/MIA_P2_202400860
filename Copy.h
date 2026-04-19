#ifndef COPY_H
#define COPY_H

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
#include "BloqueArchivos.h"
#include "Journaling.h"
#include "utils.h"

// Función auxiliar: Leer contenido de archivo
std::string readFileContent(std::fstream& file, const SuperBloque& sb, Inodos& fileInode) {
    std::string content = "";
    
    // Bloques directos
    for (int i = 0; i < 12 && fileInode.i_block[i] != -1; i++) {
        BloqueArchivos block;
        file.seekg(sb.s_block_start + fileInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
        content += std::string(block.b_content);
    }
    
    // Bloque simple indirecto
    if (fileInode.i_block[12] != -1) {
        BloqueApuntadores pointers;
        file.seekg(sb.s_block_start + fileInode.i_block[12] * sizeof(BloqueApuntadores), std::ios::beg);
        file.read(reinterpret_cast<char*>(&pointers), sizeof(BloqueApuntadores));
        
        for (int i = 0; i < 16 && pointers.b_pointers[i] != -1; i++) {
            BloqueArchivos block;
            file.seekg(sb.s_block_start + pointers.b_pointers[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            content += std::string(block.b_content);
        }
    }
    
    return content;
}

std::string Copy(const std::string& input) {
    try {
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        
        std::string file1 = "", file2 = "";
        
        // Parseo de parámetros
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            std::string tokenLower = toLowerCase(token);
            
            if (tokenLower.find("-file1=") == 0) {
                file1 = token.substr(7);
                if (file1.size() >= 2 && file1.front() == '"' && file1.back() == '"') {
                    file1 = file1.substr(1, file1.size() - 2);
                }
            }
            else if (tokenLower.find("-file2=") == 0) {
                file2 = token.substr(7);
                if (file2.size() >= 2 && file2.front() == '"' && file2.back() == '"') {
                    file2 = file2.substr(1, file2.size() - 2);
                }
            }  else if (tokenLower.find("-path=") == 0) {
        file1 = token.substr(6);
        }
        else if (tokenLower.find("-destino=") == 0) {
        file2 = token.substr(9);
        }
            if (space == std::string::npos) break;
            start = space + 1;
        }


        
        if (file1.empty() || file2.empty()) {
            return "Error: -file1 y -file2 son obligatorios para copy";
        }
        
        // Obtener información de sesión
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
        
        ///Buscamos el archivo
        int srcInodeIndex = findInodeByPath(file, sb, file1);
        
        if (srcInodeIndex == -1) {
            file.close();
            return "Error: Archivo origen '" + file1 + "' no encontrado";
        }
        
        // Leer inodo origen
        Inodos srcInode;
        file.seekg(sb.s_inode_start + srcInodeIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&srcInode), sizeof(Inodos));
        
        // Verificar que sea archivo
        if (srcInode.i_type != '0') {
            file.close();
            return "Error: '" + file1 + "' no es un archivo";
        }
        
        std::string destPath = file2;
        std::string fileName = file2;
        
        size_t lastSlash = file2.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash > 0) {
            destPath = file2.substr(0, lastSlash);
            fileName = file2.substr(lastSlash + 1);
        } else {
            destPath = "/";
        }
        
        int destDirIndex = findInodeByPath(file, sb, destPath);
        
        if (destDirIndex == -1) {
            file.close();
            return "Error: Directorio destino '" + destPath + "' no existe";
        }
        
        // Verificar que sea directorio
        Inodos destDir;
        file.seekg(sb.s_inode_start + destDirIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&destDir), sizeof(Inodos));
        
        if (destDir.i_type != '1') {
            file.close();
            return "Error: '" + destPath + "' no es un directorio";
        }
        
        //Verifica espacio libre
        if (sb.s_free_inodes_count <= 0) {
            file.close();
            return "Error: No hay inodos disponibles";
        }
        
        int blocksNeeded = (srcInode.i_s + 63) / 64;
        if (blocksNeeded == 0) blocksNeeded = 1;
        
        if (sb.s_free_blocks_count < blocksNeeded) {
            file.close();
            return "Error: No hay bloques disponibles";
        }
        
        //Se lee el contenido del archivo original
        std::string fileContent = readFileContent(file, sb, srcInode);
        
        //Crea el Inodo para el nuevo archivo
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
        
        //Encuentra los bloques libres
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
        
        //Inodo de Archivo Copia
        Inodos newInode;
        memset(&newInode, 0, sizeof(Inodos));
        
        newInode.i_uid = sessionUid;
        newInode.i_gid = sessionGid;
        newInode.i_s = srcInode.i_s;  
        newInode.i_atime = time(nullptr);
        newInode.i_ctime = time(nullptr);
        newInode.i_mtime = time(nullptr);
        newInode.i_type = '0';  // Archivo
        strncpy(newInode.i_perm, srcInode.i_perm, sizeof(newInode.i_perm) - 1); 
        
        for (int i = 0; i < 15; i++) {
            newInode.i_block[i] = -1;
        }
        for (int i = 0; i < 12 && i < (int)freeBlocks.size(); i++) {
            newInode.i_block[i] = freeBlocks[i];
        }
        
        file.seekp(sb.s_inode_start + freeInodeIndex * sizeof(Inodos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&newInode), sizeof(Inodos));
        
        //Escribe contenido de los blques
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
        
        if (!addEntryToDirectory(file, sb, destDirIndex, fileName, freeInodeIndex, bitmapByte)) {
            file.close();
            return "Error: Directorio destino lleno";
        }
        
        //Actualizan el SuperBloque
        sb.s_free_inodes_count--;
        sb.s_free_blocks_count -= freeBlocks.size();
        sb.s_mtime = time(nullptr);
        
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.close();

        // Después de copiar exitosamente:
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "COPY_FILE",
        file2,  // Ruta de destino
        "Desde: " + file1  // Ruta de origen
        );
}
        
        std::ostringstream result;
        result << "----- COPY -----\n";
        result << "Archivo copiado exitosamente\n";
        result << "  Origen: " << file1 << "\n";
        result << "  Destino: " << file2 << "\n";
        result << "  Tamaño: " << srcInode.i_s << " bytes\n";
        result << "  Nuevo Inodo: " << freeInodeIndex;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en copy: ") + e.what();
    }
}

#endif