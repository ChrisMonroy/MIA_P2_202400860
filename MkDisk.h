#ifndef MKDISK_H
#define MKDISK_H

#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include "Mbr.h"
#include "utils.h"
#include "globals.h"

// Crear directorios padre si no existen
bool createParentDirectories(const std::string& path) {
    try {
        std::filesystem::path filePath(path);
        std::filesystem::path parentPath = filePath.parent_path();
        
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            std::filesystem::create_directories(parentPath);
        }
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

//Comando MkDisk
std::string Mkdisk(const std::string& input) {
    try {
        int size = -1;
        std::string unit = "M";      // Default: Megabytes
        std::string fit = "FF";      // Default: First Fit
        std::string path = "";
        
        //parseo
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-size=") == 0) {
                size = std::atoi(token.substr(6).c_str());
            }
            else if (token.find("-unit=") == 0) {
                unit = token.substr(6);
                if (unit.size() >= 2 && unit.front() == '"' && unit.back() == '"') {
                    unit = unit.substr(1, unit.size() - 2);
                }
            }
            else if (token.find("-fit=") == 0) {
                fit = token.substr(5);
                if (fit.size() >= 2 && fit.front() == '"' && fit.back() == '"') {
                    fit = fit.substr(1, fit.size() - 2);
                }
            }
            else if (token.find("-path=") == 0) {
                path = token.substr(6);
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
                    path = path.substr(1, path.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }

start = 0;
while (start < input.length()) {
    size_t space = input.find(' ', start);
    std::string token = (space == std::string::npos)
        ? input.substr(start)
        : input.substr(start, space - start);
    
    std::string tokenLower = toLowerCase(token);
    
    if (tokenLower.find("-param=") == 0) {
        return "Error: Parámetro '-param' no es válido";
    }
    
    if (tokenLower.find("-tamaño=") == 0) {
        return "Error: Parámetro '-tamaño' no es válido. Use '-size'";
    }
    
    if (space == std::string::npos) break;
    start = space + 1;
}

        //Validar 
        if (size <= 0) {
            return "Error: -size es obligatorio y debe ser mayor a 0";
        }
        
        if (path.empty()) {
            return "Error: -path es obligatorio";
        }
    
        int sizeInBytes = 0;
        if (unit == "K" || unit == "k") {
            sizeInBytes = size * 1024;
        } else if (unit == "M" || unit == "m") {
            sizeInBytes = size * 1024 * 1024;
        } else if (unit == "B" || unit == "b") {
            sizeInBytes = size;
        } else {
            return "Error: Unidad inválida. Use B, K o M";
        }
        
        char diskFit = 'F';  // First Fit por defecto
        std::string fitUpper = fit;
        for (auto& c : fitUpper) c = toupper(c);
        
        if (fitUpper == "BF") {
            diskFit = 'B';
        } else if (fitUpper == "FF") {
            diskFit = 'F';
        } else if (fitUpper == "WF") {
            diskFit = 'W';
        } else if (!fit.empty()) {
            return "Error: Fit inválido. Use BF, FF o WF";
        }
        
        std::string expandedPath = expandPath(path);
        
        if (!createParentDirectories(expandedPath)) {
            return "Error: No se pudieron crear las carpetas necesarias";
        }
        
        std::ifstream checkFile(expandedPath);
        if (checkFile.good()) {
            checkFile.close();
            return "Error: El disco ya existe en la ruta especificada";
        }
        checkFile.close();
        
        std::ofstream diskFile(expandedPath, std::ios::binary);
        if (!diskFile.is_open()) {
            return "Error: No se pudo crear el archivo del disco";
        }
        
        //Llenar con 0
        const int BUFFER_SIZE = 1024;
        char buffer[BUFFER_SIZE] = {0};  // Inicializado en ceros
        
        int remaining = sizeInBytes;
        while (remaining > 0) {
            int toWrite = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
            diskFile.write(buffer, toWrite);
            remaining -= toWrite;
        }
        
        MBR mbr;
        memset(&mbr, 0, sizeof(MBR));
        
        mbr.mbr_tamano = sizeInBytes;
        mbr.mbr_fecha_creacion = time(nullptr);
        mbr.mbr_dsk_signature = rand() + time(nullptr);
        mbr.dsk_fit = diskFit;
        
        // Inicializar las 4 particiones
        for (int i = 0; i < 4; i++) {
            mbr.mbr_partitions[i].part_status = '0';
            mbr.mbr_partitions[i].part_type = '\0';
            mbr.mbr_partitions[i].part_fit = '\0';
            mbr.mbr_partitions[i].part_start = -1;
            mbr.mbr_partitions[i].part_size = 0;  
            mbr.mbr_partitions[i].part_correlative = -1;
            memset(mbr.mbr_partitions[i].part_name, 0, 16);
            memset(mbr.mbr_partitions[i].part_id, 0, 4);
        }
        
        // Escribir MBR al inicio del disco
        diskFile.seekp(0, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        diskFile.close();

        char timeStr[26];
        struct tm* timeinfo = localtime(&mbr.mbr_fecha_creacion);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        std::ostringstream result;
        result << "----- MKDISK -----\n";
        result << "Disco creado exitosamente\n";
        result << "  Ruta: " << expandedPath << "\n";
        result << "  Tamaño: " << size << " " << unit 
               << " (" << sizeInBytes << " bytes)\n";
        result << "  Fit: " << fit << "\n";
        result << "  Fecha: " << timeStr << "\n";
        result << "  Firma: " << mbr.mbr_dsk_signature;
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en mkdisk: ") + e.what();
    }
}

std::string Rmdisk(const std::string& input) {
    try {
        std::string path = "";
        
        //parseo
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            if (token.find("-path=") == 0) {
                path = token.substr(6);
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
                    path = path.substr(1, path.size() - 2);
                }
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        if (path.empty()) {
            return "Error: -path es obligatorio para rmdisk";
        }
        
        std::string expandedPath = expandPath(path);
        
        if (!std::filesystem::exists(expandedPath)) {
            return "Error: El disco no existe en la ruta especificada";
        }
        
        //Leemos el MBR
        std::ifstream diskFile(expandedPath, std::ios::binary);
        MBR mbr;
        diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        diskFile.close();
        
        char timeStr[26];
        struct tm* timeinfo = localtime(&mbr.mbr_fecha_creacion);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
        
        std::ostringstream result;
        result << "----- RMDISK -----\n";
        result << "Disco eliminado exitosamente\n";
        result << "  Ruta: " << expandedPath << "\n";
        result << "  Tamaño: " << mbr.mbr_tamano << " bytes\n";
        result << "  Fecha Creación: " << timeStr << "\n";
        result << "  Firma: " << mbr.mbr_dsk_signature;
        
        //Eliminar archivo
        if (!std::filesystem::remove(expandedPath)) {
            return "Error: No se pudo eliminar el archivo del disco";
        }
        
        return result.str();
        
    } catch (const std::exception& e) {
        return std::string("Error en rmdisk: ") + e.what();
    }
}

#endif