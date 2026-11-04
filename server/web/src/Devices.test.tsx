import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it } from "vitest";
import Devices from "./pages/Devices";
import { stubFetch } from "./testutil";

const devices = [
  { id: 7, label: "Tungsten", created: 1767225600, last_used: 1767229200 },
  { id: 8, label: "Zire", created: 1767225600, last_used: null },
];

it("lists devices and revokes one with the CSRF header", async () => {
  let list = devices;
  const f = stubFetch({
    "GET /api/devices": () => ({ body: list }),
    "DELETE /api/devices/7": () => {
      list = devices.slice(1);
      return {};
    },
  });
  render(<Devices />);
  expect(await screen.findByText("Tungsten")).toBeInTheDocument();
  expect(screen.getByText("Zire")).toBeInTheDocument();
  expect(screen.getByText("Never")).toBeInTheDocument();
  await userEvent.click(screen.getByRole("button", { name: "Revoke Tungsten" }));
  await screen.findByText("Zire");
  expect(screen.queryByText("Tungsten")).toBeNull();
  const del = f.mock.calls.find((c) => (c[1] as RequestInit)?.method === "DELETE")!;
  expect(new Headers((del[1] as RequestInit).headers).get("X-Requested-With")).toBe("scribble");
});

it("logs out all devices after confirm", async () => {
  let list = devices;
  const f = stubFetch({
    "GET /api/devices": () => ({ body: list }),
    "POST /api/devices/revoke-all": () => {
      list = [];
      return {};
    },
  });
  render(<Devices />);
  await userEvent.click(await screen.findByRole("button", { name: "Log out all devices" }));
  await userEvent.click(screen.getByRole("button", { name: "Confirm log out all" }));
  await screen.findByText(/no devices/i);
  const post = f.mock.calls.find((c) => (c[1] as RequestInit)?.method === "POST")!;
  expect(new Headers((post[1] as RequestInit).headers).get("X-Requested-With")).toBe("scribble");
});
