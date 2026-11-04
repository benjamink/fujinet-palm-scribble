/* palm/apps/scribble/net.c -- see net.h. Palm-only: each call builds its
 * device spec / header with netutil.c, makes one fnapp_http_request and
 * classifies the reply with netutil.c. Every MemHandle fnapp returns is
 * freed here on every path (except the list body handed to the caller).
 *
 * Header order matters: see fnapp.h. */
#include "fujinet-network.h"
#include "fujinet-palmos.h"
#include <PalmOS.h>
#include "fnapp.h"
#include "netutil.h"
#include "proto.h"
#include "net.h"

#define LOGIN_CAP 512uL
#define NOTE_CAP (NU_NOTE_BYTES + 2uL) /* reads up to 2401: detects a long reply */
#define SMALL_CAP 128uL

static char gSpec[NU_SPEC_CAP];
static char gHdr[NU_HDR_CAP];
static char gReason[64];

const char *net_last_reason(void)
{
    return gReason;
}

static void SetReason(const char *s)
{
    StrNCopy(gReason, s, sizeof(gReason) - 1);
    gReason[sizeof(gReason) - 1] = '\0';
}

/* Maps a fnapp_http_request failure. */
static Err IoFail(Err e)
{
    if (e == memErrNotEnoughSpace) {
        SetReason("out of memory");
        return NET_ERR_MEM;
    }
    StrPrintF(gReason, "no connection (%u)", (UInt16)e);
    return NET_ERR_IO;
}

/* Maps a netutil reply class (reason already in gReason). */
static Err ReplyFail(int k)
{
    return (k == NU_AUTH) ? NET_ERR_AUTH : NET_ERR_SERVER;
}

static Err BadArg(const char *what)
{
    SetReason(what);
    return NET_ERR_ARG;
}

/* Builds the auth header for token into gHdr. */
static Boolean MakeHeader(const char *token)
{
    return nu_auth_header(gHdr, sizeof(gHdr), token) >= 0;
}

/* One request whose reply is plain "ok" / "err ...". */
static Err SimpleRequest(UInt8 method, const UInt8 *body, UInt16 bodyLen,
                         MemHandle *outH, UInt32 *outLen, UInt32 cap)
{
    Err e;

    EvtResetAutoOffTimer();
    e = fnapp_http_request(gSpec, method, gHdr[0] ? gHdr : NULL, body, bodyLen,
                           outH, outLen, cap);
    if (e != errNone) {
        *outH = 0;
        return IoFail(e);
    }
    return errNone;
}

Err net_login(const char *server, const char *user, const char *pass,
              char *token, int tcap, char *handle, int hcap)
{
    char body[160];
    int n;
    MemHandle h = 0;
    UInt32 len = 0;
    Err e;
    int k;

    gReason[0] = '\0';
    gHdr[0] = '\0';
    if (nu_spec(gSpec, sizeof(gSpec), server, "/palm/v1/login") < 0) {
        return BadArg("bad server address");
    }
    n = proto_build_login(body, sizeof(body), user, pass);
    if (n < 0) {
        MemSet(body, sizeof(body), 0);
        return BadArg("bad username or password");
    }
    e = SimpleRequest(FNAPP_POST, (const UInt8 *)body, (UInt16)n, &h, &len, LOGIN_CAP);
    MemSet(body, sizeof(body), 0);
    if (e != errNone) {
        return e;
    }
    {
        char *p = (char *)MemHandleLock(h);

        k = nu_login_reply(p, len, token, tcap, handle, hcap, gReason, sizeof(gReason));
        MemHandleUnlock(h);
    }
    MemHandleFree(h);
    if (k != NU_OK) {
        /* Login carries no token, so "err auth" here is just a failure. */
        return NET_ERR_SERVER;
    }
    gReason[0] = '\0';
    return errNone;
}

/* GET gSpec (already built) with the header, and leave only the rows
 * after "ok" in *h. */
static Err GetRows(MemHandle *h)
{
    MemHandle rh = 0;
    UInt32 len = 0;
    Err e;
    const char *rest = NULL;
    char *p;
    int k;

    *h = 0;
    e = SimpleRequest(FNAPP_GET, NULL, 0, &rh, &len, NET_LIST_CAP);
    if (e != errNone) {
        return e;
    }
    p = (char *)MemHandleLock(rh);
    k = nu_reply(p, len, &rest, gReason, sizeof(gReason));
    if (k == NU_OK) {
        UInt32 skip = (UInt32)(rest - p);
        UInt32 left = (skip <= len) ? len - skip : 0;

        MemMove(p, p + skip, (Int32)left);
        p[left] = '\0';
        MemHandleUnlock(rh);
        MemHandleResize(rh, left + 1); /* shrinking only; failure is harmless */
        *h = rh;
        gReason[0] = '\0';
        return errNone;
    }
    MemHandleUnlock(rh);
    MemHandleFree(rh);
    return ReplyFail(k);
}

Err net_list(const char *server, const char *token, int sent, int offset, int limit,
             MemHandle *h)
{
    *h = 0;
    gReason[0] = '\0';
    if (nu_spec_list(gSpec, sizeof(gSpec), server, sent, offset, limit) < 0) {
        return BadArg("bad server address");
    }
    if (!MakeHeader(token)) {
        SetReason("auth");
        return NET_ERR_AUTH;
    }
    return GetRows(h);
}

Err net_directory(const char *server, const char *token, const char *prefix,
                  int offset, int limit, MemHandle *h)
{
    *h = 0;
    gReason[0] = '\0';
    if (nu_spec_directory(gSpec, sizeof(gSpec), server, prefix, offset, limit) < 0) {
        return BadArg("bad server address or name");
    }
    if (!MakeHeader(token)) {
        SetReason("auth");
        return NET_ERR_AUTH;
    }
    return GetRows(h);
}

Err net_fetch_note(const char *server, const char *token, unsigned long id,
                   unsigned char *bitmap)
{
    MemHandle h = 0;
    UInt32 len = 0;
    Err e;
    int k;

    gReason[0] = '\0';
    if (nu_spec_note(gSpec, sizeof(gSpec), server, id) < 0) {
        return BadArg("bad server address");
    }
    if (!MakeHeader(token)) {
        SetReason("auth");
        return NET_ERR_AUTH;
    }
    e = SimpleRequest(FNAPP_GET, NULL, 0, &h, &len, NOTE_CAP);
    if (e != errNone) {
        return e;
    }
    {
        char *p = (char *)MemHandleLock(h);

        k = nu_note_reply(p, len, gReason, sizeof(gReason));
        if (k == NU_OK) {
            MemMove(bitmap, p, NU_NOTE_BYTES);
        }
        MemHandleUnlock(h);
    }
    MemHandleFree(h);
    if (k != NU_OK) {
        return ReplyFail(k);
    }
    return errNone;
}

Err net_delete(const char *server, const char *token, unsigned long id)
{
    MemHandle h = 0;
    UInt32 len = 0;
    Err e;
    int k;

    gReason[0] = '\0';
    if (nu_spec_note(gSpec, sizeof(gSpec), server, id) < 0) {
        return BadArg("bad server address");
    }
    if (!MakeHeader(token)) {
        SetReason("auth");
        return NET_ERR_AUTH;
    }
    e = SimpleRequest(FNAPP_DELETE, NULL, 0, &h, &len, SMALL_CAP);
    if (e != errNone) {
        return e;
    }
    k = nu_reply((const char *)MemHandleLock(h), len, NULL, gReason, sizeof(gReason));
    MemHandleUnlock(h);
    MemHandleFree(h);
    if (k != NU_OK) {
        return ReplyFail(k);
    }
    gReason[0] = '\0';
    return errNone;
}

Err net_send(const char *server, const char *token, const char *handles_csv,
             const unsigned char *bitmap, unsigned long *newId)
{
    MemHandle bodyH, h = 0;
    UInt32 len = 0;
    UInt16 cap;
    int n;
    Err e;
    int k;
    unsigned long id = 0;

    gReason[0] = '\0';
    if (nu_spec(gSpec, sizeof(gSpec), server, "/palm/v1/send") < 0) {
        return BadArg("bad server address");
    }
    if (!MakeHeader(token)) {
        SetReason("auth");
        return NET_ERR_AUTH;
    }
    if (!handles_csv || !bitmap || StrLen(handles_csv) > 400) {
        return BadArg("bad recipients");
    }
    cap = (UInt16)(StrLen(handles_csv) + 1 + NU_NOTE_BYTES);
    bodyH = MemHandleNew(cap);
    if (!bodyH) {
        SetReason("out of memory");
        return NET_ERR_MEM;
    }
    {
        UInt8 *body = (UInt8 *)MemHandleLock(bodyH);

        n = proto_build_send(body, (int)cap, handles_csv, bitmap);
        if (n < 0) {
            e = BadArg("bad recipients");
        } else {
            e = SimpleRequest(FNAPP_POST, body, (UInt16)n, &h, &len, SMALL_CAP);
        }
        MemHandleUnlock(bodyH);
    }
    MemHandleFree(bodyH);
    if (e != errNone) {
        return e;
    }
    k = nu_send_reply((const char *)MemHandleLock(h), len, &id, gReason, sizeof(gReason));
    MemHandleUnlock(h);
    MemHandleFree(h);
    if (k != NU_OK) {
        return ReplyFail(k);
    }
    if (newId) {
        *newId = id;
    }
    gReason[0] = '\0';
    return errNone;
}
