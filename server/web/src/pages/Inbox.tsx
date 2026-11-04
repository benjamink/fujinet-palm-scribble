import { usePaged } from "../hooks";
import { formatDate } from "../format";
import type { InboxItem } from "../types";

export default function Inbox() {
  const { items, error, loading, more, loadMore } = usePaged<InboxItem>("/api/inbox");
  return (
    <section>
      <h1>Inbox</h1>
      {error && <p role="alert" className="error">{error}</p>}
      {loading && items.length === 0 && <p className="muted">Loading...</p>}
      {!loading && !error && items.length === 0 && <p className="muted">No notes yet.</p>}
      <ul className="list">
        {items.map((n) => (
          <li key={n.id}>
            <a href={`#/note/${n.id}`} className={n.read ? "" : "unread"}>
              {!n.read && <span className="dot" title="Unread"><span className="sr">Unread</span></span>}
              <span className="who">{n.sender}</span>
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
