#ifndef REMOVE_H
#define REMOVE_H


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
#include "Login.h"
#include "utils.h"
#include "globals.h"
#include "Journal.h"
#include "Journaling.h"

struct RemoveParams {
    std::string path = "";
    int inodosLiberados = 0;
    int bloquesLiberados = 0;
};

struct ResultadoEliminacion {
    int inodosLiberados = 0;
    int bloquesLiberados = 0;
};


RemoveParams parseRemoveCommand(const std::string& input) {
    RemoveParams params;
    
    size_t start = 0;
    while (start < input.length()) {
        size_t space = input.find(' ', start);
        std::string token = (space == std::string::npos) 
            ? input.substr(start) 
            : input.substr(start, space - start);
        
        if (token.find("-path=") == 0) {
            params.path = token.substr(6);
            // Remover comillas si existen
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"') {
                params.path = params.path.substr(1, params.path.size() - 2);
            }
        }
        
        if (space == std::string::npos) break;
        start = space + 1;
    }
    
    return params;
}


int obtenerInodoPadre(std::fstream& file, const SuperBloque& sb, const std::string& ruta) {
    if (ruta == "/" || ruta.empty()) return -1;  

    size_t lastSlash = ruta.find_last_of('/');
    if (lastSlash == 0) return 0;  // Padre es raíz
    if (lastSlash == std::string::npos) return 0;
    
    std::string pathPadre = ruta.substr(0, lastSlash);
    return buscarInodoPorRuta(file, sb, pathPadre);
}

std::string obtenerNombreDeRuta(const std::string& ruta) {
    if (ruta == "/" || ruta.empty()) return "/";
    size_t lastSlash = ruta.find_last_of('/');
    if (lastSlash == std::string::npos) return ruta;
    return ruta.substr(lastSlash + 1);
}

bool puedeEliminarRecursivo(std::fstream& file, const SuperBloque& sb, 
                           int inodoActual, const Usuario& usuario) {
    Inodos inode;
    file.seekg(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    // Verificar permiso de escritura en este inodo
    if (!checkPermission(usuario, inode, 'w')) {
        return false;
    }
    
    // Si es archivo, ya terminamos
    if (inode.i_type == '1') {
        return true;
    }
    
    // Si es carpeta, verificar todos sus contenidos
    for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
        int idxBloque = inode.i_block[i];
        
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo != -1) {
                std::string nombre = bloque.b_content[j].b_name;
                // Saltar "." y ".."
                if (nombre == "." || nombre == "..") continue;
                
                int inodoHijo = bloque.b_content[j].b_inodo;
                if (!puedeEliminarRecursivo(file, sb, inodoHijo, usuario)) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

// Elimina recursivamente un inodo y libera recursos ademas Retorna número de inodos/bloques liberados para actualizar bitmaps

ResultadoEliminacion eliminarRecursivo(std::fstream& file, const SuperBloque& sb,
                                       int inodoActual, int inodoPadre, 
                                       const std::string& nombreAEliminar) {
    ResultadoEliminacion resultado;
    
    Inodos inode;
    file.seekg(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    // Si es carpeta, eliminar contenidos primero
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
                    
                    ResultadoEliminacion sub = eliminarRecursivo(
                        file, sb, bloque.b_content[j].b_inodo, inodoActual, nombre);
                    resultado.inodosLiberados += sub.inodosLiberados;
                    resultado.bloquesLiberados += sub.bloquesLiberados;
                }
            }
            
            // Liberar bloque carpeta
            char cero = 0;
            file.seekp(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            for (int k = 0; k < sb.s_block_s; k++) file.write(&cero, 1);
            resultado.bloquesLiberados++;
        }
    } else {
        // Si es archivo, liberar sus bloques de contenido
        for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
            int idxBloque = inode.i_block[i];
            char cero = 0;
            file.seekp(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            for (int k = 0; k < sb.s_block_s; k++) file.write(&cero, 1);
            resultado.bloquesLiberados++;
        }
    }
    
    // Liberar el inodo actual
    file.seekp(sb.s_inode_start + inodoActual * sb.s_inode_s, std::ios::beg);
    char cero = 0;
    for (int k = 0; k < sb.s_inode_s; k++) file.write(&cero, 1);
    resultado.inodosLiberados++;
    
    // Remover referencia del padre
    if (inodoPadre != -1 && !nombreAEliminar.empty()) {
        Inodos inodePadre;
        file.seekg(sb.s_inode_start + inodoPadre * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodePadre), sb.s_inode_s);
        
        for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
            int idxBloque = inodePadre.i_block[i];
            
            BloqueCarpeta bloque;
            file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (bloque.b_content[j].b_inodo == inodoActual) {
                    bloque.b_content[j].b_inodo = -1;
                    memset(bloque.b_content[j].b_name, 0, 12);
                    
                    file.seekp(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
                    file.write(reinterpret_cast<char*>(&bloque), sb.s_block_s);
                    break;
                }
            }
        }
    }
    
    return resultado;
}

void actualizarBitmaps(std::fstream& file, const SuperBloque& sb,
                      int inodosLiberados, int bloquesLiberados) {
    // Actualizar bitmap de inodos
    for (int i = 0; i < inodosLiberados && i < sb.s_inodes_count; i++) {
        int bytePos = sb.s_bm_inode_start + (sb.s_firts_ino + i) / 8;
        int bitPos = (sb.s_firts_ino + i) % 8;
        
        char byte;
        file.seekg(bytePos, std::ios::beg);
        file.read(&byte, 1);
        byte &= ~(1 << bitPos);  // Poner bit en 0 (libre)
        file.seekp(bytePos, std::ios::beg);
        file.write(&byte, 1);
    }
    
    // Actualizar bitmap de bloques
    for (int i = 0; i < bloquesLiberados && i < sb.s_blocks_count; i++) {
        int bytePos = sb.s_bm_block_start + (sb.s_first_blo + i) / 8;
        int bitPos = (sb.s_first_blo + i) % 8;
        
        char byte;
        file.seekg(bytePos, std::ios::beg);
        file.read(&byte, 1);
        byte &= ~(1 << bitPos);
        file.seekp(bytePos, std::ios::beg);
        file.write(&byte, 1);
    }
    
    // Actualizar contadores en SuperBloque
    SuperBloque sbActualizado;
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(&sbActualizado), sb.s_block_s);
    sbActualizado.s_free_inodes_count += inodosLiberados;
    sbActualizado.s_free_blocks_count += bloquesLiberados;
    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<char*>(&sbActualizado), sb.s_block_s);
}

void registrarEnJournal(std::fstream& file, const SuperBloque& sb,
                       const char* operacion, const char* ruta, const char* contenido) {
    if (sb.s_filesystem_type != 3) return;  // Solo para EXT3
    
    // Buscar primer journal libre
    for (int i = 0; i < sb.s_inodes_count; i++) {
        long journalPos = sb.s_bm_block_start - (sb.s_inodes_count * sizeof(Journal)) + i * sizeof(Journal);
        
        Journal journal;
        file.seekg(journalPos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&journal), sizeof(Journal));
        
        if (strlen(journal.operacion) == 0 || strcmp(journal.operacion, "INIT") == 0) {
            Journal nuevo;
            strcpy(nuevo.operacion, operacion);
            strcpy(nuevo.ruta, ruta);
            strcpy(nuevo.contenido, contenido);
            nuevo.fecha = time(nullptr);
            nuevo.siguiente = -1;
            
            file.seekp(journalPos, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevo), sizeof(Journal));
            return;
        }
    }
}

inline std::string Remove(const std::string& input) {
    try {
        RemoveParams params = parseRemoveCommand(input);
        
        // Validar parámetros
        if (params.path.empty()) {
            return "Error: -path es obligatorio para remove";
        }
        
        // Validar sesión activa
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa";
        }
        
        // Buscar partición montada
        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == usuarioActual.particionId) {
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

        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;

        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sb.s_block_s);
        
        // Buscar inodo del target
        int inodoTarget = buscarInodoPorRuta(file, sb, params.path);
        if (inodoTarget == -1) {
            file.close();
            return "Error: No existe la ruta: " + params.path;
        }
        
        // Verificar permisos en el target
        Inodos inodeTarget;
        file.seekg(sb.s_inode_start + inodoTarget * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeTarget), sb.s_inode_s);
        
        if (!checkPermission(usuarioActual, inodeTarget, 'w')) {
            file.close();
            return "Error: No tiene permisos de escritura para eliminar: " + params.path;
        }
        
        // Obtener inodo padre y nombre
        int inodoPadre = obtenerInodoPadre(file, sb, params.path);
        std::string nombre = obtenerNombreDeRuta(params.path);
        
        // === REGLA CRÍTICA: DRY-RUN para carpetas ===
        if (inodeTarget.i_type == '0') {
            if (!puedeEliminarRecursivo(file, sb, inodoTarget, usuarioActual)) {
                file.close();
                return "Error: No tiene permisos para eliminar todo el contenido de: " + params.path;
            }
        }
        
        ResultadoEliminacion resultado = eliminarRecursivo(
            file, sb, inodoTarget, inodoPadre, nombre);
        
        // Actualizar bitmaps y SuperBloque
        actualizarBitmaps(file, sb, resultado.inodosLiberados, resultado.bloquesLiberados);
        
        // Registrar en Journal si es EXT3
        registrarEnJournal(file, sb, "REMOVE", params.path.c_str(), "Elemento eliminado");

        // Antes de eliminar, registrar la operación:
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "DELETE",
        params.path,
        "Archivo/Directorio eliminado"
        );
    }
        
        file.close();
        
        std::ostringstream result;
        result << "-----------REMOVE-----------\n"
        << "Elemento eliminado exitosamente: " << params.path << "\n"
        << "  Inodos liberados: " << resultado.inodosLiberados << "\n"
        << "  Bloques liberados: " << resultado.bloquesLiberados;
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en remove: ") + e.what();
    }
}

#endif