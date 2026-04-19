#ifndef CAT_H
#define CAT_H

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
#include "utils.h"
#include "globals.h"

// Validar permisos de lectura
bool hasReadPermission(const std::string& perm, int fileUid, int fileGid) {
    if (isRootUser()) return true;
    
    int sessionUid = getSessionUID();
    int sessionGid = getSessionGID();
    
    if (perm.length() < 3) return false;
    
    int userPerm = perm[0] - '0';
    int groupPerm = perm[1] - '0';
    int otherPerm = perm[2] - '0';
    
    if (sessionUid == fileUid) return (userPerm & 4) != 0;
    else if (sessionGid == fileGid) return (groupPerm & 4) != 0;
    else return (otherPerm & 4) != 0;
}

// Buscar entrada en un directorio (revisa todos sus bloques)
int findEntryInDirectory(std::fstream& file, const SuperBloque& sb, 
                         int dirInodeIndex, const std::string& name) {
    
    std::cerr << "[DEBUG] Buscando '" << name << "' en directorio inodo " << dirInodeIndex << std::endl;
    
    Inodos dirInode;
    file.seekg(sb.s_inode_start + dirInodeIndex * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&dirInode), sb.s_inode_s);
    
    std::cerr << "[DEBUG] i_type=" << (int)dirInode.i_type 
              << ", i_block[0]=" << dirInode.i_block[0] << std::endl;
    
    if (dirInode.i_type != '1') {
        std::cerr << "[DEBUG] No es directorio, retornando -1" << std::endl;
        return -1;
    }
    
    for (int b = 0; b < 12 && dirInode.i_block[b] != -1; b++) {
        std::cerr << "[DEBUG] Leyendo bloque " << dirInode.i_block[b] << std::endl;
        
        BloqueCarpeta dirBlock;
        file.seekg(sb.s_block_start + dirInode.i_block[b] * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&dirBlock), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (dirBlock.b_content[j].b_inodo == -1) continue;
            
            std::string entryName = dirBlock.b_content[j].b_name;
            entryName.erase(std::remove(entryName.begin(), entryName.end(), '\0'), entryName.end());
            
            std::cerr << "[DEBUG] Entrada[" << j << "]: '" << entryName << "' -> inodo " 
                      << dirBlock.b_content[j].b_inodo << std::endl;
            
            if (entryName == name) {
                std::cerr << "[DEBUG] ¡ENCONTRADO!" << std::endl;
                return dirBlock.b_content[j].b_inodo;
            }
        }
    }
    
    std::cerr << "[DEBUG] No encontrado :(" << std::endl;
    return -1;
}

// Buscar inodo por ruta
bool findInodeByPath(std::fstream& file, const SuperBloque& sb, 
                     const std::string& path, int& inodeIndex, Inodos& inode) {
    
    std::string cleanPath = path;
    
    // Caso raíz
    if (cleanPath.empty() || cleanPath == "/") {
        inodeIndex = 0;
        file.seekg(sb.s_inode_start, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
        return true;
    }
    
    // Normalizar ruta
    if (cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
    }
    
    // Tokenizar ruta
    std::vector<std::string> parts;
    std::string part;
    std::istringstream iss(cleanPath);
    
    while (std::getline(iss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    if (parts.empty()) {
        return false;
    }
    
    // Recorrer componentes de la ruta
    int currentInodeIndex = 0;
    
    for (size_t i = 0; i < parts.size(); i++) {
        // Buscar entrada en el directorio actual
        int foundInode = findEntryInDirectory(file, sb, currentInodeIndex, parts[i]);
        
        if (foundInode == -1) {
            return false;  // Parte de la ruta no encontrada
        }
        
        // Verificar que sea directorio (si no es la última parte)
        if (i < parts.size() - 1) {
            Inodos nextInode;
            file.seekg(sb.s_inode_start + foundInode * sb.s_inode_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&nextInode), sb.s_inode_s);
            
            if (nextInode.i_type != '1') {
                return false;  // No es directorio
            }
        }
        
        currentInodeIndex = foundInode;
    }
    
    // Obtener inodo final
    inodeIndex = currentInodeIndex;
    file.seekg(sb.s_inode_start + inodeIndex * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    return true;
}

std::string Cat(const std::string& input) {
    try {
        // Validar sesión
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        
        // Parsear archivos
        std::vector<std::string> files;
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            std::string tokenLower = toLowerCase(token);
            
            if (tokenLower.find("-file") == 0) {
                size_t eqPos = tokenLower.find('=');
                if (eqPos != std::string::npos) {
                    std::string filePath = token.substr(eqPos + 1);
                    if (filePath.size() >= 2 && filePath.front() == '"' && filePath.back() == '"') {
                        filePath = filePath.substr(1, filePath.size() - 2);
                    }
                    files.push_back(filePath);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        if (files.empty()) {
            return "Error: Al menos un -fileN es obligatorio para cat";
        }
        
        // Obtener datos de partición
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición";
        }
        
        // Abrir disco
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        // Lectura del MBR
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        // Lectura del superbloque
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        std::ostringstream result;
        result << "----- CAT ------\n";
        
        // Procesar cada archivo
        for (const auto& filePath : files) {
            int inodeIndex = -1;
            Inodos fileInode;
            
            // Buscar inodo
            bool found = findInodeByPath(file, sb, filePath, inodeIndex, fileInode);
            
            if (!found) {
                file.close();
                return "Error: Archivo '" + filePath + "' no encontrado";
            }
            
            // Verificar que sea archivo
            if (fileInode.i_type != '0') {
                file.close();
                return "Error: '" + filePath + "' no es un archivo";
            }
            
            // Validar permisos
            std::string perm = std::string(fileInode.i_perm);
            if (!hasReadPermission(perm, fileInode.i_uid, fileInode.i_gid)) {
                file.close();
                return "Error: No tiene permisos de lectura para '" + filePath + "'";
            }
            
            // Leer contenido
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
            
            if (files.size() > 1) {
                result << "\n--- " << filePath << " ---\n";
            }
            result << content;
            
            // Actualizar atime
            fileInode.i_atime = time(nullptr);
            file.seekp(sb.s_inode_start + inodeIndex * sizeof(Inodos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&fileInode), sizeof(Inodos));
        }
        
        file.close();
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en cat: ") + e.what();
    }
}

#endif