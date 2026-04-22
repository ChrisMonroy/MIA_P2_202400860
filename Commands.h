#ifndef COMMANDS_H
#define COMMANDS_H

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <vector>
#include "Mbr.h"
#include "MkDisk.h"
#include "RmDisk.h"
#include "FDisk.h"
#include "Mount.h"
#include "Mounted.h"
#include "Mkfs.h"
#include "Login.h"
#include "Logout.h"
#include "MkGrp.h"
#include "Rmgrp.h"
#include "Mkusr.h"
#include "Rmusr.h"
#include "Chgrp.h"
#include "Cat.h"
#include "Mkfile.h"
#include "Mkdir.h"
#include "utils.h"
#include "globals.h"
#include "Rep.h"
#include "Rename.h"
#include "Remove.h"
#include "Copy.h"
#include "Move.h"
#include "Find.h"
#include "Chown.h"
#include "Chmod.h"
#include "Rename.h"
#include "Remove.h"
#include "Journaling.h"
#include "Loss.h"
#include "Unmount.h"


inline std::string ejecutarComando(const std::string& comando) {
    std::string cmd = trim(comando);
    
    if (cmd.empty()) {
        return "";
    }
    
    // Obtener nombre del comando
    std::istringstream iss(cmd);
    std::string nombreComando;
    iss >> nombreComando;
    nombreComando = toLowerCase(nombreComando);

    if (nombreComando == "mkdisk") {
        return Mkdisk(cmd);
    } else if (nombreComando == "rmdisk") {
        return Rmdisk(cmd);
    } else if (nombreComando == "fdisk") {
        return CommandFdisk::FDisk(cmd);
    } else if (nombreComando == "mount") {
        return Mount(cmd);
    } else if (nombreComando == "mounted") {
        return Mounted();
    } else if (nombreComando == "mkfs") {
        return Mkfs(cmd);
    } else if (nombreComando == "login") {
        return Login(cmd);
    } else if (nombreComando == "logout") {
        return Logout();
    } else if (nombreComando == "mkgrp") {
        return Mkgrp(cmd);
    } else if (nombreComando == "rmgrp") {
        return Rmgrp(cmd);
    } else if (nombreComando == "mkusr") {
        return Mkusr(cmd);
    } else if (nombreComando == "rmusr") {
        return Rmusr(cmd);
    } else if (nombreComando == "chgrp") {
        return Chgrp(cmd);
    } else if (nombreComando == "cat") {
        return Cat(cmd);
    } else if (nombreComando == "mkfile") {
        return Mkfile(cmd);
    } else if (nombreComando == "mkdir") {
        return Mkdir(cmd);
    } else if(nombreComando == "rep"){
        return Rep(cmd);
    } else if (nombreComando == "copy") {
    return Copy(cmd);
    } else if (nombreComando == "move") {
        return Move(cmd);
    } else if (nombreComando == "find") {
        return Find(cmd);
    } else if (nombreComando == "chown") {
        return Chown(cmd);
    } else if (nombreComando == "chmod") {
        return Chmod(cmd);
    } else if (nombreComando == "rename") {
        return Rename(cmd);
    } else if (nombreComando == "remove") {
        return Remove(cmd);
    }  else if (nombreComando == "journaling") {
        return CommandJournaling::execute(cmd);
    }else if (nombreComando == "loss") {
        return ejecutarLoss(cmd);
    } else if(nombreComando == "unmount") {
        return Unmount(cmd);
    }else if (nombreComando == "exit" || nombreComando == "quit") {
        return "EXIT";
    } else {
        return "Error: Comando no reconocido '" + nombreComando + "'";
    }
}

inline std::string ejecutarScript(const std::string& contenido) {
    std::ostringstream salida;
    std::istringstream iss(contenido);
    std::string linea;
    
    while (std::getline(iss, linea)) {
        linea = trim(linea);
        
        // Ignorar vacías y comentarios
        if (linea.empty()) {
            salida << "\n";
            continue;
        }

        if (linea[0] == '#') {
            salida << linea << "\n\n";
            continue;
        }
        
        salida << "MIA> " << linea << "\n";
        std::string resultado = ejecutarComando(linea);
        
        if (resultado == "EXIT") {
            salida << "Saliendo del programa...\n";
            break;
        }
        
        if (!resultado.empty()) {
            salida << resultado << "\n";
        }
    }
    
    return salida.str();
}

#endif