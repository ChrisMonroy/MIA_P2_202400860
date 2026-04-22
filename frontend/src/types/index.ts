
export interface SessionState {
  sesion_activa: boolean;
  usuario: string;
  particiones_montadas: number;
  ids_montados: string[];
}

export interface DiskInfo {
  path: string;
  size: number;
  fit: 'BF' | 'FF' | 'WF';
  signature: number;
  partitions: PartitionInfo[];
}

export interface PartitionInfo {
  id: string;
  name: string;
  type: 'P' | 'E' | 'L';
  fit: 'BF' | 'FF' | 'WF';
  start: number;
  size: number;
  status: 'mounted' | 'unmounted';
  filesystem?: 'EXT2' | 'EXT3';
}

// src/types.ts
export interface FileSystemItem {
  name: string;        // ← antes: nombre
  type: 'folder' | 'file';
  permissions: string; // ← antes: permisos
  owner: string;       // ← antes: propietario
  group: string;       // ← antes: grupo
  size?: number;       // ← antes: tamano
}

export interface FileSystemResponse {
  ruta: string;
  tipo: 'folder' | 'file';
  contenido: any[];    // ← Backend usa 'contenido'
  permisos?: string;
  propietario?: string;
  grupo?: string;
  tamano?: number;
}
export interface JournalEntry {
  operacion: string;
  ruta: string;
  contenido: string;
  fecha: string;
}

export interface CommandResult {
  resultado: string;
  exitoso: boolean;
  error?: string;
}