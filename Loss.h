// loss.h
#ifndef LOSS_H
#define LOSS_H

#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include "SuperBloque.h"
#include "Mount.h"
#include "Journaling.h"

inline std::string ejecutarLoss(const std::string& input) {
    std::ostringstream salida;

    //Parsear -id
    std::string id;
    size_t pos = input.find("-id=");
    if (pos == std::string::npos) {
        return "Error: Parámetro -id es obligatorio para loss";
    }
    
    size_t start = pos + 4;
    size_t end = input.find(' ', start);
    id = (end == std::string::npos) ? input.substr(start) : input.substr(start, end - start);
    
    if (id.empty()) {
        return "Error: ID no puede estar vacío";
    }

    //Buscar partición montada
    MountedPartition* target = nullptr;
    for (auto& mp : mounted_list) {
        if (mp.id == id) {
            target = &mp;
            break;
        }
    }
    if (!target) {
        return "Error: Partición con ID '" + id + "' no está montada";
    }

    //Abrir disco
    std::fstream file(target->path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return "Error: No se pudo abrir el disco";
    }

    //Leer MBR y SuperBloque
    MBR mbr;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    
    long particionStart = mbr.mbr_partitions[target->partition_index].part_start;
    
    SuperBloque sb;
    file.seekg(particionStart);
    file.read(reinterpret_cast<char*>(&sb), sb.s_block_s);

    // 5. Validar EXT3
    if (sb.s_filesystem_type != 3) {
        file.close();
        return "Error: LOSS solo aplica a EXT3";
    }

    //Cálculos
    int sizeBmInodos = (sb.s_inodes_count + 7) / 8;
    int sizeBmBloques = (sb.s_blocks_count + 7) / 8;
    int sizeAreaInodos = sb.s_inodes_count * sb.s_inode_s;
    int sizeAreaBloques = sb.s_blocks_count * sb.s_block_s;

    std::vector<char> buffer(1024 * 1024, '\0');

    auto limpiarRango = [&](long inicio, int totalBytes) {
        file.seekp(inicio);
        int escritos = 0;
        while (escritos < totalBytes) {
            int aEscribir = std::min((int)buffer.size(), totalBytes - escritos);
            file.write(buffer.data(), aEscribir);
            escritos += aEscribir;
        }
    };

    //Limpiar
    limpiarRango(sb.s_bm_inode_start, sizeBmInodos);
    limpiarRango(sb.s_bm_block_start, sizeBmBloques);
    limpiarRango(sb.s_inode_start, sizeAreaInodos);
    limpiarRango(sb.s_block_start, sizeAreaBloques);

    file.flush();
    file.close();

    //Journal
    CommandJournaling::add(id, "LOSS", id, "Sistema EXT3 limpiado");

    // Salida
    salida << "===== LOSS =====\n";
    salida << "Pérdida simulada en " << id << "\n";
    salida << "Inodos: " << sizeAreaInodos << " bytes\n";
    salida << "Bloques: " << sizeAreaBloques << " bytes\n";

    return salida.str();
}

#endif