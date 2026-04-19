#ifndef LOGOUT_H
#define LOGOUT_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "globals.h"

extern bool is_logged;
extern char current_user[16];
extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;

extern int session_uid;
extern int session_gid;
extern std::string session_partition_id;
extern std::string session_partition_path;
extern int session_partition_index;


std::string Logout() {
    try {

        if (!is_logged) {
            return "Error: No hay sesión activa";
        }
        

        std::string userName = std::string(current_user);
        

        if (session_partition_path.empty() || session_partition_index == -1) {
            // No hay información de partición, solo cerrar sesión
            is_logged = false;
            current_user[0] = '\0';
            session_uid = -1;
            session_gid = -1;
            session_partition_id = "";
            session_partition_path = "";
            session_partition_index = -1;
            
            std::ostringstream result;
            result << "----- LOGOUT -----\n";
            result << "Sesión cerrada correctamente\n";
            result << "  Usuario: " << userName;
            return result.str();
        }
        

        std::fstream file(session_partition_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file.is_open()) {
            // No se pudo abrir, pero cerramos sesión de todos modos
            is_logged = false;
            current_user[0] = '\0';
            session_uid = -1;
            session_gid = -1;
            session_partition_id = "";
            session_partition_path = "";
            session_partition_index = -1;
            
            std::ostringstream result;
            result << "----- LOGOUT -----\n";
            result << "Sesión cerrada correctamente\n";
            result << "  Usuario: " << userName;
            return result.str();
        }
        
        MBR mbr;
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        Partition& part = mbr.mbr_partitions[session_partition_index];
        long partStart = part.part_start;

        SuperBloque sb;
        file.seekg(partStart, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        sb.s_umtime = time(nullptr);
        
        file.seekp(partStart, std::ios::beg);
        file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        file.close();
        
 
        is_logged = false;
        current_user[0] = '\0';
        session_uid = -1;
        session_gid = -1;
        session_partition_id = "";
        session_partition_path = "";
        session_partition_index = -1;
        

        std::ostringstream result;
        result << "----- LOGOUT -----\n";
        result << "Sesión cerrada correctamente\n";
        result << "  Usuario: " << userName;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en logout: ") + e.what();
    }
}

#endif