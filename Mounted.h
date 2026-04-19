#ifndef MOUNTED_H
#define MOUNTED_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "globals.h"

//Variables globales
extern bool is_logged;
extern char current_user[16];
extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;

//Comando
std::string Mounted() {
    try {
        //Verificar particiones
        if (mounted_list.empty() || mounted_count == 0) {
            return "No hay particiones montadas";
        }
        
        std::ostringstream result;
        result << "----- MOUNTED ------\n";
        result << "Particiones montadas: " << mounted_count << "\n\n";
        
        //Lista para cadena montada
        for (size_t i = 0; i < mounted_list.size(); i++) {
            const auto& mp = mounted_list[i];
            
            result << "[" << (i + 1) << "] ID: " << mp.id << "\n";
            result << "    Disco: " << mp.path << "\n";
            result << "    Partición: " << mp.name << "\n";
            result << "    Tipo: " << mp.type << "\n";
            result << "    Inicio: " << mp.start << " bytes\n";
            result << "    Tamaño: " << mp.size << " bytes\n";
            result << "    Correlativo: " << mp.correlative << "\n";
            
            if (i < mounted_list.size() - 1) {
                result << "    ---\n";
            }
        }
        
        //Los IDS montados resumen
        result << "\n----- IDS MONTADOS ------\n";
        result << "Total: " << mounted_count << "\n";
        result << "IDs: ";
        
        for (size_t i = 0; i < mounted_list.size(); i++) {
            result << mounted_list[i].id;
            if (i < mounted_list.size() - 1) {
                result << ", ";
            }
        }
        result << "\n";
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en mounted: ") + e.what();
    }
}

//Buscamos por Id
MountedPartition* findMountedPartitionById(const std::string& id) {
    for (auto& mp : mounted_list) {
        if (mp.id == id) {
            return &mp;
        }
    }
    return nullptr;
}

//Contar por disco
int countMountedPartitionsByDisk(const std::string& diskPath) {
    int count = 0;
    for (const auto& mp : mounted_list) {
        if (mp.path == diskPath) {
            count++;
        }
    }
    return count;
}

//Obtener letra de disco
char getDiskLetter(const std::string& diskPath) {
    char letter = 'A';
    for (const auto& mp : mounted_list) {
        if (mp.path == diskPath) {
            if (!mp.id.empty()) {
                letter = mp.id.back();
            }
            break;
        }
    }
    return letter;
}

#endif