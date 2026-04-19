#ifndef CHOWN_H
#define CHOWN_H

#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <ctime>
#include <vector>
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

struct ChownParams {
    std::string path = "";
    std::string usuario = "";
    bool recursive = false;
};

ChownParams parseChownCommand(const std::string& input) {
    ChownParams params;
    size_t start = 0;
    while (start < input.length()) {
        size_t space = input.find(' ', start);
        std::string token = (space == std::string::npos) ? input.substr(start) : input.substr(start, space - start);
        
        if (token.find("-path=") == 0) {
            params.path = token.substr(6);
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"')
                params.path = params.path.substr(1, params.path.size() - 2);
        }
        else if (token.find("-usuario=") == 0) {
            params.usuario = token.substr(9);
            if (params.usuario.size() >= 2 && params.usuario.front() == '"' && params.usuario.back() == '"')
                params.usuario = params.usuario.substr(1, params.usuario.size() - 2);
        }
        else if (token.find("-r") == 0) {
            params.recursive = true;
        }
        if (space == std::string::npos) break;
        start = space + 1;
    }
    return params;
}

// Buscar UID por nombre en users.txt (simulado en memoria o leyendo bloque)
// Para este ejemplo, asumimos que puedes leer el users.txt o tienes una función helper
int obtenerUIDPorNombre(std::fstream& file, const SuperBloque& sb, const std::string& nombreUsuario) {
    // Implementación simplificada: buscar en el archivo users.txt (inodo 1 normalmente)
    // Debes leer el contenido del archivo users.txt y parsear "UID,U,grupo,user,pass"
    // Retorna el UID si existe, -1 si no
    // NOTA: Esto depende de cómo tengas implementada la lectura de users.txt
    // Aquí pondré un placeholder lógico:
    if (nombreUsuario == "root") return 1;
    // ... lógica para leer users.txt ...
    return -1; 
}

void chownRecursivo(std::fstream& file, const SuperBloque& sb, int inodoActual, 
                    int nuevoUID, const Usuario& usuarioActual, int& cambios) {
    Inodos inode;
    file.seekg(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    // Validar permisos: Root puede todo, Usuario solo sus propios archivos
    if (usuarioActual.nombre != "root" && inode.i_uid != usuarioActual.uid) {
        return; // Saltar este archivo si no es dueño y no es root
    }
    
    // Cambiar propietario
    inode.i_uid = nuevoUID;
    inode.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    cambios++;
    
    // Si es carpeta y es recursivo, continuar
    if (inode.i_type == '0') {
        for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
            int idxBloque = inode.i_block[i];
            BloqueCarpeta bloque;
            file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (bloque.b_content[j].b_inodo != -1) {
                    std::string nombre = bloque.b_content[j].b_name;
                    if (nombre == "." || nombre == "..") continue;
                    chownRecursivo(file, sb, bloque.b_content[j].b_inodo, nuevoUID, usuarioActual, cambios);
                }
            }
        }
    }
}

void registrarJournalChown(std::fstream& file, const SuperBloque& sb, const char* path, const char* user) {
    if (sb.s_filesystem_type != 3) return;
    long journalAreaStart = sb.s_bm_inode_start - (sb.s_inodes_count * sizeof(Journal));
    for (int i = 0; i < sb.s_inodes_count; i++) {
        long journalPos = journalAreaStart + i * sizeof(Journal);
        Journal journal;
        file.seekg(journalPos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&journal), sizeof(Journal));
        if (strlen(journal.operacion) == 0 || strcmp(journal.operacion, "INIT") == 0) {
            Journal nuevo;
            strcpy(nuevo.operacion, "CHOWN");
            strncpy(nuevo.ruta, path, sizeof(nuevo.ruta) - 1);
            std::string content = std::string("Nuevo owner: ") + user;
            strncpy(nuevo.contenido, content.c_str(), sizeof(nuevo.contenido) - 1);
            nuevo.fecha = time(nullptr);
            nuevo.siguiente = -1;
            file.seekp(journalPos, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevo), sizeof(Journal));
            return;
        }
    }
}

inline std::string Chown(const std::string& input) {
    try {
        ChownParams params = parseChownCommand(input);
        
        if (params.path.empty() || params.usuario.empty()) {
            return "Error: -path y -usuario son obligatorios para chown";
        }
        
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        
        std::string partitionId = getSessionPartitionId();
        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == partitionId) {
                target = &mp;
                break;
            }
        }
        if (!target) {
            return "Error: Partición no montada";
        }
        
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) return "Error: No se pudo abrir el disco";
        
        std::ifstream mbrFile(target->path, std::ios::binary);
        MBR mbr; mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); mbrFile.close();
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        // Validar que el usuario exista (aquí debes implementar la búsqueda real en users.txt)
        int nuevoUID = obtenerUIDPorNombre(file, sb, params.usuario); 
        if (nuevoUID == -1) return "Error: El usuario '" + params.usuario + "' no existe";
        
        // Navegar al inodo objetivo (implementar función buscarInodoPorRuta)
        int inodoTarget = 0; // Placeholder: debes implementar navegación
        
        int cambios = 0;
        chownRecursivo(file, sb, inodoTarget, nuevoUID, usuarioActual, cambios);
        
        registrarJournalChown(file, sb, params.path.c_str(), params.usuario.c_str());
        file.close();

        // Después de cambiar el propietario:
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "CHANGE_OWNER",
        params.path,
        "UID: " + std::to_string(usuarioActual.uid) + " -> " + std::to_string(nuevoUID)
    );
}
        
        std::ostringstream result;
        result << "-----------CHOWN-----------\n"
               << "Propietario cambiado exitosamente\n"
               << "  Ruta: " << params.path << "\n"
               << "  Nuevo propietario: " << params.usuario << "\n"
               << "  Elementos afectados: " << cambios;
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en chown: ") + e.what();
    }
}

#endif