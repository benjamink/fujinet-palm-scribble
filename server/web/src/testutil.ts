import { vi } from "vitest";

export type Route = (init: RequestInit) => { status?: number; body?: unknown };

/** Stub global fetch with a route table keyed by "METHOD /path". Returns the mock. */
export function stubFetch(routes: Record<string, Route | { status?: number; body?: unknown }>) {
  const mock = vi.fn(async (url: string, init: RequestInit = {}) => {
    const key = `${init.method ?? "GET"} ${url}`;
    const r = routes[key];
    if (!r) return new Response(JSON.stringify({ error: `unrouted ${key}` }), { status: 599 });
    const out = typeof r === "function" ? r(init) : r;
    return new Response(JSON.stringify(out.body ?? {}), {
      status: out.status ?? 200,
      headers: { "Content-Type": "application/json" },
    });
  });
  vi.stubGlobal("fetch", mock);
  return mock;
}
