import { useCallback, useEffect, useState } from "react";
import { api, errorText } from "./api";

/** Load a GET endpoint; `reload` refetches. */
export function useApi<T>(path: string) {
  const [data, setData] = useState<T | null>(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);
  const reload = useCallback(async () => {
    try {
      setData(await api<T>("GET", path));
      setError("");
    } catch (e) {
      setError(errorText(e));
    } finally {
      setLoading(false);
    }
  }, [path]);
  useEffect(() => {
    setLoading(true);
    void reload();
  }, [reload]);
  return { data, error, loading, reload };
}

export const PAGE_SIZE = 20;

/** Offset-paged list; `loadMore` appends the next page, `more` is false after a short page. */
export function usePaged<T>(endpoint: string) {
  const [items, setItems] = useState<T[]>([]);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);
  const [more, setMore] = useState(false);
  const [offset, setOffset] = useState(0);

  const fetchPage = useCallback(async (off: number, replace: boolean) => {
    setLoading(true);
    try {
      const page = await api<T[]>("GET", `${endpoint}?offset=${off}&limit=${PAGE_SIZE}`);
      setItems((prev) => (replace ? page : [...prev, ...page]));
      setOffset(off + page.length);
      setMore(page.length >= PAGE_SIZE);
      setError("");
    } catch (e) {
      setError(errorText(e));
    } finally {
      setLoading(false);
    }
  }, [endpoint]);

  useEffect(() => {
    void fetchPage(0, true);
  }, [fetchPage]);

  const loadMore = useCallback(() => fetchPage(offset, false), [fetchPage, offset]);
  return { items, error, loading, more, loadMore };
}
