export interface InboxItem { id: number; created: number; sender: string; read: boolean }
export interface SentItem { id: number; created: number; recipients: string[] }
export interface NoteInfo { id: number; created: number; sender: string; recipients: string[]; role: "sender" | "recipient" }
export interface Device { id: number; label: string; created: number; last_used: number | null }
