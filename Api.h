#ifndef API_H
#define API_H

#include "crow.h"
#include "crow/middlewares/cors.h"
#include <string>
#include <ctime>
#include <cstring>
#include <algorithm>
#include "Commands.h"
#include "utils.h"
#include "globals.h"
#include "Journaling.h"
#include <dirent.h>
#include "Mbr.h"
#include "SuperBloque.h"
#include "Inodos.h"
#include "BloqueCarpeta.h"

inline std::string urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            try {
                char decoded = static_cast<char>(std::stoi(hex, nullptr, 16));
                result += decoded;
            } catch (...) {
                result += str[i];
            }
            i += 2;
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

inline std::string leerNombreSeguro(const char* nombre, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && nombre[len] != '\0') ++len;
    return std::string(nombre, len);
}

inline std::string normalizarRuta(const std::string& ruta) {
    if (ruta.empty()) return "/";
    std::string r = ruta;
    if (r[0] != '/') r = "/" + r;
    return r;
}


inline crow::json::wvalue listarContenidoCarpeta(std::fstream& file, const SuperBloque& sb, int inodoCarpeta) {
    crow::json::wvalue::list contenido;
    if (inodoCarpeta < 0 || inodoCarpeta >= sb.s_inodes_count) return crow::json::wvalue::list();

    Inodos inode;
    file.seekg(sb.s_inode_start + (inodoCarpeta * sb.s_inode_s), std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s)) return crow::json::wvalue::list();

    if (inode.i_type != '1') return crow::json::wvalue::list(); // '1' = carpeta

    for (int i = 0; i < 12; i++) {
        int idxBloque = inode.i_block[i];
        if (idxBloque == -1 || idxBloque < 0 || idxBloque >= sb.s_blocks_count) continue;

        BloqueCarpeta bloque;
        file.seekg(sb.s_block_start + (idxBloque * sb.s_block_s), std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&bloque), sizeof(BloqueCarpeta))) continue;

        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo != -1) {
                std::string nombre = leerNombreSeguro(bloque.b_content[j].b_name, 12);
                if (nombre == "." || nombre == "..") continue;

                int childInodo = bloque.b_content[j].b_inodo;
                if (childInodo < 0 || childInodo >= sb.s_inodes_count) continue;

                Inodos child;
                file.seekg(sb.s_inode_start + (childInodo * sb.s_inode_s), std::ios::beg);
                if (!file.read(reinterpret_cast<char*>(&child), sb.s_inode_s)) continue;

                crow::json::wvalue item;
                item["nombre"] = nombre;
                item["tipo"] = (child.i_type == '1') ? "folder" : "file"; // '1' = carpeta
                item["permisos"] = std::string(child.i_perm);
                item["propietario"] = std::to_string(child.i_uid);
                item["grupo"] = std::to_string(child.i_gid);
                if (child.i_type == '0') item["tamano"] = child.i_s; // '0' = archivo
                contenido.emplace_back(std::move(item));
            }
        }
    }
    crow::json::wvalue r; r["contenido"] = std::move(contenido);
    return r;
}


inline crow::response handleFsRequest(const std::string& idParticion, const std::string& rutaCodificada) {
    std::string ruta = normalizarRuta(urlDecode(rutaCodificada));
    
    try {
        MountedPartition* target = nullptr;
        std::string sid = toLowerCase(idParticion);
        for (auto& mp : mounted_list) {
            if (toLowerCase(mp.id) == sid) { target = &mp; break; }
        }
        if (!target) return crow::response(404, R"({"error":"Partición no montada"})");
        if (!is_logged) return crow::response(401, R"({"error":"Sin sesión"})");

        std::fstream file(target->path, std::ios::binary | std::ios::in);
        if (!file.is_open()) return crow::response(500, R"({"error":"No se abrió disco"})");

        MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        long ps = mbr.mbr_partitions[target->partition_index].part_start;
        SuperBloque sb; file.seekg(ps); file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        int inodoRuta = buscarInodoPorRuta(file, sb, ruta);
        if (inodoRuta == -1) { file.close(); return crow::response(404, R"({"error":"Ruta no encontrada"})"); }

        Inodos inode; file.seekg(sb.s_inode_start + inodoRuta * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);

        crow::json::wvalue r;
        r["ruta"] = ruta; r["permisos"] = std::string(inode.i_perm);
        r["propietario"] = std::to_string(inode.i_uid); r["grupo"] = std::to_string(inode.i_gid);
        r["tamano"] = inode.i_s;
        
        if (inode.i_type == '1') { // '1' = carpeta
            r["tipo"] = "folder";
            crow::json::wvalue resp = listarContenidoCarpeta(file, sb, inodoRuta);
            r["contenido"] = (resp.has("contenido")) ? std::move(resp["contenido"]) : crow::json::wvalue::list();
        } else {
            r["tipo"] = "file"; r["contenido"] = crow::json::wvalue::list();
        }
        file.close();
        return crow::response(200, "application/json", r.dump());
    } catch (const std::exception& e) { 
        return crow::response(500, R"({"error":"Excepción backend"})"); 
    }
}

inline void leerLogicasDesdeEBR(const std::string& diskPath, long ebrStart, crow::json::wvalue& partitionsList, int& idx) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return;
    long currentEBR = ebrStart;
    while (currentEBR != -1) {
        EBR ebr; file.seekg(currentEBR, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR))) break;
        if (ebr.part_status == '1') {
            crow::json::wvalue logical;
            logical["name"] = std::string(ebr.part_name); logical["type"] = "L";
            logical["fit"] = std::string(1, ebr.part_fit); logical["start"] = ebr.part_start;
            logical["size"] = ebr.part_size; logical["id"] = ""; logical["status"] = "unmounted";
            logical["filesystem"] = "";
            for (const auto& mp : mounted_list) {
                if (mp.path == diskPath && mp.partition_index == -1 && mp.id.find("L") != std::string::npos) {
                    logical["status"] = "mounted"; logical["id"] = mp.id; break;
                }
            }
            partitionsList[idx++] = std::move(logical);
        }
        currentEBR = ebr.part_next;
        if (currentEBR <= 0) break;
    }
    file.close();
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
        e["sesion_activa"] = is_logged; e["usuario"] = std::string(current_user);
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

    CROW_ROUTE(app, "/api/fs/<string>")([](const std::string& idParticion) {
        return handleFsRequest(idParticion, "");
    });
    CROW_ROUTE(app, "/api/fs/<string>/<path>")([](const std::string& idParticion, const std::string& rutaCodificada) {
        return handleFsRequest(idParticion, rutaCodificada);
    });

    CROW_ROUTE(app, "/api/discos/<string>/particiones")([](const std::string& rutaDiscoCodificada) {
        try {
            std::string rutaDisco = urlDecode(rutaDiscoCodificada);
            crow::json::wvalue respuesta; respuesta["particiones"] = crow::json::wvalue::list();
            std::ifstream file(rutaDisco, std::ios::binary);
            if (!file.is_open()) return crow::response(404, "Disco no encontrado: " + rutaDisco);
            MBR mbr; file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); file.close();
            int partIdx = 0;
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_status == '0' || mbr.mbr_partitions[i].part_status == 0) continue;
                crow::json::wvalue part;
                part["name"] = std::string(mbr.mbr_partitions[i].part_name);
                part["type"] = std::string(1, mbr.mbr_partitions[i].part_type);
                part["fit"] = std::string(1, mbr.mbr_partitions[i].part_fit);
                part["start"] = mbr.mbr_partitions[i].part_start; part["size"] = mbr.mbr_partitions[i].part_size;
                part["id"] = ""; part["status"] = "unmounted"; part["filesystem"] = "";
                for (const auto& mp : mounted_list) {
                    if (mp.path == rutaDisco && mp.partition_index == i) {
                        part["status"] = "mounted"; part["id"] = mp.id;
                        std::fstream sbFile(rutaDisco, std::ios::binary);
                        if (sbFile.is_open()) {
                            SuperBloque sb; sbFile.seekg(mbr.mbr_partitions[i].part_start);
                            sbFile.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
                            if (sb.s_magic == 0xEF53) part["filesystem"] = (sb.s_filesystem_type == 3) ? "EXT3" : "EXT2";
                            sbFile.close();
                        }
                        break;
                    }
                }
                respuesta["particiones"][partIdx++] = std::move(part);
                if (mbr.mbr_partitions[i].part_type == 'E') {
                    leerLogicasDesdeEBR(rutaDisco, mbr.mbr_partitions[i].part_start, respuesta["particiones"], partIdx);
                }
            }
            return crow::response(respuesta);
        } catch (const std::exception& e) {
            crow::json::wvalue err; err["error"] = e.what(); return crow::response(500, err);
        }
    });

    CROW_ROUTE(app, "/api/journaling/<string>")([](const std::string& idParticion) {
        try {
            std::string target = CommandJournaling::toLower(idParticion);
            crow::json::wvalue r; r["particion"] = idParticion; r["entradas"] = crow::json::wvalue::list();
            int idx = 0;
            for (const auto& e : CommandJournaling::store()) {
                if (CommandJournaling::toLower(e.mountId) == target) {
                    crow::json::wvalue entry;
                    entry["operacion"] = e.operation; entry["ruta"] = e.path; entry["contenido"] = e.content;
                    char fb[20]; std::tm* tm = std::localtime(&e.when); std::strftime(fb, sizeof(fb), "%d/%m/%Y %H:%M", tm);
                    entry["fecha"] = std::string(fb);
                    r["entradas"][idx++] = std::move(entry);
                }
            }
            return crow::response(r);
        } catch (const std::exception& e) { 
            crow::json::wvalue err; err["error"] = e.what(); return crow::response(500, err); 
        }
    });

    CROW_ROUTE(app, "/api/file/<string>")([](const crow::request& req, const std::string& idParticion) {
        try {
            std::string rutaCodificada = "";
            auto pathParam = req.url_params.get("path");
            if (pathParam) rutaCodificada = pathParam;
            std::string ruta = normalizarRuta(urlDecode(rutaCodificada));
            
            if (!is_logged) return crow::response(401, R"({"error":"Sin sesión"})");
            
            MountedPartition* target = nullptr;
            std::string sid = toLowerCase(idParticion);
            for (auto& mp : mounted_list) { if (toLowerCase(mp.id) == sid) { target = &mp; break; } }
            if (!target) return crow::response(404, R"({"error":"Partición no montada"})");
            
            std::fstream file(target->path, std::ios::binary | std::ios::in);
            if (!file.is_open()) return crow::response(500, R"({"error":"No se pudo abrir disco"})");
            
            MBR mbr; file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            long ps = mbr.mbr_partitions[target->partition_index].part_start;
            SuperBloque sb; file.seekg(ps); file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
            
            int inodoArchivo = buscarInodoPorRuta(file, sb, ruta);
            if (inodoArchivo == -1) { file.close(); return crow::response(404, R"({"error":"Archivo no encontrado"})"); }
            
            Inodos inode; file.seekg(sb.s_inode_start + inodoArchivo * sb.s_inode_s);
            file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
            
            if (inode.i_type != '0') { 
                file.close(); 
                return crow::response(400, R"({"error":"No es un archivo"})"); 
            }
            
            std::string contenido = "";
            for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
                int idxBloque = inode.i_block[i];
                if (idxBloque < 0 || idxBloque >= sb.s_blocks_count) continue;
                BloqueArchivos bloque;
                long posBloque = sb.s_block_start + (idxBloque * sb.s_block_s);
                file.seekg(posBloque, std::ios::beg);
                if (file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s)) {
                    for (int j = 0; j < sb.s_block_s && bloque.b_content[j] != '\0'; j++) {
                        contenido += bloque.b_content[j];
                    }
                }
            }
            file.close();
            
            crow::json::wvalue r; r["ruta"] = ruta; r["contenido"] = contenido; r["tamano"] = inode.i_s;
            return crow::response(200, "application/json", r.dump());
        } catch (const std::exception& e) {
            return crow::response(500, R"({"error":"Error interno"})");
        }
    });

    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([]() {
        is_logged = false; memset(current_user, 0, sizeof(current_user));
        crow::json::wvalue r; r["mensaje"] = "Sesion cerrada"; r["exitoso"] = true;
        r["ids_montados"] = crow::json::wvalue::list();
        int idx = 0; for (const auto& mp : mounted_list) r["ids_montados"][idx++] = mp.id;
        return crow::response(r);
    });

    std::cout << "Servidor iniciado en http://localhost:" << puerto << "\n";
    app.port(puerto).run();
}

#endif