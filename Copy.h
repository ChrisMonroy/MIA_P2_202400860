#ifndef COPY_H
#define COPY_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include "Mbr.h"
#include "Mount.h"
#include "Login.h"
#include "globals.h"
#include "BloqueCarpeta.h"
#include "Inodos.h"
#include "SuperBloque.h"
#include "BloqueArchivos.h"
#include "Journaling.h"
#include "utils.h"

int buscarEnCarpetaCopy(std::fstream& file, const SuperBloque& sb, int inodoId, const std::string& nombreBuscado) {
    Inodos inodoCarpeta;
    file.seekg(sb.s_inode_start + (inodoId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoCarpeta), sb.s_inode_s);

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
                    temp[12] = '\0';
                    std::string nombreEnBloque = trim(std::string(temp));
                    
                    if (nombreEnBloque == nombreBuscado) {
                        return fb.b_content[j].b_inodo;
                    }
                }
            }
        }
    }
    return -1;
}


int rastrearRutaCopy(std::fstream& file, const SuperBloque& sb, const std::string& ruta) {
    if (ruta == "/") return 0;
    
    std::vector<std::string> componentes;
    std::stringstream ss(ruta);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) componentes.push_back(token);
    }

    int inodoActual = 0;
    for (const std::string& nombre : componentes) {
        inodoActual = buscarEnCarpetaCopy(file, sb, inodoActual, nombre);
        if (inodoActual == -1) return -1;
    }
    return inodoActual;
}

bool tienePermisoCopy(const Inodos& inodo, int permisoRequerido, int sessionUid, int sessionGid, const std::string& sessionUser) {
    if (sessionUser == "root") return true;

    int permisos = 0;
    try {
        permisos = std::stoi(inodo.i_perm);
    } catch (...) {
        return false;
    }
    
    int permPropietario = permisos / 100;
    int permGrupo = (permisos / 10) % 10;
    int permOtros = permisos % 10;

    if (inodo.i_uid == sessionUid) 
        return (permPropietario & permisoRequerido) == permisoRequerido;
    if (inodo.i_gid == sessionGid) 
        return (permGrupo & permisoRequerido) == permisoRequerido;
    return (permOtros & permisoRequerido) == permisoRequerido;
}

//Leer contenido de un archivo (solo bloques directos por simplicidad)
std::string leerContenidoArchivo(std::fstream& file, const SuperBloque& sb, const Inodos& fileInode) {
    std::string content = "";
    for (int i = 0; i < 12 && fileInode.i_block[i] != -1; i++) {
        BloqueArchivos block;
        file.seekg(sb.s_block_start + fileInode.i_block[i] * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&block), sb.s_block_s);
        content += std::string(block.b_content, fileInode.i_s);
    }
    return content;
}

// Obtener un inodo libre del bitmap
int obtenerInodoLibreCopy(std::fstream& file, SuperBloque& sb, long particionStart) {
    if (sb.s_free_inodes_count <= 0) return -1;
    
    char bitmapByte;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        
        file.seekg(sb.s_bm_inode_start + byteIndex, std::ios::beg);
        file.read(&bitmapByte, 1);
        
        if ((bitmapByte & (1 << bitIndex)) == 0) {
            // Marcar como ocupado
            bitmapByte |= (1 << bitIndex);
            file.seekp(sb.s_bm_inode_start + byteIndex, std::ios::beg);
            file.write(&bitmapByte, 1);
            
            sb.s_free_inodes_count--;
            file.seekp(particionStart, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            return i;
        }
    }
    return -1;
}

//Obtener un bloque libre del bitmap
int obtenerBloqueLibreCopy(std::fstream& file, SuperBloque& sb, long particionStart) {
    if (sb.s_free_blocks_count <= 0) return -1;
    
    char bitmapByte;
    for (int i = 0; i < sb.s_blocks_count; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        
        file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
        file.read(&bitmapByte, 1);
        
        if ((bitmapByte & (1 << bitIndex)) == 0) {
            bitmapByte |= (1 << bitIndex);
            file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
            file.write(&bitmapByte, 1);
            
            sb.s_free_blocks_count--;
            file.seekp(particionStart, std::ios::beg);
            file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            return i;
        }
    }
    return -1;
}


bool agregarEntradaCarpetaCopy(std::fstream& file, SuperBloque& sb, int inodoPadreId, 
                                const std::string& nombre, int nuevoInodoId, long particionStart) {
    
    Inodos inodoPadre;
    file.seekg(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);

    for (int i = 0; i < 12 && inodoPadre.i_block[i] != -1; i++) {
        BloqueCarpeta fb;
        file.seekg(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
        file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                escribirNombreEnBloque(fb.b_content[j].b_name, nombre);
                fb.b_content[j].b_inodo = nuevoInodoId;
                
                file.seekp(sb.s_block_start + (inodoPadre.i_block[i] * sb.s_block_s), std::ios::beg);
                file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
                
                inodoPadre.i_mtime = time(nullptr);
                file.seekp(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
                file.write(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);
                file.flush();
                return true;
            }
        }
    }

    int slotLibre = -1;
    for (int i = 0; i < 12; i++) {
        if (inodoPadre.i_block[i] == -1) {
            slotLibre = i;
            break;
        }
    }
    
    if (slotLibre == -1) {
        return false;
    }
    
    int nuevoBloque = obtenerBloqueLibreCopy(file, sb, particionStart);
    if (nuevoBloque == -1) return false;
    
    inodoPadre.i_block[slotLibre] = nuevoBloque;
    
    BloqueCarpeta fb;
    for(int k = 0; k < 4; k++) { 
        fb.b_content[k].b_inodo = -1; 
        memset(fb.b_content[k].b_name, 0, 12); 
    }
    
    escribirNombreEnBloque(fb.b_content[0].b_name, nombre);
    fb.b_content[0].b_inodo = nuevoInodoId;
    
    file.seekp(sb.s_block_start + (nuevoBloque * sb.s_block_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
    
    inodoPadre.i_mtime = time(nullptr);
    file.seekp(sb.s_inode_start + (inodoPadreId * sb.s_inode_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodoPadre), sb.s_inode_s);
    file.flush();
    
    return true;
}

int crearCarpetaCopia(std::fstream& file, SuperBloque& sb, int inodoPadreId, 
                      const std::string& nombre, long particionStart, int sessionUid, int sessionGid) {
    int nuevoInodo = obtenerInodoLibreCopy(file, sb, particionStart);
    int nuevoBloque = obtenerBloqueLibreCopy(file, sb, particionStart);
    if(nuevoInodo == -1 || nuevoBloque == -1) return -1;

    Inodos inodo;
    memset(&inodo, 0, sizeof(Inodos));
    inodo.i_uid = sessionUid;
    inodo.i_gid = sessionGid;
    inodo.i_s = 0;
    inodo.i_type = '1';
    strncpy(inodo.i_perm, "755", sizeof(inodo.i_perm) - 1);
    inodo.i_perm[sizeof(inodo.i_perm) - 1] = '\0';
    inodo.i_atime = inodo.i_ctime = inodo.i_mtime = time(nullptr);
    
    for (int i = 0; i < 15; i++) inodo.i_block[i] = -1;
    inodo.i_block[0] = nuevoBloque;
    
    file.seekp(sb.s_inode_start + (nuevoInodo * sb.s_inode_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodo), sb.s_inode_s);

    BloqueCarpeta fb;
    for(int i = 0; i < 4; i++) {
        fb.b_content[i].b_inodo = -1; 
        memset(fb.b_content[i].b_name, 0, 12);
    }
    
    memset(fb.b_content[0].b_name, 0, 12);
    strncpy(fb.b_content[0].b_name, ".", 11);
    fb.b_content[0].b_name[11] = '\0';
    fb.b_content[0].b_inodo = nuevoInodo;
    
    memset(fb.b_content[1].b_name, 0, 12);
    strncpy(fb.b_content[1].b_name, "..", 11);
    fb.b_content[1].b_name[11] = '\0';
    fb.b_content[1].b_inodo = inodoPadreId;

    file.seekp(sb.s_block_start + (nuevoBloque * sb.s_block_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);

    if (!agregarEntradaCarpetaCopy(file, sb, inodoPadreId, nombre, nuevoInodo, particionStart)) {
        return -1;
    }
    return nuevoInodo;
}

int crearArchivoCopia(std::fstream& file, SuperBloque& sb, int inodoPadreId, 
                       const std::string& nombre, const std::string& contenido, 
                       long particionStart, int sessionUid, int sessionGid, const Inodos& origen) {
    int bloquesReq = (contenido.length() / 64) + ((contenido.length() % 64 != 0) ? 1 : 0);
    if (bloquesReq == 0) bloquesReq = 1;

    int nuevoInodo = obtenerInodoLibreCopy(file, sb, particionStart);
    if (nuevoInodo == -1) return -1;

    Inodos inodo;
    memset(&inodo, 0, sizeof(Inodos));
    inodo.i_uid = sessionUid; 
    inodo.i_gid = sessionGid; 
    inodo.i_s = contenido.length(); 
    inodo.i_type = '0';
    strncpy(inodo.i_perm, origen.i_perm, sizeof(inodo.i_perm) - 1);
    inodo.i_perm[sizeof(inodo.i_perm) - 1] = '\0';
    inodo.i_atime = inodo.i_ctime = inodo.i_mtime = time(nullptr);

    for (int i = 0; i < 15; i++) inodo.i_block[i] = -1;

    for (int i = 0; i < bloquesReq && i < 12; i++) {
        int nuevoBloque = obtenerBloqueLibreCopy(file, sb, particionStart);
        if (nuevoBloque == -1) {
            // Liberar inodo si falla
            return -1;
        }
        inodo.i_block[i] = nuevoBloque;

        BloqueArchivos fb;
        memset(&fb, 0, sizeof(BloqueArchivos));
        size_t inicioChunk = i * 64;
        size_t longitud = std::min((size_t)64, contenido.length() - inicioChunk);
        if (longitud > 0) {
            memcpy(fb.b_content, contenido.c_str() + inicioChunk, longitud);
        }

        file.seekp(sb.s_block_start + (nuevoBloque * sb.s_block_s), std::ios::beg);
        file.write(reinterpret_cast<char*>(&fb), sb.s_block_s);
    }

    file.seekp(sb.s_inode_start + (nuevoInodo * sb.s_inode_s), std::ios::beg);
    file.write(reinterpret_cast<char*>(&inodo), sb.s_inode_s);
    
    if (!agregarEntradaCarpetaCopy(file, sb, inodoPadreId, nombre, nuevoInodo, particionStart)) {
        return -1;
    }
    
    return nuevoInodo;
}

bool copiarRecursivo(std::fstream& file, SuperBloque& sb, int inodoOrigenId, int inodoDestinoId, 
                     const std::string& nombre, long particionStart, int sessionUid, int sessionGid,
                     int& inodoCreado) {
    Inodos inodoOrigen;
    file.seekg(sb.s_inode_start + (inodoOrigenId * sb.s_inode_s), std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodoOrigen), sb.s_inode_s);

    if (!tienePermisoCopy(inodoOrigen, 4, sessionUid, sessionGid, "root")) {
        return true;
    }

    if (inodoOrigen.i_type == '0') {
        std::string contenido = leerContenidoArchivo(file, sb, inodoOrigen);
        inodoCreado = crearArchivoCopia(file, sb, inodoDestinoId, nombre, contenido, 
                                particionStart, sessionUid, sessionGid, inodoOrigen);
        return (inodoCreado != -1);
    } else {
        int nuevoInodoCarpeta = crearCarpetaCopia(file, sb, inodoDestinoId, nombre, 
                                                  particionStart, sessionUid, sessionGid);
        if (nuevoInodoCarpeta == -1) return false;
        
        inodoCreado = nuevoInodoCarpeta; 

        for (int i = 0; i < 12; i++) {
            if (inodoOrigen.i_block[i] != -1) {
                BloqueCarpeta fb;
                file.seekg(sb.s_block_start + (inodoOrigen.i_block[i] * sb.s_block_s), std::ios::beg);
                file.read(reinterpret_cast<char*>(&fb), sb.s_block_s);

                for (int j = 0; j < 4; j++) {
                    if (fb.b_content[j].b_inodo != -1) {
                        char temp[13] = {0};
                        std::strncpy(temp, fb.b_content[j].b_name, 12);
                        temp[12] = '\0';
                        std::string nombreHijo = trim(std::string(temp));
                        
                        if (nombreHijo != "." && nombreHijo != "..") {
                            int dummyInodo = -1;
                            if (!copiarRecursivo(file, sb, fb.b_content[j].b_inodo, 
                                           nuevoInodoCarpeta, nombreHijo, 
                                           particionStart, sessionUid, sessionGid, dummyInodo)) {
                                return false;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
}



inline std::string Copy(const std::string& input) {
    try {
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        
        std::string file1 = "", file2 = "";
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos) 
                ? input.substr(start) 
                : input.substr(start, space - start);
            
            std::string tokenLower = toLowerCase(token);
            
            if (tokenLower.find("-file1=") == 0 || tokenLower.find("-path=") == 0) {
                file1 = token.substr(tokenLower.find("-file1=") == 0 ? 7 : 6);
                if (file1.size() >= 2 && file1.front() == '"' && file1.back() == '"')
                    file1 = file1.substr(1, file1.size() - 2);
            }
            else if (tokenLower.find("-file2=") == 0 || tokenLower.find("-destino=") == 0) {
                file2 = token.substr(tokenLower.find("-file2=") == 0 ? 7 : 9);
                if (file2.size() >= 2 && file2.front() == '"' && file2.back() == '"')
                    file2 = file2.substr(1, file2.size() - 2);
            }
            if (space == std::string::npos) break;
            start = space + 1;
        }

        if (file1.empty() || file2.empty()) {
            return "Error: -file1 y -file2 son obligatorios para copy";
        }
        if (file1 == "/") {
            return "Error: no se puede copiar el directorio raíz";
        }
        
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        int sessionUid = getSessionUID();
        int sessionGid = getSessionGID();
        std::string sessionUser = trim(usuarioActual.nombre);
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición";
        }
        
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long particionStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(particionStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        int inodoOrigenId = rastrearRutaCopy(file, sb, file1);
        if (inodoOrigenId == -1) {
            file.close();
            return "Error: la ruta origen '" + file1 + "' no existe";
        }
        
        Inodos inodoOrigen;
        file.seekg(sb.s_inode_start + (inodoOrigenId * sb.s_inode_s), std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodoOrigen), sb.s_inode_s);
        
        std::string rutaPadreDestino, nombreNuevo;
        size_t lastSlash = file2.find_last_of('/');
        
        if (lastSlash != std::string::npos && lastSlash > 0) {
            rutaPadreDestino = file2.substr(0, lastSlash);
            nombreNuevo = file2.substr(lastSlash + 1);
        } else {
            rutaPadreDestino = "/";
            nombreNuevo = file2;
        }
        
        int inodoPadreDestinoId = rastrearRutaCopy(file, sb, rutaPadreDestino);
        if (inodoPadreDestinoId == -1) {
            file.close();
            return "Error: el directorio destino '" + rutaPadreDestino + "' no existe";
        }
        
        Inodos inodoPadreDestino;
        file.seekg(sb.s_inode_start + (inodoPadreDestinoId * sb.s_inode_s), std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodoPadreDestino), sb.s_inode_s);
        
        if (inodoPadreDestino.i_type != '1') {
            file.close();
            return "Error: '" + rutaPadreDestino + "' no es un directorio";
        }
        
        if (!tienePermisoCopy(inodoPadreDestino, 2, sessionUid, sessionGid, sessionUser)) {
            file.close();
            return "Error: no tienes permisos de escritura en el directorio destino";
        }
        
        if (buscarEnCarpetaCopy(file, sb, inodoPadreDestinoId, nombreNuevo) != -1) {
            file.close();
            return "Error: ya existe un elemento llamado '" + nombreNuevo + "' en el destino";
        }
        
        int bloquesNecesarios = (inodoOrigen.i_s / 64) + ((inodoOrigen.i_s % 64 != 0) ? 1 : 0);
        if (bloquesNecesarios == 0) bloquesNecesarios = 1;
        
        if (sb.s_free_inodes_count <= 0) {
            file.close();
            return "Error: no hay inodos disponibles";
        }
        if (sb.s_free_blocks_count < bloquesNecesarios) {
            file.close();
            return "Error: no hay bloques disponibles para la copia";
        }
        
        int inodoCreado = -1;
        if (!copiarRecursivo(file, sb, inodoOrigenId, inodoPadreDestinoId, nombreNuevo, 
                            particionStart, sessionUid, sessionGid, inodoCreado)) {
            file.close();
            return "Error: fallo al copiar - disco lleno o error de escritura";
        }

        std::cerr << "[DEBUG COPY] Verificando entrada creada: " << nombreNuevo << "\n";
int verificacion = buscarEnCarpetaCopy(file, sb, inodoPadreDestinoId, nombreNuevo);
std::cerr << "[DEBUG COPY] Buscar resultado: inodo=" << verificacion << "\n";

if (verificacion == -1) {
    Inodos dirDebug;
    file.seekg(sb.s_inode_start + inodoPadreDestinoId * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&dirDebug), sb.s_inode_s);
    for (int b = 0; b < 12 && dirDebug.i_block[b] != -1; b++) {
        BloqueCarpeta blk;
        file.seekg(sb.s_block_start + dirDebug.i_block[b] * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&blk), sb.s_block_s);
        for (int e = 0; e < 4; e++) {
            if (blk.b_content[e].b_inodo != -1) {
                char tmp[13] = {0};
                strncpy(tmp, blk.b_content[e].b_name, 12);
                std::cerr << "[DEBUG] Entrada: '" << tmp << "' -> inodo " << blk.b_content[e].b_inodo << "\n";
            }
        }
    }
}
        
        if (inodoCreado == -1) {
            file.close();
            return "Error: no se pudo asignar inodo para la copia";
        }
        
        sb.s_mtime = time(nullptr);
        file.seekp(particionStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.flush();
        file.close();

        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
            CommandJournaling::add(partitionId, "COPY_FILE", file2, "Desde: " + file1);
        }
        
        std::ostringstream result;
        result << "----- COPY -----\n";
        result << "Archivo copiado exitosamente\n";
        result << "  Origen: " << file1 << "\n";
        result << "  Destino: " << file2 << "\n";
        result << "  Tamaño: " << inodoOrigen.i_s << " bytes\n";
        result << "  Nuevo Inodo: " << inodoCreado;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en copy: ") + e.what();
    }
}

#endif