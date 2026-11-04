import { useState } from "react";
import { api, errorText } from "../api";
import { formatDate } from "../format";
import { useApi } from "../hooks";
import type { Device } from "../types";

export default function Devices() {
  const { data, error: loadError, loading, reload } = useApi<Device[]>("/api/devices");
  const [error, setError] = useState("");
  const [confirmAll, setConfirmAll] = useState(false);

  async function run(method: "DELETE" | "POST", path: string) {
    try {
      await api(method, path);
      setError("");
      setConfirmAll(false);
      await reload();
    } catch (e) {
      setError(errorText(e));
    }
  }

  const shown = error || loadError;
  return (
    <section>
      <h1>Devices</h1>
      {shown && <p role="alert" className="error">{shown}</p>}
      {loading && <p className="muted">Loading...</p>}
      {data && data.length === 0 && <p className="muted">No devices linked.</p>}
      <ul className="list devices">
        {data?.map((d) => (
          <li key={d.id}>
            <div>
              <span className="who">{d.label}</span>
              <span className="sub">
                Added <span>{formatDate(d.created)}</span> / Last used <span>{formatDate(d.last_used)}</span>
              </span>
            </div>
            <button onClick={() => run("DELETE", `/api/devices/${d.id}`)} aria-label={`Revoke ${d.label}`}>Revoke</button>
          </li>
        ))}
      </ul>
      {data && data.length > 0 && (
        <div className="actions">
          {confirmAll ? (
            <>
              <button className="danger" onClick={() => run("POST", "/api/devices/revoke-all")}>Confirm log out all</button>
              <button onClick={() => setConfirmAll(false)}>Cancel</button>
            </>
          ) : (
            <button onClick={() => setConfirmAll(true)}>Log out all devices</button>
          )}
        </div>
      )}
    </section>
  );
}
