/* palm/apps/scribble/scribble.c
 *
 * Scribble -- send, receive and view 160x120 1-bit sketch notes through a
 * Scribble server over FujiNet (docs/scribble.md). Setup (server, username,
 * password -> bearer token) -> Inbox / Sent list, 8 rows per page ->
 * Viewer (the sketch drawn run by run, Delete, Reply, Back). Inbox New and
 * Viewer Reply open Compose (draw with the stylus, Clear, Undo, To...,
 * Send); To... opens the Picker (directory search, toggle up to 10
 * recipients). Targets Palm OS 3.x, 160x160 mono/4-gray, no floating point.
 *
 * Network calls (net.c) only happen on a user action: launch / login
 * (first inbox load), Refresh, Prev/Next, the Inbox/Sent toggle, opening a
 * row, Delete, Send, and in the Picker its first open, Search and Prev/Next.
 * The password is never stored: the field text and every copy are zeroed
 * right after the login attempt and when Setup closes.
 *
 * The sketch being composed (gSketch), the recipients (gSel) and the
 * Picker's page are globals, so they survive moving between Compose and
 * the Picker. If the session expires while composing, the draft is kept
 * in memory (gDraftKept) and the same user's next login returns to Compose.
 *
 * Header order matters: see fnapp.h / fujiconfig.c.
 */
#include "fujinet-network.h"
#include "fujinet-palmos.h"
#include <PalmOS.h>
#include "fnapp.h"
#include "sketch.h"
#include "proto.h"
#include "netutil.h"
#include "net.h"
#include "selection.h"
#include "canvas.h"
#include "scribble_rsc.h"

#define CREATOR_ID 'SkNt'
#define PREFS_ID 0
#define PREFS_VERSION 1

#define ROWS 8           /* list rows per page */
#define SKETCH_Y 16      /* screen y of sketch row 0 */
#define INBOX_STATUS_Y 130
#define SETUP_STATUS_Y 120
#define PICKER_STATUS_Y 122
#define VIEWER_DATE_X_INBOX 104 /* right of the Reply button */
#define VIEWER_DATE_X_SENT 72   /* right of the Back button */

/* ------------------------------------------------------------------ */
/* Preferences                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    char server[80];
    char token[48];
    char handle[20];
} Prefs;

static Prefs gPrefs;

static void LoadPrefs(void)
{
    UInt16 size = sizeof(Prefs);
    Int16 ver = PrefGetAppPreferences(CREATOR_ID, PREFS_ID, &gPrefs, &size, false);

    if (ver != PREFS_VERSION || size != sizeof(Prefs)) {
        MemSet(&gPrefs, sizeof(gPrefs), 0);
    }
    gPrefs.server[sizeof(gPrefs.server) - 1] = '\0';
    gPrefs.token[sizeof(gPrefs.token) - 1] = '\0';
    gPrefs.handle[sizeof(gPrefs.handle) - 1] = '\0';
}

static void SavePrefs(void)
{
    /* saved=false: stored in unsaved prefs, so the token is not backed up by HotSync */
    PrefSetAppPreferences(CREATOR_ID, PREFS_ID, PREFS_VERSION, &gPrefs, sizeof(Prefs), false);
}

static Boolean LoggedIn(void)
{
    return gPrefs.token[0] != '\0';
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static InboxRow gInbox[ROWS];
static SentRow gSent[ROWS];
static Int16 gCount = 0;        /* rows on the current page */
static Int16 gPage = 0;         /* 0-based page */
static Int16 gMode = 0;         /* 0 = inbox, 1 = sent (what the rows hold) */
static Boolean gNeedLoad = true; /* load the list when the Inbox opens */
static Boolean gBackOff = false; /* that load may step back an empty page */
static Int16 gCur = 0;          /* row shown in the Viewer */
static sk_u8 gBitmap[SK_BYTES]; /* the note shown in the Viewer */
static char gTitle[40];
static char gLine[192];         /* scratch for list rows / headers */
static char gLine2[192];
static int gRunStart[SK_W / 2 + 1];
static int gRunLen[SK_W / 2 + 1];

/* Compose / Picker. All of it global: Sketch alone is ~4.8 KB and the
 * Palm stack is small. */
static Sketch gSketch;              /* the note being drawn */
static Selection gSel;              /* chosen recipients */
static char gCsv[SEL_CSV_CAP];      /* "h1,h2,..." for net_send */
static CvPen gPen;                  /* stroke in progress */
static Boolean gDraftKept = false;  /* session expired while composing */
static char gDirRows[ROWS][SEL_HCAP]; /* Picker page shown */
static char gDirTmp[ROWS][SEL_HCAP];  /* Picker page being parsed */
static Int16 gDirCount = 0;         /* rows in gDirRows */
static Int16 gDirPage = 0;
static Boolean gDirFull = false;    /* the server sent a full page */
static Boolean gDirLoaded = false;  /* gDirRows holds a loaded page */
static char gDirPrefix[SEL_HCAP];   /* prefix of the loaded page */

/* ------------------------------------------------------------------ */
/* Small helpers (as in ../rss/rss.c)                                  */
/* ------------------------------------------------------------------ */

static void StartCompose(const char *to);

static void *GetObj(FormType *frm, UInt16 id)
{
    return FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

static void ShowError(const char *what, const char *reason)
{
    FrmCustomAlert(GeneralAlert, what, (reason && reason[0]) ? reason : "unknown error", "");
}

static void ShowInfo(const char *msg)
{
    FrmCustomAlert(InfoAlert, msg, "", "");
}

static void DrawTextLine(const char *text, Coord x, Coord y, Coord width)
{
    RectangleType r;

    r.topLeft.x = x;
    r.topLeft.y = y;
    r.extent.x = width;
    r.extent.y = FntLineHeight();
    WinEraseRectangle(&r, 0);
    WinDrawChars(text, StrLen(text), x, y);
}

/* Truncates src to fit within maxWidth pixels (current font), copying
 * into out (outCap bytes), appending "..." if it was cut. */
static void TruncateToWidth(char *out, UInt16 outCap, const char *src, Int16 maxWidth)
{
    Int16 width;
    Int16 len;
    Boolean fits;

    StrNCopy(out, src, (Int16)(outCap - 4));
    out[outCap - 4] = '\0';
    len = StrLen(out);
    width = maxWidth;
    FntCharsInWidth(out, &width, &len, &fits);

    if (!fits) {
        Int16 ellipsisWidth = FntCharsWidth("...", 3);
        Int16 width2 = (Int16)(maxWidth - ellipsisWidth);
        Int16 len2 = StrLen(out);

        if (width2 < 0) {
            width2 = 0;
        }
        FntCharsInWidth(out, &width2, &len2, &fits);
        out[len2] = '\0';
        StrCat(out, "...");
    } else {
        out[len] = '\0';
    }
}

/* Copies src to dst (cap bytes), trimming surrounding whitespace; with
 * noSpaces, also dropping every embedded whitespace/control character (a
 * Graffiti newline stroke can sneak one into the server field). */
static void TrimCopy(char *dst, UInt16 cap, const char *src, Boolean noSpaces)
{
    UInt16 n = 0;
    const char *p = src ? src : "";

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    for (; *p && n < cap - 1; p++) {
        if (noSpaces && (UInt8)*p <= ' ') {
            continue;
        }
        dst[n++] = *p;
    }
    while (n > 0 && ((UInt8)dst[n - 1] <= ' ')) {
        n--;
    }
    dst[n] = '\0';
}

/* Forgets the Picker's page (it was filtered for the old handle). */
static void ResetDir(void)
{
    gDirCount = 0;
    gDirPage = 0;
    gDirFull = false;
    gDirLoaded = false;
    gDirPrefix[0] = '\0';
}

/* Empties the draft: sketch (and its undo), recipients. */
static void ClearDraft(void)
{
    sk_clear(&gSketch);
    sel_clear(&gSel);
    gDraftKept = false;
}

/* Forgets the session. full also forgets the handle and the draft
 * (explicit log out); an expired session keeps the draft if gDraftKept. */
static void DropSession(Boolean full)
{
    MemSet(gPrefs.token, sizeof(gPrefs.token), 0);
    if (full) {
        MemSet(gPrefs.handle, sizeof(gPrefs.handle), 0);
        ClearDraft();
    }
    SavePrefs();
    gCount = 0;
    gPage = 0;
    gMode = 0;
    gNeedLoad = true;
    ResetDir();
}

/* Reports a net_* failure. An expired session drops the token and goes
 * back to Setup; anything else shows the reason. */
static void NetFailed(Err e, const char *what)
{
    if (e == NET_ERR_AUTH) {
        DropSession(false);
        ShowInfo(gDraftKept ? "Session expired - log in again. Your sketch is kept."
                            : "Session expired - log in again");
        FrmGotoForm(SetupForm);
        return;
    }
    ShowError(what, net_last_reason());
}

/* As NetFailed, from Compose or the Picker: an expired session keeps the
 * draft for the same user's next login. */
static void DraftNetFailed(Err e, const char *what)
{
    if (e == NET_ERR_AUTH) {
        gDraftKept = true;
    }
    NetFailed(e, what);
}

/* Draws an SK_BYTES bitmap at y = SKETCH_Y..SKETCH_Y+119, one WinDrawLine
 * per black run. Used by the Viewer and by Compose (Undo, redraws). */
static void DrawSketchBits(const sk_u8 *bits)
{
    RectangleType r;
    Int16 y;

    r.topLeft.x = 0;
    r.topLeft.y = SKETCH_Y;
    r.extent.x = SK_W;
    r.extent.y = SK_H;
    WinEraseRectangle(&r, 0);
    for (y = 0; y < SK_H; y++) {
        int n = sk_runs(bits, y, gRunStart, gRunLen, (int)(sizeof(gRunStart) / sizeof(gRunStart[0])));
        int i;

        for (i = 0; i < n; i++) {
            WinDrawLine((Coord)gRunStart[i], (Coord)(SKETCH_Y + y),
                        (Coord)(gRunStart[i] + gRunLen[i] - 1), (Coord)(SKETCH_Y + y));
        }
    }
}

/* ------------------------------------------------------------------ */
/* SetupForm                                                           */
/* ------------------------------------------------------------------ */

/* Zeroes and frees the password field's text. */
static void WipePassword(FormType *frm, Boolean redraw)
{
    FieldType *fld = (FieldType *)GetObj(frm, SetupPassField);
    MemHandle h = FldGetTextHandle(fld);

    if (h) {
        FldSetTextHandle(fld, NULL);
        MemSet(MemHandleLock(h), MemHandleSize(h), 0);
        MemHandleUnlock(h);
        MemHandleFree(h);
    }
    if (redraw) {
        FldEraseField(fld);
        FldDrawField(fld);
    }
}

static void DrawSetupStatus(void)
{
    if (LoggedIn()) {
        StrCopy(gLine, "Logged in as ");
        StrNCat(gLine, gPrefs.handle, (Int16)sizeof(gLine));
    } else {
        StrCopy(gLine, "Not logged in.");
    }
    DrawTextLine(gLine, 4, SETUP_STATUS_Y, 152);
}

static void ShowSessionButtons(FormType *frm)
{
    if (LoggedIn()) {
        FrmShowObject(frm, FrmGetObjectIndex(frm, SetupLogoutButton));
        FrmShowObject(frm, FrmGetObjectIndex(frm, SetupInboxButton));
    } else {
        FrmHideObject(frm, FrmGetObjectIndex(frm, SetupLogoutButton));
        FrmHideObject(frm, FrmGetObjectIndex(frm, SetupInboxButton));
    }
}

static void DoLogin(FormType *frm)
{
    FieldType *serverFld = (FieldType *)GetObj(frm, SetupServerField);
    FieldType *userFld = (FieldType *)GetObj(frm, SetupUserField);
    FieldType *passFld = (FieldType *)GetObj(frm, SetupPassField);
    char server[sizeof(gPrefs.server)];
    char user[33];
    char pass[64];
    char token[sizeof(gPrefs.token)];
    char handle[sizeof(gPrefs.handle)];
    const char *pp;
    Boolean sameUser;
    Err e;

    TrimCopy(server, sizeof(server), FldGetTextPtr(serverFld), true);
    if (server[0] && !StrStr(server, "://") && StrLen(server) + 8 < sizeof(server)) {
        char tmp[sizeof(server)];

        StrCopy(tmp, "https://");
        StrCat(tmp, server);
        StrCopy(server, tmp);
    }
    if (!nu_server_ok(server)) {
        ShowInfo("Enter the server address, like https://host");
        return;
    }
    StrCopy(gPrefs.server, server);
    SavePrefs();
    if (nu_server_cleartext(server)) {
        /* Non-blocking: the login goes ahead after OK. */
        ShowInfo("Warning: http:// sends your password unencrypted. Use https:// unless this is a private network.");
    }

    TrimCopy(user, sizeof(user), FldGetTextPtr(userFld), false);
    pp = FldGetTextPtr(passFld);
    StrNCopy(pass, pp ? pp : "", (Int16)(sizeof(pass) - 1));
    pass[sizeof(pass) - 1] = '\0';
    if (user[0] == '\0' || pass[0] == '\0') {
        MemSet(pass, sizeof(pass), 0);
        ShowInfo("Enter your username and password.");
        return;
    }

    DrawTextLine("Logging in...", 4, SETUP_STATUS_Y, 152);
    e = net_login(server, user, pass, token, sizeof(token), handle, sizeof(handle));
    MemSet(pass, sizeof(pass), 0);
    WipePassword(frm, true);

    if (e != errNone) {
        DrawSetupStatus();
        ShowError("Login failed", net_last_reason());
        return;
    }
    sameUser = (Boolean)(gPrefs.handle[0] && StrCompare(handle, gPrefs.handle) == 0);
    StrCopy(gPrefs.token, token);
    StrCopy(gPrefs.handle, handle);
    MemSet(token, sizeof(token), 0);
    SavePrefs();
    gCount = 0;
    gPage = 0;
    gMode = 0;
    gNeedLoad = true;
    if (gDraftKept && sameUser) {
        /* The session expired while composing: back to that draft. */
        gDraftKept = false;
        FrmGotoForm(ComposeForm);
        return;
    }
    if (!sameUser) {
        ClearDraft();
        ResetDir();
    }
    gDraftKept = false;
    FrmGotoForm(InboxForm);
}

static Boolean SetupFormHandleEvent(EventType *e)
{
    switch (e->eType) {
    case frmOpenEvent: {
        FormType *frm = FrmGetActiveForm();
        FieldType *serverFld = (FieldType *)GetObj(frm, SetupServerField);
        const char *s = gPrefs.server[0] ? gPrefs.server : "https://";

        ShowSessionButtons(frm);
        FrmDrawForm(frm);
        FldInsert(serverFld, s, StrLen(s));
        DrawSetupStatus();
        FrmSetFocus(frm, FrmGetObjectIndex(frm, gPrefs.server[0] ? SetupUserField : SetupServerField));
        return true;
    }

    case frmUpdateEvent: {
        FormType *frm = FrmGetActiveForm();

        FrmDrawForm(frm);
        DrawSetupStatus();
        return true;
    }

    case frmCloseEvent:
        WipePassword(FrmGetFormPtr(SetupForm), false);
        return false;

    case ctlSelectEvent: {
        FormType *frm = FrmGetActiveForm();

        switch (e->data.ctlSelect.controlID) {
        case SetupLoginButton:
            DoLogin(frm);
            return true;
        case SetupLogoutButton:
            DropSession(true);
            ShowSessionButtons(frm);
            DrawSetupStatus();
            return true;
        case SetupInboxButton:
            if (LoggedIn()) {
                FrmGotoForm(InboxForm);
            }
            return true;
        default:
            return false;
        }
    }

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* InboxForm                                                           */
/* ------------------------------------------------------------------ */

static void DrawInboxStatus(const char *text)
{
    DrawTextLine(text, 4, INBOX_STATUS_Y, 152);
}

/* Loads the given page of the inbox (mode 0) or sent list (mode 1). On
 * success the rows, gPage and gMode are replaced; on failure they are
 * kept. A page past the end keeps the old rows and says so, unless
 * backOff is set (after a delete), when it steps back a page instead. */
static Boolean LoadPage(Int16 page, Int16 mode, Boolean backOff)
{
    for (;;) {
        MemHandle h;
        char *p;
        char *q;
        Err e;

        DrawInboxStatus("Loading...");
        e = net_list(gPrefs.server, gPrefs.token, mode, page * ROWS, ROWS, &h);
        DrawInboxStatus("");
        if (e != errNone) {
            NetFailed(e, "Could not load notes");
            return false;
        }
        p = (char *)MemHandleLock(h);
        for (q = p; *q == '\n' || *q == '\r'; q++) {
        }
        if (*q == '\0' && page > 0) {
            MemHandleUnlock(h);
            MemHandleFree(h);
            if (backOff) {
                page--;
                continue;
            }
            ShowInfo("No more notes.");
            return false;
        }
        if (mode) {
            gCount = (Int16)proto_parse_sent(p, gSent, ROWS);
        } else {
            gCount = (Int16)proto_parse_inbox(p, gInbox, ROWS);
        }
        MemHandleUnlock(h);
        MemHandleFree(h);
        gPage = page;
        gMode = mode;
        return true;
    }
}

static void InboxDrawFunc(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
    char date[17];
    Coord x = bounds->topLeft.x;
    Coord y = bounds->topLeft.y;
    Int16 w = bounds->extent.x;
    Int16 dw;
    unsigned long created;

    if (itemNum < 0 || itemNum >= gCount) {
        return;
    }
    created = gMode ? gSent[itemNum].created : gInbox[itemNum].created;
    proto_format_time(created, date);
    dw = FntCharsWidth(date, StrLen(date));
    WinDrawChars(date, StrLen(date), (Coord)(x + w - dw), y);

    if (gMode) {
        StrCopy(gLine2, "to ");
        StrNCat(gLine2, gSent[itemNum].to, (Int16)sizeof(gLine2));
        TruncateToWidth(gLine, sizeof(gLine), gLine2, (Int16)(w - dw - 4));
        WinDrawChars(gLine, StrLen(gLine), x, y);
    } else {
        if (gInbox[itemNum].unread) {
            WinDrawChars("*", 1, x, y);
        }
        TruncateToWidth(gLine, sizeof(gLine), gInbox[itemNum].sender, (Int16)(w - dw - 11));
        WinDrawChars(gLine, StrLen(gLine), (Coord)(x + 7), y);
    }
}

static void RelistInbox(FormType *frm)
{
    UInt16 idx = FrmGetObjectIndex(frm, InboxList);
    ListType *lst = (ListType *)FrmGetObjectPtr(frm, idx);
    RectangleType r;
    Int16 w;

    StrCopy(gTitle, "Scribble - ");
    StrNCat(gTitle, gPrefs.handle, (Int16)sizeof(gTitle));
    FrmSetTitle(frm, gTitle);
    CtlSetLabel((ControlType *)GetObj(frm, InboxModeButton), gMode ? "Inbox" : "Sent");

    StrPrintF(gLine, "%s, page %d", gMode ? "Sent" : "Inbox", gPage + 1);
    DrawTextLine(gLine, 4, 16, 152);
    w = FntCharsWidth("times UTC", 9);
    WinDrawChars("times UTC", 9, (Coord)(156 - w), 16);

    FrmGetObjectBounds(frm, idx, &r);
    WinEraseRectangle(&r, 0);
    LstSetDrawFunction(lst, InboxDrawFunc);
    LstSetListChoices(lst, NULL, gCount);
    LstSetSelection(lst, noListSelection);
    LstDrawList(lst);

    DrawInboxStatus((gCount == 0 && !gNeedLoad) ? "No notes" : "");
}

static void InboxLoadAndRelist(Int16 page, Int16 mode, Boolean backOff)
{
    LoadPage(page, mode, backOff);
    if (FrmGetActiveFormID() == InboxForm) {
        RelistInbox(FrmGetActiveForm());
    }
}

/* Fetches row sel's sketch and opens it in the Viewer. */
static void OpenRow(FormType *frm, Int16 sel)
{
    unsigned long id;
    Err e;

    if (sel < 0 || sel >= gCount) {
        return;
    }
    id = gMode ? gSent[sel].id : gInbox[sel].id;
    DrawInboxStatus("Loading...");
    e = net_fetch_note(gPrefs.server, gPrefs.token, id, gBitmap);
    DrawInboxStatus("");
    LstSetSelection((ListType *)GetObj(frm, InboxList), noListSelection);
    if (e != errNone) {
        NetFailed(e, "Could not open note");
        if (FrmGetActiveFormID() == InboxForm) {
            RelistInbox(frm);
        }
        return;
    }
    if (!gMode) {
        gInbox[sel].unread = 0; /* the fetch marked it read */
    }
    gCur = sel;
    FrmGotoForm(ViewerForm);
}

static Boolean InboxFormHandleEvent(EventType *e)
{
    switch (e->eType) {
    case frmOpenEvent: {
        FormType *frm = FrmGetActiveForm();

        StrCopy(gTitle, "Scribble - ");
        StrNCat(gTitle, gPrefs.handle, (Int16)sizeof(gTitle));
        FrmSetTitle(frm, gTitle);
        FrmDrawForm(frm);
        RelistInbox(frm);
        if (gNeedLoad) {
            Boolean backOff = gBackOff;

            gNeedLoad = false;
            gBackOff = false;
            InboxLoadAndRelist(gPage, gMode, backOff);
        }
        return true;
    }

    case frmUpdateEvent: {
        FormType *frm = FrmGetActiveForm();

        FrmDrawForm(frm);
        RelistInbox(frm);
        return true;
    }

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case InboxPrevButton:
            if (gPage > 0) {
                InboxLoadAndRelist((Int16)(gPage - 1), gMode, false);
            }
            return true;
        case InboxNextButton:
            if (gCount == ROWS) {
                InboxLoadAndRelist((Int16)(gPage + 1), gMode, false);
            } else {
                ShowInfo("No more notes.");
            }
            return true;
        case InboxRefreshButton:
            InboxLoadAndRelist(gPage, gMode, false);
            return true;
        case InboxModeButton:
            InboxLoadAndRelist(0, (Int16)(gMode ? 0 : 1), false);
            return true;
        case InboxSetupButton:
            FrmGotoForm(SetupForm);
            return true;
        case InboxNewButton:
            StartCompose(NULL);
            return true;
        default:
            return false;
        }

    case lstSelectEvent:
        if (e->data.lstSelect.listID == InboxList) {
            OpenRow(FrmGetActiveForm(), e->data.lstSelect.selection);
            return true;
        }
        return false;

    case keyDownEvent:
        if (e->data.keyDown.chr == vchrPageUp) {
            if (gPage > 0) {
                InboxLoadAndRelist((Int16)(gPage - 1), gMode, false);
            }
            return true;
        }
        if (e->data.keyDown.chr == vchrPageDown) {
            if (gCount == ROWS) {
                InboxLoadAndRelist((Int16)(gPage + 1), gMode, false);
            }
            return true;
        }
        return false;

    case menuEvent:
        switch (e->data.menu.itemID) {
        case MenuSetup:
            FrmGotoForm(SetupForm);
            return true;
        case MenuLogout:
            DropSession(true);
            FrmGotoForm(SetupForm);
            return true;
        default:
            return false;
        }

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* ViewerForm                                                          */
/* ------------------------------------------------------------------ */

static void DrawViewer(void)
{
    RectangleType r;
    FontID oldFont;
    char date[17];
    const char *dp;
    Coord left;
    Int16 w;

    if (gCur < 0 || gCur >= gCount) {
        return;
    }

    /* Header: who it is from / to. */
    r.topLeft.x = 0;
    r.topLeft.y = 0;
    r.extent.x = 160;
    r.extent.y = SKETCH_Y;
    WinEraseRectangle(&r, 0);
    oldFont = FntSetFont(boldFont);
    if (gMode) {
        StrCopy(gLine2, "To ");
        StrNCat(gLine2, gSent[gCur].to, (Int16)sizeof(gLine2));
    } else {
        StrCopy(gLine2, "From ");
        StrNCat(gLine2, gInbox[gCur].sender, (Int16)sizeof(gLine2));
    }
    TruncateToWidth(gLine, sizeof(gLine), gLine2, 156);
    WinDrawChars(gLine, StrLen(gLine), 2, 2);
    FntSetFont(oldFont);
    WinDrawLine(0, SKETCH_Y - 1, 159, SKETCH_Y - 1);

    DrawSketchBits(gBitmap);
    WinDrawLine(0, SKETCH_Y + SK_H, 159, SKETCH_Y + SK_H);

    /* Date, right aligned beside the buttons (Reply is hidden in Sent
     * mode). Without room for "YYYY-MM-DD HH:MM" the year is left off. */
    left = (Coord)(gMode ? VIEWER_DATE_X_SENT : VIEWER_DATE_X_INBOX);
    proto_format_time(gMode ? gSent[gCur].created : gInbox[gCur].created, date);
    dp = date;
    w = FntCharsWidth(dp, StrLen(dp));
    if (w > 158 - left) {
        dp = date + 5;
        w = FntCharsWidth(dp, StrLen(dp));
    }
    DrawTextLine("", left, 147, (Coord)(160 - left));
    WinDrawChars(dp, StrLen(dp), (Coord)(158 - w), 147);
}

static void DeleteCurrent(void)
{
    unsigned long id;
    Err e;

    if (gCur < 0 || gCur >= gCount) {
        return;
    }
    if (FrmCustomAlert(ConfirmAlert, "", "", "") != 0) {
        return;
    }
    id = gMode ? gSent[gCur].id : gInbox[gCur].id;
    e = net_delete(gPrefs.server, gPrefs.token, id);
    if (e != errNone) {
        NetFailed(e, "Could not delete note");
        return;
    }
    gNeedLoad = true;
    gBackOff = true;
    FrmGotoForm(InboxForm);
}

/* Reply to the received note shown: Compose with its sender as the only
 * recipient and a blank sketch. */
static void ReplyCurrent(void)
{
    const char *who;

    if (gMode || gCur < 0 || gCur >= gCount) {
        return;
    }
    who = gInbox[gCur].sender;
    if (!sel_handle_ok(who) || StrCompare(who, gPrefs.handle) == 0) {
        ShowInfo("Cannot reply to this sender.");
        DrawViewer();
        return;
    }
    StartCompose(who);
}

static Boolean ViewerFormHandleEvent(EventType *e)
{
    switch (e->eType) {
    case frmOpenEvent: {
        FormType *frm = FrmGetActiveForm();

        if (gMode) {
            FrmHideObject(frm, FrmGetObjectIndex(frm, ViewerReplyButton));
        }
        FrmDrawForm(frm);
        DrawViewer();
        return true;
    }

    case frmUpdateEvent:
        FrmDrawForm(FrmGetActiveForm());
        DrawViewer();
        return true;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case ViewerDeleteButton:
            DeleteCurrent();
            return true;
        case ViewerReplyButton:
            ReplyCurrent();
            return true;
        case ViewerBackButton:
            FrmGotoForm(InboxForm);
            return true;
        default:
            return false;
        }

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* ComposeForm                                                         */
/* ------------------------------------------------------------------ */

/* Opens Compose on a blank sketch, with to (a handle, may be NULL) as the
 * only recipient. */
static void StartCompose(const char *to)
{
    ClearDraft();
    if (to) {
        sel_add(&gSel, to); /* the caller checked it */
    }
    FrmGotoForm(ComposeForm);
}

/* Header line: "To: a, b, ..." (truncated), "To: (none)", or msg. */
static void DrawComposeHeader(const char *msg)
{
    RectangleType r;
    FontID oldFont;

    r.topLeft.x = 0;
    r.topLeft.y = 0;
    r.extent.x = 160;
    r.extent.y = SKETCH_Y - 1;
    WinEraseRectangle(&r, 0);
    if (msg) {
        StrCopy(gLine2, msg);
    } else if (sel_count(&gSel) == 0) {
        StrCopy(gLine2, "To: (none)");
    } else {
        StrCopy(gLine2, "To: ");
        if (sel_join(&gSel, ", ", gLine2 + 4, (int)(sizeof(gLine2) - 4)) < 0) {
            StrPrintF(gLine2, "To: %d people", sel_count(&gSel));
        }
    }
    oldFont = FntSetFont(boldFont);
    TruncateToWidth(gLine, sizeof(gLine), gLine2, 156);
    WinDrawChars(gLine, StrLen(gLine), 2, 2);
    FntSetFont(oldFont);
}

/* Header, borders and the canvas, all from the globals. */
static void DrawCompose(void)
{
    DrawComposeHeader(NULL);
    WinDrawLine(0, SKETCH_Y - 1, 159, SKETCH_Y - 1);
    DrawSketchBits(gSketch.cur);
    WinDrawLine(0, SKETCH_Y + SK_H, 159, SKETCH_Y + SK_H);
}

/* Draws a pen segment (canvas coordinates) in black on the screen,
 * clipped to the canvas, and into gSketch (sk_line clips there). */
static void DrawSeg(const CvSeg *g)
{
    RectangleType clip;
    RectangleType old;

    cv_apply(&gSketch, g);
    WinGetClip(&old);
    clip.topLeft.x = 0;
    clip.topLeft.y = SKETCH_Y;
    clip.extent.x = SK_W;
    clip.extent.y = SK_H;
    WinSetClip(&clip);
    WinDrawLine((Coord)g->x0, (Coord)(g->y0 + SKETCH_Y), (Coord)g->x1, (Coord)(g->y1 + SKETCH_Y));
    WinSetClip(&old);
}

static void DoSend(void)
{
    unsigned long id = 0;
    Err e;

    if (sel_count(&gSel) == 0) {
        ShowInfo("Choose recipients first");
        DrawCompose();
        return;
    }
    if (cv_blank(gSketch.cur)) {
        ShowInfo("Draw something first");
        DrawCompose();
        return;
    }
    /* At most 10 x 16 chars + 9 commas = 169 < the 400 the server takes. */
    if (sel_join(&gSel, ",", gCsv, (int)sizeof(gCsv)) <= 0 || StrLen(gCsv) > 400) {
        ShowInfo("Too many recipients");
        DrawCompose();
        return;
    }
    DrawComposeHeader("Sending...");
    e = net_send(gPrefs.server, gPrefs.token, gCsv, gSketch.cur, &id);
    gCsv[0] = '\0';
    if (e != errNone) {
        /* A server err keeps the sketch and recipients for a retry. */
        DraftNetFailed(e, "Could not send");
        if (e != NET_ERR_AUTH) {
            DrawCompose();
        }
        return;
    }
    ShowInfo("Sent");
    ClearDraft();
    /* Show it in the Sent list, first page, freshly loaded. */
    gMode = 1;
    gPage = 0;
    gCount = 0;
    gNeedLoad = true;
    gBackOff = false;
    FrmGotoForm(InboxForm);
}

/* Back: a drawn sketch needs a confirmation; a blank one leaves quietly. */
static void LeaveCompose(void)
{
    if (!cv_blank(gSketch.cur) && FrmCustomAlert(DiscardAlert, "", "", "") != 0) {
        DrawCompose(); /* Keep editing */
        return;
    }
    ClearDraft();
    FrmGotoForm(InboxForm);
}

static Boolean ComposeFormHandleEvent(EventType *e)
{
    CvSeg g;

    switch (e->eType) {
    case frmOpenEvent:
    case frmUpdateEvent:
        cv_pen_reset(&gPen);
        FrmDrawForm(FrmGetActiveForm());
        DrawCompose();
        return true;

    case penDownEvent:
        /* Outside the canvas (buttons, header): normal form handling. */
        if (!cv_pen_down(&gPen, e->screenX, e->screenY, &g)) {
            return false;
        }
        sk_begin_stroke(&gSketch);
        DrawSeg(&g); /* a tap leaves a dot */
        return true;

    case penMoveEvent:
        if (!gPen.active) {
            return false;
        }
        if (cv_pen_move(&gPen, e->screenX, e->screenY, &g)) {
            DrawSeg(&g);
        }
        return true;

    case penUpEvent:
        if (!gPen.active) {
            return false;
        }
        if (cv_pen_up(&gPen, e->screenX, e->screenY, &g)) {
            DrawSeg(&g);
        }
        return true;

    case ctlSelectEvent:
        switch (e->data.ctlSelect.controlID) {
        case ComposeClearButton:
            sk_clear(&gSketch); /* also drops the undo snapshot */
            DrawSketchBits(gSketch.cur);
            return true;
        case ComposeUndoButton:
            if (sk_undo(&gSketch)) {
                DrawSketchBits(gSketch.cur);
            }
            return true;
        case ComposeToButton:
            FrmGotoForm(PickerForm);
            return true;
        case ComposeSendButton:
            DoSend();
            return true;
        case ComposeBackButton:
            LeaveCompose();
            return true;
        default:
            return false;
        }

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* PickerForm                                                          */
/* ------------------------------------------------------------------ */

/* "Selected: n/10" on the left; right is a status ("Loading...") or,
 * when NULL, the page number. */
static void DrawPickerStatus(const char *right)
{
    StrPrintF(gLine, "Selected: %d/%d", sel_count(&gSel), SEL_MAX);
    DrawTextLine(gLine, 2, PICKER_STATUS_Y, 76);
    if (right) {
        StrCopy(gLine, right);
    } else if (!gDirLoaded) {
        gLine[0] = '\0';
    } else if (gDirCount == 0) {
        StrCopy(gLine, "No names");
    } else {
        StrPrintF(gLine, "Page %d", gDirPage + 1);
    }
    DrawTextLine(gLine, 80, PICKER_STATUS_Y, 66);
}

static void PickerDrawFunc(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
    Coord x = bounds->topLeft.x;
    Coord y = bounds->topLeft.y;

    if (itemNum < 0 || itemNum >= gDirCount) {
        return;
    }
    if (sel_contains(&gSel, gDirRows[itemNum])) {
        /* a check mark */
        WinDrawLine((Coord)(x + 1), (Coord)(y + 5), (Coord)(x + 3), (Coord)(y + 8));
        WinDrawLine((Coord)(x + 3), (Coord)(y + 8), (Coord)(x + 8), (Coord)(y + 2));
    }
    WinDrawChars(gDirRows[itemNum], StrLen(gDirRows[itemNum]), (Coord)(x + 12), y);
}

static void RelistPicker(FormType *frm)
{
    UInt16 idx = FrmGetObjectIndex(frm, PickerList);
    ListType *lst = (ListType *)FrmGetObjectPtr(frm, idx);
    RectangleType r;

    FrmGetObjectBounds(frm, idx, &r);
    WinEraseRectangle(&r, 0);
    LstSetDrawFunction(lst, PickerDrawFunc);
    LstSetListChoices(lst, NULL, gDirCount);
    LstSetSelection(lst, noListSelection);
    LstDrawList(lst);
    DrawPickerStatus(NULL);
}

/* Loads directory page `page` for prefix (already [a-z0-9_]). On success
 * the rows (without my own handle), page and prefix are replaced; on
 * failure, or an empty page past the first, the old page is kept. */
static Boolean LoadDir(Int16 page, const char *prefix)
{
    MemHandle h;
    Int16 raw;
    Int16 n;
    Err e;

    DrawPickerStatus("Loading...");
    e = net_directory(gPrefs.server, gPrefs.token, prefix, page * ROWS, ROWS, &h);
    if (e != errNone) {
        DraftNetFailed(e, "Could not search");
        return false;
    }
    /* net_directory checked the "ok" status line; h holds the rows,
     * writable, for proto_parse_handles. */
    raw = (Int16)proto_parse_handles((char *)MemHandleLock(h), gDirTmp, ROWS);
    MemHandleUnlock(h);
    MemHandleFree(h);
    if (raw == 0 && page > 0) {
        ShowInfo("No more names.");
        return false;
    }
    n = (Int16)sel_drop(gDirTmp, raw, gPrefs.handle);
    MemMove(gDirRows, gDirTmp, sizeof(gDirRows));
    gDirCount = n;
    gDirFull = (Boolean)(raw == ROWS);
    gDirPage = page;
    if (prefix != gDirPrefix) {
        StrCopy(gDirPrefix, prefix);
    }
    gDirLoaded = true;
    return true;
}

static void PickerLoadAndRelist(Int16 page, const char *prefix)
{
    LoadDir(page, prefix);
    if (FrmGetActiveFormID() == PickerForm) {
        RelistPicker(FrmGetActiveForm());
    }
}

/* Search: the typed prefix, lower-cased; anything but [a-z0-9_] (spaces
 * are dropped) is refused, so field text never reaches the request. */
static void PickerSearch(FormType *frm)
{
    char prefix[SEL_HCAP];

    if (sel_prefix(FldGetTextPtr((FieldType *)GetObj(frm, PickerPrefixField)),
                   prefix, (int)sizeof(prefix)) < 0) {
        ShowInfo("Search with letters, digits and _ only.");
        RelistPicker(frm);
        return;
    }
    PickerLoadAndRelist(0, prefix);
}

static Boolean PickerFormHandleEvent(EventType *e)
{
    switch (e->eType) {
    case frmOpenEvent: {
        FormType *frm = FrmGetActiveForm();
        UInt16 fi = FrmGetObjectIndex(frm, PickerPrefixField);

        FrmDrawForm(frm);
        if (gDirPrefix[0]) {
            FldInsert((FieldType *)FrmGetObjectPtr(frm, fi), gDirPrefix, StrLen(gDirPrefix));
        }
        RelistPicker(frm);
        FrmSetFocus(frm, fi);
        if (!gDirLoaded) {
            PickerLoadAndRelist(0, gDirPrefix);
        }
        return true;
    }

    case frmUpdateEvent: {
        FormType *frm = FrmGetActiveForm();

        FrmDrawForm(frm);
        RelistPicker(frm);
        return true;
    }

    case ctlSelectEvent: {
        FormType *frm = FrmGetActiveForm();

        switch (e->data.ctlSelect.controlID) {
        case PickerSearchButton:
            PickerSearch(frm);
            return true;
        case PickerPrevButton:
            if (gDirPage > 0) {
                PickerLoadAndRelist((Int16)(gDirPage - 1), gDirPrefix);
            }
            return true;
        case PickerNextButton:
            if (gDirLoaded && gDirFull) {
                PickerLoadAndRelist((Int16)(gDirPage + 1), gDirPrefix);
            } else {
                ShowInfo("No more names.");
            }
            return true;
        case PickerClearButton:
            sel_clear(&gSel);
            RelistPicker(frm);
            return true;
        case PickerDoneButton:
            FrmGotoForm(ComposeForm);
            return true;
        default:
            return false;
        }
    }

    case lstSelectEvent:
        if (e->data.lstSelect.listID == PickerList) {
            FormType *frm = FrmGetActiveForm();
            Int16 sel = e->data.lstSelect.selection;

            if (sel >= 0 && sel < gDirCount && sel_toggle(&gSel, gDirRows[sel]) == SEL_FULL) {
                ShowInfo("At most 10 recipients.");
            }
            RelistPicker(frm);
            return true;
        }
        return false;

    case keyDownEvent:
        /* A Graffiti return in the prefix field searches. */
        if (e->data.keyDown.chr == chrLineFeed) {
            PickerSearch(FrmGetActiveForm());
            return true;
        }
        return false;

    default:
        return false;
    }
}

/* ------------------------------------------------------------------ */
/* Form / event handling                                                */
/* ------------------------------------------------------------------ */

static Boolean AppHandleEvent(EventType *e)
{
    if (e->eType == frmLoadEvent) {
        UInt16 formId = e->data.frmLoad.formID;
        FormType *frm = FrmInitForm(formId);

        FrmSetActiveForm(frm);
        switch (formId) {
        case SetupForm:
            FrmSetEventHandler(frm, SetupFormHandleEvent);
            break;
        case InboxForm:
            FrmSetEventHandler(frm, InboxFormHandleEvent);
            break;
        case ViewerForm:
            FrmSetEventHandler(frm, ViewerFormHandleEvent);
            break;
        case ComposeForm:
            FrmSetEventHandler(frm, ComposeFormHandleEvent);
            break;
        case PickerForm:
            FrmSetEventHandler(frm, PickerFormHandleEvent);
            break;
        default:
            break;
        }
        return true;
    }
    return false;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    EventType e;
    UInt16 err;

    if (cmd != sysAppLaunchCmdNormalLaunch) {
        return 0;
    }

    LoadPrefs();
    FrmGotoForm(LoggedIn() ? InboxForm : SetupForm);

    do {
        EvtGetEvent(&e, evtWaitForever);
        if (SysHandleEvent(&e)) continue;
        if (MenuHandleEvent(0, &e, &err)) continue;
        if (AppHandleEvent(&e)) continue;
        FrmDispatchEvent(&e);
    } while (e.eType != appStopEvent);

    SavePrefs();

    if (fuji_palmos_is_open()) {
        fuji_palmos_close();
    }

    FrmCloseAllForms();
    return 0;
}
