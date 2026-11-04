import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import Login from "./pages/Login";
import { stubFetch } from "./testutil";

describe("Login", () => {
  it("shows the server's error text in an alert", async () => {
    stubFetch({ "POST /api/login": { status: 401, body: { error: "bad credentials" } } });
    const onLogin = vi.fn();
    render(<Login onLogin={onLogin} />);
    await userEvent.type(screen.getByLabelText("Username"), "ann");
    await userEvent.type(screen.getByLabelText("Password"), "pw");
    await userEvent.click(screen.getByRole("button", { name: "Log in" }));
    expect(await screen.findByRole("alert")).toHaveTextContent("bad credentials");
    expect(onLogin).not.toHaveBeenCalled();
  });

  it("reports the handle on success", async () => {
    stubFetch({ "POST /api/login": { body: { handle: "ann" } } });
    const onLogin = vi.fn();
    render(<Login onLogin={onLogin} />);
    await userEvent.type(screen.getByLabelText("Username"), "ann");
    await userEvent.type(screen.getByLabelText("Password"), "pw");
    await userEvent.click(screen.getByRole("button", { name: "Log in" }));
    await vi.waitFor(() => expect(onLogin).toHaveBeenCalledWith("ann"));
  });
});
