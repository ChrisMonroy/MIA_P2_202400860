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
#include "utils.h"
#include "globals.h"
#include "Journaling.h"
#include "Login.h"

struct MoveParams {
    std::string path = "";
    std::string destino = "";
};

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
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"')
                params.path = params.path.substr(1, params.path.size() - 2);
        }
        else if (token.find("-destino=") == 0) {
            params.destino = token.substr(9);
            if (params.destino.size() >= 2 && params.destino.front() == '"' && params.destino.back() == '"')
                params.destino = params.destino.substr(1, params.destino.size() - 2);
        }
        if (space == std::string::npos) break;
        start = space + 1;
    }
    return params;
}


struct BusquedaResultado {
    int inodoEncontrado = -1;
    int idxBloque = -1;
    int idxEntrada = -1;
};

BusquedaResultado buscarEnCarpetaMove(std::fstream& file, const SuperBloque& sb, 
                                       int inodoId, const std::string& nombreBuscado) {
    BusquedaResultado resultado;
    Inodos inodoCarpeta;
    file.seekg(sb.s_inode_start + (inodoId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoCarpeta), sb.s_inode_s);

    if (inodoCarpeta.i_type != '1') return resultado;

    for (int i = 0; i < 12; i++) {
        if (inodoCarpeta.i_block[i] != -1) {
            BloqueCarpeta fb;
            file.seekg(sb.s_block_start + (inodoCarpeta.i_block[i] * sb.s_block_s), std::ios::beg);
            file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);

            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1) {
                    char temp[13] = {0};
                    std::strncpy(temp, fb.b_content[j].b_name, 12);
                    temp[12] = '\0';
                    std::string nombreEnBloque = trim(std::string(temp));
                    
                    if (nombreEnBloque == nombreBuscado) {
                        resultado.inodoEncontrado = fb.b_content[j].b_inodo;
                        resultado.idxBloque = inodoCarpeta.i_block[i];
                        resultado.idxEntrada = j;
                        return resultado;
                    }
                }
            }
        }
    }
    return resultado;
}

int rastrearRutaMove(std::fstream& file, const SuperBloque& sb, const std::string& ruta) {
    if (ruta == "/") return 0;
    std::vector<std::string> componentes;
    std::stringstream ss(ruta);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) componentes.push_back(trim(token));
    }
    int inodoActual = 0;
    for (const std::string& nombre : componentes) {
        BusquedaResultado res = buscarEnCarpetaMove(file, sb, inodoActual, nombre);
        if (res.inodoEncontrado == -1) return -1;
        inodoActual = res.inodoEncontrado;
    }
    return inodoActual;
}

bool tienePermisoMove(const Inodos& inodo, char permisoRequerido, const Usuario& usuario) {
    if (usuario.nombre == "root" || usuario.uid == 1) return true;
    if (usuario.uid == -1) return false;
    
    int perm = std::stoi(inodo.i_perm);
    int bitRequerido = (permisoRequerido == 'r') ? 4 : (permisoRequerido == 'w') ? 2 : 1;
    int permU = (perm / 100) % 10;
    int permG = (perm / 10) % 10;
    int permO = perm % 10;
    
    if (usuario.uid == inodo.i_uid) return (permU & bitRequerido) == bitRequerido;
    if (usuario.gid == inodo.i_gid) return (permG & bitRequerido) == bitRequerido;
    return (permO & bitRequerido) == bitRequerido;
}

bool agregarEntradaCarpetaMove(std::fstream& file, SuperBloque& sb, int inodoPadreId, 
                                const std::string& nombre, int nuevoInodoId, long particionStart) {
    Inodos inodoPadre;
    file.seekg(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);

    for (int i = 0; i < 12; i++) {
        if (inodoPadre.i_block[i] != -1) {
            BloqueCarpeta fb;
            file.seekg(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
            file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo == -1) {
                    memset(fb.b_content[j].b_name, 0, 12);
                    std::strncpy(fb.b_content[j].b_name, nombre.c_str(), 11);
                    fb.b_content[j].b_name[11] = '\0';
                    fb.b_content[j].b_inodo = nuevoInodoId;
                    
                    file.seekp(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
                    file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
                    return true;
                }
            }
        } else {
            if (sb.s_free_blocks_count <= 0) return false;
            char bitmapByte;
            int nuevoBloque = -1;
            for (int b = 0; b < sb.s_blocks_count; b++) {
                int byteIdx = b / 8, bitIdx = b % 8;
                file.seekg(sb.s_bm_block_start + byteIdx, std::ios::beg);
                file.read(&bitmapByte, 1);
                if ((bitmapByte & (1 << bitIdx)) == 0) {
                    bitmapByte |= (1 << bitIdx);
                    file.seekp(sb.s_bm_block_start + byteIdx, std::ios::beg);
                    file.write(&bitmapByte, 1);
                    nuevoBloque = b;
                    sb.s_free_blocks_count--;
                    break;
                }
            }
            if (nuevoBloque == -1) return false;
            
            inodoPadre.i_block[i] = nuevoBloque;
            BloqueCarpeta fb;
            for(int k = 0; k < 4; k++) { 
                fb.b_content[k].b_inodo = -1; 
                memset(fb.b_content[k].b_name, 0, 12); 
            }
            memset(fb.b_content[0].b_name, 0, 12);
            std::strncpy(fb.b_content[0].b_name, nombre.c_str(), 11);
            fb.b_content[0].b_name[11] = '\0';
            fb.b_content[0].b_inodo = nuevoInodoId;
            
            file.seekp(sb.s_block_start + (nuevoBloque * sb.s_block_s), std::ios::beg);
            file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
            file.seekp(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
            file.write(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);
            return true;
        }
    }
    return false;
}
bool eliminarEntradaCarpetaMove(std::fstream& file, const SuperBloque& sb, 
                                 int inodoPadreId, const std::string& nombreObjetivo) {
    Inodos inodoPadre;
    file.seekg(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);
    
    for (int i = 0; i < 12; i++) {
        if (inodoPadre.i_block[i] != -1) {
            BloqueCarpeta fb;
            file.seekg(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
            file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1) {
                    char temp[13] = {0};
                    std::strncpy(temp, fb.b_content[j].b_name, 12);
                    temp[12] = '\0';
                    std::string nombreEnBloque = trim(std::string(temp));
                    
                    if (nombreEnBloque == nombreObjetivo) {
                        fb.b_content[j].b_inodo = -1;
                        memset(fb.b_content[j].b_name, 0, 12);
                        
                        file.seekp(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
                        file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
                        
                        inodoPadre.i_mtime = time(nullptr);
                        file.seekp(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
                        file.write(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}


bool moverMismaParticion(std::fstream& file, SuperBloque& sb,
                        int inodoOrigen, int inodoPadreOrigen, int idxBloquePadreOrigen, int idxEntradaOrigen,
                        int inodoPadreDestino, const std::string& nombreNuevo, long particionStart) {
    
    BloqueCarpeta bloqueOrigen;
    file.seekg(sb.s_block_start + idxBloquePadreOrigen * sb.s_block_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&bloqueOrigen), sb.s_block_s);
    
    bloqueOrigen.b_content[idxEntradaOrigen].b_inodo = -1;
    memset(bloqueOrigen.b_content[idxEntradaOrigen].b_name, 0, 12);
    
    file.seekp(sb.s_block_start + idxBloquePadreOrigen * sb.s_block_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&bloqueOrigen), sb.s_block_s);
    
    Inodos inodePadreOrigen;
    file.seekg(sb.s_inode_start + inodoPadreOrigen * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodePadreOrigen), sb.s_inode_s);
    inodePadreOrigen.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + inodoPadreOrigen * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodePadreOrigen), sb.s_inode_s);

    if (!agregarEntradaCarpetaMove(file, sb, inodoPadreDestino, nombreNuevo, inodoOrigen, particionStart)) {
        return false;
    }
    
    Inodos inodePadreDestino;
    file.seekg(sb.s_inode_start + inodoPadreDestino * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodePadreDestino), sb.s_inode_s);
    inodePadreDestino.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + inodoPadreDestino * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodePadreDestino), sb.s_inode_s);

    Inodos inodeOrigen;
    file.seekg(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);
    inodeOrigen.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);

    file.flush();
    return true;
}

// Journal
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
        if (params.path == "/") {
            return "Error: no se puede mover el directorio raíz";
        }
        if (params.destino.find(params.path) == 0 && 
            (params.destino.length() == params.path.length() || 
             params.destino[params.path.length()] == '/')) {
            return "Error: no puedes mover una carpeta dentro de sí misma";
        }
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa";
        }
        
        std::string partitionId = getSessionPartitionId();
        MountedPartition* target = nullptr;
        for (auto& mp : mounted_list) {
            if (mp.id == partitionId) { target = &mp; break; }
        }
        if (!target) return "Error: Partición no montada";
        
        std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) return "Error: No se pudo abrir el disco";
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
        
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        int inodoOrigen = rastrearRutaMove(file, sb, params.path);
        if (inodoOrigen == -1) {
            std::string pathTrimmed = trim(params.path);
            inodoOrigen = rastrearRutaMove(file, sb, pathTrimmed);
        }
        if (inodoOrigen == -1) {
            file.close();
            return "Error: No existe la ruta origen: " + params.path;
        }
        
        int inodoPadreDestino = rastrearRutaMove(file, sb, params.destino);
        if (inodoPadreDestino == -1) {
            file.close();
            return "Error: No existe la carpeta destino: " + params.destino;
        }
        
        Inodos inodeDestino;
        file.seekg(sb.s_inode_start + inodoPadreDestino * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeDestino), sb.s_inode_s);
        if (inodeDestino.i_type != '1') {
            file.close();
            return "Error: El destino debe ser una carpeta";
        }
        
        Inodos inodeOrigen;
        file.seekg(sb.s_inode_start + inodoOrigen * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodeOrigen), sb.s_inode_s);
        
        if (!tienePermisoMove(inodeOrigen, 'w', usuarioActual)) {
            file.close();
            return "Error: No tiene permisos de escritura en el origen";
        }
        if (!tienePermisoMove(inodeDestino, 'w', usuarioActual)) {
            file.close();
            return "Error: No tiene permisos de escritura en el destino";
        }
        
        std::vector<std::string> pathComponents;
        std::stringstream ss(params.path);
        std::string token;
        while (std::getline(ss, token, '/')) {
            std::string t = trim(token);
            if (!t.empty()) pathComponents.push_back(t);
        }
        if (pathComponents.empty()) {
            file.close();
            return "Error: Ruta origen inválida";
        }
        
        std::string nombreElemento = pathComponents.back();
        pathComponents.pop_back();
        
        int inodoPadreOrigen = 0;
        for (const std::string& nombre : pathComponents) {
            BusquedaResultado res = buscarEnCarpetaMove(file, sb, inodoPadreOrigen, nombre);
            if (res.inodoEncontrado == -1) {
                file.close();
                return "Error: No se pudo navegar al padre del origen";
            }
            inodoPadreOrigen = res.inodoEncontrado;
        }
        
        BusquedaResultado origenInfo = buscarEnCarpetaMove(file, sb, inodoPadreOrigen, nombreElemento);
        if (origenInfo.inodoEncontrado == -1) {
            file.close();
            return "Error: No se encontró el elemento en su directorio padre";
        }
        
        BusquedaResultado existe = buscarEnCarpetaMove(file, sb, inodoPadreDestino, nombreElemento);
        if (existe.inodoEncontrado != -1) {
            file.close();
            return "Error: Ya existe '" + nombreElemento + "' en el destino";
        }
        
        bool exito = moverMismaParticion(file, sb, inodoOrigen, inodoPadreOrigen, 
                                        origenInfo.idxBloque, origenInfo.idxEntrada,
                                        inodoPadreDestino, nombreElemento, particionStart);
        if (!exito) {
            file.close();
            return "Error: No se pudo completar el movimiento";
        }
        
        registrarJournalMove(file, sb, params.path.c_str(), params.destino.c_str());
        sb.s_mtime = time(nullptr);
        file.seekp(particionStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        file.flush();
        file.close();
        
        if (!partitionId.empty()) {
            CommandJournaling::add(partitionId, "MOVE_FILE", 
                                  params.destino + "/" + nombreElemento, 
                                  "Desde: " + params.path);
        }
        
        std::ostringstream result;
        result << "-----------MOVE-----------\n"
               << "Elemento movido exitosamente:\n"
               << "  Origen: " << params.path << "\n"
               << "  Destino: " << params.destino << "/" << nombreElemento << "\n"
               << "  Tipo: " << (inodeOrigen.i_type == '1' ? "Carpeta" : "Archivo");
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en move: ") + e.what();
    }
}

#endif