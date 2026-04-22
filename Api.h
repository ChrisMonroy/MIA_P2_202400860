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

extern bool is_logged;
extern char current_user[16];
extern std::vector<MountedPartition> mounted_list;
extern int mounted_count;

// ============================================================================
// UTILIDADES
// ============================================================================

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

// ============================================================================
// LISTAR CONTENIDO DE CARPETA (CORREGIDO)
// ============================================================================

inline crow::json::wvalue listarContenidoCarpeta(std::fstream& file, const SuperBloque& sb, int inodoCarpeta) {
    std::cerr << "[DEBUG] >> listarContenidoCarpeta | Inodo: " << inodoCarpeta << std::endl;
    crow::json::wvalue::list contenido;
    
    if (inodoCarpeta < 0 || inodoCarpeta >= sb.s_inodes_count) {
        std::cerr << "[DEBUG] >> Inodo fuera de rango. Retornando vacío." << std::endl;
        return crow::json::wvalue::list();
    }

    Inodos inode;
    long posInodo = sb.s_inode_start + (inodoCarpeta * sb.s_inode_s);
    file.seekg(posInodo, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s)) {
        std::cerr << "[ERROR] No se pudo leer inodo " << inodoCarpeta << std::endl;
        return crow::json::wvalue::list();
    }
    
    std::cerr << "[DEBUG] >> Inodo leído: Tipo='" << inode.i_type << "' | Bloque[0]=" << inode.i_block[0] << " | Permisos=" << inode.i_perm << std::endl;

    if (inode.i_type != '0') {
        std::cerr << "[DEBUG] >> No es carpeta. Retornando vacío." << std::endl;
        return crow::json::wvalue::list();
    }

    int vistos = 0;
    for (int i = 0; i < 12 && vistos < 500; i++) {
        int idxBloque = inode.i_block[i];
        if (idxBloque == -1) continue;
        if (idxBloque < 0 || idxBloque >= sb.s_blocks_count) {
            std::cerr << "[DEBUG] Bloque inválido: " << idxBloque << std::endl;
            continue;
        }

        long posBloque = sb.s_block_start + (idxBloque * sb.s_block_s);
        std::cerr << "[DEBUG] >> Leyendo bloque " << i << " en offset " << posBloque << std::endl;
        
        BloqueCarpeta bloque;
        file.seekg(posBloque, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&bloque), sizeof(BloqueCarpeta))) {
            std::cerr << "[ERROR] No se pudo leer bloque " << idxBloque << std::endl;
            continue;
        }

        for (int j = 0; j < 4; j++) {
            if (bloque.b_content[j].b_inodo != -1) {
                // ✅ CORRECCIÓN: Usar función segura para leer nombre
                std::string nombre = leerNombreSeguro(bloque.b_content[j].b_name, 12);
                std::cerr << "[DEBUG] >> Entrada encontrada: '" << nombre << "' -> Inodo " << bloque.b_content[j].b_inodo << std::endl;

                if (nombre == "." || nombre == "..") continue;

                int childInodo = bloque.b_content[j].b_inodo;
                if (childInodo < 0 || childInodo >= sb.s_inodes_count) continue;

                Inodos child;
                long posChild = sb.s_inode_start + (childInodo * sb.s_inode_s);
                file.seekg(posChild, std::ios::beg);
                // ✅ CORRECCIÓN: Validar lectura del inodo hijo
                if (!file.read(reinterpret_cast<char*>(&child), sb.s_inode_s)) {
                    std::cerr << "[ERROR] No se pudo leer inodo hijo " << childInodo << std::endl;
                    continue;
                }

                crow::json::wvalue item;
                item["nombre"] = nombre;
                item["tipo"] = (child.i_type == '0') ? "folder" : "file";
                item["permisos"] = std::string(child.i_perm);
                item["propietario"] = std::to_string(child.i_uid);
                item["grupo"] = std::to_string(child.i_gid);
                if (child.i_type == '1') item["tamano"] = child.i_s;
                contenido.emplace_back(std::move(item));
                vistos++;
            }
        }
    }

    std::cerr << "[DEBUG] >> Total elementos retornados al frontend: " << vistos << std::endl;
    crow::json::wvalue r; r["contenido"] = std::move(contenido);
    return r;
}


inline crow::response handleFsRequest(const std::string& idParticion, const std::string& rutaCodificada) {
    //  CORRECCIÓN: Decodificar y normalizar ruta
    std::string ruta = normalizarRuta(urlDecode(rutaCodificada));
    
    std::cerr << "[API] handleFsRequest | ID: " << idParticion 
              << " | RutaCodificada: '" << rutaCodificada << "'"
              << " | RutaDecodificada: '" << ruta << "'" << std::endl;
    
    try {
        // 1. Buscar partición montada
        MountedPartition* target = nullptr;
        std::string sid = toLowerCase(idParticion);
        for (auto& mp : mounted_list) {
            if (toLowerCase(mp.id) == sid) { 
                target = &mp; 
                break; 
            }
        }
        
        if (!target) { 
            std::cerr << "[ERROR] Partición no encontrada en RAM." << std::endl; 
            return crow::response(404, R"({"error":"Partición no montada"})"); 
        }
        if (!is_logged) { 
            std::cerr << "[ERROR] Sin sesión activa." << std::endl; 
            return crow::response(401, R"({"error":"Sin sesión"})"); 
        }

        // 2. Abrir disco
        std::fstream file(target->path, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            std::cerr << "[ERROR] No se pudo abrir disco: " << target->path << std::endl;
            return crow::response(500, R"({"error":"No se abrió disco"})");
        }

        // 3. Leer MBR y SuperBloque
        MBR mbr; 
        file.seekg(0); 
        if (!file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR))) {
            file.close();
            return crow::response(500, R"({"error":"No se pudo leer MBR"})");
        }
        
        long ps = mbr.mbr_partitions[target->partition_index].part_start;
        SuperBloque sb; 
        file.seekg(ps); 
        if (!file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque))) {
            file.close();
            return crow::response(500, R"({"error":"No se pudo leer SuperBloque"})");
        }
        
        // 4. Buscar inodo de la ruta
        int inodoRuta = buscarInodoPorRuta(file, sb, ruta);
        std::cerr << "[API] buscarInodoPorRuta('" << ruta << "') = " << inodoRuta << std::endl;
        
        if (inodoRuta == -1) { 
            file.close(); 
            return crow::response(404, R"({"error":"Ruta no encontrada"})"); 
        }

        // 5. Leer inodo
        Inodos inode; 
        file.seekg(sb.s_inode_start + inodoRuta * sb.s_inode_s); 
        if (!file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s)) {
            file.close();
            return crow::response(500, R"({"error":"No se pudo leer inodo de ruta"})");
        }

        // 6. Construir respuesta
        crow::json::wvalue r;
        r["ruta"] = ruta;
        r["permisos"] = std::string(inode.i_perm);
        r["propietario"] = std::to_string(inode.i_uid);
        r["grupo"] = std::to_string(inode.i_gid);
        r["tamano"] = inode.i_s;
        
       if (inode.i_type == '0') {
    r["tipo"] = "folder";
    crow::json::wvalue resp = listarContenidoCarpeta(file, sb, inodoRuta);
    // Forzar array vacío si no hay contenido
    if (resp.t() == crow::json::type::Object && resp.has("contenido")) {
        r["contenido"] = std::move(resp["contenido"]);
    } else {
        r["contenido"] = crow::json::wvalue::list();  // ✅ Array vacío explícito
    }
} else {
    r["tipo"] = "file";
    r["contenido"] = crow::json::wvalue::list();  // ✅ También para archivos
}

file.close();

// ✅ Debug: imprimir JSON antes de enviar
std::string jsonResponse = r.dump();
std::cerr << "[DEBUG] JSON Response: " << jsonResponse << std::endl;

return crow::response(200, "application/json", jsonResponse);  // ✅ Forzar content-type
        
    } catch (const std::exception& e) { 
        std::cerr << "[EXCEPCIÓN] handleFsRequest: " << e.what() << std::endl;
        return crow::response(500, R"({"error":"Excepción backend"})"); 
    }
}

// ============================================================================
// LEER LÓGICAS DESDE EBR
// ============================================================================

inline void leerLogicasDesdeEBR(const std::string& diskPath, long ebrStart, 
                                crow::json::wvalue& partitionsList, int& idx) {
    std::fstream file(diskPath, std::ios::binary | std::ios::in);
    if (!file.is_open()) return;
    
    long currentEBR = ebrStart;
    
    while (currentEBR != -1) {
        EBR ebr;
        file.seekg(currentEBR, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ebr), sizeof(EBR))) break;
        
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
            
            for (const auto& mp : mounted_list) {
                if (mp.path == diskPath && mp.partition_index == -1) {
                    if (mp.id.find("L") != std::string::npos) {
                        logical["status"] = "mounted";
                        logical["id"] = mp.id;
                        break;
                    }
                }
            }
            
            partitionsList[idx++] = std::move(logical);
        }
        
        currentEBR = ebr.part_next;
        if (currentEBR <= 0) break;
    }
    
    file.close();
}

// ============================================================================
// INICIAR SERVIDOR (CON RUTAS CORREGIDAS)
// ============================================================================

inline void iniciarServidor(int puerto = 3001) {
    crow::App<crow::CORSHandler> app;
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global().origin("*").methods("GET"_method, "POST"_method, "OPTIONS"_method).headers("Content-Type");

    // Route: Root
    CROW_ROUTE(app, "/")([]() {
        crow::json::wvalue r; 
        r["mensaje"] = "ExtreamFS API"; 
        r["estado"] = "activo"; 
        r["carnet"] = "202400860"; 
        return r;
    });

    // Route: Ejecutar comando
    CROW_ROUTE(app, "/api/ejecutar").methods("POST"_method)([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "JSON invalido");
        std::string cmd = body["comando"].s();
        if (cmd.empty()) return crow::response(400, "Comando requerido");
        std::string resultado = ejecutarComando(cmd);
        crow::json::wvalue r; 
        r["resultado"] = resultado; 
        r["exitoso"] = (resultado.find("Error") == std::string::npos);
        return crow::response(r);
    });

    // Route: Ejecutar script
    CROW_ROUTE(app, "/api/script").methods("POST"_method)([](const crow::request& req) {
        auto body = crow::json::load(req.body);
        if (!body) return crow::response(400, "JSON invalido");
        std::string script = body["script"].s();
        if (script.empty()) return crow::response(400, "Script requerido");
        std::string resultado = ejecutarScript(script);
        crow::json::wvalue r; 
        r["resultado"] = resultado; 
        r["exitoso"] = true;
        return crow::response(r);
    });

    // Route: Estado de sesión
    CROW_ROUTE(app, "/api/estado")([]() {
        crow::json::wvalue e;
        e["sesion_activa"] = is_logged;
        e["usuario"] = std::string(current_user);
        e["particiones_montadas"] = mounted_count;
        e["ids_montados"] = crow::json::wvalue::list();
        int i = 0; 
        for (const auto& mp : mounted_list) e["ids_montados"][i++] = mp.id;
        return e;
    });

    // Route: Listar discos
    CROW_ROUTE(app, "/api/discos")([]() {
        crow::json::wvalue r; 
        r["discos"] = crow::json::wvalue::list();
        std::string dir = "/home/christopher/Calificacion_MIA/Discos/";
        DIR* d = opendir(dir.c_str()); 
        if (!d) return crow::response(r);
        
        struct dirent* ent; 
        int idx = 0;
        while ((ent = readdir(d)) != nullptr) {
            std::string fn = ent->d_name;
            if (fn.size() > 4 && fn.substr(fn.size()-4) == ".mia") {
                std::string fp = dir + fn;
                std::ifstream df(fp, std::ios::binary); 
                if (!df.is_open()) continue;
                
                MBR mbr; 
                df.read(reinterpret_cast<char*>(&mbr), sizeof(MBR)); 
                df.close();
                
                crow::json::wvalue disk; 
                disk["path"] = fp;
                std::ifstream sf(fp, std::ios::ate); 
                disk["size"] = sf.is_open() ? (long)sf.tellg() : 0; 
                sf.close();
                
                disk["fit"] = std::string(1, mbr.dsk_fit); 
                disk["signature"] = mbr.mbr_dsk_signature;
                disk["partitions"] = crow::json::wvalue::list(); 
                int pi = 0;
                
                for (int i = 0; i < 4; i++) {
                    if (mbr.mbr_partitions[i].part_status == '1') {
                        crow::json::wvalue p;
                        p["id"] = ""; 
                        p["name"] = std::string(mbr.mbr_partitions[i].part_name);
                        p["type"] = std::string(1, mbr.mbr_partitions[i].part_type);
                        p["fit"] = std::string(1, mbr.mbr_partitions[i].part_fit);
                        p["start"] = mbr.mbr_partitions[i].part_start; 
                        p["size"] = mbr.mbr_partitions[i].part_size;
                        p["status"] = "unmounted";
                        
                        for (const auto& mp : mounted_list) {
                            if (mp.path == fp && mp.partition_index == i) { 
                                p["status"] = "mounted"; 
                                p["id"] = mp.id; 
                                break; 
                            }
                        }
                        disk["partitions"][pi++] = std::move(p);
                    }
                }
                r["discos"][idx++] = std::move(disk);
            }
        }
        closedir(d); 
        return crow::response(r);
    });

    // ✅ CORRECCIÓN PRINCIPAL: Rutas /api/fs unificadas con decodificación
    // Maneja: /api/fs/{id} y /api/fs/{id}/{path}
    
    CROW_ROUTE(app, "/api/fs/<string>")
    ([](const std::string& idParticion) {
        return handleFsRequest(idParticion, "");  // ruta vacía = raíz "/"
    });

    CROW_ROUTE(app, "/api/fs/<string>/<path>")
    ([](const std::string& idParticion, const std::string& rutaCodificada) {
        return handleFsRequest(idParticion, rutaCodificada);  // ✅ Decodifica internamente
    });

    // Route: Particiones de un disco
    CROW_ROUTE(app, "/api/discos/<string>/particiones")
    ([](const std::string& rutaDiscoCodificada) {
        try {
            std::string rutaDisco = urlDecode(rutaDiscoCodificada);
            
            crow::json::wvalue respuesta;
            respuesta["particiones"] = crow::json::wvalue::list();
            
            std::ifstream file(rutaDisco, std::ios::binary);
            if (!file.is_open()) {
                return crow::response(404, "Disco no encontrado: " + rutaDisco);
            }
            
            MBR mbr;
            file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
            file.close();
            
            int partIdx = 0;
            
            for (int i = 0; i < 4; i++) {
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
                
                for (const auto& mp : mounted_list) {
                    if (mp.path == rutaDisco && mp.partition_index == i) {
                        part["status"] = "mounted";
                        part["id"] = mp.id;
                        
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
                
                if (mbr.mbr_partitions[i].part_type == 'E') {
                    leerLogicasDesdeEBR(rutaDisco, mbr.mbr_partitions[i].part_start, respuesta["particiones"], partIdx);
                }
            }
            
            return crow::response(respuesta);
            
        } catch (const std::exception& e) {
            crow::json::wvalue err; err["error"] = e.what();
            return crow::response(500, err);
        }
    });

    // Route: Journaling
    CROW_ROUTE(app, "/api/journaling/<string>")([](const std::string& idParticion) {
        try {
            std::string target = CommandJournaling::toLower(idParticion);
            std::vector<CommandJournaling::Entry> filtered;
            for (const auto& e : CommandJournaling::store()) {
                if (CommandJournaling::toLower(e.mountId) == target) {
                    filtered.push_back(e);
                }
            }
            crow::json::wvalue r; 
            r["particion"] = idParticion; 
            r["entradas"] = crow::json::wvalue::list();
            int idx = 0;
            for (const auto& e : filtered) {
                crow::json::wvalue entry;
                entry["operacion"] = e.operation; 
                entry["ruta"] = e.path; 
                entry["contenido"] = e.content;
                char fb[20]; 
                std::tm* tm = std::localtime(&e.when); 
                std::strftime(fb, sizeof(fb), "%d/%m/%Y %H:%M", tm);
                entry["fecha"] = std::string(fb);
                r["entradas"][idx++] = std::move(entry);
            }
            return crow::response(r);
        } catch (const std::exception& e) { 
            crow::json::wvalue err; err["error"] = e.what(); 
            return crow::response(500, err); 
        }
    });

    // ============================================================================
// ROUTE: LEER CONTENIDO DE ARCHIVO
// GET /api/file/<id>?path=<ruta_codificada>
// ============================================================================
CROW_ROUTE(app, "/api/file/<string>")
([](const crow::request& req, const std::string& idParticion) {
    try {
        // 1. Obtener path del query string
        std::string rutaCodificada = "";
        auto pathParam = req.url_params.get("path");
        if (pathParam) {
            rutaCodificada = pathParam;
        }
        
        std::string ruta = normalizarRuta(urlDecode(rutaCodificada));
        std::cerr << "[FILE] Leer archivo | ID: " << idParticion << " | Ruta: '" << ruta << "'" << std::endl;
        
        // 2. Verificar sesión
        if (!is_logged) {
            return crow::response(401, R"({"error":"Sin sesión"})");
        }
        
        // 3. Buscar partición montada
        MountedPartition* target = nullptr;
        std::string sid = toLowerCase(idParticion);
        for (auto& mp : mounted_list) {
            if (toLowerCase(mp.id) == sid) {
                target = &mp;
                break;
            }
        }
        if (!target) {
            return crow::response(404, R"({"error":"Partición no montada"})");
        }
        
        // 4. Abrir disco
        std::fstream file(target->path, std::ios::binary | std::ios::in);
        if (!file.is_open()) {
            return crow::response(500, R"({"error":"No se pudo abrir disco"})");
        }
        
        // 5. Leer MBR y SuperBloque
        MBR mbr;
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        long ps = mbr.mbr_partitions[target->partition_index].part_start;
        SuperBloque sb;
        file.seekg(ps);
        file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
        
        // 6. Buscar inodo del archivo
        int inodoArchivo = buscarInodoPorRuta(file, sb, ruta);
        if (inodoArchivo == -1) {
            file.close();
            return crow::response(404, R"({"error":"Archivo no encontrado"})");
        }
        
        // 7. Leer inodo y verificar que sea archivo
        Inodos inode;
        file.seekg(sb.s_inode_start + inodoArchivo * sb.s_inode_s);
        file.read(reinterpret_cast<char*>(&inode), sb.s_inode_s);
        
        if (inode.i_type != '1') {  // '1' = archivo
            file.close();
            return crow::response(400, R"({"error":"No es un archivo"})");
        }
        
        // 8. Leer contenido del archivo desde sus bloques
        std::string contenido = "";
        for (int i = 0; i < 12 && inode.i_block[i] != -1; i++) {
            int idxBloque = inode.i_block[i];
            if (idxBloque < 0 || idxBloque >= sb.s_blocks_count) continue;
            
            BloqueArchivos bloque;
            long posBloque = sb.s_block_start + (idxBloque * sb.s_block_s);
            file.seekg(posBloque, std::ios::beg);
            if (file.read(reinterpret_cast<char*>(&bloque), sb.s_block_s)) {
                // Leer hasta encontrar null o fin del bloque
                for (int j = 0; j < sb.s_block_s && bloque.b_content[j] != '\0'; j++) {
                    contenido += bloque.b_content[j];
                }
            }
        }
        
        file.close();
        
        // 9. Retornar contenido
        crow::json::wvalue r;
        r["ruta"] = ruta;
        r["contenido"] = contenido;
        r["tamano"] = inode.i_s;
        
        std::cerr << "[FILE] Contenido leído: " << contenido.size() << " bytes" << std::endl;
        return crow::response(200, "application/json", r.dump());
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR FILE] Excepción: " << e.what() << std::endl;
        return crow::response(500, R"({"error":"Error interno"})");
    }
});

    // Route: Logout
    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([]() {
        is_logged = false; 
        memset(current_user, 0, sizeof(current_user));
        crow::json::wvalue r; 
        r["mensaje"] = "Sesion cerrada"; 
        r["exitoso"] = true;
        r["ids_montados"] = crow::json::wvalue::list();
        int idx = 0; 
        for (const auto& mp : mounted_list) r["ids_montados"][idx++] = mp.id;
        return crow::response(r);
    });

    std::cout << "Servidor iniciado en http://localhost:" << puerto << "\n";
    app.port(puerto).run();
}

#endif