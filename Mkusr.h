#ifndef MKUSR_H
#define MKUSR_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "Login.h"
#include "globals.h"
#include "Journaling.h"

//Comando
std::string Mkusr(const std::string& input) {
    try {
        //Verificar sesion
        if (!hasActiveSession()) {
            return "Error: No hay sesión activa. Inicie sesión primero";
        }
        
        //verificar que sea root
        if (!isRootUser()) {
            return "Error: Solo el usuario root puede crear usuarios";
        }
        
        std::string user = "", pass = "", grp = "";
        
        //Parseo
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
            else if (token.find("-grp=") == 0) {
                grp = token.substr(5);
                if (grp.size() >= 2 && grp.front() == '"' && grp.back() == '"') {
                    grp = grp.substr(1, grp.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        //Validacion
        if (user.empty() || pass.empty() || grp.empty()) {
            return "Error: -user, -pass y -grp son obligatorios para mkusr";
        }
        
        if (user.length() > 10) {
            return "Error: El nombre de usuario no debe exceder 10 caracteres";
        }
        
        if (pass.length() > 10) {
            return "Error: La contraseña no debe exceder 10 caracteres";
        }
        
        if (grp.length() > 10) {
            return "Error: El nombre del grupo no debe exceder 10 caracteres";
        }
        
        if (user == "root") {
            return "Error: No se puede crear un usuario llamado root";
        }
        
        //Obtener info
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición en la sesión";
        }
        //Disco
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }
        
        //Mbr y SuperBloque
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        std::string usersContent = "";
        Inodos usersInode;
        file.seekg(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        
        if (usersInode.i_type != '0') { 
            file.close();
            return "Error: users.txt no es un archivo válido";
        }
        
        // Leer bloques del archivo users.txt
        for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
            BloqueArchivos block;
            file.seekg(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            usersContent += std::string(block.b_content);
        }
        
        //Verificaciones
        bool groupFound = false;
        bool userFound = false;
        int maxUid = 1;
        int groupGid = -1;
        
        std::istringstream stream(usersContent);
        std::string line;
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            // Parsear línea: ID,Tipo,Nombre,Pass,Grupo o ID,Tipo,Nombre
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lineStream(line);
            
            while (std::getline(lineStream, field, ',')) {
                fields.push_back(field);
            }
            
            if (fields.size() >= 3) {
                int id = std::atoi(fields[0].c_str());
                std::string type = fields[1];
                std::string name = fields[2];
                
                // Si está eliminado (ID = 0), saltar
                if (id == 0) continue;
                
                // Buscar grupo
                if (type == "G" && name == grp) {
                    groupFound = true;
                    groupGid = id;
                }
                // Buscar usuario
                else if (type == "U" && name == user) {
                    userFound = true;
                }
                // Encontrar max UID
                else if (type == "U" && id > maxUid) {
                    maxUid = id;
                }
            }
        }
        
        if (!groupFound) {
            file.close();
            return "Error: El grupo '" + grp + "' no existe";
        }
        
        if (userFound) {
            file.close();
            return "Error: Ya existe un usuario con ese nombre";
        }
        
        //Creamos el nuevo usuario
        int newUid = maxUid + 1;
        std::string newUser = std::to_string(newUid) + ",U," + user + "," + pass + "," + grp + "\n";
        usersContent += newUser;
        
        // Limpiar bloques anteriores
        for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
            BloqueArchivos emptyBlock;
            memset(&emptyBlock, 0, sizeof(BloqueArchivos));
            file.seekp(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&emptyBlock), sizeof(BloqueArchivos));
        }
        
        int blockIndex = 0;
        size_t pos = 0;
        int blocksUsed = 0;
        
        // Limpiar bloques anteriores del users.txt
        for (int i = 0; i < 15 && usersInode.i_block[i] != -1; i++) {
        BloqueArchivos emptyBlock;
        memset(&emptyBlock, 0, sizeof(BloqueArchivos));
        file.seekp(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&emptyBlock), sizeof(BloqueArchivos));
        usersInode.i_block[i] = -1;
        }



while (pos < usersContent.length() && blockIndex < 15) {
    BloqueArchivos block;
    memset(&block, 0, sizeof(BloqueArchivos));
    
    // Copiar chunk de 63 bytes (dejar espacio para \0)
    size_t chunkSize = std::min(size_t(63), usersContent.length() - pos);
    strncpy(block.b_content, usersContent.c_str() + pos, chunkSize);
    block.b_content[chunkSize] = '\0';
    
    // Buscar bloque libre en bitmap
    int freeBlock = -1;
    for (int b = 0; b < sb.s_blocks_count; b++) {
        int byteIndex = b / 8;
        int bitIndex = b % 8;
        
        file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
        char bitmapByte;
        file.read(&bitmapByte, 1);
        
        if ((bitmapByte & (1 << bitIndex)) == 0) {
            freeBlock = b;
            bitmapByte |= (1 << bitIndex);
            file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
            file.write(&bitmapByte, 1);
            break;
        }
    }
    
    if (freeBlock == -1) {
        file.close();
        return "Error: No hay bloques disponibles";
    }
    
    usersInode.i_block[blockIndex] = freeBlock;
    
    // Escribir bloque
    file.seekp(sb.s_block_start + freeBlock * sizeof(BloqueArchivos), std::ios::beg);
    file.write(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
    
    pos += chunkSize;
    blockIndex++;
    blocksUsed++;
}

// Actualizar inodo
usersInode.i_s = usersContent.length();
usersInode.i_mtime = time(nullptr);
file.seekp(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));

// Actualizar superbloque
sb.s_free_blocks_count -= blocksUsed;
sb.s_mtime = time(nullptr);
file.seekp(partStart, std::ios::beg);
file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));


        while (pos < usersContent.length() && blockIndex < 12) {
            BloqueArchivos block;
            memset(&block, 0, sizeof(BloqueArchivos));
            
            size_t chunkSize = std::min(size_t(64), usersContent.length() - pos);
            strncpy(block.b_content, usersContent.c_str() + pos, chunkSize);
            
            if (usersInode.i_block[blockIndex] == -1) {
                // Asignar nuevo bloque desde bitmap
                for (int b = sb.s_first_blo; b < sb.s_blocks_count; b++) {
                    int byteIndex = b / 8;
                    int bitIndex = b % 8;
                    
                    file.seekg(sb.s_bm_block_start + byteIndex, std::ios::beg);
                    char bitmapByte;
                    file.read(&bitmapByte, 1);
                    
                    if ((bitmapByte & (1 << bitIndex)) == 0) {
                        usersInode.i_block[blockIndex] = b;
                        
                        // Marcar como ocupado
                        bitmapByte |= (1 << bitIndex);
                        file.seekp(sb.s_bm_block_start + byteIndex, std::ios::beg);
                        file.write(&bitmapByte, 1);
                        
                        break;
                    }
                }
            }
            
            file.seekp(sb.s_block_start + usersInode.i_block[blockIndex] * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            
            pos += chunkSize;
            blockIndex++;
        }
        
        // Actualizar inodo
        usersInode.i_s = usersContent.length();
        usersInode.i_mtime = time(nullptr);
        file.seekp(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
        file.write(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        
        // Actualizar superbloque
        sb.s_free_blocks_count -= blockIndex;
        sb.s_mtime = time(nullptr);
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.close();

        // Después de crear el usuario:
        std::string partitionId = getSessionPartitionId();
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "CREATE_USER",
        "/users.txt",
        "Usuario: " + user + ", GID: " + std::to_string(groupGid)
    );
}
        
        //Mensaje de éxito
        std::ostringstream result;
        result << "----- MKUSR -----\n";
        result << "Usuario creado exitosamente\n";
        result << "  Usuario: " << user << "\n";
        result << "  UID: " << newUid << "\n";
        result << "  Grupo: " << grp << " (GID: " << groupGid << ")";
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en mkusr: ") + e.what();
    }
}

#endif