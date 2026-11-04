import { describe, expect, it } from "vitest";
import { api, ApiError } from "./api";
import { stubFetch } from "./testutil";

describe("api wrapper", () => {
  it("sends CSRF header, JSON body and same-origin credentials on POST", async () => {
    const f = stubFetch({ "POST /api/login": { body: { handle: "ann" } } });
    const out = await api<{ handle: string }>("POST", "/api/login", { username: "a", password: "b" });
    expect(out.handle).toBe("ann");
    const init = f.mock.calls[0][1] as RequestInit;
    const h = new Headers(init.headers);
    expect(h.get("X-Requested-With")).toBe("scribble");
    expect(h.get("Content-Type")).toBe("application/json");
    expect(init.credentials).toBe("same-origin");
    expect(init.body).toBe(JSON.stringify({ username: "a", password: "b" }));
  });

  it("sends CSRF header on DELETE and on POST without body", async () => {
    const f = stubFetch({ "DELETE /api/notes/1": {}, "POST /api/logout": {} });
    await api("DELETE", "/api/notes/1");
    await api("POST", "/api/logout");
    for (const call of f.mock.calls) {
      const init = call[1] as RequestInit;
      expect(new Headers(init.headers).get("X-Requested-With")).toBe("scribble");
      expect(init.credentials).toBe("same-origin");
    }
  });

  it("GET uses same-origin credentials and needs no CSRF header", async () => {
    const f = stubFetch({ "GET /api/me": { body: { handle: "ann" } } });
    await api("GET", "/api/me");
    const init = f.mock.calls[0][1] as RequestInit;
    expect(init.credentials).toBe("same-origin");
    expect(new Headers(init.headers).get("X-Requested-With")).toBeNull();
    expect(init.body).toBeUndefined();
  });

  it("throws ApiError with the server's error text and status", async () => {
    stubFetch({ "POST /api/login": { status: 401, body: { error: "bad credentials" } } });
    const err = await api("POST", "/api/login", {}).catch((e: unknown) => e) as ApiError;
    expect(err).toBeInstanceOf(ApiError);
    expect(err.message).toBe("bad credentials");
    expect(err.status).toBe(401);
  });

  it("falls back to a generic message when the body is not JSON", async () => {
    const { vi } = await import("vitest");
    vi.stubGlobal("fetch", vi.fn(async () => new Response("<html>", { status: 502 })));
    const err = await api("GET", "/api/me").catch((e: unknown) => e) as ApiError;
    expect(err).toBeInstanceOf(ApiError);
    expect(err.status).toBe(502);
    expect(err.message).toBeTruthy();
  });
});
