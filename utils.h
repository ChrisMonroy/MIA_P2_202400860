#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <algorithm>
#include <cstdlib>
#include "globals.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "BloqueCarpeta.h"

inline std::string expandPath(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    const char* home = std::getenv("HOME");
    if (!home) {
        return "/tmp" + path.substr(1);
    }
    return std::string(home) + path.substr(1);
}

inline std::string toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

inline bool checkPermission(const Usuario& usuario, const Inodos& inode, char permisoRequerido) {
    // ROOT tiene todos los permisos
    if (usuario.nombre == "root") return true;
    
    int perm = atoi(inode.i_perm);
    int bitRequerido = (permisoRequerido == 'r') ? 4 : (permisoRequerido == 'w') ? 2 : 1;
    
    int permU = (perm / 100) % 10;
    int permG = (perm / 10) % 10;
    int permO = perm % 10;
    
    if (usuario.uid == inode.i_uid) {
        // Usuario propietario
        return (permU & bitRequerido) == bitRequerido;
    } else if (usuario.gid == inode.i_gid) {
        // Mismo grupo
        return (permG & bitRequerido) == bitRequerido;
    } else {
        // Otros
        return (permO & bitRequerido) == bitRequerido;
    }
}

// Obtener nombre de archivo/carpeta desde una ruta
inline std::string getFileNameFromPath(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return path;
    if (pos == path.length() - 1) return "/";  // Termina en /
    return path.substr(pos + 1);
}

// Obtener ruta padre de una ruta
inline std::string getParentPath(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    size_t pos = path.find_last_of('/');
    if (pos == 0) return "/";  // Padre de /home es /
    if (pos == std::string::npos) return "/";
    return path.substr(0, pos);
}

// Verificar si un nombre ya existe en una carpeta
inline bool existeNombreEnCarpeta(std::fstream& file, const SuperBloque& sb, 
                                  int inodoPadre, const std::string& nombreNuevo) {
    Inodos inodePadre;
    file.seekg(sb.s_inode_start + inodoPadre * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodePadre), sb.s_inode_s);
    
    for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
        int idxBloque = inodePadre.i_block[i];
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo != -1 && 
                strcmp(bloque.b_content[j].b_name, nombreNuevo.c_str()) == 0) {
                return true;
            }
        }
    }
    return false;
}

// Función auxiliar buscar inodo por ruta
int findInodeByPath(std::fstream& file, const SuperBloque& sb, const std::string& path) {
    std::string cleanPath = path;
    if (cleanPath.empty() || cleanPath == "/") return 0;
    if (cleanPath[0] == '/') cleanPath = cleanPath.substr(1);
    
    std::vector<std::string> parts;
    std::string part;
    std::istringstream iss(cleanPath);
    while (std::getline(iss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    if (parts.empty()) return 0;
    
    int currentInodeIndex = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        Inodos currentInode;
        file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&currentInode), sizeof(Inodos));
        
        if (currentInode.i_type != '1') return -1;
        
        bool found = false;
        for (int b = 0; b < 12 && currentInode.i_block[b] != -1; b++) {
            BloqueCarpeta dirBlock;
            file.seekg(sb.s_block_start + currentInode.i_block[b] * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&dirBlock), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (dirBlock.b_content[j].b_inodo == -1) continue;
                std::string name = dirBlock.b_content[j].b_name;
                name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
                if (name == parts[i]) {
                    currentInodeIndex = dirBlock.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return -1;
    }
    return currentInodeIndex;
}

// Función auxiliar agregar entrada a un directorio
bool addEntryToDirectory(std::fstream& file, const SuperBloque& sb, 
                         int dirInodeIndex, const std::string& name, int newInodeIndex,
                         char& bitmapByte) {
    Inodos dirInode;
    file.seekg(sb.s_inode_start + dirInodeIndex * sizeof(Inodos), std::ios::beg);
    file.read(reinterpret_cast<char*>(&dirInode), sizeof(Inodos));
    
    if (dirInode.i_type != '1') return false;
    
    for (int b = 0; b < 12; b++) {
        if (dirInode.i_block[b] == -1) {
            // Crear nuevo bloque
            int newBlockIndex = -1;
            for (int i = 0; i < sb.s_blocks_count; i++) {
                int byteIndex = i / 8;
                int bitIndex = i % 8;
                file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                file.read(&bitmapByte, 1);
                if ((bitmapByte & (1 << bitIndex)) == 0) {
                    newBlockIndex = i;
                    bitmapByte |= (1 << bitIndex);
                    file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    file.write(&bitmapByte, 1);
                    break;
                }
            }
            
            if (newBlockIndex == -1) return false;
            
            BloqueCarpeta newBlock;
            memset(&newBlock, 0, sb.s_block_s);
            for (int j = 0; j < 4; j++) newBlock.b_content[j].b_inodo = -1;
            
            strncpy(newBlock.b_content[0].b_name, name.c_str(), 12);
            newBlock.b_content[0].b_inodo = newInodeIndex;
            
            file.seekp(sb.s_block_start + newBlockIndex * sb.s_block_s, std::ios::beg);
            file.write(reinterpret_cast<char*>(&newBlock), sb.s_block_s);
            
            dirInode.i_block[b] = newBlockIndex;
            file.seekp(sb.s_inode_start + dirInodeIndex * sizeof(Inodos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&dirInode), sizeof(Inodos));
            
            return true;
        }
        
        BloqueCarpeta block;
        file.seekg(sb.s_block_start + dirInode.i_block[b] * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&block), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (block.b_content[j].b_inodo == -1) {
                strncpy(block.b_content[j].b_name, name.c_str(), 12);
                block.b_content[j].b_inodo = newInodeIndex;
                
                file.seekp(sb.s_block_start + dirInode.i_block[b] * sb.s_block_s, std::ios::beg);
                file.write(reinterpret_cast<char*>(&block), sb.s_block_s);
                
                return true;
            }
        }
    }
    
    return false;
}

struct PathInfo {
    int inodoPadre;
    int idxBloquePadre;
    int idxEntrada;
    std::string nombreActual;
};

PathInfo obtenerInfoPadre(std::fstream& file, const SuperBloque& sb, 
                          const std::string& ruta) {
    PathInfo info = {-1, -1, -1, ""};
    
    if (ruta == "/" || ruta.empty()) return info;
    
    size_t lastSlash = ruta.find_last_of('/');
    std::string pathPadre = (lastSlash == 0) ? "/" : ruta.substr(0, lastSlash);
    std::string nombreBuscado = ruta.substr(lastSlash + 1);
    info.inodoPadre = findInodeByPath(file, sb, pathPadre);
    if (info.inodoPadre == -1) return info;
    
    Inodos inodePadre;
    file.seekg(sb.s_inode_start + info.inodoPadre * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&inodePadre), sb.s_inode_s);
    
    for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
        int idxBloque = inodePadre.i_block[i];
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
        
        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo != -1 && 
                strcmp(bloque.b_content[j].b_name, nombreBuscado.c_str()) == 0) {
                info.idxBloquePadre = idxBloque;
                info.idxEntrada = j;
                info.nombreActual = bloque.b_content[j].b_name;
                return info;
            }
        }
    }
    
    return info;
}

// Buscar inodo por ruta (retorna -1 si no existe)
int buscarInodoPorRuta(std::fstream& file, const SuperBloque& sb, 
                       const std::string& ruta, int inodoActual = 0) {
    if (ruta == "/" || ruta.empty()) return 0;
    
    // Normalizar ruta: eliminar slashes dobles y trailing slash
    std::string cleanPath = ruta;
    while (cleanPath.find("//") != std::string::npos) {
        cleanPath.replace(cleanPath.find("//"), 2, "/");
    }
    if (cleanPath.size() > 1 && cleanPath.back() == '/') {
        cleanPath.pop_back();
    }
    
    // Dividir ruta en componentes
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
    
    // Navegar desde el inodo actual
    int inodoPadre = inodoActual;
    
    for (const std::string& nombre : componentes) {
        // Leer inodo padre
        Inodos inodePadre;
        file.seekg(sb.s_inode_start + inodoPadre * sb.s_inode_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&inodePadre), sb.s_inode_s);
        
        if (inodePadre.i_type != '1') return -1;
        
        // Buscar en sus bloques directos
        bool encontrado = false;
        for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
            int idxBloque = inodePadre.i_block[i];
            
            // Leer bloque carpeta
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

#endif 