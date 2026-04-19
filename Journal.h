#ifndef JOURNALIST_H
#define JOURNALIST_H

#include <iostream>
#include <fstream>
#include <cstring>
#include <ctime>
#include "Mbr.h"

//Parametros para el journal
struct Journal{
    char operacion[20];
    char ruta [100];
    char contenido[200];
    time_t fecha;
    int siguiente;

    Journal() {
        memset(operacion, 0, sizeof(operacion));
        memset(ruta, 0, sizeof(ruta));
        memset(contenido, 0, sizeof(contenido));
        fecha = 0;
        siguiente = -1;
    }
};
const int JOURNAL_SIZE = sizeof(Journal);

#endif