import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";
import Signup from "./pages/Signup";
import { stubFetch } from "./testutil";

it("posts invite, username, password and handle", async () => {
  const f = stubFetch({ "POST /api/signup": { body: { handle: "Ann" } } });
  const onLogin = vi.fn();
  render(<Signup onLogin={onLogin} />);
  await userEvent.type(screen.getByLabelText("Invite code"), "abc");
  await userEvent.type(screen.getByLabelText("Username"), "ann");
  await userEvent.type(screen.getByLabelText("Password"), "pw");
  await userEvent.type(screen.getByLabelText("Handle"), "Ann");
  await userEvent.click(screen.getByRole("button", { name: "Sign up" }));
  await vi.waitFor(() => expect(onLogin).toHaveBeenCalledWith("Ann"));
  const init = f.mock.calls[0][1] as RequestInit;
  expect(JSON.parse(init.body as string)).toEqual({ invite: "abc", username: "ann", password: "pw", handle: "Ann" });
});
