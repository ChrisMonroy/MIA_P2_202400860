#ifndef FIND_H
#define FIND_H

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

std::string getInodeName(std::fstream& file, const SuperBloque& sb, int targetInodeIndex) {
    // Buscar en todos los directorios
    Inodos rootInode;
    file.seekg(sb.s_inode_start, std::ios::beg);
    file.read(reinterpret_cast<char*>(&rootInode), sizeof(Inodos));
    
    // BFS para buscar el inodo
    std::vector<std::pair<int, std::string>> queue;
    queue.push_back({0, ""});
    
    std::vector<bool> visited(sb.s_inodes_count, false);
    visited[0] = true;
    
    while (!queue.empty()) {
        auto [currentInodeIndex, currentPath] = queue.front();
        queue.erase(queue.begin());
        
        Inodos currentInode;
        file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inodos));
        
        if (currentInode.i_type != '1') continue;
        
        for (int b = 0; b < 12 && currentInode.i_block[b] != -1; b++) {
            BloqueCarpeta dirBlock;
            file.seekg(sb.s_block_start + currentInode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
            file.read(reinterpret_cast<char*>(&dirBlock), sizeof(BloqueCarpeta));
            
            for (int j = 0; j < 4; j++) {
                if (dirBlock.b_content[j].b_inodo == -1) continue;
                
                std::string name = dirBlock.b_content[j].b_name;
                name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
                
                if (name == "." || name == "..") continue;
                
                int childInodeIndex = dirBlock.b_content[j].b_inodo;
                
                if (childInodeIndex == targetInodeIndex) {
                    return currentPath + "/" + name;
                }
                
                if (!visited[childInodeIndex] && dirBlock.b_content[j].b_inodo != -1) {
                    visited[childInodeIndex] = true;
                    queue.push_back({childInodeIndex, currentPath + "/" + name});
                }
            }
        }
    }
    
    return "";
}

bool matchPattern(const std::string& str, const std::string& pattern){
    if (pattern.empty()) return str.empty();

    if (pattern[0] == '*') {
        for (size_t i = 0; i <= str.length(); i++) {
            if (matchPattern(str.substr(i), pattern.substr(1))) {
                return true;
            }
        }
        return false;
    }

    if (pattern[0] == '?') {
        if (str.empty()) return false;
        return matchPattern(str.substr(1), pattern.substr(1));
    }

    if (str.empty() || str[0] != pattern[0]) return false;
    return matchPattern(str.substr(1), pattern.substr(1));
}

//Funcion auxiliar para buscar direcotrio
void searchDirectory(std::fstream& file, const SuperBloque& sb, int dirInodeIndex, const std::string& currentPath, const std::string& namePattern, char typeFilter, int sizeFilter, std::vector<std::string>& results) {

    Inodos dirInode;
    file.seekg(sb.s_inode_start + dirInodeIndex * sizeof(Inodos), std::ios::beg);
    file.read(reinterpret_cast<char*>(&dirInode), sizeof(Inodos));
    
    if (dirInode.i_type != '1') return;
    
    for (int b = 0; b < 12 && dirInode.i_block[b] != -1; b++) {
        BloqueCarpeta dirBlock;
        file.seekg(sb.s_block_start + dirInode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
        file.read(reinterpret_cast<char*>(&dirBlock), sizeof(BloqueCarpeta));
        
        for (int j = 0; j < 4; j++) {
            if (dirBlock.b_content[j].b_inodo == -1) continue;
            
            std::string name = dirBlock.b_content[j].b_name;
            name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
            
            if (name == "." || name == "..") continue;
            
            int childInodeIndex = dirBlock.b_content[j].b_inodo;
            std::string fullPath = currentPath + "/" + name;
            
            // Leer inodo hijo
            Inodos childInode;
            file.seekg(sb.s_inode_start + childInodeIndex * sizeof(Inodos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&childInode), sizeof(Inodos));
            
            // Aplicar filtros
            bool nameMatch = namePattern.empty() || matchPattern(name, namePattern);
            bool typeMatch = (typeFilter == '\0') || 
                            (typeFilter == 'f' && childInode.i_type == '0') ||
                            (typeFilter == 'd' && childInode.i_type == '1');
            bool sizeMatch = (sizeFilter == -1) || (childInode.i_s >= sizeFilter);
            
            if (nameMatch && typeMatch && sizeMatch) {
                results.push_back(fullPath);
            }
            
            // Si es directorio, buscar recursivamente
            if (childInode.i_type == '1') {
                searchDirectory(file, sb, childInodeIndex, fullPath, 
                               namePattern, typeFilter, sizeFilter, results);
            }
        }
    }
}

std::string Find(const std::string& input){
    try {
        if (!hasActiveSession()){
            return "Error no hay sesion activa";
        }

        std::string path = "/";
        std::string name = "";
        char type = '\0';
        int size = -1;

        //Parseo de los datos
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
            else if (tokenLower.find("-name=") == 0) {
                name = token.substr(6);
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                    name = name.substr(1, name.size() - 2);
                }
            }
            else if (tokenLower.find("-type=") == 0) {
                std::string typeValue = token.substr(6);
                if (typeValue.size() >= 2 && typeValue.front() == '"' && typeValue.back() == '"') {
                    typeValue = typeValue.substr(1, typeValue.size() - 2);
                }
                if (typeValue == "f" || typeValue == "file") type = 'f';
                else if (typeValue == "d" || typeValue == "dir") type = 'd';
            }
            else if (tokenLower.find("-size=") == 0) {
                size = std::atoi(token.substr(6).c_str());
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        // Obtener información de sesión
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
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

        //Buscar directorio del root
        int startInodeIndex = findInodeByPath(file, sb, path);
        
        if (startInodeIndex == -1) {
            file.close();
            return "Error: Directorio '" + path + "' no encontrado";
        }
        
        // Verificar que sea directorio
        Inodos startInode;
        file.seekg(sb.s_inode_start + startInodeIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&startInode), sizeof(Inodos));
        
        if (startInode.i_type != '1') {
            file.close();
            return "Error: '" + path + "' no es un directorio";
        }

        //Buscamos el archivo
         std::vector<std::string> results;
        searchDirectory(file, sb, startInodeIndex, path == "/" ? "" : path, name, type, size, results);
        file.close();

       std::ostringstream result;
        result << "----- FIND -----\n";
        result << "Búsqueda completada\n";
        result << "  Directorio: " << path << "\n";
        result << "  Nombre: " << (name.empty() ? "*" : name) << "\n";
        result << "  Tipo: " << (type == '\0' ? "todos" : (type == 'f' ? "archivo" : "directorio")) << "\n";
        result << "  Tamaño mínimo: " << (size == -1 ? "N/A" : std::to_string(size) + " bytes") << "\n";
        result << "  Resultados: " << results.size() << "\n\n";
        
        if (results.empty()) {
            result << "No se encontraron coincidencias";
        } else {
            result << "Coincidencias:\n";
            for (const auto& r : results) {
                result << "  " << r << "\n";
            }
        }
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en find: ") + e.what();
    }
}

#endif