import { useState } from "react";
import { api, errorText } from "../api";
import { formatDate } from "../format";
import { useApi } from "../hooks";
import type { NoteInfo } from "../types";

const SCALE = 2; // integer scaling keeps the 160x120 pixels crisp

export default function Note({ id, onDeleted }: { id: number; onDeleted: () => void }) {
  const { data: note, error: loadError } = useApi<NoteInfo>(`/api/notes/${id}`);
  const [confirming, setConfirming] = useState(false);
  const [error, setError] = useState("");

  async function remove() {
    try {
      await api("DELETE", `/api/notes/${id}`);
      onDeleted();
    } catch (e) {
      setError(errorText(e));
      setConfirming(false);
    }
  }

  const shownError = error || loadError;
  if (!note) {
    return shownError ? <p role="alert" className="error">{shownError}</p> : <p className="muted">Loading...</p>;
  }
  return (
    <article className="note">
      <figure className="paper">
        <img
          src={`/api/notes/${id}.png`}
          alt={`Sketch from ${note.sender}`}
          width={160 * SCALE}
          height={120 * SCALE}
          className="sketch"
        />
      </figure>
      <dl className="meta">
        <dt>From</dt><dd>{note.sender}</dd>
        <dt>Date</dt><dd>{formatDate(note.created)}</dd>
        <dt>To</dt><dd>{note.recipients.join(", ")}</dd>
      </dl>
      {shownError && <p role="alert" className="error">{shownError}</p>}
      <div className="actions">
        {confirming ? (
          <>
            <button className="danger" onClick={remove}>Confirm delete</button>
            <button onClick={() => setConfirming(false)}>Cancel</button>
          </>
        ) : (
          <button onClick={() => setConfirming(true)}>Delete</button>
        )}
      </div>
    </article>
  );
}
