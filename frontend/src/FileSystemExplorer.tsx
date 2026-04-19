import { useState, useEffect } from 'react';
import { api } from './services/Api';
import DiskSelector from './DiskSelector';
import PartitionSelector from './PartitionSelector';
import FileBrowser from './FileBrowser';
import FileViewer from './FileViewer';
import JournalViewer from './JournalViewer';
import type { DiskInfo, PartitionInfo, FileSystemItem } from './types';

interface ExplorerProps {
  onLogout: () => void;
}

type Step = 'disk' | 'partition' | 'browse' | 'file' | 'journal';

export default function FileSystemExplorer({ onLogout }: ExplorerProps) {
  const [step, setStep] = useState<Step>('disk');
  const [selectedDisk, setSelectedDisk] = useState<DiskInfo | null>(null);
  const [selectedPartition, setSelectedPartition] = useState<PartitionInfo | null>(null);
  const [currentPath, setCurrentPath] = useState('/');
  const [viewingFile, setViewingFile] = useState<string | null>(null);
  const [folderContent, setFolderContent] = useState<FileSystemItem[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const loadFolder = async (path: string) => {
    if (!selectedPartition) return;
    setLoading(true);
    setError('');
    try {
      const data = await api.navegar(selectedPartition.id, path);
      setFolderContent(data.content || []);
    } catch (err: any) {
      setError(err.message || 'Error al cargar la carpeta');
      setFolderContent([]);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    if (step === 'browse' && selectedPartition && !viewingFile) {
      loadFolder(currentPath);
    }
  }, [step, selectedPartition, currentPath, viewingFile]);

  // =====================
  // HANDLERS
  // =====================
  const handleDiskSelect = (disk: DiskInfo) => {
    setSelectedDisk(disk);
    setStep('partition');
  };

  const handlePartitionSelect = (partition: PartitionInfo) => {
    setSelectedPartition(partition);
    setStep('browse');
    setCurrentPath('/');
    setViewingFile(null);
  };

  const handleNavigate = (path: string) => {
    setCurrentPath(path);
    setViewingFile(null);
    setStep('browse');
  };

  const handleViewFile = (filePath: string) => {
    setViewingFile(filePath);
    setStep('file');
  };

  const handleBack = () => {
    if (viewingFile) {
      setViewingFile(null);
      setStep('browse');
    } else if (currentPath !== '/') {
      const parts = currentPath.split('/').filter(Boolean);
      parts.pop();
      setCurrentPath(parts.length === 0 ? '/' : `/${parts.join('/')}`);
    } else if (step === 'browse') {
      setStep('partition');
    } else if (step === 'partition') {
      setStep('disk');
    }
  };

  const handleToggleJournal = () => {
    if (!selectedPartition) return;
    setStep(step === 'journal' ? 'browse' : 'journal');
  };

  // =====================
  // RENDER
  // =====================
  return (
    <div className="explorer-container">
      <header className="explorer-header">
        <h2>📁 Explorador del Sistema de Archivos</h2>

        <div className="header-controls">
          {step !== 'disk' && (
            <button onClick={handleBack}>⬅ Atrás</button>
          )}

          {selectedPartition && (
            <button onClick={handleToggleJournal}>
              {step === 'journal' ? '📁 Explorador' : '📜 Journaling'}
            </button>
          )}

          <button onClick={onLogout}>🔒 Salir</button>
        </div>
      </header>

      <main className="explorer-content">

        {step === 'disk' && (
          <DiskSelector onSelect={handleDiskSelect} selectedDisk={selectedDisk} />
        )}

        {step === 'partition' && selectedDisk && (
          <PartitionSelector
            disk={selectedDisk}
            onSelect={handlePartitionSelect}
            selectedPartition={selectedPartition}
          />
        )}

        {step === 'browse' && selectedPartition && !viewingFile && (
          <>
            <div className="breadcrumb">📂 {currentPath}</div>
            <FileBrowser
              items={folderContent}
              currentPath={currentPath}
              onNavigate={handleNavigate}
              onViewFile={handleViewFile}
            />
          </>
        )}

        {step === 'file' && selectedPartition && viewingFile && (
          <FileViewer
            partitionId={selectedPartition.id}
            filePath={viewingFile}
            onBack={() => {
              setViewingFile(null);
              setStep('browse');
            }}
          />
        )}

        {step === 'journal' && selectedPartition && (
          <JournalViewer partitionId={selectedPartition.id} />
        )}

        {loading && <div className="overlay-loading">⏳ Cargando...</div>}
        {error && <div className="overlay-error">{error}</div>}
      </main>
    </div>
  );
}