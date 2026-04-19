// src/components/PartitionSelector.tsx
import { useEffect, useState } from 'react';
import { api } from './services/Api';
import type { PartitionInfo, DiskInfo } from './types';

interface PartitionSelectorProps {
  disk: DiskInfo;
  onSelect: (partition: PartitionInfo) => void;
  selectedPartition?: PartitionInfo | null;
}

export default function PartitionSelector({ disk, onSelect, selectedPartition }: PartitionSelectorProps) {
  const [partitions, setPartitions] = useState<PartitionInfo[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  useEffect(() => {
    const loadPartitions = async () => {
      try {
        const data = await api.getParticiones(disk.path);
        setPartitions(data);
      } catch (err: any) {
        setError('Error al cargar particiones');
      } finally {
        setLoading(false);
      }
    };
    loadPartitions();
  }, [disk.path]);

  if (loading) return <div className="loading">Cargando particiones...</div>;
  if (error) return <div className="error">{error}</div>;

  const mountedPartitions = partitions.filter(p => p.status === 'mounted');

  return (
    <div className="partition-selector">
      <h3>Seleccionar Partición</h3>
      {mountedPartitions.length === 0 ? (
        <div className="empty"> No hay particiones montadas</div>
      ) : (
        <div className="partition-list">
          {mountedPartitions.map((partition, idx) => (
            <button
              key={idx}
              className={`partition-card ${selectedPartition?.id === partition.id ? 'selected' : ''}`}
              onClick={() => onSelect(partition)}
            >
              <div className="partition-header">
                <span className="partition-id">{partition.id}</span>
                <span className="partition-type">{partition.type === 'P' ? 'Primaria' : partition.type === 'E' ? 'Extendida' : 'Lógica'}</span>
              </div>
              <div className="partition-details">
                <strong>{partition.name}</strong>
                <span>Tamaño: {(partition.size / 1024).toFixed(2)} KB</span>
                <span>Fit: {partition.fit}</span>
                {partition.filesystem && (
                  <span className={`fs-badge ${partition.filesystem === 'EXT3' ? 'ext3' : 'ext2'}`}>
                    {partition.filesystem}
                  </span>
                )}
              </div>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}