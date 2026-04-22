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
            if (bloque.b_content[j].b_inodo == -1) continue;
            
            std::string nombreEnBloque(bloque.b_content[j].b_name);
            nombreEnBloque = nombreEnBloque.substr(0, nombreEnBloque.find('\0'));
            
            if (nombreEnBloque == nombreNuevo) {
                return true;
            }
        }
    }
    return false;
}

int findInodeByPath(std::fstream& file, const SuperBloque& sb, const std::string& path) {
    if (path.empty() || path == "/") return 0;

    std::string cleanPath = path;
    if (cleanPath[0] == '/') cleanPath = cleanPath.substr(1);

    std::vector<std::string> parts;
    std::stringstream ss(cleanPath);
    std::string item;

    while (std::getline(ss, item, '/')) {
        if (!item.empty()) parts.push_back(item);
    }

    int current = 0;

    for (const std::string& part : parts) {

        Inodos inode;
        file.seekg(sb.s_inode_start + current * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);

        if (inode.i_type != '1') return -1;

        bool found = false;

        for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {

            BloqueCarpeta block;
            file.seekg(sb.s_block_start + inode.i_block[i] * sb.s_block_s);
            file.read(reinterpret_cast<char*>(&block), sb.s_block_s);

            for (int j = 0; j < 4; j++) {
                if (block.b_content[j].b_inodo == -1) continue;

                std::string name(block.b_content[j].b_name);
                name.erase(std::find(name.begin(), name.end(), '\0'), name.end());

                if (name == part) {
                    current = block.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }

            if (found) break;
        }

        if (!found) return -1;
    }

    return current;
}


bool addEntryToDirectory(std::fstream& file, const SuperBloque& sb, 
                         int dirInodeIndex, const std::string& name, int newInodeIndex) {
    
    std::cerr << "[DEBUG addEntry] Agregando '" << name << "' (inodo " << newInodeIndex 
              << ") al directorio inodo " << dirInodeIndex << std::endl;
    
    // Leer inodo del directorio
    Inodos dirInode;
    file.seekg(sb.s_inode_start + dirInodeIndex * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&dirInode), sb.s_inode_s);

    if (dirInode.i_type != '1') {
        std::cerr << "[DEBUG addEntry] ERROR: No es directorio" << std::endl;
        return false;
    }

    // 🔴 1. BUSCAR ESPACIO EN BLOQUES EXISTENTES
    for (int i = 0; i < 12 && dirInode.i_block[i] != -1; i++) {
        int idxBloque = dirInode.i_block[i];
        std::cerr << "[DEBUG addEntry] Revisando bloque existente: " << idxBloque << std::endl;
        
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);

        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo == -1) {
                std::cerr << "[DEBUG addEntry] Espacio libre en bloque " << idxBloque 
                          << ", entrada " << j << std::endl;
                
                memset(bloque.b_content[j].b_name, 0, 12);
                strncpy(bloque.b_content[j].b_name, name.c_str(), 11);
                bloque.b_content[j].b_name[11] = '\0';
                bloque.b_content[j].b_inodo = newInodeIndex;

                file.seekp(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
                file.write(reinterpret_cast<char*>(&bloque), sb.s_block_s);
                
                // Actualizar i_mtime del directorio
                dirInode.i_mtime = time(nullptr);
                file.seekp(sb.s_inode_start + dirInodeIndex * sb.s_inode_s, std::ios::beg);
                file.write(reinterpret_cast<char*>(&dirInode), sb.s_inode_s);
                
                file.flush();
                std::cerr << "[DEBUG addEntry] ✅ Entrada escrita y flushed" << std::endl;
                return true;
            }
        }
    }

    // 🔴 2. CREAR NUEVO BLOQUE (BITMAP CORREGIDO CON BITS)
    std::cerr << "[DEBUG addEntry] No hay espacio, creando nuevo bloque..." << std::endl;
    for (int i = 0; i < 12; i++) {
        if (dirInode.i_block[i] == -1) {
            int newBlock = -1;
            
            // ✅ Bitmap con operaciones de BITS (NO caracteres)
            for (int b = 0; b < sb.s_blocks_count; b++) {
                int byteIndex = b / 8;  // ✅ Byte correcto en el bitmap
                int bitIndex = b % 8;   // ✅ Bit dentro del byte
                
                char bitmapByte;
                file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                file.read(&bitmapByte, 1);
                
                if ((bitmapByte & (1 << bitIndex)) == 0) {  // ✅ Bit libre
                    newBlock = b;
                    bitmapByte |= (1 << bitIndex);  // ✅ Marcar como ocupado
                    file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    file.write(&bitmapByte, 1);
                    std::cerr << "[DEBUG addEntry] Bloque libre encontrado: " << newBlock 
                              << " (byte:" << byteIndex << ", bit:" << bitIndex << ")" << std::endl;
                    break;
                }
            }
            
            if (newBlock == -1) {
                std::cerr << "[DEBUG addEntry] ❌ No hay bloques libres" << std::endl;
                return false;
            }

            // Crear nuevo BloqueCarpeta
            BloqueCarpeta nuevoBloque;
            memset(&nuevoBloque, 0, sizeof(BloqueCarpeta));
            for (int k = 0; k < 4; k++) nuevoBloque.b_content[k].b_inodo = -1;
            
            memset(nuevoBloque.b_content[0].b_name, 0, 12);
            strncpy(nuevoBloque.b_content[0].b_name, name.c_str(), 11);
            nuevoBloque.b_content[0].b_name[11] = '\0';
            nuevoBloque.b_content[0].b_inodo = newInodeIndex;

            // Escribir bloque en disco
            file.seekp(sb.s_block_start + newBlock * sb.s_block_s, std::ios::beg);
            file.write(reinterpret_cast<char*>(&nuevoBloque), sb.s_block_s);
            std::cerr << "[DEBUG addEntry] Nuevo bloque " << newBlock << " escrito" << std::endl;

            // ACTUALIZAR INODO DEL DIRECTORIO
            dirInode.i_block[i] = newBlock;
            dirInode.i_mtime = time(nullptr);
            
            file.seekp(sb.s_inode_start + dirInodeIndex * sb.s_inode_s, std::ios::beg);
            file.write(reinterpret_cast<char*>(&dirInode), sb.s_inode_s);
            
            file.flush();
            std::cerr << "[DEBUG addEntry] Inodo directorio actualizado y flushed" << std::endl;
            return true;
        }
    }

    std::cerr << "[DEBUG addEntry]  Directorio lleno (no hay slots en i_block)" << std::endl;
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
    if (bloque.b_content[j].b_inodo == -1) continue;
    
    std::string nombreEnBloque(bloque.b_content[j].b_name);
    nombreEnBloque = nombreEnBloque.substr(0, nombreEnBloque.find('\0'));
    
        if (nombreEnBloque == nombreBuscado) {  
        info.idxBloquePadre = idxBloque;  
        info.idxEntrada = j;
        info.nombreActual = nombreEnBloque;
        return info;                       
    }
}
    }
    
    return info;
}

int buscarInodoPorRuta(std::fstream& file, const SuperBloque& sb, 
                       const std::string& ruta, int inodoActual = 0) {
    if (ruta == "/" || ruta.empty()) return 0;
    
    std::string cleanPath = ruta;
    while (cleanPath.find("//") != std::string::npos) {
        cleanPath.replace(cleanPath.find("//"), 2, "/");
    }
    if (cleanPath.size() > 1 && cleanPath.back() == '/') {
        cleanPath.pop_back();
    }
    
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
        
        if (inodePadre.i_type != '1') return -1;
        
        bool encontrado = false;
        for (int i = 0; i < 12 && inodePadre.i_block[i] != -1; i++) {
            int idxBloque = inodePadre.i_block[i];
            BloqueCarpeta bloque;
            file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
            file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
            
            for (int j = 0; j < 4; j++) {
                if (bloque.b_content[j].b_inodo == -1) continue;
                
                // ✅ Comparación segura con limpieza de \0
                std::string nombreEnBloque(bloque.b_content[j].b_name);
                nombreEnBloque = nombreEnBloque.substr(0, nombreEnBloque.find('\0'));
                
                if (nombreEnBloque == nombre) {  // ✅ COMPARACIÓN REAL
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

inline std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r\0");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r\0");
    return str.substr(start, end - start + 1);
}

inline bool validarNombreArchivo(const std::string& nombre, std::string& errorMsg) {
    if (nombre.empty()) { errorMsg = "nombre vacío"; return false; }
    if (nombre == "." || nombre == "..") { errorMsg = "nombre reservado"; return false; }
    if (nombre.length() > 12) { 
        errorMsg = "nombre demasiado largo (máximo 12 caracteres, tiene " + std::to_string(nombre.length()) + ")"; 
        return false; 
    }
    if (nombre.find('/') != std::string::npos) { errorMsg = "contiene carácter inválido '/'"; return false; }
    return true;
}

inline void escribirNombreEnBloque(char* destino, const std::string& nombre) {
    memset(destino, 0, 12);
    size_t len = std::min(nombre.size(), (size_t)12);
    std::memcpy(destino, nombre.c_str(), len);
}

inline std::string leerNombreDeBloque(const char* origen) {
    char temp[13] = {0};
    std::memcpy(temp, origen, 12);
    std::string result(temp);
    size_t nullPos = result.find('\0');
    if (nullPos != std::string::npos) result = result.substr(0, nullPos);
    return result;
}

#endif 