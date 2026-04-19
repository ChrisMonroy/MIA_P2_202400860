#ifndef FDISK_H
#define FDISK_H

#include <string>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include "Mbr.h"
#include "utils.h"
#include "globals.h"

namespace CommandFdisk{

// Parsear el comando fdisk y extraer parámetros
struct FdiskParams {
    int size = -1;
    std::string unit = "M";
    std::string path = "";
    std::string type = "P";
    std::string fit = "WF";
    std::string name = "";
    std::string deleteMode = ""; //Puede ser fast o Full
    int addSize = 0;
    bool hasAdd = false;
    bool hasDelete = false;
};

FdiskParams parseFdiskCommand(const std::string& input) {
    FdiskParams params;
    
    size_t start = 0;
    while (start < input.length()) {
        size_t space = input.find(' ', start);
        std::string token = (space == std::string::npos) 
            ? input.substr(start) 
            : input.substr(start, space - start);
        
        // Parsear cada parámetro
        if (token.find("-size=") == 0) {
            params.size = std::atoi(token.substr(6).c_str());
        }
        else if (token.find("-unit=") == 0) {
            params.unit = token.substr(6);
        }
        else if (token.find("-path=") == 0) {
            params.path = token.substr(6);
            // Remover comillas
            if (params.path.size() >= 2 && params.path.front() == '"' && params.path.back() == '"') {
                params.path = params.path.substr(1, params.path.size() - 2);
            }
        }
        else if (token.find("-type=") == 0) {
            params.type = token.substr(6);
        }
        else if (token.find("-fit=") == 0) {
            params.fit = token.substr(5);
        }
        else if (token.find("-name=") == 0) {
            params.name = token.substr(6);
            // Remover comillas
            if (params.name.size() >= 2 && params.name.front() == '"' && params.name.back() == '"') {
                params.name = params.name.substr(1, params.name.size() - 2);
            }
        }
        else if (token.find("-delete=") == 0) {
            params.deleteMode = token.substr(8);
            params.hasDelete = true;
        }
        else if (token.find("-add=") == 0) {
            params.addSize = std::atoi(token.substr(5).c_str());
            params.hasAdd = true;
        }

        if (space == std::string::npos) break;
        start = space + 1;
    }
    
    return params;
}

// Calcular tamaño en bytes según unidad
long long calculateSizeInBytes(int size, const std::string& unit) {
    if (unit == "b" || unit == "B") {
        return static_cast<long long>(size);
    } else if (unit == "k" || unit == "K") {
        return static_cast<long long>(size) * 1024LL;
    } else if (unit == "m" || unit == "M") {
        return static_cast<long long>(size) * 1024LL * 1024LL;
    }
    return -1;  // Unidad inválida
}

// Encontrar espacio libre según el algoritmo de fit
int findFreeSlot(MBR& mbr, long long partitionSize, char fit, int& outStart) {
    int selectedSlot = -1;
    int bestStart = -1;
    
    if (fit == 'F') {  // First Fit - Primer espacio disponible
        int currentPos = sizeof(MBR);
        
        // Ordenar particiones por start para calcular espacios correctamente
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '0') {
                // Calcular posición actual considerando particiones existentes
                currentPos = sizeof(MBR);
                for (int j = 0; j < 4; j++) {
                    if (mbr.mbr_partitions[j].part_status == '1') {
                        int partEnd = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                        if (partEnd > currentPos) {
                            currentPos = partEnd;
                        }
                    }
                }
                
                long long availableSpace = mbr.mbr_tamano - currentPos;
                if (availableSpace >= partitionSize) {
                    selectedSlot = i;
                    bestStart = currentPos;
                    break;  // First fit - tomar el primero
                }
            }
        }
    } else if (fit == 'B') {  // Best Fit - Menor desperdicio
        long long minWaste = mbr.mbr_tamano;
        
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '0') {
                int currentPos = sizeof(MBR);
                for (int j = 0; j < 4; j++) {
                    if (mbr.mbr_partitions[j].part_status == '1') {
                        int partEnd = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                        if (partEnd > currentPos) {
                            currentPos = partEnd;
                        }
                    }
                }
                
                long long availableSpace = mbr.mbr_tamano - currentPos;
                long long waste = availableSpace - partitionSize;
                
                if (availableSpace >= partitionSize && waste < minWaste) {
                    selectedSlot = i;
                    bestStart = currentPos;
                    minWaste = waste;
                }
            }
        }
    } else {  // Worst Fit - Mayor espacio disponible (default)
        long long maxSpace = 0;
        
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_status == '0') {
                int currentPos = sizeof(MBR);
                for (int j = 0; j < 4; j++) {
                    if (mbr.mbr_partitions[j].part_status == '1') {
                        int partEnd = mbr.mbr_partitions[j].part_start + mbr.mbr_partitions[j].part_size;
                        if (partEnd > currentPos) {
                            currentPos = partEnd;
                        }
                    }
                }
                
                long long availableSpace = mbr.mbr_tamano - currentPos;
                if (availableSpace >= partitionSize && availableSpace > maxSpace) {
                    selectedSlot = i;
                    bestStart = currentPos;
                    maxSpace = availableSpace;
                }
            }
        }
    }
    
    outStart = bestStart;
    return selectedSlot;
}

// Crear partición primaria o extendida
std::string createPrimaryOrExtended(const std::string& path, long long size, 
                                    char type, char fit, const std::string& name) {
    std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!diskFile.is_open()) {
        return "Error: No se pudo abrir el disco";
    }

    MBR mbr;
    diskFile.seekg(0, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Validar nombre único
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' && 
            strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
            diskFile.close();
            return "Error: Ya existe una partición con ese nombre";
        }
    }

    // Contar particiones y verificar si hay extendida
    int partCount = 0;
    bool hasExtended = false;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1') {
            partCount++;
            if (mbr.mbr_partitions[i].part_type == 'E') {
                hasExtended = true;
            }
        }
    }

    if (partCount >= 4) {
        diskFile.close();
        return "Error: Máximo 4 particiones permitidas";
    }

    if (type == 'E' && hasExtended) {
        diskFile.close();
        return "Error: Ya existe una partición extendida";
    }

    // Encontrar espacio según fit
    int selectedSlot = -1;
    int startByte = -1;
    selectedSlot = findFreeSlot(mbr, size, fit, startByte);

    if (selectedSlot == -1 || startByte == -1) {
        diskFile.close();
        return "Error: No hay espacio suficiente en el disco";
    }

    // Crear la partición
    mbr.mbr_partitions[selectedSlot].part_status = '1';
    mbr.mbr_partitions[selectedSlot].part_type = type;
    mbr.mbr_partitions[selectedSlot].part_fit = fit;
    mbr.mbr_partitions[selectedSlot].part_start = startByte;
    mbr.mbr_partitions[selectedSlot].part_size = static_cast<int>(size);
    strncpy(mbr.mbr_partitions[selectedSlot].part_name, name.c_str(), 16);
    mbr.mbr_partitions[selectedSlot].part_correlative = -1;
    memset(mbr.mbr_partitions[selectedSlot].part_id, 0, 4);

    // Si es extendida, inicializar con EBR
    if (type == 'E') {
        EBR ebr;
        ebr.part_status = '0';
        ebr.part_fit = fit;
        ebr.part_start = -1;
        ebr.part_size = 0;
        ebr.part_next = -1;
        memset(ebr.part_name, 0, 16);

        diskFile.seekp(startByte, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
    }

    // Escribir MBR actualizado
    diskFile.seekp(0, std::ios::beg);
    diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    diskFile.close();

    return "Partición " + std::string(1, type) + " '" + name + "' creada exitosamente\n" +
           "  Inicio: " + std::to_string(startByte) + "\n" +
           "  Tamaño: " + std::to_string(size) + " bytes\n" +
           "  Ajuste: " + std::string(1, fit);
}

// Crear partición lógica
std::string createLogical(const std::string& path, long long size, 
                          char fit, const std::string& name) {
    std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!diskFile.is_open()) {
        return "Error: No se pudo abrir el disco";
    }

    MBR mbr;
    diskFile.seekg(0, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar partición extendida
    int extendedIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' && 
            mbr.mbr_partitions[i].part_type == 'E') {
            extendedIndex = i;
            break;
        }
    }

    if (extendedIndex == -1) {
        diskFile.close();
        return "Error: No existe partición extendida para crear lógicas";
    }

    Partition& extended = mbr.mbr_partitions[extendedIndex];
    int extStart = extended.part_start;
    int extEnd = extended.part_start + extended.part_size;

    // Leer primer EBR
    EBR currentEBR;
    diskFile.seekg(extStart, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

    // Validar nombre único en EBRs existentes
    int currentEBRPos = extStart;
    while (currentEBRPos != -1 && currentEBRPos < extEnd) {
        diskFile.seekg(currentEBRPos, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
        
        if (currentEBR.part_status == '1' && 
            strcmp(currentEBR.part_name, name.c_str()) == 0) {
            diskFile.close();
            return "Error: Ya existe una partición lógica con ese nombre";
        }
        
        currentEBRPos = currentEBR.part_next;
    }

    // Si el primer EBR está vacío
    diskFile.seekg(extStart, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
    
    if (currentEBR.part_status == '0') {
        currentEBR.part_status = '1';
        currentEBR.part_fit = fit;
        currentEBR.part_start = extStart + sizeof(EBR);
        currentEBR.part_size = static_cast<int>(size);
        currentEBR.part_next = -1;
        strncpy(currentEBR.part_name, name.c_str(), 16);

        diskFile.seekp(extStart, std::ios::beg);
        diskFile.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));
        diskFile.close();

        return "Partición lógica '" + name + "' creada exitosamente\n" +
               "  Inicio: " + std::to_string(currentEBR.part_start) + "\n" +
               "  Tamaño: " + std::to_string(size) + " bytes";
    }

    // Buscar el último EBR
    currentEBRPos = extStart;
    while (true) {
        diskFile.seekg(currentEBRPos, std::ios::beg);
        diskFile.read(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

        if (currentEBR.part_next == -1) {
            // Último EBR encontrado
            int nextEBRPos = currentEBR.part_start + currentEBR.part_size;
            int availableSpace = extEnd - nextEBRPos - static_cast<int>(sizeof(EBR));

            if (availableSpace < size) {
                diskFile.close();
                return "Error: No hay espacio suficiente en la partición extendida";
            }

            // Crear nuevo EBR
            EBR newEBR;
            newEBR.part_status = '1';
            newEBR.part_fit = fit;
            newEBR.part_start = nextEBRPos + static_cast<int>(sizeof(EBR));
            newEBR.part_size = static_cast<int>(size);
            newEBR.part_next = -1;
            strncpy(newEBR.part_name, name.c_str(), 16);

            // Actualizar EBR anterior
            currentEBR.part_next = nextEBRPos;
            diskFile.seekp(currentEBRPos, std::ios::beg);
            diskFile.write(reinterpret_cast<char*>(&currentEBR), sizeof(EBR));

            // Escribir nuevo EBR
            diskFile.seekp(nextEBRPos, std::ios::beg);
            diskFile.write(reinterpret_cast<char*>(&newEBR), sizeof(EBR));
            diskFile.close();

            return "Partición lógica '" + name + "' creada exitosamente\n" +
                   "  Inicio: " + std::to_string(newEBR.part_start) + "\n" +
                   "  Tamaño: " + std::to_string(size) + " bytes";
        }

        currentEBRPos = currentEBR.part_next;
    }
}

// Eliminar partición
std::string deletePartition(const std::string& path, const std::string& name, 
                           const std::string& mode) {
    std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!diskFile.is_open()) {
        return "Error: No se pudo abrir el disco";
    }

    MBR mbr;
    diskFile.seekg(0, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar partición por nombre
    int foundIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' && 
            strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex == -1) {
        diskFile.close();
        return "Error: No existe una partición con ese nombre";
    }

    Partition& target = mbr.mbr_partitions[foundIndex];
    
    // Si es extendida, eliminar primero todas las lógicas
    if (target.part_type == 'E') {
        int extStart = target.part_start;
        int extEnd = target.part_start + target.part_size;
        
        // Recorrer cadena de EBRs y marcar como eliminados
        int currentEBRPos = extStart;
        while (currentEBRPos != -1 && currentEBRPos < extEnd) {
            EBR ebr;
            diskFile.seekg(currentEBRPos, std::ios::beg);
            diskFile.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
            
            if (ebr.part_status == '1') {
                // Limpiar espacio de la lógica si es mode=full
                if (mode == "full") {
                    diskFile.seekp(ebr.part_start, std::ios::beg);
                    std::vector<char> zeros(ebr.part_size, 0);
                    diskFile.write(zeros.data(), ebr.part_size);
                }
                // Marcar EBR como vacío
                ebr.part_status = '0';
                memset(ebr.part_name, 0, 16);
                diskFile.seekp(currentEBRPos, std::ios::beg);
                diskFile.write(reinterpret_cast<char*>(&ebr), sizeof(EBR));
            }
            currentEBRPos = ebr.part_next;
        }
    }

    // Eliminar la partición del MBR
    target.part_status = '0';
    memset(target.part_name, 0, 16);
    target.part_correlative = 0; 
    memset(target.part_id, 0, 4);

    // Si es mode=full, limpiar el espacio en disco
    if (mode == "full") {
        diskFile.seekp(target.part_start, std::ios::beg);
        std::vector<char> zeros(target.part_size, 0);
        diskFile.write(zeros.data(), target.part_size);
    }

    // Escribir MBR actualizado
    diskFile.seekp(0, std::ios::beg);
    diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    diskFile.close();

    return "Partición '" + name + "' eliminada exitosamente (modo: " + mode + ")";
}

std::string addSpaceToPartition(const std::string& path, const std::string& name, 
                                int deltaSize, const std::string& unit) {
    // Calcular delta en bytes
    long long deltaBytes = calculateSizeInBytes(abs(deltaSize), unit);
    if (deltaBytes < 0) return "Error: Unidad inválida en -add";
    if (deltaSize < 0) deltaBytes = -deltaBytes;  // Mantener signo negativo

    std::fstream diskFile(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!diskFile.is_open()) {
        return "Error: No se pudo abrir el disco";
    }

    MBR mbr;
    diskFile.seekg(0, std::ios::beg);
    diskFile.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar partición
    int foundIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_status == '1' && 
            strcmp(mbr.mbr_partitions[i].part_name, name.c_str()) == 0) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex == -1) {
        diskFile.close();
        return "Error: No existe una partición con ese nombre";
    }

    Partition& target = mbr.mbr_partitions[foundIndex];
    long long newEnd = target.part_start + target.part_size + deltaBytes;
    long long newStart = target.part_start;

    // Validaciones para QUITAR espacio
    if (deltaBytes < 0) {
        if (target.part_size + deltaBytes <= 0) {
            diskFile.close();
            return "Error: No se puede reducir a tamaño cero o negativo";
        }
    }
    // Validaciones para AGREGAR espacio
    else {
        // Verificar que haya espacio libre DESPUÉS de la partición
        long long diskEnd = mbr.mbr_tamano;
        
        // Revisar si hay otra partición que empiece donde queremos expandir
        for (int i = 0; i < 4; i++) {
            if (i != foundIndex && mbr.mbr_partitions[i].part_status == '1') {
                Partition& other = mbr.mbr_partitions[i];
                // Si otra partición está después y se solapa con la expansión
                if (other.part_start >= target.part_start && 
                    other.part_start < newEnd) {
                    diskFile.close();
                    return "Error: No hay espacio libre después de la partición";
                }
            }
        }
        
        if (newEnd > diskEnd) {
            diskFile.close();
            return "Error: La expansión excede el tamaño del disco";
        }
    }

    // Actualizar tamaño de la partición
    target.part_size = static_cast<int>(target.part_size + deltaBytes);

    // Escribir MBR actualizado
    diskFile.seekp(0, std::ios::beg);
    diskFile.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    diskFile.close();

    std::string action = (deltaBytes > 0) ? "agregado" : "reducido";
    return "Espacio " + action + " a partición '" + name + "'\n" +
           "  Nuevo tamaño: " + std::to_string(target.part_size) + " bytes";
}

inline std::string FDisk(const std::string& input) {
    try {
        // Parsear comando
        FdiskParams params = parseFdiskCommand(input);

        // Validar parámetros obligatorios
        if (params.path.empty()) {
            return "Error: -path es obligatorio";
        }
        std::string expandedPath = expandPath(params.path);

        //Eliminacion de la particion
        if (params.hasDelete){
            if(params.deleteMode != "fast" && params.deleteMode != "full"){
                return "Error el modo eliminacion solo lee fast o full";
        }
        if (params.name.empty()) {
                return "Error: -name es obligatorio para eliminar";
            }
        return deletePartition(expandedPath, params.name, params.deleteMode);
        }

        //La creacion del ADD
        if (params.hasAdd){
            if(params.name.empty()){
                return "Error: -name es obligaorio para agregar el espacio";
            }
            if (params.addSize <= 0){
                return "Error: -add dee tener un tamano mayor a 0";
            }
            return addSpaceToPartition(expandedPath, params.name, params.addSize, params.unit);
        }

        // Operación de creación
        if (params.size <= 0) {
            return "Error: -size es obligatorio y debe ser > 0";
        }

        if (params.name.empty()) {
            return "Error: -name es obligatorio";
        }

        if (params.name.length() > 16) {
            return "Error: El nombre no debe exceder 16 caracteres";
        }

        // Verificar que el disco exista
        if (!std::filesystem::exists(expandedPath)) {
            return "Error: El disco no existe en " + expandedPath;
        }

        // Calcular tamaño en bytes
        long long sizeInBytes = calculateSizeInBytes(params.size, params.unit);
        if (sizeInBytes < 0) {
            return "Error: Unidad inválida. Use B, K o M";
        }

        // Validar tipo de partición
        char partType = 'P';
        if (params.type == "P" || params.type == "p") {
            partType = 'P';
        } else if (params.type == "E" || params.type == "e") {
            partType = 'E';
        } else if (params.type == "L" || params.type == "l") {
            partType = 'L';
        } else {
            return "Error: Tipo inválido. Use P, E o L";
        }

        // Validar fit
        char partFit = 'W';  // WF por defecto según PDF
        if (params.fit == "BF" || params.fit == "bf") {
            partFit = 'B';
        } else if (params.fit == "FF" || params.fit == "ff") {
            partFit = 'F';
        } else if (params.fit == "WF" || params.fit == "wf") {
            partFit = 'W';
        } else {
            return "Error: Fit inválido. Use BF, FF o WF";
        }

        // Crear partición según tipo
        if (partType == 'L') {
            return createLogical(expandedPath, sizeInBytes, partFit, params.name);
        } else {
            return createPrimaryOrExtended(expandedPath, sizeInBytes, partType, partFit, params.name);
        }
    } catch (const std::exception& e) {
        return std::string("Error en fdisk: ") + e.what();
    }
}

}
#endif