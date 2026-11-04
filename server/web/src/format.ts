const fmt = new Intl.DateTimeFormat(undefined, { dateStyle: "medium", timeStyle: "short" });

/** Server timestamps are Unix seconds; show them in the viewer's local time. */
export function formatDate(seconds: number | null | undefined): string {
  return seconds ? fmt.format(new Date(seconds * 1000)) : "Never";
}
