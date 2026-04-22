#ifndef MOUNT_H
#define MOUNT_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include "Mbr.h"
#include "globals.h"
#include "utils.h"

extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;
extern char current_user[16];
extern bool is_logged;

// Mapa para tracking de discos: ruta -> (letra, último número de partición)
extern std::map<std::string, std::pair<char, int>> diskTracking;
extern char nextDiskLetter;


// Limpiar nombre de partición
std::string cleanPartitionName(const char* name) {
    std::string clean(name);
    clean.erase(std::remove(clean.begin(), clean.end(), '\0'), clean.end());
    clean.erase(std::remove_if(clean.begin(), clean.end(), ::isspace), clean.end());
    return clean;
}

std::string getLastTwoCarnetDigits() {
    return "60"; 
}

// Generar ID de montaje según especificaciones del PDF
std::string generateMountID(const std::string& path) {
    std::string carnetDigits = getLastTwoCarnetDigits();
    char diskLetter;
    int partitionNumber;
    
    // Verificar si el disco ya tiene tracking
    auto it = diskTracking.find(path);
    if (it != diskTracking.end()) {
        // Disco ya montado antes - usar misma letra, incrementar número
        diskLetter = it->second.first;
        partitionNumber = it->second.second + 1;
        it->second.second = partitionNumber;  // Actualizar contador
    } else {
        // Nuevo disco - nueva letra, número inicia en 1
        diskLetter = nextDiskLetter++;
        partitionNumber = 1;
        diskTracking[path] = {diskLetter, partitionNumber};
    }
    
    // Construir ID: carnet + número + letra
    std::string id = carnetDigits;
    id += std::to_string(partitionNumber);
    id += diskLetter;
    
    return id;
}

// Verificar si partición ya está montada
bool isPartitionMounted(const std::string& path, const std::string& name) {
    for (const auto& mp : mounted_list) {
        if (mp.path == path && mp.name == name) {
            return true;
        }
    }
    return false;
}

// Buscar partición por ID (para otros comandos)
bool getMountedPartitionById(const std::string& id, MountedPartition& result) {
    for (const auto& mp : mounted_list) {
        if (mp.id == id) {
            result = mp;
            return true;
        }
    }
    return false;
}

// Buscar partición en MBR/EBR por nombre
bool findPartitionInDisk(const std::string& path, const std::string& name,
                         int& index, char& type, int& start, int& size) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    MBR mbr;
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    
    // Buscar en particiones primarias
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1') {
            std::string partName = cleanPartitionName(mbr.mbr_partitions[i].part_name);
            
            if (partName == name) {
                // Solo primarias y lógicas se pueden montar (no extendidas)
                if (mbr.mbr_partitions[i].part_type == 'E') {
                    file.close();
                    return false;  // No montar extendidas
                }
                
                index = i;
                type = mbr.mbr_partitions[i].part_type;
                start = mbr.mbr_partitions[i].part_start;
                size = mbr.mbr_partitions[i].part_size;
                file.close();
                return true;
            }
            
            // Si es extendida, buscar lógicas dentro
            if (mbr.mbr_partitions[i].part_type == 'E') {
                int ebrPos = mbr.mbr_partitions[i].part_start;
                int logicIndex = 0;
                
                while (ebrPos != -1 && ebrPos < mbr.mbr_partitions[i].part_start + mbr.mbr_partitions[i].part_size) {
                    EBR ebr;
                    file.seekg(ebrPos, std::ios::beg);
                    file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                    
                    if (ebr.part_status == '1') {
                        std::string ebrName = cleanPartitionName(ebr.part_name);
                        
                        if (ebrName == name) {
                            index = i;  // Índice de la extendida que la contiene
                            type = 'L';
                            start = ebr.part_start;
                            size = ebr.part_size;
                            file.close();
                            return true;
                        }
                    }
                    
                    ebrPos = ebr.part_next;
                    logicIndex++;
                }
            }
        }
    }
    
    file.close();
    return false;
}

// Actualizar MBR en disco (status y correlativo)
bool updateMBR(const std::string& path, int partitionIndex, char newStatus, int correlative) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return false;
    }
    
    MBR mbr;
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    
    mbr.mbr_partitions[partitionIndex].part_status = newStatus;
    mbr.mbr_partitions[partitionIndex].part_correlative = correlative;
    
    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.close();
    
    return true;
}


std::string Mount(const std::string& input) {
    try {
        std::string path, name;
        
        // Parseo de parámetros
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-path=") == 0) {
                path = token.substr(6);
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                    path = path.substr(1, path.size() - 2);
            }
            else if (token.find("-name=") == 0) {
                name = token.substr(6);
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                    name = name.substr(1, name.size() - 2);
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        // Validar parámetros obligatorios
        if (path.empty() || name.empty()) {
            return "Error: -path y -name son obligatorios";
        }
        
        std::string fullPath = expandPath(path);
        
        // Verificar que el disco exista
        if (!std::filesystem::exists(fullPath)) {
            return "Error: Disco no encontrado en " + fullPath;
        }
        
        // Verificar si ya está montada
        if (isPartitionMounted(fullPath, name)) {
            return "Error: La partición '" + name + "' ya está montada";
        }
        
        // Buscar partición en el disco
        int partitionIndex = -1;
        char partType;
        int partStart, partSize;
        
        if (!findPartitionInDisk(fullPath, name, partitionIndex, partType, partStart, partSize)) {
            return "Error: Partición '" + name + "' no encontrada";
        }
        
        // Generar ID de montaje
        std::string mountID = generateMountID(fullPath);
        
        // Calcular número correlativo (basado en particiones montadas del mismo disco)
        int correlative = 1;
        for (const auto& mp : mounted_list) {
            if (mp.path == fullPath) {
                correlative++;
            }
        }
        
        // Actualizar MBR en disco (status = '1', correlativo)
        if (!updateMBR(fullPath, partitionIndex, '1', correlative)) {
            return "Error: No se pudo actualizar el MBR";
        }
        
        // Agregar a lista de montadas en memoria
        MountedPartition mp;
        mp.id = mountID;
        mp.path = fullPath;
        mp.name = name;
        mp.partition_index = partitionIndex;
        mp.type = partType;
        mp.start = partStart;
        mp.size = partSize;
        mp.correlative = correlative;
        
        mounted_list.push_back(mp);
        mounted_count = mounted_list.size();
        
        // Retornar información de éxito
        std::ostringstream result;
        result << "Partición montada exitosamente\n";
        result << "  ID: " << mountID << "\n";
        result << "  Disco: " << fullPath << "\n";
        result << "  Partición: " << name << "\n";
        result << "  Tipo: " << partType << "\n";
        result << "  Inicio: " << partStart << " bytes\n";
        result << "  Tamaño: " << partSize << " bytes\n";
        result << "  Correlativo: " << correlative;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en mount: ") + e.what();
    }
}

std::string getCurrentSessionId() {
    if (!is_logged || mounted_list.empty()) {
        return "";
    }
    // Retornar el ID de la partición donde se hizo login
    return mounted_list[0].id;
}

#endif