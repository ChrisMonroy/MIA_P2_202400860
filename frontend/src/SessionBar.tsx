import { api } from './services/Api';

interface SessionBarProps {
  session: {
    active: boolean;
    user: string;
    partition: string;
  };
  onLogout: () => Promise<void>; 
}

export default function SessionBar({ session, onLogout }: SessionBarProps) {
  const handleLogout = async () => {
    await api.logout();
    onLogout(); 
  };

  return (
    <div className="session-bar">
      <div className="session-info">
        <span> Usuario: <strong>{session.user || 'Invitado'}</strong></span>
        <span> Partición: <strong>{session.partition}</strong></span>
        <span className={`status ${session.active ? 'active' : 'inactive'}`}>
          {session.active ? '● Activa' : '○ Inactiva'}
        </span>
      </div>
      <button onClick={handleLogout} className="btn-logout" disabled={!session.active}>
         Cerrar Sesión
      </button>
    </div>
  );
}