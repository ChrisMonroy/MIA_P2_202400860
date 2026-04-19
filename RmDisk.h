#ifndef RMDISK_H
#define RMDISK_H

#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/stat.h>
#include <filesystem>
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "Mbr.h"
#include "utils.h"
#include "globals.h"

namespace CommandRmdisk {

    // Extrae el valor de -path del comando
    inline std::string extractPath(const std::string& input) {
        std::string path;
        size_t pos = input.find("-path=");
        
        if (pos == std::string::npos) {
            return "";
        }
        
        size_t start = pos + 6;
        size_t end = input.find(' ', start);
        
        if (end == std::string::npos) {
            path = input.substr(start);
        } else {
            path = input.substr(start, end - start);
        }
        
        // Remover comillas si existen
        if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
            path = path.substr(1, path.size() - 2);
        }
        
        return path;
    }

    inline std::string execute(const std::string& input) {
        try {
            // 1. Extraer path del comando
            std::string path = extractPath(input);
            
            if (path.empty()) {
                return "Error: -path es obligatorio";
            }
            
            // 2. Expandir ruta
            std::string expandedPath = expandPath(path);
            
            // 3. Verificar existencia
            if (!std::filesystem::exists(expandedPath)) {
                return "Error: El disco no existe en la ruta especificada";
            }

            // 4. Leer MBR para información (requerimiento del proyecto)
            std::ifstream diskFile(expandedPath, std::ios::binary);
            MBR mbr;
            diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            diskFile.close();

            // 5. Formatear fecha
            char timeStr[26];
            struct tm* timeinfo = localtime(&mbr.mbr_fecha_creacion);
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);

            // 6. Eliminar archivo
            if (!std::filesystem::remove(expandedPath)) {
                return "Error: No se pudo eliminar el archivo del disco";
            }

            return "Disco eliminado exitosamente:\n" +
                   std::string("  Ruta: ") + expandedPath + "\n" +
                   std::string("  Tamaño: ") + std::to_string(mbr.mbr_tamano) + " bytes\n" +
                   std::string("  Fecha creación: ") + timeStr + "\n" +
                   std::string("  Firma: ") + std::to_string(mbr.mbr_dsk_signature);

        } catch (const std::exception& e) {
            return std::string("Error al eliminar disco: ") + e.what();
        }
    }

}

#endif