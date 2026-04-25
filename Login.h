#ifndef LOGIN_H
#define LOGIN_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "globals.h"

inline std::string cleanString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c != '\0' && c != '\n' && c != '\r' && c != ' ' && c != '\t') {
            result += c;
        }
    }
    return result;
}

MountedPartition* findMountedPartition(const std::string& id) {
    for (auto& mp : mounted_list) {
        if (mp.id == id) {
            return &mp;
        }
    }
    return nullptr;
}

std::string readUsersTxt(std::fstream& file, const SuperBloque& sb) {
    std::string content = "";
    
    // Leer inodo de users.txt (inodo 1, el inodo 0 es raíz)
    Inodos usersInode;
    file.seekg(sb.s_inode_start + 1 * sb.s_inode_s, std::ios::beg);
    file.read(reinterpret_cast<char*>(&usersInode), sb.s_inode_s);
    
    // Validar que sea un archivo ('0' = archivo, '1' = directorio)
    if (usersInode.i_type != '0') {
        return "";
    }
    
    // Leer bloques del archivo (solo bloques directos por simplicidad)
    for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
        BloqueArchivos block;
        file.seekg(sb.s_block_start + usersInode.i_block[i] * sb.s_block_s, std::ios::beg);
        file.read(reinterpret_cast<char*>(&block), sb.s_block_s);
        content += std::string(block.b_content);
    }
    
    return content;
}

bool findUserInContent(const std::string& content, const std::string& username, 
                       const std::string& password, int& uid, int& gid) {
    std::istringstream stream(content);
    std::string line;
    
    std::string userGroupName = "";
    {
        std::istringstream tempStream(content);
        std::string tempLine;
        while (std::getline(tempStream, tempLine)) {
            tempLine = cleanString(tempLine);
            if (tempLine.empty()) continue;
            
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lineStream(tempLine);
            while (std::getline(lineStream, field, ',')) {
                field = cleanString(field);
                fields.push_back(field);
            }
            
            if (fields.size() >= 5 && fields[1] == "U" && fields[3] == username) {
                userGroupName = fields[4];
                break;
            }
        }
    }
    
    if (!userGroupName.empty()) {
        std::istringstream tempStream(content);
        std::string tempLine;
        while (std::getline(tempStream, tempLine)) {
            tempLine = cleanString(tempLine);
            if (tempLine.empty()) continue;
            
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lineStream(tempLine);
            while (std::getline(lineStream, field, ',')) {
                field = cleanString(field);
                fields.push_back(field);
            }
            
            if (fields.size() >= 3 && fields[1] == "G" && fields[2] == userGroupName) {
                gid = std::atoi(fields[0].c_str());
                break;
            }
        }
    }
    
    // Tercer pass: validar usuario y contraseña
    while (std::getline(stream, line)) {
        line = cleanString(line);
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::string field;
        std::istringstream lineStream(line);
        while (std::getline(lineStream, field, ',')) {
            field = cleanString(field);
            fields.push_back(field);
        }
        
        if (fields.size() < 3) continue;
        
        int id = std::atoi(fields[0].c_str());
        std::string type = fields[1];
        std::string name = fields[2];
        
        if (id == 0) continue;
        
        // Buscar usuario: ID,U,Nombre,Password,Grupo
        if (type == "U" && fields.size() >= 5 && name == username) {
            uid = id;
            std::string storedPass = fields[3];
            
            if (storedPass == password) {
                return true;  
            }
            return false; 
        }
    }
    
    return false;  
}

std::string Login(const std::string& input) {
    try {
        std::string user = "";
        std::string pass = "";
        std::string id = "";
        
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-user=") == 0) {
                user = token.substr(6);
                if (user.size() >= 2 && user.front() == '"' && user.back() == '"') {
                    user = user.substr(1, user.size() - 2);
                }
            }
            else if (token.find("-pass=") == 0) {
                pass = token.substr(6);
                if (pass.size() >= 2 && pass.front() == '"' && pass.back() == '"') {
                    pass = pass.substr(1, pass.size() - 2);
                }
            }
            else if (token.find("-id=") == 0) {
                id = token.substr(4);
                if (id.size() >= 2 && id.front() == '"' && id.back() == '"') {
                    id = id.substr(1, id.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        // Validar parámetros obligatorios
        if (user.empty() || pass.empty() || id.empty()) {
            return "Error: -user, -pass y -id son obligatorios para login";
        }
        
        if (is_logged) {
            return "Error: Ya existe una sesión activa. Use logout primero";
        }
        
        MountedPartition* mp = findMountedPartition(id);
        if (!mp) {
            return "Error: Partición con ID " + id + " no está montada";
        }
        
        std::fstream file(mp->path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[mp->partition_index];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        std::string usersContent = readUsersTxt(file, sb);
        if (usersContent.empty()) {
            file.close();
            return "Error: No se pudo leer el archivo users.txt";
        }
        
        int uid = -1, gid = -1;
        bool found = findUserInContent(usersContent, user, pass, uid, gid);
        
        if (!found) {
            file.close();
            // Determinar mensaje de error más específico
            std::istringstream stream(usersContent);
            std::string line;
            bool userExists = false;
            while (std::getline(stream, line)) {
                line = cleanString(line);
                if (line.empty()) continue;
                std::vector<std::string> fields;
                std::string field;
                std::istringstream lineStream(line);
                while (std::getline(lineStream, field, ',')) {
                    field = cleanString(field);
                    fields.push_back(field);
                }
                if (fields.size() >= 4 && fields[1] == "U" && fields[3] == user) {
                    userExists = true;
                    break;
                }
            }
            return userExists ? "Error: Contraseña incorrecta para el usuario '" + user + "'" 
                              : "Error: El usuario '" + user + "' no existe";
        }

        sb.s_mtime = time(nullptr);
        sb.s_mnt_count++;
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        file.close();
        
        strncpy(current_user, user.c_str(), 15);
        current_user[15] = '\0';
        is_logged = true;
        session_uid = uid;
        session_gid = gid;
        session_partition_id = id;
        session_partition_path = mp->path;
        session_partition_index = mp->partition_index;
        
        usuarioActual.nombre = user;
        usuarioActual.uid = uid;
        usuarioActual.gid = gid;
        usuarioActual.particionId = id;
        
        std::ostringstream result;
        result << "----- LOGIN -----\n";
        result << "Sesión iniciada exitosamente\n";
        result << "  Usuario: " << user << "\n";
        result << "  UID: " << uid << "\n";
        result << "  GID: " << gid << "\n";
        result << "  Partición: " << id;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en login: ") + e.what();
    }
}

inline bool hasActiveSession() {
    return is_logged && !session_partition_id.empty();
}

inline int getSessionUID() {
    return is_logged ? session_uid : -1;
}

inline int getSessionGID() {
    return is_logged ? session_gid : -1;
}

inline std::string getSessionPartitionId() {
    return is_logged ? session_partition_id : "";
}

inline std::string getSessionPartitionPath() {
    return is_logged ? session_partition_path : "";
}

inline int getSessionPartitionIndex() {
    return is_logged ? session_partition_index : -1;
}

inline bool isRootUser() {
    return is_logged && (session_uid == 1 || std::string(current_user) == "root");
}

inline std::string getCurrentUserName() {
    return is_logged ? std::string(current_user) : "";
}

#endif