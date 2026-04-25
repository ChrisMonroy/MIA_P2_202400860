// src/components/FileBrowser.tsx
import type { FileSystemItem } from './types';

interface FileBrowserProps {
  items: FileSystemItem[];
  currentPath: string;
  onNavigate: (path: string) => void;
  onViewFile: (path: string) => void;
}

export default function FileBrowser({ items, currentPath, onNavigate, onViewFile }: FileBrowserProps) {
  const handleItemClick = (item: FileSystemItem) => {
    const newPath = currentPath === '/' 
      ? `/${item.name}` 
      : `${currentPath}/${item.name}`;
    
    if (item.type === 'folder') {
      onNavigate(newPath);
    } else {
      onViewFile(newPath);
    }
  };

  const handleParent = () => {
    if (currentPath === '/') return;
    const parts = currentPath.split('/').filter(Boolean);
    parts.pop();
    const parentPath = parts.length === 0 ? '/' : `/${parts.join('/')}`;
    onNavigate(parentPath);
  };

  if (items.length === 0) {
    return <div className="empty-folder">Carpeta vacía</div>;
  }

  return (
    <div className="file-browser">
      <div className="breadcrumb">
        <button onClick={() => onNavigate('/')} className="breadcrumb-home">home</button>
        {currentPath !== '/' && (
          <>
            <span>/</span>
            <button onClick={handleParent}>⬅regresar</button>
            <span>{currentPath}</span>
          </>
        )}
      </div>

      <table className="file-table">
        <thead>
          <tr>
            <th> Nombre</th>
            <th>Tipo</th>
            <th>Permisos</th>
            <th>Propietario</th>
            <th>Grupo</th>
            <th>Tamaño</th>
          </tr>
        </thead>
        <tbody>
          {items.map((item, idx) => (
            <tr 
              key={idx} 
              onClick={() => handleItemClick(item)}
              className={`file-row ${item.type} clickable`}
            >
              <td>
                {item.type === 'folder' ? 'carpeta' : 'archivo'} {item.name}
              </td>
              <td>{item.type === 'folder' ? 'Carpeta' : 'Archivo'}</td>
              <td><code className="perm-badge">{item.permissions}</code></td>
              <td>{item.owner}</td>
              <td>{item.group}</td>
              <td>{item.size !== undefined ? `${item.size} B` : '-'}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}