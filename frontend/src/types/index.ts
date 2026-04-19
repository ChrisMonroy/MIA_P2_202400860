
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

export interface FileSystemItem {
  name: string;
  type: 'file' | 'folder';
  permissions: string;
  owner: string;
  group: string;
  size?: number;
  modified?: string;
  created?: string;
}

export interface FileSystemResponse {
  path: string;
  type: 'file' | 'folder';
  permissions: string;
  owner: string;
  group: string;
  size?: number;
  content?: FileSystemItem[];
  fileContent?: string;
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