import { useState } from 'react';
import { api } from './services/Api';

interface LoginProps {
  onLoginSuccess: (user: string, partition: string) => void;
}

export default function Login({ onLoginSuccess }: LoginProps) {
  const [user, setUser] = useState('root');
  const [pass, setPass] = useState('123');
  const [partitionId, setPartitionId] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const result = await api.login(user, pass, partitionId.toUpperCase());
      
      if (result.exitoso || result.resultado) {
        onLoginSuccess(user, partitionId.toUpperCase());
      } else {
        setError(result.resultado || result.error || 'Error de autenticación');
      }
    } catch (err: any) {
      setError(err.message || 'Error de conexión con el servidor');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="login-container">
      <div className="login-card">
        <h2> Iniciar Sesión</h2>
        <form onSubmit={handleSubmit} className="login-form">
          <div className="form-group">
            <label htmlFor="user">Usuario:</label>
            <input
              id="user"
              type="text"
              value={user}
              onChange={(e) => setUser(e.target.value)}
              placeholder="root"
              required
              disabled={loading}
              className="form-input"
            />
          </div>
          <div className="form-group">
            <label htmlFor="pass">Contraseña:</label>
            <input
              id="pass"
              type="password"
              value={pass}
              onChange={(e) => setPass(e.target.value)}
              placeholder="123"
              required
              disabled={loading}
              className="form-input"
            />
          </div>
          <div className="form-group">
            <label htmlFor="partition">ID de Partición:</label>
            <input
              id="partition"
              type="text"
              value={partitionId}
              onChange={(e) => setPartitionId(e.target.value.toUpperCase())}
              placeholder="601A"
              maxLength={4}
              pattern="[A-Z0-9]{4}"
              required
              disabled={loading}
              className="form-input"
            />
          </div>
          {error && <div className="error-message" role="alert">{error}</div>}
          <button type="submit" disabled={loading} className="btn-primary">
            {loading ? 'Conectando...' : 'Ingresar'}
          </button>
        </form>
      </div>
    </div>
  );
}