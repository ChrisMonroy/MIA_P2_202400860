#ifndef REP_H
#define REP_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <functional>
#include "Mbr.h"
#include "Mount.h"
#include "BloqueApuntadores.h"
#include "BloqueArchivos.h"
#include "BloqueCarpeta.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "Login.h"
#include "utils.h"
#include "globals.h"

std::string Rep(const std::string& input) {
    try {
        std::string name, id, path, path_file_ls;
        
        // Parseo de parámetros
        size_t start = 0;
        while (start < input.length()) {
            size_t space = input.find(' ', start);
            std::string token = (space == std::string::npos)
                ? input.substr(start)
                : input.substr(start, space - start);
            
            std::string tokenLower = toLowerCase(token);
            
            if (tokenLower.find("-name=") == 0) {
                name = token.substr(6);
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                    name = name.substr(1, name.size() - 2);
            }
            else if (tokenLower.find("-id=") == 0) {
                id = token.substr(4);
                if (id.size() >= 2 && id.front() == '"' && id.back() == '"')
                    id = id.substr(1, id.size() - 2);
            }
            else if (tokenLower.find("-path=") == 0) {
                path = token.substr(6);
                if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
                    path = path.substr(1, path.size() - 2);
            }
            else if (tokenLower.find("-path_file_ls=") == 0) {
                path_file_ls = token.substr(14);
                if (path_file_ls.size() >= 2 && path_file_ls.front() == '"' && path_file_ls.back() == '"')
                    path_file_ls = path_file_ls.substr(1, path_file_ls.size() - 2);
            }
            
            if (space == std::string::npos) break;
            start = space + 1;
        }
        
        if (name.empty() || id.empty() || path.empty()) {
            return "Error: -name, -id y -path son obligatorios para rep";
        }
        
        // Buscar partición montada
        MountedPartition* mp = nullptr;
        for (auto& p : mounted_list) {
            if (p.id == id) {
                mp = &p;
                break;
            }
        }
        
        if (!mp) {
            return "Error: Partición con ID '" + id + "' no está montada";
        }
        
        std::string nameLower = toLowerCase(name);
        
        //Mbr
        if (nameLower == "mbr") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            MBR mbr;
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            file.close();
            
            // Crear directorio si no existe
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=TB;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            dot << "general [label = <<table>\n";
            dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#102027\"><font color=\"white\">MBR</font></td></tr>\n";
            dot << "<tr><td BGCOLOR=\"#ff6f00\">NOMBRE</td><td BGCOLOR=\"#ff6f00\">VALOR</td></tr>\n";
            dot << "<tr><td>mbr_tamaño</td><td>" << mbr.mbr_tamano << "</td></tr>\n";
            
            // Formatear fecha
            std::time_t fecha = mbr.mbr_fecha_creacion;
            std::tm* tm = std::localtime(&fecha);
            char fechaStr[30];
            std::strftime(fechaStr, 30, "%Y/%m/%d %H:%M:%S", tm);
            dot << "<tr><td>mbr_fecha_creación</td><td>" << fechaStr << "</td></tr>\n";
            dot << "<tr><td>mbr_disk_signature</td><td>" << mbr.mbr_dsk_signature << "</td></tr>\n";
            dot << "<tr><td>Disk_fit</td><td>" << mbr.dsk_fit << "</td></tr>\n";
            
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '1') {
                    std::string pname = mbr.mbr_partitions[i].part_name;
                    pname.erase(std::remove(pname.begin(), pname.end(), '\0'), pname.end());
                    
                    dot << "<tr><td>part_status_" << (i + 1) << "</td><td>" << mbr.mbr_partitions[i].part_status << "</td></tr>\n";
                    dot << "<tr><td>part_type_" << (i + 1) << "</td><td>" << mbr.mbr_partitions[i].part_type << "</td></tr>\n";
                    dot << "<tr><td>part_fit_" << (i + 1) << "</td><td>" << mbr.mbr_partitions[i].part_fit << "</td></tr>\n";
                    dot << "<tr><td>part_start_" << (i + 1) << "</td><td>" << mbr.mbr_partitions[i].part_start << "</td></tr>\n";
                    dot << "<tr><td>part_size_" << (i + 1) << "</td><td>" << mbr.mbr_partitions[i].part_size << "</td></tr>\n";
                    dot << "<tr><td>part_name_" << (i + 1) << "</td><td>" << pname << "</td></tr>\n";
                }
            }
            
            dot << "</table>>];\n";
            dot << "}\n";
            
            // Guardar .dot
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            // Convertir a imagen
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        //Disco 
        else if (nameLower == "disk" || nameLower == "disco") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            MBR mbr;
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            file.close();
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=TB;\n";
            dot << "forcelabels= true;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "node [shape = plaintext];\n";
            dot << "nodo1 [label = <<table>\n";
            dot << "<tr>\n";
            
            // Calcular espacios libres
            int positions[5] = {0, 0, 0, 0, 0};
            positions[0] = mp->start - (1 + sizeof(MBR));
            
            // Espacios entre particiones (simplificado)
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '1') {
                    float res = (float)mbr.mbr_partitions[i].part_size / (float)mbr.mbr_tamano;
                    res = round(res * 10000.00F) / 100.00F;
                    
                    if (mbr.mbr_partitions[i].part_type == 'E') {
                        dot << "<td COLSPAN='2'>Extendida</td>\n";
                    } else {
                        dot << "<td ROWSPAN='2'>Primaria " << res << "% del disco</td>\n";
                    }
                }
            }
            
            dot << "</tr>\n";
            dot << "</table>>];\n";
            dot << "}\n";
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        //Super Bloque
        else if (nameLower == "sb" || nameLower == "superblock") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            file.close();
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=TB;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            dot << "general [label = <<table>\n";
            dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">SUPERBLOCK</font></td></tr>\n";
            dot << "<tr><td BGCOLOR=\"#90EE90\">NOMBRE</td><td BGCOLOR=\"#90EE90\">VALOR</td></tr>\n";
            dot << "<tr><td>s_filesystem_type</td><td>" << sb.s_filesystem_type << "</td></tr>\n";
            dot << "<tr><td>s_inodes_count</td><td>" << sb.s_inodes_count << "</td></tr>\n";
            dot << "<tr><td>s_blocks_count</td><td>" << sb.s_blocks_count << "</td></tr>\n";
            dot << "<tr><td>s_free_inodes_count</td><td>" << sb.s_free_inodes_count << "</td></tr>\n";
            dot << "<tr><td>s_free_blocks_count</td><td>" << sb.s_free_blocks_count << "</td></tr>\n";
            dot << "<tr><td>s_magic</td><td>" << std::hex << sb.s_magic << std::dec << "</td></tr>\n";
            dot << "<tr><td>s_inode_size</td><td>" << sb.s_inode_s << "</td></tr>\n";
            dot << "<tr><td>s_block_size</td><td>" << sb.s_block_s << "</td></tr>\n";
            dot << "<tr><td>s_first_ino</td><td>" << sb.s_firts_ino << "</td></tr>\n";
            dot << "<tr><td>s_first_blo</td><td>" << sb.s_first_blo << "</td></tr>\n";
            dot << "<tr><td>s_bm_inode_start</td><td>" << sb.s_bm_inode_start << "</td></tr>\n";
            dot << "<tr><td>s_bm_block_start</td><td>" << sb.s_bm_block_start << "</td></tr>\n";
            dot << "<tr><td>s_inode_start</td><td>" << sb.s_inode_start << "</td></tr>\n";
            dot << "<tr><td>s_block_start</td><td>" << sb.s_block_start << "</td></tr>\n";
            dot << "</table>>];\n";
            dot << "}\n";
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        //Inodos
        else if (nameLower == "inode" || nameLower == "inodo") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=LR;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            
            int lastInode = -1;
            for (int i = 0; i < sb.s_inodes_count && i < 50; i++) {
                Inodos inode;
                file.seekg(sb.s_inode_start + i * sizeof(Inodos), std::ios::beg);
                file.read(reinterpret_cast<char*>(&inode), sizeof(Inodos));
                
                if (inode.i_type == '0' || inode.i_type == '1') {
                    dot << "inode" << i << " [label = <<table>\n";
                    dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">INODO " << i << "</font></td></tr>\n";
                    dot << "<tr><td BGCOLOR=\"#90EE90\">NOMBRE</td><td BGCOLOR=\"#90EE90\">VALOR</td></tr>\n";
                    dot << "<tr><td>i_uid</td><td>" << (int)inode.i_uid << "</td></tr>\n";
                    dot << "<tr><td>i_gid</td><td>" << (int)inode.i_gid << "</td></tr>\n";
                    dot << "<tr><td>i_size</td><td>" << inode.i_s << "</td></tr>\n";
                    dot << "<tr><td>i_atime</td><td>" << inode.i_atime << "</td></tr>\n";
                    dot << "<tr><td>i_ctime</td><td>" << inode.i_ctime << "</td></tr>\n";
                    dot << "<tr><td>i_mtime</td><td>" << inode.i_mtime << "</td></tr>\n";
                    
                    for (int j = 0; j < 15; j++) {
                        dot << "<tr><td>i_block_" << (j + 1) << "</td><td>" << inode.i_block[j] << "</td></tr>\n";
                    }
                    
                    dot << "<tr><td>i_type</td><td>" << inode.i_type << "</td></tr>\n";
                    dot << "<tr><td>i_perm</td><td>" << inode.i_perm << "</td></tr>\n";
                    dot << "</table>>];\n";
                    
                    if (lastInode != -1) {
                        dot << "inode" << (lastInode) << "-> inode" << i << ";\n";
                    }
                    lastInode = i;
                }
            }
            
            dot << "}\n";
            file.close();
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        // Block
        else if (nameLower == "block" || nameLower == "bloque") {
        std::fstream file(mp->path, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
        return "Error: No se pudo abrir el disco";
        }
    
        SuperBloque sb;
        file.seekg(mp->start, std::ios::beg);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    
        // Crear directorio
        std::string parentPath = path;
        size_t lastSlash = parentPath.find_last_of('/');
        if (lastSlash != std::string::npos && lastSlash > 0) {
        parentPath = parentPath.substr(0, lastSlash);
        std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
        system(mkdirCmd.c_str());
        }
    
        std::string dotPath = path;
        size_t dotPos = dotPath.find_last_of('.');
        if (dotPos != std::string::npos) {
        dotPath = dotPath.substr(0, dotPos);
        }
        dotPath += ".dot";
    
        std::ostringstream dot;
        dot << "digraph G{\n";
        dot << "rankdir=LR;\n";
        dot << "graph [ dpi = \"600\" ];\n";
        dot << "forcelabels= true;\n";
        dot << "node [shape = plaintext];\n";
    
        int lastBlock = -1;
        std::vector<int> visitedBlocks;
    
        //Recorrer inodos y sus bloques referenciados
        for (int i = 0; i < sb.s_inodes_count && i < 50; i++) {
        Inodos inode;
        file.seekg(sb.s_inode_start + i * sizeof(Inodos), std::ios::beg);
        file.read(reinterpret_cast<char*>(&inode), sizeof(Inodos));
        
        if (inode.i_type != '0' && inode.i_type != '1') continue;
        
        // Recorrer los 15 punteros a bloques del inodo
        for (int j = 0; j < 15; j++) {
            if (inode.i_block[j] != -1) {
                int blockIndex = inode.i_block[j];
                
                // Verificar si ya graficamos este bloque
                if (std::find(visitedBlocks.begin(), visitedBlocks.end(), blockIndex) != visitedBlocks.end()) {
                    continue;
                }
                visitedBlocks.push_back(blockIndex);
                
                // Leer bloque como BloqueArchivos
                BloqueArchivos block;
                file.seekg(sb.s_block_start + blockIndex * sizeof(BloqueArchivos), std::ios::beg);
                file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
                
                dot << "BLOCK" << blockIndex << " [label = <<table>\n";
                dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">BLOCK " << blockIndex << "</font></td></tr>\n";
                
                // Extraer contenido legible
                std::string content = "";
                for (int k = 0; k < 30 && block.b_content[k] != '\0'; k++) {
                    char c = block.b_content[k];
                    if (c >= 32 && c <= 126) content += c;
                    else content += '.';
                }
                if (content.empty()) content = "(vacío)";
                
                dot << "<tr><td COLSPAN = '2'>" << content << "</td></tr>\n";
                dot << "</table>>];\n";
                
                // Conectar con el bloque anterior
                if (lastBlock != -1) {
                    dot << "BLOCK" << lastBlock << "-> BLOCK" << blockIndex << ";\n";
                }
                lastBlock = blockIndex;
                }
            }
        }
    
        dot << "}\n";
        file.close();
    
        std::ofstream outFile(dotPath);
        if (!outFile.is_open()) {
            return "Error: No se pudo crear el archivo .dot";
        }
        outFile << dot.str();
        outFile.close();
    
        std::string ext = "jpg";
        size_t extPos = path.find_last_of('.');
        if (extPos != std::string::npos) {
            ext = path.substr(extPos + 1);
        }
        std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
        system(cmd.c_str());
        remove(dotPath.c_str());
    
        return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }

        //Bitmap Block
        else if (nameLower == "bm_block" || nameLower == "bitmap_bloque") {
            std::fstream bmFile(mp->path, std::ios::binary | std::ios::in);
            
            std::string bitmapContent = "=== BITMAP DE BLOQUES ===\n\n";
            char byte;
            SuperBloque sb;
            
            for (int i = 0; i < sb.s_blocks_count && i < 200; i++) {
                if (i % 8 == 0) {
                    bmFile.seekg(sb.s_bm_block_start + (i / 8), std::ios::beg);
                    bmFile.read(&byte, 1);
                }
                int bit = (byte >> (i % 8)) & 1;
                bitmapContent += std::to_string(bit);
                if ((i + 1) % 20 == 0) bitmapContent += "\n";
                else if ((i + 1) % 10 == 0) bitmapContent += " ";
            }
            bmFile.close();
            
            // Crear directorio si no existe
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            // Guardar como archivo de texto
            std::ofstream outFile(path);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo en " + path;
            }
            outFile << bitmapContent;
            outFile.close();
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path + "\n(Formato: texto plano)";
        }
        
        else if (nameLower == "bm_block" || nameLower == "bitmap_bloque") {
            std::fstream bmFile(mp->path, std::ios::binary | std::ios::in);
            
            std::string bitmapContent = "=== BITMAP DE BLOQUES ===\n\n";
            char byte;
            SuperBloque sb;
            
            for (int i = 0; i < sb.s_blocks_count && i < 200; i++) {
                if (i % 8 == 0) {
                    bmFile.seekg(sb.s_bm_block_start + (i / 8), std::ios::beg);
                    bmFile.read(&byte, 1);
                }
                int bit = (byte >> (i % 8)) & 1;
                bitmapContent += std::to_string(bit);
                if ((i + 1) % 20 == 0) bitmapContent += "\n";
                else if ((i + 1) % 10 == 0) bitmapContent += " ";
            }
            bmFile.close();
            
            // Crear directorio si no existe
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            // Guardar como archivo de texto
            std::ofstream outFile(path);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo en " + path;
            }
            outFile << bitmapContent;
            outFile.close();
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path + "\n(Formato: texto plano)";
        }
        //bit map inodos
        else if (nameLower == "bm_inode" || nameLower == "bitmap_inodo") {
            std::fstream bmFile(mp->path, std::ios::binary | std::ios::in);
            
            std::string bitmapContent = "=== BITMAP DE INODOS ===\n\n";
            char byte;
            SuperBloque sb;
            
            for (int i = 0; i < sb.s_inodes_count && i < 200; i++) {
                if (i % 8 == 0) {
                    bmFile.seekg(sb.s_bm_inode_start + (i / 8), std::ios::beg);
                    bmFile.read(&byte, 1);
                }
                int bit = (byte >> (i % 8)) & 1;
                bitmapContent += std::to_string(bit);
                if ((i + 1) % 20 == 0) bitmapContent += "\n";
                else if ((i + 1) % 10 == 0) bitmapContent += " ";
            }
            bmFile.close();
            
            // Crear directorio si no existe
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            // Guardar como archivo de TEXTO (NO DOT)
            std::ofstream outFile(path);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo en " + path;
            }
            outFile << bitmapContent;
            outFile.close();
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path + "\n(Formato: texto plano)";
        }

        //Arbol 
        else if (nameLower == "tree") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=LR;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            
            // Función recursiva para recorrer árbol
            std::function<void(int)> traverseInode = [&](int inodeIndex) {
                if (inodeIndex < 0 || inodeIndex >= sb.s_inodes_count) return;
                
                Inodos inode;
                file.seekg(sb.s_inode_start + inodeIndex * sizeof(Inodos), std::ios::beg);
                file.read(reinterpret_cast<char*>(&inode), sizeof(Inodos));
                
                if (inode.i_type != '0' && inode.i_type != '1') return;
                
                std::string headerColor = (inode.i_type == '1') ? "#000080" : "#CCCC00";
                
                dot << "inode" << inodeIndex << " [label = <<table>\n";
                dot << "<tr><td COLSPAN = '2' BGCOLOR=\"" << headerColor << "\"><font color=\"white\">INODO " << inodeIndex << "</font></td></tr>\n";
                dot << "<tr><td BGCOLOR=\"#87CEFA\">NOMBRE</td><td BGCOLOR=\"#87CEFA\">VALOR</td></tr>\n";
                dot << "<tr><td>i_uid</td><td>" << (int)inode.i_uid << "</td></tr>\n";
                dot << "<tr><td>i_gid</td><td>" << (int)inode.i_gid << "</td></tr>\n";
                dot << "<tr><td>i_size</td><td>" << inode.i_s << "</td></tr>\n";
                dot << "<tr><td>i_type</td><td>" << inode.i_type << "</td></tr>\n";
                dot << "<tr><td>i_perm</td><td>" << inode.i_perm << "</td></tr>\n";
                
                for (int j = 0; j < 15; j++) {
                    if (inode.i_block[j] != -1) {
                        dot << "<tr><td port=\"" << j << "\">i_block_" << (j + 1) << "</td><td>" << inode.i_block[j] << "</td></tr>\n";
                    }
                }
                dot << "</table>>];\n";
                
                // Si es carpeta, procesar bloques
                if (inode.i_type == '1') {
                    for (int b = 0; b < 12 && inode.i_block[b] != -1; b++) {
                        BloqueCarpeta dirBlock;
                        file.seekg(sb.s_block_start + inode.i_block[b] * sizeof(BloqueCarpeta), std::ios::beg);
                        file.read(reinterpret_cast<char*>(&dirBlock), sizeof(BloqueCarpeta));
                        
                        dot << "BLOCK" << inode.i_block[b] << " [label = <<table>\n";
                        dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">BLOCK " << inode.i_block[b] << "</font></td></tr>\n";
                        dot << "<tr><td BGCOLOR=\"#90EE90\">B_NAME</td><td BGCOLOR=\"#90EE90\">B_INODO</td></tr>\n";
                        
                        for (int k = 0; k < 4; k++) {
                            if (dirBlock.b_content[k].b_inodo != -1) {
                                std::string name = dirBlock.b_content[k].b_name;
                                name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
                                
                                if (name != "." && name != "..") {
                                    dot << "<tr><td port=\"" << k << "\">" << name << "</td><td port=\"i" << k << "\">" << dirBlock.b_content[k].b_inodo << "</td></tr>\n";
                                }
                            }
                        }
                        dot << "</table>>];\n";
                        
                        // Conectar inodo -> bloque
                        dot << "inode" << inodeIndex << ":" << b << " -> BLOCK" << inode.i_block[b] << ":header;\n";
                        
                        // Conectar bloque -> inodos hijos
                        for (int k = 0; k < 4; k++) {
                            if (dirBlock.b_content[k].b_inodo != -1) {
                                std::string name = dirBlock.b_content[k].b_name;
                                name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
                                
                                if (name != "." && name != "..") {
                                    dot << "BLOCK" << inode.i_block[b] << ":i" << k << " -> inode" << dirBlock.b_content[k].b_inodo << ":header;\n";
                                    
                                    // Recursivo para carpetas
                                    Inodos childInode;
                                    file.seekg(sb.s_inode_start + dirBlock.b_content[k].b_inodo * sizeof(Inodos), std::ios::beg);
                                    file.read(reinterpret_cast<char*>(&childInode), sizeof(Inodos));
                                    
                                    if (childInode.i_type == '1') {
                                        traverseInode(dirBlock.b_content[k].b_inodo);
                                    }
                                }
                            }
                        }
                    }
                }
            };
            
            // Iniciar desde raíz (inodo 0)
            traverseInode(0);
            
            dot << "}\n";
            file.close();
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        ///Journalist
        else if (nameLower == "journaling") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            if (sb.s_filesystem_type == 2) {
                file.close();
                return "Error: sistema de archivos no válido para journaling";
            }
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=LR;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            
            // Leer journaling después del superblock
            file.seekg(mp->start + sizeof(SuperBloque), std::ios::beg);
            
            // Nota: Ajusta el struct Journaling según tu implementación
            int i = 0;
            char buffer[256];
            while (file.read(buffer, 64) && i < 50) {
                // Verificar si es fin de journaling
                int type = reinterpret_cast<int*>(buffer)[1]; // Ajusta según tu struct
                if (type == -1) break;
                
                dot << "jor" << i << " [label = <<table>\n";
                dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">JOURNALING " << i << "</font></td></tr>\n";
                dot << "<tr><td BGCOLOR=\"#90EE90\">NOMBRE</td><td BGCOLOR=\"#90EE90\">VALOR</td></tr>\n";
                dot << "<tr><td>operation</td><td>" << buffer << "</td></tr>\n";
                dot << "<tr><td>type</td><td>" << type << "</td></tr>\n";
                dot << "</table>>];\n";
                
                if (i != 0) {
                    dot << "jor" << (i - 1) << "-> jor" << i << ";\n";
                }
                i++;
            }
            
            dot << "}\n";
            file.close();
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        ///Ls
        else if (nameLower == "ls") {
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            Inodos rootInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&rootInode), sizeof(Inodos));
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=LR;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            dot << "files [label = <<table>\n";
            dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#145A32\"><font color=\"white\">DIRECTORIO</font></td></tr>\n";
            dot << "<tr><td BGCOLOR=\"#90EE90\">NOMBRE</td><td BGCOLOR=\"#90EE90\">INODO</td></tr>\n";
            
            if (rootInode.i_block[0] != -1) {
                BloqueCarpeta rootBlock;
                file.seekg(sb.s_block_start + rootInode.i_block[0] * sizeof(BloqueCarpeta), std::ios::beg);
                file.read(reinterpret_cast<char*>(&rootBlock), sizeof(BloqueCarpeta));
                
                for (int i = 0; i < 4; i++) {
                    if (rootBlock.b_content[i].b_inodo != -1) {
                        std::string entryName = rootBlock.b_content[i].b_name;
                        entryName.erase(std::remove(entryName.begin(), entryName.end(), '\0'), entryName.end());
                        
                        if (entryName != "." && entryName != "..") {
                            dot << "<tr><td>" << entryName << "</td><td>" << rootBlock.b_content[i].b_inodo << "</td></tr>\n";
                        }
                    }
                }
            }
            
            dot << "</table>>];\n";
            dot << "}\n";
            file.close();
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        //File
        else if (nameLower == "file") {
            if (path_file_ls.empty()) {
                return "Error: el reporte file requiere -path_file_ls con la ruta del archivo";
            }
            
            std::fstream file(mp->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) {
                return "Error: No se pudo abrir el disco";
            }
            
            SuperBloque sb;
            file.seekg(mp->start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            // Crear directorio
            std::string parentPath = path;
            size_t lastSlash = parentPath.find_last_of('/');
            if (lastSlash != std::string::npos && lastSlash > 0) {
                parentPath = parentPath.substr(0, lastSlash);
                std::string mkdirCmd = "mkdir -p \"" + parentPath + "\"";
                system(mkdirCmd.c_str());
            }
            
            std::string dotPath = path;
            size_t dotPos = dotPath.find_last_of('.');
            if (dotPos != std::string::npos) {
                dotPath = dotPath.substr(0, dotPos);
            }
            dotPath += ".dot";
            
            std::ostringstream dot;
            dot << "digraph G{\n";
            dot << "rankdir=TB;\n";
            dot << "graph [ dpi = \"600\" ];\n";
            dot << "forcelabels= true;\n";
            dot << "node [shape = plaintext];\n";
            dot << "file [label = <<table>\n";
            dot << "<tr><td COLSPAN = '2' BGCOLOR=\"#CCCC00\"><font color=\"white\">ARCHIVO</font></td></tr>\n";
            dot << "<tr><td BGCOLOR=\"#90EE90\">PROPIEDAD</td><td BGCOLOR=\"#90EE90\">VALOR</td></tr>\n";
            dot << "<tr><td>Ruta</td><td>" << path_file_ls << "</td></tr>\n";
            dot << "<tr><td COLSPAN = '2'>CONTENIDO</td></tr>\n";
            
            // Buscar archivo y leer contenido (implementación simplificada)
            Inodos rootInode;
            file.seekg(sb.s_inode_start, std::ios::beg);
            file.read(reinterpret_cast<char*>(&rootInode), sizeof(Inodos));
            
            if (rootInode.i_block[0] != -1) {
                BloqueCarpeta rootBlock;
                file.seekg(sb.s_block_start + rootInode.i_block[0] * sizeof(BloqueCarpeta), std::ios::beg);
                file.read(reinterpret_cast<char*>(&rootBlock), sizeof(BloqueCarpeta));
                
                std::string fileName = path_file_ls;
                size_t lastSlash = fileName.find_last_of('/');
                if (lastSlash != std::string::npos) {
                    fileName = fileName.substr(lastSlash + 1);
                }
                
                for (int i = 0; i < 4; i++) {
                    if (rootBlock.b_content[i].b_inodo != -1) {
                        std::string entryName = rootBlock.b_content[i].b_name;
                        entryName.erase(std::remove(entryName.begin(), entryName.end(), '\0'), entryName.end());
                        
                        if (entryName == fileName) {
                            Inodos fileInode;
                            file.seekg(sb.s_inode_start + rootBlock.b_content[i].b_inodo * sizeof(Inodos), std::ios::beg);
                            file.read(reinterpret_cast<char*>(&fileInode), sizeof(Inodos));
                            
                            if (fileInode.i_type == '0' && fileInode.i_block[0] != -1) {
                                BloqueArchivos block;
                                file.seekg(sb.s_block_start + fileInode.i_block[0] * sizeof(BloqueArchivos), std::ios::beg);
                                file.read(reinterpret_cast<char*>(&block), sizeof(BloqueArchivos));
                                
                                std::string content = "";
                                for (int j = 0; j < 50 && block.b_content[j] != '\0'; j++) {
                                    char c = block.b_content[j];
                                    if (c >= 32 && c <= 126) content += c;
                                    else if (c == '\n') content += "\\n";
                                }
                                dot << "<tr><td COLSPAN = '2'>" << content << "</td></tr>\n";
                            }
                            break;
                        }
                    }
                }
            }
            
            dot << "</table>>];\n";
            dot << "}\n";
            file.close();
            
            std::ofstream outFile(dotPath);
            if (!outFile.is_open()) {
                return "Error: No se pudo crear el archivo .dot";
            }
            outFile << dot.str();
            outFile.close();
            
            std::string ext = "jpg";
            size_t extPos = path.find_last_of('.');
            if (extPos != std::string::npos) {
                ext = path.substr(extPos + 1);
            }
            std::string cmd = "dot -T" + ext + " \"" + dotPath + "\" -o \"" + path + "\"";
            system(cmd.c_str());
            remove(dotPath.c_str());
            
            return "=== REP ===\nReporte '" + name + "' generado exitosamente en: " + path;
        }
        
        else {
            return "Error: Tipo de reporte no reconocido '" + name + "'\nTipos válidos: mbr, disk, sb, tree, ls, bm_inode, bm_block, inode, block, file, journaling";
        }
        
    } catch (const std::exception& e) {
        return std::string("Error en rep: ") + e.what();
    }
}

#endif