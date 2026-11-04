import { FormEvent, useState } from "react";
import { api, errorText } from "../api";

export default function Login({ onLogin }: { onLogin: (handle: string) => void }) {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError("");
    try {
      const r = await api<{ handle: string }>("POST", "/api/login", { username, password });
      onLogin(r.handle);
    } catch (err) {
      setError(errorText(err));
      setBusy(false);
    }
  }

  return (
    <form className="card form" onSubmit={submit}>
      <h1>Log in</h1>
      <label>Username
        <input value={username} onChange={(e) => setUsername(e.target.value)} autoComplete="username" autoCapitalize="none" required />
      </label>
      <label>Password
        <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} autoComplete="current-password" required />
      </label>
      {error && <p role="alert" className="error">{error}</p>}
      <button className="primary" disabled={busy}>Log in</button>
      <p className="muted">Have an invite? <a href="#/signup">Sign up</a></p>
    </form>
  );
}
