import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";
import Note from "./pages/Note";
import { stubFetch } from "./testutil";

const note = { id: 5, created: 1767225600, sender: "bob", recipients: ["ann"], role: "recipient" };

it("shows only the recipients the API returned, with a pixelated image", async () => {
  stubFetch({ "GET /api/notes/5": { body: note } });
  render(<Note id={5} onDeleted={() => {}} />);
  expect(await screen.findByText("bob")).toBeInTheDocument();
  expect(screen.getByText("ann")).toBeInTheDocument();
  expect(screen.queryByText("cyd")).toBeNull();
  const img = screen.getByRole("img", { name: /sketch from bob/i });
  expect(img).toHaveAttribute("src", "/api/notes/5.png");
  expect(img).toHaveAttribute("width", "320");
  expect(img).toHaveAttribute("height", "240");
});

it("deletes only after a confirm step", async () => {
  const f = stubFetch({ "GET /api/notes/5": { body: note }, "DELETE /api/notes/5": {} });
  const onDeleted = vi.fn();
  render(<Note id={5} onDeleted={onDeleted} />);
  await userEvent.click(await screen.findByRole("button", { name: "Delete" }));
  expect(f.mock.calls.some((c) => (c[1] as RequestInit)?.method === "DELETE")).toBe(false);
  await userEvent.click(screen.getByRole("button", { name: "Confirm delete" }));
  await vi.waitFor(() => expect(onDeleted).toHaveBeenCalled());
  const del = f.mock.calls.find((c) => (c[1] as RequestInit)?.method === "DELETE")!;
  expect(new Headers((del[1] as RequestInit).headers).get("X-Requested-With")).toBe("scribble");
});

it("shows an error when the note is missing", async () => {
  stubFetch({ "GET /api/notes/9": { status: 404, body: { error: "no such note" } } });
  render(<Note id={9} onDeleted={() => {}} />);
  expect(await screen.findByRole("alert")).toHaveTextContent("no such note");
});
