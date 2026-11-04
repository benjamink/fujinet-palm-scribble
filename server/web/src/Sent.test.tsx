import { render, screen } from "@testing-library/react";
import { expect, it } from "vitest";
import Sent from "./pages/Sent";
import { stubFetch } from "./testutil";

it("lists recipients as returned", async () => {
  stubFetch({ "GET /api/sent?offset=0&limit=20": { body: [{ id: 3, created: 1767225600, recipients: ["ann", "cyd"] }] } });
  render(<Sent />);
  const link = await screen.findByRole("link");
  expect(link).toHaveAttribute("href", "#/note/3");
  expect(link).toHaveTextContent("ann, cyd");
});
