#ifndef MOVE_H
#define MOVE_H

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
#include "BloqueArchivos.h"
#include "BloqueApuntadores.h"
#include "utils.h"
#include "globals.h"
#include "Journaling.h"

struct MoveParams {
    std::string path = "";
    std::string destino = "";
};

struct MoveResult {
    bool exito = false;
    std::string mensaje;
    int inodosMovidos = 0;
    int bloquesMovidos = 0;
};

// Parseo
MoveParams parseMoveCommand(const std::string& input) {
    MoveParams params;
    
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
        else if (token.find("-destino=") == 0) {
            params.destino = token.substr(9);
            if (params.destino.size() >= 2 && params.destino.front() == '"' && params.destino.back() == '"') {
                params.destino = params.destino.substr(1, params.destino.size() - 2);
            }
        }
        
        if (space == std::string::npos) break;
        start = space + 1;
    }
    
    return params;
}

bool checkPermissionInode(const Usuario& usuario, const Inodos& inode, char permisoRequerido) {
    if (usuario.nombre == "root") return true;
    
    int perm = atoi(inode.i_perm);
    int bitRequerido = (permisoRequerido == 'r') ? 4 : (permisoRequerido == 'w') ? 2 : 1;
    
    int permU = (perm / 100) % 10;
    int permG = (perm / 10) % 10;
    int permO = perm % 10;
    
    if (usuario.uid == inode.i_uid) {
        return (permU & bitRequerido) == bitRequerido;
    } else if (usuario.gid == inode.i_gid) {
        return (permG & bitRequerido) == bitRequerido;
    } else {
        return (permO & bitRequerido) == bitRequerido;
    }
}



// Mover en la misma particion
// Solo cambia referencias, no copia bloques
bool moverMismaParticion(std::fstream& file, const SuperBloque& sb,
                        int inodoOrigen, int inodoPadreOrigen, int idxBloquePadreOrigen, int idxEntradaOrigen,
                        int inodoDestino, const std::string& nombreNuevo) {
    
    // 1. Remover referencia del padre origen
    BloqueCarpeta bloqueOrigen;
    file.seekg(sb.s_block_start + idxBloquePadreOrigen * sb.s_block_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&bloqueOrigen), sb.s_block_s);
    
    bloqueOrigen.b_content[idxEntradaOrigen].b_inodo = -1;
    memset(bloqueOrigen.b_content[idxEntradaOrigen].b_name, 0, 12);
    
    file.seekp(sb.s_block_start + idxBloquePadreOrigen * sb.s_block_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&bloqueOrigen), sb.s_block_s);
    
    // 2. Agregar referencia en carpeta destino
    Inodos inodeDestino;
    file.seekg(sb.s_inode_start + inodoDestino * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodeDestino), sb.s_inode_s);
    
    bool agregado = false;
    for (int i = 0; i < 12 && inodeDestino.i_block[i] != -1 && !agregado; i++) {
        int idxBloque = inodeDestino.i_block[i];
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo == -1) {
                memset(bloque.b_content[j].b_name, 0, 12);
                strncpy(bloque.b_content[j].b_name, nombreNuevo.c_str(), 11);
                bloque.b_content[j].b_inodo = inodoOrigen;
                
                file.seekp(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
                file.write(reinterpret_cast<char*>(&bloque), sb.s_block_s);
                agregado = true;
                break;
            }
        }
    }
    
    if (!agregado) return false;  // No hay espacio en bloques directos
    
    // 3. Actualizar i_mtime del inodo movido
    Inodos inodeOrigen;
    file.seekg(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);
    inodeOrigen.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);
    
    return true;
}

// journalist
void registrarJournalMove(std::fstream& file, const SuperBloque& sb,
                         const char* origen, const char* destino) {
    if (sb.s_filesystem_type != 3) return;
    
    long journalAreaStart = sb.s_bm_inode_start - (sb.s_inodes_count * sizeof(Journal));
    
    for (int i = 0; i < sb.s_inodes_count; i++) {
        long journalPos = journalAreaStart + i * sizeof(Journal);
        Journal journal;
        file.seekg(journalPos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&journal), sizeof(Journal));
        
        if (strlen(journal.operacion) == 0 || strcmp(journal.operacion, "INIT") == 0) {
            Journal nuevo;
            strcpy(nuevo.operacion, "MOVE");
            strncpy(nuevo.ruta, origen, sizeof(nuevo.ruta) - 1);
            
            std::string contenido = std::string("Movido a: ") + destino;
            strncpy(nuevo.contenido, contenido.c_str(), sizeof(nuevo.contenido) - 1);
            nuevo.fecha = time(nullptr);
            nuevo.siguiente = -1;
            
            file.seekp(journalPos, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevo), sizeof(Journal));
            return;
        }
    }
}

inline std::string Move(const std::string& input) {
    try {
        MoveParams params = parseMoveCommand(input);
        
        if (params.path.empty() || params.destino.empty()) {
            return "Error: -path y -destino son obligatorios para move";
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
        
        // Abrir disco y leer MBR
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        std::ifstream mbrFile(target->path, std::ios::binary);
        MBR mbr;
        mbrFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        mbrFile.close();
        
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        
        // Leer SuperBloque
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        // Buscar inodo origen
        int inodoOrigen = buscarInodoPorRuta(file, sb, params.path);
        if (inodoOrigen == -1) {
            file.close();
            return "Error: No existe la ruta origen: " + params.path;
        }
        
        // Buscar inodo destino (debe ser carpeta)
        int inodoDestino = buscarInodoPorRuta(file, sb, params.destino);
        if (inodoDestino == -1) {
            file.close();
            return "Error: No existe la carpeta destino: " + params.destino;
        }
        
        Inodos inodeDestino;
        file.seekg(sb.s_inode_start + inodoDestino * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeDestino), sb.s_inode_s);
        
        if (inodeDestino.i_type != '1') {
            file.close();
            return "Error: El destino debe ser una carpeta: " + params.destino;
        }
        

        Inodos inodeOrigen;
        file.seekg(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);
        
        if (!checkPermissionInode(usuarioActual, inodeOrigen, 'w')) {
            file.close();
            return "Error: No tiene permisos de escritura en el origen: " + params.path;
        }
        
        // Obtener información del padre origen
        PathInfo infoOrigen = obtenerInfoPadre(file, sb, params.path);
        if (infoOrigen.inodoPadre == -1) {
            file.close();
            return "Error: No se pudo obtener información del padre origen";
        }
        
        // Verificar que el nombre no exista ya en destino
        if (existeNombreEnCarpeta(file, sb, inodoDestino, getFileNameFromPath(params.path))) {
            file.close();
            return "Error: Ya existe un archivo/carpeta con ese nombre en el destino";
        }
        

        bool exito = moverMismaParticion(file, sb,
                                        inodoOrigen, 
                                        infoOrigen.inodoPadre, infoOrigen.idxBloquePadre, infoOrigen.idxEntrada,
                                        inodoDestino, getFileNameFromPath(params.path));
        
        if (!exito) {
            file.close();
            return "Error: No se pudo completar el movimiento (espacio insuficiente en destino)";
        }
        
        // Registrar en Journal si EXT3
        registrarJournalMove(file, sb, params.path.c_str(), params.destino.c_str());
        
        file.close();

        // Después de mover exitosamente:
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "MOVE_FILE",
        params.destino,  // Ruta de destino
        "Desde: " + params.path  // Ruta de origen
    );
}
        std::ostringstream result;
        result << "-----------MOVE-----------\n"
               << "Elemento movido exitosamente:\n"
               << "  Origen: " << params.path << "\n"
               << "  Destino: " << params.destino << "/" << getFileNameFromPath(params.path) << "\n"
               << "  Tipo: " << (inodeOrigen.i_type == '0' ? "Carpeta" : "Archivo");
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en move: ") + e.what();
    }
}

#endif