#ifndef CHMOD_H
#define CHMOD_H

#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
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

// 🔍 Buscar un nombre dentro de un bloque de carpeta
int buscarEnCarpeta(std::fstream& file, const SuperBloque& sb, int inodoId, const std::string& nombreBuscado) {
    Inodos inodoCarpeta;
    file.seekg(sb.s_inode_start + (inodoId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoCarpeta), sb.s_inode_s);

    // ✅ '1' = directorio en tu sistema
    if (inodoCarpeta.i_type != '1') return -1;

    for (int i = 0; i < 12; i++) {
        if (inodoCarpeta.i_block[i] != -1) {
            BloqueCarpeta fb;
            file.seekg(sb.s_block_start + (inodoCarpeta.i_block[i] * sb.s_block_s), std::ios::beg);
            file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);

            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1) {
                    char temp[13] = {0};
                    std::strncpy(temp, fb.b_content[j].b_name, 12);
                    if (std::string(temp) == nombreBuscado) {
                        return fb.b_content[j].b_inodo;
                    }
                }
            }
        }
    }
    return -1;
}

// 🗺️ Navegar por la ruta completa hasta encontrar el inodo objetivo
int rastrearRuta(std::fstream& file, const SuperBloque& sb, const std::string& ruta) {
    if (ruta == "/") return 0;
    
    std::vector<std::string> componentes;
    std::stringstream ss(ruta);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) componentes.push_back(token);
    }

    int inodoActual = 0;
    for (const std::string& nombre : componentes) {
        inodoActual = buscarEnCarpeta(file, sb, inodoActual, nombre);
        if (inodoActual == -1) return -1;
    }
    return inodoActual;
}

// 🔄 Cambiar permisos de forma recursiva (solo directorios)
void cambiarPermisos(std::fstream& file, const SuperBloque& sb, int inodoId, 
                     const std::string& nuevoPermiso, bool recursivo, const Usuario& usuario, int& cambios) {
    Inodos inodo;
    file.seekg(sb.s_inode_start + (inodoId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodo), sb.s_inode_s);

    // Validar: solo root o propietario puede cambiar permisos
    if (usuario.uid != 1 && inodo.i_uid != usuario.uid) {
        return;
    }

    // Actualizar permisos (se almacenan como string "755")
    strncpy(inodo.i_perm, nuevoPermiso.c_str(), sizeof(inodo.i_perm) - 1);
    inodo.i_perm[sizeof(inodo.i_perm) - 1] = '\0';
    inodo.i_mtime = time(nullptr);

    file.seekp(sb.s_inode_start + (inodoId * sb.s_inode_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodo), sb.s_inode_s);
    cambios++;
    
    // ✅ Recursar SOLO si es directorio ('1') y se pidió recursividad
    if (recursivo && inodo.i_type == '1') {
        for (int i = 0; i < 12; i++) {
            if (inodo.i_block[i] != -1) {
                BloqueCarpeta fb;
                file.seekg(sb.s_block_start + (inodo.i_block[i] * sb.s_block_s), std::ios::beg);
                file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);

                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo != -1) {
                        char temp[13] = {0};
                        std::strncpy(temp, fb.b_content[j].b_name, 12);
                        std::string nombreHijo(temp);
                        if (nombreHijo != "." && nombreHijo != "..") {
                            cambiarPermisos(file, sb, fb.b_content[j].b_inodo, nuevoPermiso, recursivo, usuario, cambios);
                        }
                    }
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
            return "Error: -ugo debe ser 3 dígitos entre 0-7 (ej. 755)";
        }
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa";
        }
        
        // ✅ Validación robusta de root por UID (más seguro que comparar nombres)
        if (usuarioActual.uid != 1) {
            return "Error: Solo el usuario root puede ejecutar chmod";
        }
        
        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == usuarioActual.particionId) { 
                target = &mp; 
                break; 
            }
        }
        if (!target) return "Error: Partición no montada";
        
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) return "Error: No se pudo abrir el disco";
        
        // Leer MBR para obtener inicio de partición
        std::ifstream mbrFile(target->path, std::ios::binary);
        MBR mbr; 
        mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); 
        mbrFile.close();
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        // Navegar al inodo objetivo
        int inodoTarget = rastrearRuta(file, sb, params.path);
        if (inodoTarget == -1) {
            file.close();
            return "Error: No existe la ruta: " + params.path;
        }
        
        // Validación adicional: verificar propiedad del inodo
        Inodos inodoObjetivo;
        file.seekg(sb.s_inode_start + (inodoTarget * sb.s_inode_s), std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodoObjetivo), sb.s_inode_s);
        
        if (usuarioActual.uid != 1 && inodoObjetivo.i_uid != usuarioActual.uid) {
            file.close();
            return "Error: no puedes cambiar los permisos de un archivo que no te pertenece";
        }
        
        int cambios = 0;
        cambiarPermisos(file, sb, inodoTarget, params.ugo, params.recursive, usuarioActual, cambios);
        
        // Registrar en journal del filesystem
        registrarJournalChmod(file, sb, params.path.c_str(), params.ugo.c_str());
        
        file.flush();
        file.close();

        // Registrar en journal de comandos
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
            CommandJournaling::add(partitionId, "CHANGE_PERM", params.path, "Perm: " + params.ugo);
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