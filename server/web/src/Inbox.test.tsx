import { render, screen, within } from "@testing-library/react";
import { expect, it } from "vitest";
import Inbox from "./pages/Inbox";
import { stubFetch } from "./testutil";

it("renders sender, date and an unread marker", async () => {
  stubFetch({
    "GET /api/inbox?offset=0&limit=20": {
      body: [
        { id: 2, created: 1767225600, sender: "bob", read: false },
        { id: 1, created: 1767139200, sender: "cyd", read: true },
      ],
    },
  });
  render(<Inbox />);
  const links = await screen.findAllByRole("link");
  expect(links).toHaveLength(2);
  expect(links[0]).toHaveAttribute("href", "#/note/2");
  expect(within(links[0]).getByText("bob")).toBeInTheDocument();
  expect(within(links[0]).getByText(/2025|2026/)).toBeInTheDocument();
  expect(within(links[0]).getByText("Unread")).toBeInTheDocument();
  expect(within(links[1]).getByText("cyd")).toBeInTheDocument();
  expect(within(links[1]).queryByText("Unread")).toBeNull();
});

it("shows an empty state", async () => {
  stubFetch({ "GET /api/inbox?offset=0&limit=20": { body: [] } });
  render(<Inbox />);
  expect(await screen.findByText(/no notes/i)).toBeInTheDocument();
});
