
import type { 
  SessionState, DiskInfo, PartitionInfo, 
  FileSystemItem, FileSystemResponse, JournalEntry, CommandResult 
} from '../types';

const API_BASE = import.meta.env.VITE_API_URL || 'http://localhost:3001';

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
    // Mock para desarrollo - REMOVER cuando backend esté listo
    if (import.meta.env.DEV) {
      return new Promise((resolve) => {
        setTimeout(() => {
          if (path === '/' || path === '') {
            resolve({
              path: '/',
              type: 'folder',
              permissions: '755',
              owner: 'root',
              group: 'root',
              content: [
                { name: 'home', type: 'folder', permissions: '755', owner: 'root', group: 'root' },
                { name: 'users.txt', type: 'file', permissions: '664', owner: 'root', group: 'root', size: 45 }
              ]
            });
          } else if (path === '/home') {
            resolve({
              path: '/home',
              type: 'folder',
              permissions: '755',
              owner: 'root',
              group: 'root',
              content: [
                { name: 'user', type: 'folder', permissions: '755', owner: 'user1', group: 'users' }
              ]
            });
          }
          resolve({
            path,
            type: 'folder',
            permissions: '755',
            owner: 'root',
            group: 'root',
            content: []
          });
        }, 300);
      });
    }

    const encodedPath = path === '/' || path === '' ? '' : encodeURIComponent(path.startsWith('/') ? path.slice(1) : path);
    const url = `${API_BASE}/api/fs/${idParticion}/${encodedPath}`;
    const res = await fetch(url);
    if (!res.ok) throw new Error('Error al navegar');
    return res.json();
  },

  // Leer contenido de archivo
  leerArchivo: async (idParticion: string, path: string): Promise<string> => {
    if (import.meta.env.DEV) {
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve('1,G,root\n1,U,root,root,123\n');
        }, 200);
      });
    }
    
    const encodedPath = encodeURIComponent(path);
    const res = await fetch(`${API_BASE}/api/file/${idParticion}?path=${encodedPath}`);
    if (!res.ok) throw new Error('Error al leer archivo');
    const data = await res.json();
    return data.contenido;
  },

  // Obtener journaling de partición EXT3
  getJournaling: async (idParticion: string): Promise<JournalEntry[]> => {
    if (import.meta.env.DEV) {
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve([
            { operacion: 'MKFS', ruta: '/', contenido: 'Sistema EXT3 inicializado', fecha: '2026-04-13 10:30:00' },
            { operacion: 'MKDIR', ruta: '/home', contenido: 'Carpeta creada', fecha: '2026-04-13 10:31:15' },
            { operacion: 'MKFILE', ruta: '/home/users.txt', contenido: 'Archivo creado', fecha: '2026-04-13 10:32:00' }
          ]);
        }, 300);
      });
    }
    
    const res = await fetch(`${API_BASE}/api/journaling/${idParticion}`);
    if (!res.ok) throw new Error('Error al obtener journaling');
    return res.json();
  },

  // Login gráfico
  login: async (user: string, pass: string, id: string): Promise<CommandResult> => {
    return api.ejecutar(`login -user=${user} -pass=${pass} -id=${id}`);
  },
};