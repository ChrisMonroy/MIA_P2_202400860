#ifndef MKGRP_H
#define MKGRP_H
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "Login.h"
#include "globals.h"
#include "Journaling.h"

std::string Mkgrp(const std::string& input){
    try {
        if (!hasActiveSession()){
            return "Error: No hay sesion activa";
        }
        if (!isRootUser()){
            return "Error: SOlo el usuario root puede crear grupos";
        }

        std::string name = "";

        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-name=") == 0) {
                name = token.substr(6);
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
                    name = name.substr(1, name.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }

        //Validar los parametros
        if (name.empty()){
            return "Error: El nombre es Obligatorio";
        }

        if (name.length() > 10){
            return "Error: El nombre no debe ser mayor a 10";
        }
        std::string partitionId = getSessionPartitionId();
        std::string partitionPath = getSessionPartitionPath();
        int partitionIndex = getSessionPartitionIndex();
        
        if (partitionPath.empty()) {
            return "Error: No hay información de partición en la sesión";
        }

        //Abrimos el disco
        std::fstream file(partitionPath, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            return "Error: No se pudo abrir el disco";
        }

        //Leemos el superBloque
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[partitionIndex];
        long partStart = part.part_start;
        
        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

        //Leemos el txt
        std::string usersContent = "";
        Inodos usersInode;
        file.seekg(sb.s_inode_start + 1 * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&usersInode), sizeof(Inodos));
        
        if (usersInode.i_type != '0') {
            file.close();
            return "Error: users.txt no es un archivo válido";
        }
        
        // Leer bloques del archivo
        for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
            BloqueArchivos block;
            file.seekg(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
            usersContent += std::string(block.b_content);
        }

        //Verificamos si el grupo ya existe:
        std::istringstream stream(usersContent);
        std::string line;
        int maxGid = 1;
        
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lineStream(line);
            
            while (std::getline(lineStream, field, ',')) {
                fields.push_back(field);
            }
            
            if (fields.size() >= 3) {
                int id = std::atoi(fields[0].c_str());
                std::string type = fields[1];
                std::string groupName = fields[2];
                
                if (type == "G" && id != 0) {
                    if (groupName == name) {
                        file.close();
                        return "Error: Ya existe un grupo con ese nombre";
                    }
                    if (id > maxGid) maxGid = id;
                }
            }
        }

        //Creacion del nuevo Grupo:
        int newGid = maxGid + 1;
        std::string newGroup = std::to_string(newGid) + ",G," + name + "\n";
        usersContent += newGroup;

        //Se actualiza el txt
        for (int i = 0; i < 12 && usersInode.i_block[i] != -1; i++) {
            BloqueArchivos emptyBlock;
            memset(&emptyBlock, 0, sizeof(BloqueArchivos));
            file.seekp(sb.s_block_start + usersInode.i_block[i] * sizeof(BloqueArchivos), std::ios::beg);
            file.write(reinterpret_cast<char*>(&emptyBlock), sizeof(BloqueArchivos));
        }
        
        // Escribir nuevo contenido
        int blockIndex = 0;
        size_t pos = 0;
        
        while (pos < usersContent.length() && blockIndex < 12) {
            BloqueArchivos block;
            memset(&block, 0, sizeof(BloqueArchivos));
            
            size_t chunkSize = std::min(size_t(64), usersContent.length() - pos);
            strncpy(block.b_content, usersContent.c_str() + pos, chunkSize);
            
            if (usersInode.i_block[blockIndex] == -1) {
                // Asignar nuevo bloque desde bitmap
                for (int b = sb.s_first_blo; b < sb.s_blocks_count; b++) {
                    // Verificar bitmap (implementación simplificada)
                    usersInode.i_block[blockIndex] = b;
                    break;
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
        sb.s_mtime = time(nullptr);
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        file.close();

        // Después de crear el grupo:
        if (!partitionId.empty()) {
        CommandJournaling::add(
        partitionId,
        "CREATE_GROUP",
        "/users.txt",
        "Grupo: " + name + ", GID: " + std::to_string(newGid)
    );
}

        //Retornamos que fue un exito
        std::ostringstream result;
        result << "MKGRP \n";
        result << "Grupo creado exitosamente\n";
        result << "  Nombre: " << name << "\n";
        result << "  GID: " << newGid;
        
        return result.str();

    }catch(const std::exception& e) {
        return std::string("Error en mkgrp: ") + e.what();
    }
}


#endif