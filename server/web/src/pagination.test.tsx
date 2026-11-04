import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import type React from "react";
import { describe, expect, it } from "vitest";
import Inbox from "./pages/Inbox";
import Sent from "./pages/Sent";
import { stubFetch } from "./testutil";

const inboxItem = (id: number) => ({ id, created: 1767225600, sender: `s${id}`, read: true });
const sentItem = (id: number) => ({ id, created: 1767225600, recipients: [`r${id}`] });
const range = (a: number, b: number) => Array.from({ length: b - a }, (_, i) => a + i);

describe.each([
  ["Inbox", Inbox, "inbox", inboxItem, (i: number) => `s${i}`],
  ["Sent", Sent, "sent", sentItem, (i: number) => `r${i}`],
] as unknown as [string, React.ComponentType, string, (i: number) => object, (i: number) => string][])("%s pagination", (_n, Page, ep, mk, label) => {
  it("loads more pages and hides the button after a short page", async () => {
    const f = stubFetch({
      [`GET /api/${ep}?offset=0&limit=20`]: { body: range(0, 20).map(mk) },
      [`GET /api/${ep}?offset=20&limit=20`]: { body: range(20, 25).map(mk) },
    });
    render(<Page />);
    expect(await screen.findByText(label(0))).toBeInTheDocument();
    expect(screen.getAllByRole("link")).toHaveLength(20);
    await userEvent.click(screen.getByRole("button", { name: "Load more" }));
    expect(await screen.findByText(label(24))).toBeInTheDocument();
    expect(screen.getAllByRole("link")).toHaveLength(25);
    expect(screen.getByText(label(0))).toBeInTheDocument();
    expect(f.mock.calls.map((c) => c[0])).toContain(`/api/${ep}?offset=20&limit=20`);
    expect(screen.queryByRole("button", { name: "Load more" })).toBeNull();
  });

  it("shows no button when the first page is short", async () => {
    stubFetch({ [`GET /api/${ep}?offset=0&limit=20`]: { body: range(0, 3).map(mk) } });
    render(<Page />);
    await screen.findByText(label(0));
    expect(screen.queryByRole("button", { name: "Load more" })).toBeNull();
  });
});
