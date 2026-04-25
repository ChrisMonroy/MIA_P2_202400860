import { useState, useEffect } from 'react';
import Terminal from './Terminal';
import Login from './Login';
import FileSystemExplorer from './FileSystemExplorer';
import JournalViewer from './JournalViewer';
import SessionBar from './SessionBar';
import { api } from './services/Api';
import './App.css';

type View = 'terminal' | 'login' | 'explorer' | 'journal';

interface SessionState {
  active: boolean;
  user: string;
  partition: string;
}

function App() {
  const [session, setSession] = useState<SessionState>({ active: false, user: '', partition: '' });
  const [view, setView] = useState<View>('terminal');
  const [mountedIds, setMountedIds] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);

  // App.tsx - useEffect inicial
useEffect(() => {
  const checkSession = async () => {
    try {
      const state = await api.getEstado();
      setMountedIds(state.ids_montados || []);
      
      if (state.sesion_activa && state.usuario) {
        const lastPartition = localStorage.getItem('lastPartition');
        if (lastPartition && state.ids_montados?.includes(lastPartition)) {
          setSession({ active: true, user: state.usuario, partition: lastPartition });
        }
      }
    } catch (err) {
      console.error('Error verificando sesión:', err);
    } finally {
      setLoading(false);
    }
  };
  checkSession();
}, []);

// Y en handleLoginSuccess, guardar la partición seleccionada:
const handleLoginSuccess = (user: string, partition: string) => {
  localStorage.setItem('lastPartition', partition);
  setSession({ active: true, user, partition });
  setView('explorer');
};

  const handleLogout = async () => {
    try {
      await api.logout();
      setSession({ active: false, user: '', partition: '' });
      setView('terminal'); // Volver a la terminal al cerrar sesión
    } catch (err) {
      console.error('Error cerrando sesión:', err);
    }
  };

  const switchView = (newView: View) => {
    if (!session.active && (newView === 'explorer' || newView === 'journal')) return;
    setView(newView);
  };

  if (loading) {
    return (
      <div className="app loading-screen">
        <div className="spinner"></div>
        <p>Conectando con ExtreamFS...</p>
      </div>
    );
  }

  return (
    <div className="app">
      <header className="app-header">
        <div className="header-left">
          <h1>ExtreamFS 2.0</h1>
          <p className="subtitle">Christopher Monroy 202400860 | MIA 1S2026</p>
        </div>
        <SessionBar session={session} onLogout={handleLogout} />
      </header>

      <nav className="app-nav">
        <button 
          className={`nav-btn ${view === 'terminal' ? 'active' : ''}`} 
          onClick={() => switchView('terminal')}
        >
           Terminal
        </button>
        
        {session.active ? (
          <>
            <button 
              className={`nav-btn ${view === 'explorer' ? 'active' : ''}`} 
              onClick={() => switchView('explorer')}
            >
               Explorador
            </button>
            <button 
              className={`nav-btn ${view === 'journal' ? 'active' : ''}`} 
              onClick={() => switchView('journal')}
            >
               Journaling
            </button>
          </>
        ) : (
          <button className="nav-btn btn-login" onClick={() => switchView('login')}>
             Iniciar Sesión
          </button>
        )}
      </nav>

      <main className="app-main">
        {view === 'terminal' && <Terminal />}
        {view === 'login' && !session.active && <Login onLoginSuccess={handleLoginSuccess} />}
        {view === 'explorer' && session.active && (
        <FileSystemExplorer 
        onLogout={handleLogout}  
        />
        )}
        {view === 'journal' && session.active && (
          <JournalViewer partitionId={session.partition} />
        )}
      </main>

      <footer className="app-footer">
        <p>Universidad de San Carlos de Guatemala • Facultad de Ingeniería</p>
      </footer>
    </div>
  );
}

export default App;