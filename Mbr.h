#ifndef MBR_H
#define MBR_H
#include <cstring>
#include <ctime>
#include <cstdlib> 

struct Partition {
    char part_status;      
    char part_type;       
    char part_fit;         
    int part_start;        
    int part_size;         
    char part_name[16];   
    int part_correlative; 
    char part_id[4];       

    Partition() {
        part_status = '0';
        part_type = '\0';
        part_fit = '\0';
        part_start = -1;           
        part_size = 0;
        memset(part_name, 0, sizeof(part_name));
        part_correlative = -1;    
        memset(part_id, 0, sizeof(part_id));
    }
};

struct MBR {
    int mbr_tamano;
    time_t mbr_fecha_creacion;
    int mbr_dsk_signature;
    char dsk_fit;        
    Partition mbr_partitions[4];

    MBR() {
        mbr_tamano = 0;
        mbr_fecha_creacion = time(nullptr);
        mbr_dsk_signature = rand();
        dsk_fit = 'F';
    }
};

struct EBR {
    char part_status;
    char part_fit;
    int part_start;
    int part_size;
    int part_next;
    char part_name[16];

    EBR() {
        part_status = '0';
        part_fit = 'F';
        part_start = -1;
        part_size = 0;
        part_next = -1;
        memset(part_name, 0, sizeof(part_name));
    }
};

struct MountedPartition {
    std::string id;              // ID de montaje
    std::string path;            // Ruta del disco
    std::string name;            // Nombre de la partición
    int partition_index;         // Índice en el MBR (0-3)
    char type;                   // Tipo (P, L)
    int start;                   // Byte de inicio
    int size;                    // Tamaño en bytes
    int correlative;             // Número correlativo
};

#endif