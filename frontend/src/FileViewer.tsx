// src/components/FileViewer.tsx
import { useState, useEffect } from 'react';
import { api } from './services/Api';

interface FileViewerProps {
  partitionId: string;
  filePath: string;
  onBack: () => void;
}

export default function FileViewer({ partitionId, filePath, onBack }: FileViewerProps) {
  const [content, setContent] = useState('');
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');

  useEffect(() => {
    const loadContent = async () => {
      try {
        const data = await api.leerArchivo(partitionId, filePath);
        setContent(data);
      } catch (err: any) {
        setError(err.message || 'Error al leer archivo');
      } finally {
        setLoading(false);
      }
    };
    loadContent();
  }, [partitionId, filePath]);

  if (loading) return <div className="loading"> Cargando contenido...</div>;
  if (error) return <div className="error"> {error}</div>;

  return (
    <div className="file-viewer">
      <div className="file-viewer-header">
        <button onClick={onBack} className="btn-back">← Volver</button>
        <h4>📄 {filePath.split('/').pop()}</h4>
      </div>
      <pre className="file-content">{content || '(archivo vacío)'}</pre>
    </div>
  );
}