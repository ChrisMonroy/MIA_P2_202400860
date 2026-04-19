// src/components/JournalViewer.tsx
import { useState, useEffect } from 'react';
import { api } from './services/Api';
import type { JournalEntry } from './types';

interface JournalViewerProps {
  partitionId: string;
}

export default function JournalViewer({ partitionId }: JournalViewerProps) {
  const [entries, setEntries] = useState<JournalEntry[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  const loadJournal = async () => {
    setLoading(true);
    setError('');
    try {
      const data = await api.getJournaling(partitionId);
      setEntries(data);
    } catch (err: any) {
      setError(err.message || 'Error al cargar journaling');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadJournal();
  }, [partitionId]);

  if (loading) {
    return (
      <div className="journal-container">
        <div className="loading-spinner"> Cargando bitácora...</div>
      </div>
    );
  }

  if (error) {
    return (
      <div className="journal-container">
        <div className="error-message"> {error}</div>
      </div>
    );
  }

  return (
    <div className="journal-container">
      <div className="journal-header">
        <h3>Bitácora EXT3 - Partición {partitionId}</h3>
        <button onClick={loadJournal} className="btn-refresh">
           Actualizar
        </button>
      </div>
      
      {entries.length === 0 ? (
        <div className="empty-state">
          <p>📭 No hay transacciones registradas</p>
          <p className="hint">Realiza operaciones (mkdir, mkfile, copy, etc.) para ver el journaling</p>
        </div>
      ) : (
        <div className="journal-table-wrapper">
          <table className="journal-table">
            <thead>
              <tr>
                <th>Operación</th>
                <th>Ruta</th>
                <th>Contenido</th>
                <th>Fecha</th>
              </tr>
            </thead>
            <tbody>
              {entries.map((entry, index) => (
                <tr key={index} className="journal-row">
                  <td>
                    <span className={`badge ${entry.operacion.toLowerCase().replace('_', '-')}`}>
                      {entry.operacion}
                    </span>
                  </td>
                  <td className="path-cell">{entry.ruta}</td>
                  <td className="content-cell">{entry.contenido}</td>
                  <td className="date-cell">{entry.fecha}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <div className="journal-footer">
            Total: {entries.length} transaccion(es)
          </div>
        </div>
      )}
    </div>
  );
}