export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.name = "ApiError";
    this.status = status;
  }
}

type Listener = () => void;
const listeners = new Set<Listener>();

/** Subscribe to "session expired" (401 from any endpoint except login/signup/me). Returns unsubscribe. */
export function onUnauthorized(cb: Listener): () => void {
  listeners.add(cb);
  return () => {
    listeners.delete(cb);
  };
}

const QUIET_401 = new Set(["/api/login", "/api/signup", "/api/me"]);

/** The one place that talks to the server: same-origin cookies, CSRF header on mutations. */
export async function api<T = unknown>(method: "GET" | "POST" | "DELETE", path: string, body?: unknown): Promise<T> {
  const headers = new Headers();
  const init: RequestInit = { method, credentials: "same-origin", headers };
  if (method !== "GET") headers.set("X-Requested-With", "scribble");
  if (body !== undefined) {
    headers.set("Content-Type", "application/json");
    init.body = JSON.stringify(body);
  }
  const res = await fetch(path, init);
  let data: unknown = null;
  try {
    data = await res.json();
  } catch {
    /* non-JSON body */
  }
  if (!res.ok) {
    if (res.status === 401 && !QUIET_401.has(path.split("?")[0])) listeners.forEach((cb) => cb());
    const msg = (data as { error?: unknown } | null)?.error;
    throw new ApiError(res.status, typeof msg === "string" ? msg : `Request failed (${res.status})`);
  }
  return data as T;
}

export const errorText = (e: unknown): string => (e instanceof Error ? e.message : "Something went wrong");
