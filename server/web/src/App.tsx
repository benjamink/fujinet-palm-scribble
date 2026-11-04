import { useCallback, useEffect, useState } from "react";
import { api, ApiError, onUnauthorized } from "./api";
import Devices from "./pages/Devices";
import Inbox from "./pages/Inbox";
import Login from "./pages/Login";
import Note from "./pages/Note";
import Sent from "./pages/Sent";
import Signup from "./pages/Signup";

const route = () => window.location.hash.replace(/^#/, "") || "/inbox";

function useRoute() {
  const [r, setR] = useState(route());
  useEffect(() => {
    const on = () => setR(route());
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);
  return r;
}

const go = (path: string) => {
  window.location.hash = `#${path}`;
};

export default function App() {
  const r = useRoute();
  const [handle, setHandle] = useState<string | null>(null);
  const [checked, setChecked] = useState(false);

  useEffect(() => {
    api<{ handle: string }>("GET", "/api/me")
      .then((m) => setHandle(m.handle))
      .catch((e) => {
        if (!(e instanceof ApiError && e.status === 401)) console.error(e);
      })
      .finally(() => setChecked(true));
  }, []);

  useEffect(
    () =>
      onUnauthorized(() => {
        setHandle(null);
        go("/login");
      }),
    [],
  );

  const onLogin = useCallback((h: string) => {
    setHandle(h);
    go("/inbox");
  }, []);

  async function logout() {
    try {
      await api("POST", "/api/logout");
    } catch {
      /* session already gone */
    }
    setHandle(null);
    go("/login");
  }

  if (!checked) return <main><p className="muted">Loading...</p></main>;

  if (!handle) {
    return (
      <>
        <header className="bar"><span className="brand">Scribble</span></header>
        <main>{r === "/signup" ? <Signup onLogin={onLogin} /> : <Login onLogin={onLogin} />}</main>
      </>
    );
  }

  const noteMatch = /^\/note\/(\d+)$/.exec(r);
  const nav = (path: string, label: string) => (
    <a href={`#${path}`} aria-current={r === path ? "page" : undefined}>{label}</a>
  );
  return (
    <>
      <header className="bar">
        <span className="brand">Scribble</span>
        <nav aria-label="Main">
          {nav("/inbox", "Inbox")}
          {nav("/sent", "Sent")}
          {nav("/devices", "Devices")}
        </nav>
        <span className="user">
          <span>{handle}</span>
          <button onClick={logout}>Logout</button>
        </span>
      </header>
      <main>
        {noteMatch ? (
          <>
            <p><a href="#/inbox">&larr; Back</a></p>
            <Note key={noteMatch[1]} id={Number(noteMatch[1])} onDeleted={() => go("/inbox")} />
          </>
        ) : r === "/sent" ? <Sent />
        : r === "/devices" ? <Devices />
        : <Inbox />}
      </main>
    </>
  );
}
