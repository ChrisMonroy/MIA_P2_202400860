// src/components/DiskSelector.tsx
import { useEffect, useState } from 'react';
import { api } from './services/Api';
import type { DiskInfo } from './types';

interface DiskSelectorProps {
  onSelect: (disk: DiskInfo) => void;
  selectedDisk?: DiskInfo | null;
}

export default function DiskSelector({ onSelect, selectedDisk }: DiskSelectorProps) {
  const [disks, setDisks] = useState<DiskInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  useEffect(() => {
    const loadDisks = async () => {
      try {
        const data = await api.getDiscos();
        setDisks(data);
      } catch (err: any) {
        setError('Error al cargar discos');
      } finally {
        setLoading(false);
      }
    };
    loadDisks();
  }, []);

  const formatSize = (bytes: number): string => {
    if (bytes >= 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    if (bytes >= 1024) return `${(bytes / 1024).toFixed(2)} KB`;
    return `${bytes} B`;
  };

  if (loading) return <div className="loading"> Cargando discos...</div>;
  if (error) return <div className="error">{error}</div>;
  if (disks.length === 0) return <div className="empty">No hay discos disponibles</div>;

  return (
    <div className="disk-selector">
      <h3> Seleccionar Disco</h3>
      <div className="disk-list">
        {disks.map((disk, idx) => (
          <button
            key={idx}
            className={`disk-card ${selectedDisk?.path === disk.path ? 'selected' : ''}`}
            onClick={() => onSelect(disk)}
          >
            <div className="disk-info">
              <strong> {disk.path.split('/').pop()}</strong>
              <span className="disk-size">{formatSize(disk.size)}</span>
              <span className="disk-fit">Fit: {disk.fit}</span>
              <span className="disk-partitions">
                Particiones: {disk.partitions.filter(p => p.status === 'mounted').length} montadas
              </span>
            </div>
          </button>
        ))}
      </div>
    </div>
  );
}