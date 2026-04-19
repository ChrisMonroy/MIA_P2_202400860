#include <iostream>
#include <cstdlib>
#include <ctime>
#include "Api.h"
#include "utils.h"


int main() {
    srand(time(nullptr));

    iniciarServidor(3001);

    return 0;
}