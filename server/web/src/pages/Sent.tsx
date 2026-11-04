import { usePaged } from "../hooks";
import { formatDate } from "../format";
import type { SentItem } from "../types";

export default function Sent() {
  const { items, error, loading, more, loadMore } = usePaged<SentItem>("/api/sent");
  return (
    <section>
      <h1>Sent</h1>
      {error && <p role="alert" className="error">{error}</p>}
      {loading && items.length === 0 && <p className="muted">Loading...</p>}
      {!loading && !error && items.length === 0 && <p className="muted">Nothing sent yet.</p>}
      <ul className="list">
        {items.map((n) => (
          <li key={n.id}>
            <a href={`#/note/${n.id}`}>
              <span className="who">{n.recipients.join(", ")}</span>
              <time dateTime={new Date(n.created * 1000).toISOString()}>{formatDate(n.created)}</time>
            </a>
          </li>
        ))}
      </ul>
      {more && (
        <div className="actions">
          <button onClick={loadMore} disabled={loading}>Load more</button>
        </div>
      )}
    </section>
  );
}
