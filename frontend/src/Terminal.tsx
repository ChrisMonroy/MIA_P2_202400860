// src/components/Terminal.tsx
import { useState } from 'react';
import { api } from './services/Api';

export default function Terminal() {
  const [input, setInput] = useState('');
  const [output, setOutput] = useState('');
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState('');

  const execute = async () => {
    if (!input.trim()) {
      setStatus('Escribe un comando primero');
      return;
    }

    setLoading(true);
    setOutput('');
    setStatus('Ejecutando...');

    try {
      const lines = input.split('\n').filter(l => l.trim());
      const isScript = lines.length > 1;

      const result = isScript 
        ? await api.ejecutarScript(input)
        : await api.ejecutar(input);

      setOutput(result.resultado || 'Sin respuesta');
      setStatus(result.exitoso ? 'Ejecución completada' : ' Error en ejecución');
    } catch (err: any) {
      setOutput(`Error de conexión: ${err.message || 'Desconocido'}`);
      setStatus('Error de conexión');
    } finally {
      setLoading(false);
    }
  };

  const loadScript = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (event) => {
      if (event.target?.result) {
        setInput(event.target.result as string);
        setStatus(`Script cargado: ${file.name}`);
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  };

  const clear = () => {
    setInput('');
    setOutput('');
    setStatus('');
  };

  return (
    <div className="terminal-panel">
      <div className="terminal-toolbar">
        <label className="btn">
           Cargar Script
          <input 
            type="file" 
            onChange={loadScript} 
            accept=".smia,.txt" 
            hidden 
          />
        </label>
        <button 
          className="btn btn-run" 
          onClick={execute} 
          disabled={loading}
        >
          {loading ? ' Ejecutando...' : 'Ejecutar'}
        </button>
        <button className="btn btn-clear" onClick={clear}>
          Limpiar
        </button>
      </div>

      {status && <div className={`status ${status.includes('Error') || status.includes('❌') ? 'error' : 'success'}`}>
        {status}
      </div>}

      <div className="terminal-body">
        <div className="panel">
          <label> Entrada</label>
          <textarea
            value={input}
            onChange={(e) => setInput(e.target.value)}
            placeholder="Escribe comandos o carga un script .smia..."
            disabled={loading}
            spellCheck={false}
          />
        </div>
        <div className="panel">
          <label> Salida</label>
          <pre className="output">{output || 'Esperando comando...'}</pre>
        </div>
      </div>
    </div>
  );
}