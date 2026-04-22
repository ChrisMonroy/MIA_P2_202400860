#ifndef UNMOUNT_H
#define UNMOUNT_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "globals.h"
#include "Journaling.h"

// Variables globales externas
extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;

std::string Unmount(const std::string& input) {
    try {
        std::string id;
        
        // Parseo de parámetros
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
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        if (id.empty()) {
            return "Error: -id es obligatorio para unmount";
        }
        
        // Buscar partición montada por ID
        MountedPartition* target = nullptr;
        size_t targetIndex = 0;
        for (size_t i = 0; i < mounted_list.size(); i++) {
            if (mounted_list[i].id == id) {
                target = &mounted_list[i];
                targetIndex = i;
                break;
            }
        }
        
        if (!target) {
            return "Error: Partición con ID '" + id + "' no está montada";
        }
        
        // Abrir disco para actualizar MBR/EBR
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco: " + target->path;
        }
        
        // Leer MBR
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        // Si es partición primaria: actualizar en MBR
        if (target->type == 'P') {
            mbr.mbr_partitions[target->partition_index].part_status = '0';
            mbr.mbr_partitions[target->partition_index].part_correlative = -1;
            memset(mbr.mbr_partitions[target->partition_index].part_id, 0, 4);
            
            // Guardar MBR actualizado
            file.seekp(0, std::ios::beg);
            file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        }
        // Si es partición lógica: actualizar en EBR
        else if (target->type == 'L') {
            // Buscar el EBR que contiene esta partición lógica
            Partition& extPart = mbr.mbr_partitions[target->partition_index];
            long ebrPos = extPart.part_start;
            
            while (ebrPos != -1 && ebrPos < extPart.part_start + extPart.part_size) {
                EBR ebr;
                file.seekg(ebrPos, std::ios::beg);
                file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                
                if (ebr.part_start == target->start && ebr.part_size == target->size) {
                    // Encontramos el EBR de esta partición lógica
                    ebr.part_status = '0';
                    // No limpiamos part_name para mantener referencia
                    
                    file.seekp(ebrPos, std::ios::beg);
                    file.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                    break;
                }
                
                ebrPos = ebr.part_next;
            }
        }
        
        file.close();
        
        // Remover de lista en memoria
        mounted_list.erase(mounted_list.begin() + targetIndex);
        mounted_count = mounted_list.size();
        
        // Registrar en journaling si hay sesión activa
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
            CommandJournaling::add(
                partitionId,
                "UNMOUNT",
                "Partición " + id,
                "Partición desmontada"
            );
        }
        
        return "Partición '" + id + "' desmontada exitosamente";
        
    } catch (const std::exception& e) {
        return std::string("Error en unmount: ") + e.what();
    }
}

#endif