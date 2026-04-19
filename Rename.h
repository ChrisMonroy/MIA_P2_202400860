#ifndef RENAME_H
#define RENAME_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <functional>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "utils.h"
#include "globals.h"
#include "Journal.h"
#include "Journaling.h"
#include "Move.h"

struct RenameParams {
    std::string path = "";
    std::string name = "";
};

RenameParams parseRenameCommand(const std::string& input) {
    RenameParams params;
    
    size_t start = 0;
    while (start < input.length()) {
        size_t space = input.find(' ', start);
        std::string token = (space == std::string::npos) 
            ? input.substr(start) 
            : input.substr(start, space - start);
        
        if (token.find("-path=") == 0) {
            params.path = token.substr(6);
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"') {
                params.path = params.path.substr(1, params.path.size() - 2);
            }
        }
        else if (token.find("-name=") == 0) {
            params.name = token.substr(6);
            if (params.name.size() >= 2 && params.name.front() == '"' && params.name.back() == '"') {
                params.name = params.name.substr(1, params.name.size() - 2);
            }
        }
        
        if (space == std::string::npos) break;
        start = space + 1;
    }
    
    return params;
}

int buscarInodoPorRuta2(std::fstream& file, const SuperBloque& sb, 
                       const std::string& ruta, int inodoActual) {
    if (ruta == "/" || ruta.empty()) return 0;
    
    // Normalizar ruta
    std::string cleanPath = ruta;
    while (cleanPath.find("//") != std::string::npos) {
        cleanPath.replace(cleanPath.find("//"), 2, "/");
    }
    if (cleanPath.size() > 1 && cleanPath.back() == '/') {
        cleanPath.pop_back();
    }
    
    // Dividir en componentes
    std::vector<std::string> componentes;
    size_t start = (cleanPath[0] == '/') ? 1 : 0;
    while (start < cleanPath.size()) {
        size_t end = cleanPath.find('/', start);
        if (end == std::string::npos) end = cleanPath.size();
        std::string comp = cleanPath.substr(start, end - start);
        if (!comp.empty() && comp != ".") componentes.push_back(comp);
        start = end + 1;
    }
    
    if (componentes.empty()) return 0;
    
    int inodoPadre = inodoActual;
    
    for (const std::string& nombre : componentes) {
        Inodos inodePadre;
        file.seekg(sb.s_inode_start + inodoPadre * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodePadre), sb.s_inode_s);
        
        if (inodePadre.i_type != '0') return -1;  // No es carpeta
        
        bool encontrado = false;
        for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
            int idxBloque = inodePadre.i_block[i];
            
            BloqueCarpeta bloque;
            file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (bloque.b_content[j].b_inodo != -1 && 
                    strcmp(bloque.b_content[j].b_name, nombre.c_str()) == 0) {
                    inodoPadre = bloque.b_content[j].b_inodo;
                    encontrado = true;
                    break;
                }
            }
            if (encontrado) break;
        }
        
        if (!encontrado) return -1;
    }
    
    return inodoPadre;
}

void registrarJournalRename(std::fstream& file, const SuperBloque& sb,
                           const char* rutaAntigua, const char* rutaNueva) {
    if (sb.s_filesystem_type != 3) return;
    
    // Calcular inicio del área de journaling
    long journalAreaStart = sb.s_bm_inode_start - (sb.s_inodes_count * sizeof(Journal));
    
    for (int i = 0; i < sb.s_inodes_count; i++) {
        long journalPos = journalAreaStart + i * sizeof(Journal);
        
        Journal journal;
        file.seekg(journalPos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&journal), sizeof(Journal));
        
        if (strlen(journal.operacion) == 0 || strcmp(journal.operacion, "INIT") == 0) {
            Journal nuevo;
            strcpy(nuevo.operacion, "RENAME");
            
            std::string contenido = std::string(rutaAntigua) + " -> " + rutaNueva;
            strncpy(nuevo.ruta, rutaAntigua, sizeof(nuevo.ruta) - 1);
            strncpy(nuevo.contenido, contenido.c_str(), sizeof(nuevo.contenido) - 1);
            nuevo.fecha = time(nullptr);
            nuevo.siguiente = -1;
            
            file.seekp(journalPos, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevo), sizeof(Journal));
            return;
        }
    }
}

inline std::string Rename(const std::string& input) {
    try {
        RenameParams params = parseRenameCommand(input);
        
        if (params.path.empty() || params.name.empty()) {
            return "Error: -path y -name son obligatorios para rename";
        }
        if (params.name.length() > 12) {
            return "Error: El nombre no debe exceder 12 caracteres";
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
        
        // Abrir disco
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        // Leer SuperBloque
        SuperBloque sb;
        std::ifstream mbrFile(target->path, std::ios::binary);
        if (!mbrFile.is_open()) {
            file.close();
            return "Error: No se pudo abrir el disco para leer MBR";
        }
        MBR mbr;
        mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        mbrFile.close();

        // Obtener el start desde la tabla de particiones
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        // Buscar inodo del target
        int inodoTarget = buscarInodoPorRuta2(file, sb, params.path, 0);
        if (inodoTarget == -1) {
            file.close();
            return "Error: No existe la ruta: " + params.path;
        }
        
        // Obtener información del padre
        PathInfo info = obtenerInfoPadre(file, sb, params.path);
        if (info.inodoPadre == -1) {
            file.close();
            return "Error: No se pudo obtener información del padre";
        }
        
        // Verificar permisos de escritura en el target
        Inodos inodeTarget;
        file.seekg(sb.s_inode_start + inodoTarget * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeTarget), sb.s_inode_s);
        
        if (!checkPermission(usuarioActual, inodeTarget, 'w')) {
            file.close();
            return "Error: No tiene permisos de escritura para renombrar: " + params.path;
        }
        
        // Leer el bloque carpeta del padre
        BloqueCarpeta bloquePadre;
        file.seekg(sb.s_block_start + info.idxBloquePadre * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloquePadre), sb.s_block_s);
        
        // Actualizar el nombre en la entrada correspondiente
        memset(bloquePadre.b_content[info.idxEntrada].b_name, 0, 12);
        strncpy(bloquePadre.b_content[info.idxEntrada].b_name, params.name.c_str(), 11);
        
        // Escribir el bloque actualizado
        file.seekp(sb.s_block_start + info.idxBloquePadre * sb.s_block_s, std::ios::beg);
        file.write(reinterpret_cast<char*>(&bloquePadre), sb.s_block_s);
        
        // Actualizar i_mtime del inodo target
        inodeTarget.i_mtime = time(nullptr);
        file.seekp(sb.s_inode_start + inodoTarget * sb.s_inode_s, std::ios::beg);
        file.write(reinterpret_cast<char*>(&inodeTarget), sb.s_inode_s);
        
        // Registrar en Journal si es EXT3
        std::string nuevaRuta = getParentPath(params.path) + "/" + params.name;
        registrarJournalRename(file, sb, params.path.c_str(), nuevaRuta.c_str());
        
        file.close();

        // Después de renombrar exitosamente:
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "RENAME",
        nuevaRuta,
        "Antes: " + info.nombreActual
    );
}
        
        return std::string("Archivo/Carpeta renombrado exitosamente:\n") +
       "  De: " + params.path + "\n" +
       "  A: " + params.name;
        
    } catch (const std::exception& e) {
        return std::string("Error en rename: ") + e.what();
    }
}

#endif