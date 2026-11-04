import { render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";
import { api, onUnauthorized } from "./api";
import App from "./App";
import { stubFetch } from "./testutil";

describe("401 handling", () => {
  it("fires on 401 from a normal endpoint", async () => {
    stubFetch({ "GET /api/inbox?offset=0&limit=20": { status: 401, body: { error: "not logged in" } } });
    const cb = vi.fn();
    const off = onUnauthorized(cb);
    await api("GET", "/api/inbox?offset=0&limit=20").catch(() => {});
    expect(cb).toHaveBeenCalledTimes(1);
    off();
  });

  it("does not fire for login/signup/me failures", async () => {
    stubFetch({
      "POST /api/login": { status: 401, body: { error: "bad credentials" } },
      "POST /api/signup": { status: 400, body: { error: "bad invite" } },
      "GET /api/me": { status: 401, body: { error: "not logged in" } },
    });
    const cb = vi.fn();
    const off = onUnauthorized(cb);
    await api("POST", "/api/login", {}).catch(() => {});
    await api("POST", "/api/signup", {}).catch(() => {});
    await api("GET", "/api/me").catch(() => {});
    expect(cb).not.toHaveBeenCalled();
    off();
  });

  it("App returns to the login page on a later 401", async () => {
    stubFetch({
      "GET /api/me": { body: { handle: "ann" } },
      "GET /api/inbox?offset=0&limit=20": { status: 401, body: { error: "not logged in" } },
    });
    render(<App />);
    expect(await screen.findByRole("button", { name: "Log in" })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Logout" })).toBeNull();
  });
});
