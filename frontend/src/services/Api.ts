
import type { 
  SessionState, DiskInfo, PartitionInfo, 
  FileSystemResponse, JournalEntry, CommandResult 
} from '../types';

const API_BASE = import.meta.env.VITE_API_URL || 'http://3.141.166.93';

export const api = {
  
  ejecutar: async (comando: string): Promise<CommandResult> => {
    const res = await fetch(`${API_BASE}/api/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ comando }),
    });
    return res.json();
  },

  ejecutarScript: async (script: string): Promise<CommandResult> => {
    const res = await fetch(`${API_BASE}/api/script`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script }),
    });
    return res.json();
  },

  getEstado: async (): Promise<SessionState> => {
    const res = await fetch(`${API_BASE}/api/estado`);
    return res.json();
  },

  logout: async (): Promise<void> => {
    await fetch(`${API_BASE}/api/logout`, { method: 'POST' });
  },

  // Obtener lista de discos (mock - implementar en backend)
getDiscos: async (): Promise<DiskInfo[]> => {
  try {
    const res = await fetch(`${API_BASE}/api/discos`);
    if (!res.ok) throw new Error('Error al obtener discos');
    const data = await res.json();
    return data.discos || [];
  } catch (err) {
    console.error('Error getting disks:', err);
    return [];
  }
},

  // Obtener particiones de un disco
getParticiones: async (diskPath: string): Promise<PartitionInfo[]> => {
  try {
    const encodedPath = encodeURIComponent(diskPath);
    const res = await fetch(`${API_BASE}/api/discos/${encodedPath}/particiones`);
    if (!res.ok) throw new Error('Error al obtener particiones');
    const data = await res.json();
    return data.particiones || [];
  } catch (err) {
    console.error('Error getting partitions:', err);
    return [];
  }
},
  // Navegar sistema de archivos
navegar: async (idParticion: string, path: string): Promise<FileSystemResponse> => {
  try {
    const encodedPath = path === '/' || path === '' ? '' : encodeURIComponent(path.startsWith('/') ? path.slice(1) : path);
    const url = encodedPath 
      ? `${API_BASE}/api/fs/${idParticion}/${encodedPath}`
      : `${API_BASE}/api/fs/${idParticion}`;
    
    console.log('[DEBUG] Fetch URL:', url);
    
    const res = await fetch(url, {
      method: 'GET',
      headers: { 'Content-Type': 'application/json' }
    });
    
    const responseText = await res.text();
    console.log('[DEBUG] Response status:', res.status, 'body:', responseText);
    
    if (!res.ok) {
      throw new Error(`Error ${res.status}: ${responseText}`);
    }
    
    const data = JSON.parse(responseText);
    console.log('=== [FS DEBUG] Respuesta completa ===');
  console.log('data:', data);
  console.log('data.contenido:', data.contenido);
  console.log('Es array?', Array.isArray(data.contenido));
  console.log('Longitud:', data.contenido?.length);
  
  if (data.contenido && Array.isArray(data.contenido)) {
    console.log('Elementos:', data.contenido.map((item: any) => ({
      nombre: item.nombre,
      tipo: item.tipo
    })));
  }
    
    
    return data;
  } catch (err: any) {
    console.error('Error navigating:', err);
    throw err;
  }
},

  // Leer contenido de archivo
leerArchivo: async (idParticion: string, path: string): Promise<string> => {
  try {
    const encodedPath = encodeURIComponent(path);
    const res = await fetch(`${API_BASE}/api/file/${idParticion}?path=${encodedPath}`, {
      method: 'GET',
      headers: { 'Content-Type': 'application/json' }
    });
    
    if (!res.ok) {
      const errorText = await res.text();
      throw new Error(`Error ${res.status}: ${errorText}`);
    }
    
    const data = await res.json();
    return data.contenido || '';
  } catch (err: any) {
    console.error('Error reading file:', err);
    throw err;
  }
},

// Obtener journaling - VERSIÓN REAL
getJournaling: async (idParticion: string): Promise<JournalEntry[]> => {
  try {
    const res = await fetch(`${API_BASE}/api/journaling/${idParticion}`, {
      method: 'GET',
      headers: { 'Content-Type': 'application/json' }
    });
    
    if (!res.ok) {
      const errorText = await res.text();
      throw new Error(`Error ${res.status}: ${errorText}`);
    }
    
    const data = await res.json();
    return data.entradas || [];
  } catch (err: any) {
    console.error('Error getting journaling:', err);
    throw err;
  }
},

  // Login gráfico
  login: async (user: string, pass: string, id: string): Promise<CommandResult> => {
    return api.ejecutar(`login -user=${user} -pass=${pass} -id=${id}`);
  },
};