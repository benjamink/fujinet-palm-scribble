import { FormEvent, useState } from "react";
import { api, errorText } from "../api";

export default function Signup({ onLogin }: { onLogin: (handle: string) => void }) {
  const [f, setF] = useState({ invite: "", username: "", password: "", handle: "" });
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const set = (k: keyof typeof f) => (e: React.ChangeEvent<HTMLInputElement>) => setF({ ...f, [k]: e.target.value });

  async function submit(e: FormEvent) {
    e.preventDefault();
    setBusy(true);
    setError("");
    try {
      const r = await api<{ handle: string }>("POST", "/api/signup", f);
      onLogin(r.handle);
    } catch (err) {
      setError(errorText(err));
      setBusy(false);
    }
  }

  return (
    <form className="card form" onSubmit={submit}>
      <h1>Sign up</h1>
      <label>Invite code
        <input value={f.invite} onChange={set("invite")} autoCapitalize="none" autoComplete="off" required />
      </label>
      <label>Username
        <input value={f.username} onChange={set("username")} autoComplete="username" autoCapitalize="none" required />
      </label>
      <label>Password
        <input type="password" value={f.password} onChange={set("password")} autoComplete="new-password" required />
      </label>
      <label>Handle
        <input value={f.handle} onChange={set("handle")} autoComplete="nickname" aria-describedby="handle-hint" required />
      </label>
      <p id="handle-hint" className="hint">The name other people see on your notes.</p>
      {error && <p role="alert" className="error">{error}</p>}
      <button className="primary" disabled={busy}>Sign up</button>
      <p className="muted">Already registered? <a href="#/login">Log in</a></p>
    </form>
  );
}
