#ifndef API_H
#define API_H

#include "crow.h"
#include "crow/middlewares/cors.h"
#include <string>
#include <ctime>
#include <cstring>
#include "Commands.h"
#include "utils.h"
#include "globals.h"
#include "Journaling.h"
#include <dirent.h>
#include "Mbr.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "BloqueCarpeta.h"

extern bool is_logged;
extern char current_user[16];
extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;

inline crow::json::wvalue listarContenidoCarpeta(std::fstream& file, const SuperBloque& sb, int inodoCarpeta) {
    crow::json::wvalue::list contenido;
    Inodos inode;
    
    // Validar inodo
    if (inodoCarpeta < 0 || inodoCarpeta >= sb.s_inodes_count) return crow::json::wvalue::list();
    
    file.seekg(sb.s_inode_start + inodoCarpeta * sb.s_inode_s, std::ios::beg);
    if (!file.good()) return crow::json::wvalue::list();
    file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
    
    // Recorrer solo bloques directos (12 máx) con límite de seguridad
    int elementosVistos = 0;
    const int MAX_ELEMENTOS = 1000; // Límite anti-bucle infinito
    
    for (int i = 0; i < 12 && inode.i_block[i] != -1 && elementosVistos < MAX_ELEMENTOS; i++) {
        int idxBloque = inode.i_block[i];
        if (idxBloque < 0 || idxBloque >= sb.s_blocks_count) continue;
        
        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + idxBloque * sb.s_block_s, std::ios::beg);
        if (!file.good()) continue;
        file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s);
        
        for (int j = 0; j < 4 && elementosVistos < MAX_ELEMENTOS; j++) {
            if (bloque.b_content[j].b_inodo != -1 && strlen(bloque.b_content[j].b_name) > 0) {
                // Saltar . y .. para evitar bucles
                if (strcmp(bloque.b_content[j].b_name, ".") == 0 || 
                    strcmp(bloque.b_content[j].b_name, "..") == 0) continue;
                
                int childInodoIdx = bloque.b_content[j].b_inodo;
                if (childInodoIdx < 0 || childInodoIdx >= sb.s_inodes_count) continue;
                
                Inodos childInode;
                file.seekg(sb.s_inode_start + childInodoIdx * sb.s_inode_s, std::ios::beg);
                if (!file.good()) continue;
                file.read(reinterpret_cast<char*>(&childInode), sb.s_inode_s);
                
                crow::json::wvalue item;
                item["nombre"] = std::string(bloque.b_content[j].b_name);
                item["tipo"] = (childInode.i_type == '0') ? "folder" : "file";
                item["permisos"] = std::string(childInode.i_perm);
                item["propietario"] = std::to_string(childInode.i_uid);
                item["grupo"] = std::to_string(childInode.i_gid);
                if (childInode.i_type == '1') item["tamano"] = childInode.i_s;
                contenido.emplace_back(std::move(item));
                elementosVistos++;
            }
        }
    }
    crow::json::wvalue respuesta;
    respuesta["contenido"] = std::move(contenido);
    return respuesta;
}

inline void leerLogicasDesdeEBR(const std::string& diskPath, long ebrStart, 
                                crow::json::wvalue& partitionsList, int& idx) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return;
    
    long currentEBR = ebrStart;
    
    while (currentEBR != -1) {
        EBR ebr;
        file.seekg(currentEBR, std::ios::beg);
        file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
        
        // Primera entrada del EBR = partición lógica actual
        if (ebr.part_status == '1') {
            crow::json::wvalue logical;
            logical["name"] = std::string(ebr.part_name);
            logical["type"] = "L";
            logical["fit"] = std::string(1, ebr.part_fit);
            logical["start"] = ebr.part_start;
            logical["size"] = ebr.part_size;
            logical["id"] = "";
            logical["status"] = "unmounted";
            logical["filesystem"] = "";
            
            // Verificar mount
            for (const auto& mp : mounted_list) {
                if (mp.path == diskPath && mp.partition_index == -1 && mp.path == diskPath) {
                    // Nota: para lógicas, matching es más complejo; simplificar si es necesario
                    if (mp.id.find("L") != std::string::npos) { // Heurística simple
                        logical["status"] = "mounted";
                        logical["id"] = mp.id;
                        break;
                    }
                }
            }
            
            partitionsList[idx++] = std::move(logical);
        }
        
        // Siguiente EBR en la cadena
        currentEBR = ebr.part_next;
        if (currentEBR <= 0) break; // Fin de cadena
    }
    
    file.close();
}

inline std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
            result += decoded;
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

inline void iniciarServidor(int puerto = 3001) {
    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global().origin("*").methods("GET"_method, "POST"_method, "OPTIONS"_method).headers("Content-Type");

    CROW_ROUTE(app, "/")([]() {
        crow::json::wvalue r; r["mensaje"] = "ExtreamFS API"; r["estado"] = "activo"; r["carnet"] = "202400860"; return r;
    });

    CROW_ROUTE(app, "/api/ejecutar").methods("POST"_method)([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "JSON invalido");
        std::string cmd = body["comando"].s();
        if (cmd.empty()) return crow::response(400, "Comando requerido");
        std::string resultado = ejecutarComando(cmd);
        crow::json::wvalue r; r["resultado"] = resultado; r["exitoso"] = (resultado.find("Error") == std::string::npos);
        return crow::response(r);
    });

    CROW_ROUTE(app, "/api/script").methods("POST"_method)([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "JSON invalido");
        std::string script = body["script"].s();
        if (script.empty()) return crow::response(400, "Script requerido");
        std::string resultado = ejecutarScript(script);
        crow::json::wvalue r; r["resultado"] = resultado; r["exitoso"] = true;
        return crow::response(r);
    });

    CROW_ROUTE(app, "/api/estado")([]() {
        crow::json::wvalue e;
        e["sesion_activa"] = is_logged;
        e["usuario"] = std::string(current_user);
        e["particiones_montadas"] = mounted_count;
        e["ids_montados"] = crow::json::wvalue::list();
        int i = 0; for (const auto& mp : mounted_list) e["ids_montados"][i++] = mp.id;
        return e;
    });

    CROW_ROUTE(app, "/api/discos")([]() {
        crow::json::wvalue r; r["discos"] = crow::json::wvalue::list();
        std::string dir = "/home/christopher/Calificacion_MIA/Discos/";
        DIR* d = opendir(dir.c_str()); if (!d) return crow::response(r);
        struct dirent* ent; int idx = 0;
        while ((ent = readdir(d)) != nullptr) {
            std::string fn = ent->d_name;
            if (fn.size() > 4 && fn.substr(fn.size()-4) == ".mia") {
                std::string fp = dir + fn;
                std::ifstream df(fp, std::ios::binary); if (!df.is_open()) continue;
                MBR mbr; df.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); df.close();
                crow::json::wvalue disk; disk["path"] = fp;
                std::ifstream sf(fp, std::ios::ate); disk["size"] = sf.is_open() ? (long)sf.tellg() : 0; sf.close();
                disk["fit"] = std::string(1, mbr.dsk_fit); disk["signature"] = mbr.mbr_dsk_signature;
                disk["partitions"] = crow::json::wvalue::list(); int pi = 0;
                for (int i = 0; i < 4; i++) {
                    if (mbr.mbr_partitions[i].part_status == '1') {
                        crow::json::wvalue p;
                        p["id"] = ""; p["name"] = std::string(mbr.mbr_partitions[i].part_name);
                        p["type"] = std::string(1, mbr.mbr_partitions[i].part_type);
                        p["fit"] = std::string(1, mbr.mbr_partitions[i].part_fit);
                        p["start"] = mbr.mbr_partitions[i].part_start; p["size"] = mbr.mbr_partitions[i].part_size;
                        p["status"] = "unmounted";
                        for (const auto& mp : mounted_list) {
                            if (mp.path == fp && mp.partition_index == i) { p["status"] = "mounted"; p["id"] = mp.id; break; }
                        }
                        disk["partitions"][pi++] = std::move(p);
                    }
                }
                r["discos"][idx++] = std::move(disk);
            }
        }
        closedir(d); return crow::response(r);
    });

    CROW_ROUTE(app, "/api/fs/<string>/<path>")([](const std::string& idParticion, const std::string& ruta) {
    try {
        // 1. Buscar partición
        MountedPartition* target = nullptr;
        std::string sid = toLowerCase(idParticion);
        for (auto& mp : mounted_list) if (toLowerCase(mp.id) == sid) { target = &mp; break; }
        if (!target) { crow::json::wvalue e; e["error"] = "Particion '" + idParticion + "' no montada"; return crow::response(404, e); }
        if (!is_logged) { crow::json::wvalue e; e["error"] = "No hay sesion activa"; return crow::response(401, e); }

        // 2. Abrir disco
        std::fstream file(target->path, std::ios::binary | std::ios::in);
        if (!file.is_open()) return crow::response(500, "No se pudo abrir disco");

        // 3. Leer MBR y SuperBloque
        MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        long ps = mbr.mbr_partitions[target->partition_index].part_start;
        SuperBloque sb; file.seekg(ps); file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

        // 4. Buscar inodo de la ruta
        int inodoRuta = buscarInodoPorRuta(file, sb, ruta.empty() ? "/" : ruta);
        if (inodoRuta == -1) { file.close(); return crow::response(404, "Ruta no encontrada: " + ruta); }

        // 5. Leer inodo
        Inodos inode; file.seekg(sb.s_inode_start + inodoRuta * sb.s_inode_s); file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);

        // 6. Construir respuesta
        crow::json::wvalue r;
        r["ruta"] = ruta.empty() ? "/" : ruta;
        r["permisos"] = std::string(inode.i_perm);
        r["propietario"] = std::to_string(inode.i_uid);
        r["grupo"] = std::to_string(inode.i_gid);
        r["tamano"] = inode.i_s;
        
        if (inode.i_type == '0') { 
            r["tipo"] = "folder"; 
            crow::json::wvalue contenidoResp = listarContenidoCarpeta(file, sb, inodoRuta);
            r["contenido"] = std::move(contenidoResp["contenido"]); 
        } else { 
            r["tipo"] = "file"; 
        }
        
        // 7. Cerrar archivo DESPUÉS de todo el procesamiento
        file.close();
        return crow::response(r);
        
    } catch (const std::exception& e) { 
        crow::json::wvalue err; err["error"] = e.what(); return crow::response(500, err); 
    }
});

CROW_ROUTE(app, "/api/discos/<string>/particiones")
([](const std::string& rutaDiscoCodificada) {
    try {
        // ✅ Decodificar ruta
        std::string rutaDisco = urlDecode(rutaDiscoCodificada);
        
        // Debug (opcional, remover en producción)
        // std::cerr << "[DEBUG] Ruta decodificada: " << rutaDisco << std::endl;
        
        crow::json::wvalue respuesta;
        respuesta["particiones"] = crow::json::wvalue::list();
        
        // Abrir disco
        std::ifstream file(rutaDisco, std::ios::binary);
        if (!file.is_open()) {
            // std::cerr << "[ERROR] No se pudo abrir: " << rutaDisco << std::endl;
            return crow::response(404, "Disco no encontrado: " + rutaDisco);
        }
        
        // Leer MBR
        MBR mbr;
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        file.close();
        
        int partIdx = 0;
        
        // Iterar particiones del MBR
        for (int i = 0; i < 4; i++) {
            // ✅ Verificar que la partición esté creada (status != '0')
            if (mbr.mbr_partitions[i].part_status == '0' || 
                mbr.mbr_partitions[i].part_status == 0) continue;
            
            crow::json::wvalue part;
            part["name"] = std::string(mbr.mbr_partitions[i].part_name);
            part["type"] = std::string(1, mbr.mbr_partitions[i].part_type);
            part["fit"] = std::string(1, mbr.mbr_partitions[i].part_fit);
            part["start"] = mbr.mbr_partitions[i].part_start;
            part["size"] = mbr.mbr_partitions[i].part_size;
            part["id"] = "";
            part["status"] = "unmounted";
            part["filesystem"] = "";
            
            // Verificar si está montada
            for (const auto& mp : mounted_list) {
                if (mp.path == rutaDisco && mp.partition_index == i) {
                    part["status"] = "mounted";
                    part["id"] = mp.id;
                    
                    // Leer filesystem type
                    std::fstream sbFile(rutaDisco, std::ios::binary);
                    if (sbFile.is_open()) {
                        SuperBloque sb;
                        sbFile.seekg(mbr.mbr_partitions[i].part_start);
                        sbFile.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
                        if (sb.s_magic == 0xEF53) {
                            part["filesystem"] = (sb.s_filesystem_type == 3) ? "EXT3" : "EXT2";
                        }
                        sbFile.close();
                    }
                    break;
                }
            }
            
            respuesta["particiones"][partIdx++] = std::move(part);
            
            // Si es extendida, leer lógicas desde EBR
            if (mbr.mbr_partitions[i].part_type == 'E') {
                leerLogicasDesdeEBR(rutaDisco, mbr.mbr_partitions[i].part_start, respuesta["particiones"], partIdx);
            }
        }
        
        // Debug: mostrar cuántas particiones se encontraron
        
        return crow::response(respuesta);
        
    } catch (const std::exception& e) {
        crow::json::wvalue err; err["error"] = e.what();
        return crow::response(500, err);
    }
});

    CROW_ROUTE(app, "/api/journaling/<string>")([](const std::string& idParticion) {
        try {
            std::string target = CommandJournaling::toLower(idParticion);
            std::vector<CommandJournaling::Entry> filtered;
            for (const auto& e : CommandJournaling::store()) if (CommandJournaling::toLower(e.mountId) == target) filtered.push_back(e);
            crow::json::wvalue r; r["particion"] = idParticion; r["entradas"] = crow::json::wvalue::list();
            int idx = 0;
            for (const auto& e : filtered) {
                crow::json::wvalue entry;
                entry["operacion"] = e.operation; entry["ruta"] = e.path; entry["contenido"] = e.content;
                char fb[20]; std::tm* tm = std::localtime(&e.when); std::strftime(fb, sizeof(fb), "%d/%m/%Y %H:%M", tm);
                entry["fecha"] = std::string(fb);
                r["entradas"][idx++] = std::move(entry);
            }
            return crow::response(r);
        } catch (const std::exception& e) { crow::json::wvalue err; err["error"] = e.what(); return crow::response(500, err); }
    });

    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([]() {
        is_logged = false; memset(current_user, 0, sizeof(current_user));
        crow::json::wvalue r; r["mensaje"] = "Sesion cerrada"; r["exitoso"] = true;
        r["ids_montados"] = crow::json::wvalue::list();
        int idx = 0; for (const auto& mp : mounted_list) r["ids_montados"][idx++] = mp.id;
        return crow::response(r);
    });

    std::cout << "Servidor iniciado en http://localhost:" << puerto << "\n";
    app.port(puerto).multithreaded().run();
}

#endif