#ifndef CHMOD_H
#define CHMOD_H

#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include "Mbr.h"
#include "Mount.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "BloqueCarpeta.h"
#include "Journal.h"
#include "utils.h"
#include "globals.h"
#include "Journaling.h"
#include "Login.h"

struct ChmodParams {
    std::string path = "";
    std::string ugo = "";
    bool recursive = false;
};

ChmodParams parseChmodCommand(const std::string& input) {
    ChmodParams params;
    size_t start = 0;
    while (start < input.length()) {
        size_t space = input.find(' ', start);
        std::string token = (space == std::string::npos) ? input.substr(start) : input.substr(start, space - start);
        
        if (token.find("-path=") == 0) {
            params.path = token.substr(6);
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"')
                params.path = params.path.substr(1, params.path.size() - 2);
        }
        else if (token.find("-ugo=") == 0) {
            params.ugo = token.substr(5);
            if (params.ugo.size() >= 2 && params.ugo.front() == '"' && params.ugo.back() == '"')
                params.ugo = params.ugo.substr(1, params.ugo.size() - 2);
        }
        else if (token.find("-r") == 0) {
            params.recursive = true;
        }
        if (space == std::string::npos) break;
        start = space + 1;
    }
    return params;
}

bool validarUGO(const std::string& ugo) {
    if (ugo.length() != 3) return false;
    for (char c : ugo) {
        if (c < '0' || c > '7') return false;
    }
    return true;
}

void chmodRecursivo(std::fstream& file, const SuperBloque& sb, int inodoActual, 
                    const std::string& permisos, const Usuario& usuarioActual, int& cambios) {
    Inodos inode;
    file.seekg(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    // Solo root puede ejecutar chmod, pero si fuera usuario normal, solo sobre propios
    if (usuarioActual.nombre != "root" && inode.i_uid != usuarioActual.uid) {
        return;
    }
    
    strncpy(inode.i_perm, permisos.c_str(), sizeof(inode.i_perm) - 1);
    inode.i_mtime = time(nullptr);
    
    file.seekp(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    cambios++;
    
    if (inode.i_type == '0' && usuarioActual.nombre == "root") { // Root puede recursivo total
         for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
            int idxBloque = inode.i_block[i];
            BloqueCarpeta bloque;
            file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
            for (int j = 0; j < 4; j++) {
                if (bloque.b_content[j].b_inodo != -1) {
                    std::string nombre = bloque.b_content[j].b_name;
                    if (nombre == "." || nombre == "..") continue;
                    chmodRecursivo(file, sb, bloque.b_content[j].b_inodo, permisos, usuarioActual, cambios);
                }
            }
        }
    }
}

void registrarJournalChmod(std::fstream& file, const SuperBloque& sb, const char* path, const char* ugo) {
    if (sb.s_filesystem_type != 3) return;
    long journalAreaStart = sb.s_bm_inode_start - (sb.s_inodes_count * sizeof(Journal));
    for (int i = 0; i < sb.s_inodes_count; i++) {
        long journalPos = journalAreaStart + i * sizeof(Journal);
        Journal journal;
        file.seekg(journalPos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&journal), sizeof(Journal));
        if (strlen(journal.operacion) == 0 || strcmp(journal.operacion, "INIT") == 0) {
            Journal nuevo;
            strcpy(nuevo.operacion, "CHMOD");
            strncpy(nuevo.ruta, path, sizeof(nuevo.ruta) - 1);
            std::string content = std::string("Permisos: ") + ugo;
            strncpy(nuevo.contenido, content.c_str(), sizeof(nuevo.contenido) - 1);
            nuevo.fecha = time(nullptr);
            nuevo.siguiente = -1;
            file.seekp(journalPos, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevo), sizeof(Journal));
            return;
        }
    }
}

inline std::string Chmod(const std::string& input) {
    try {
        ChmodParams params = parseChmodCommand(input);
        
        if (params.path.empty() || params.ugo.empty()) {
            return "Error: -path y -ugo son obligatorios para chmod";
        }
        if (!validarUGO(params.ugo)) {
            return "Error: -ugo debe ser 3 dígitos entre 0-7 (ej. 764)";
        }
        if (!hasActiveSession()) {
        return "Error: No hay sesión activa";
        }
        // Solo ROOT puede ejecutar CHMOD según enunciado
        if (usuarioActual.nombre != "root") {
            return "Error: Solo el usuario root puede ejecutar chmod";
        }
        
        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == usuarioActual.particionId) { target = &mp; break; }
        }
        if (!target) return "Error: Partición no montada";
        
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) return "Error: No se pudo abrir el disco";
        
        std::ifstream mbrFile(target->path, std::ios::binary);
        MBR mbr; mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); mbrFile.close();
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        int inodoTarget = 0; // Placeholder: implementar navegación
        int cambios = 0;
        
        chmodRecursivo(file, sb, inodoTarget, params.ugo, usuarioActual, cambios);
        registrarJournalChmod(file, sb, params.path.c_str(), params.ugo.c_str());
        file.close();

        // Después de cambiar los permisos:
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "CHANGE_PERM",
        params.path,
        "Perm: " + params.ugo + " -> " + params.ugo
        );
    }
        
        std::ostringstream result;
        result << "-----------CHMOD-----------\n"
               << "Permisos cambiados exitosamente\n"
               << "  Ruta: " << params.path << "\n"
               << "  Nuevos permisos: " << params.ugo << "\n"
               << "  Elementos afectados: " << cambios;
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en chmod: ") + e.what();
    }
}
#endif