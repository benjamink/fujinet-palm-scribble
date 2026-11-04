/* Host tests for netutil.c. Build: make test_netutil */
#include <stdio.h>
#include <string.h>
#include "netutil.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void t_server_ok(void)
{
    CHECK(nu_server_ok("https://host") == 1);
    CHECK(nu_server_ok("http://10.0.0.2:8080") == 1);
    CHECK(nu_server_ok("https://host/") == 1);
    CHECK(nu_server_ok("https://host/base") == 1);
    CHECK(nu_server_ok("https://") == 0);
    CHECK(nu_server_ok("https:///") == 0);
    CHECK(nu_server_ok("host") == 0);
    CHECK(nu_server_ok("ftp://host") == 0);
    CHECK(nu_server_ok("") == 0);
    CHECK(nu_server_ok(0) == 0);
    CHECK(nu_server_ok("https://ho st") == 0);
    CHECK(nu_server_ok("https://host\n") == 0);
    CHECK(nu_server_ok("https://host?x=1") == 0);
    CHECK(nu_server_ok("https://host#a") == 0);
    CHECK(nu_server_ok("https://h\x7f") == 0);
    CHECK(nu_server_ok("https://h\xe9") == 0);
}

static void t_cleartext(void)
{
    CHECK(nu_server_cleartext("http://10.0.0.2:8080") == 1);
    CHECK(nu_server_cleartext("http://host/") == 1);
    CHECK(nu_server_cleartext("https://host") == 0);
    CHECK(nu_server_cleartext("https://http://x") == 0);
    CHECK(nu_server_cleartext("http://") == 0);
    CHECK(nu_server_cleartext("ftp://host") == 0);
    CHECK(nu_server_cleartext("") == 0);
    CHECK(nu_server_cleartext(0) == 0);
}

static void t_spec(void)
{
    char out[NU_SPEC_CAP];
    char big[200];
    char tiny[8];
    int i, n;

    n = nu_spec(out, sizeof out, "https://host", "/palm/v1/login");
    CHECK(n == (int)strlen("N:https://host/palm/v1/login"));
    CHECK(strcmp(out, "N:https://host/palm/v1/login") == 0);

    /* trailing slashes are stripped */
    CHECK(nu_spec(out, sizeof out, "https://host/", "/x") > 0 && strcmp(out, "N:https://host/x") == 0);
    CHECK(nu_spec(out, sizeof out, "https://host//", "/x") > 0 && strcmp(out, "N:https://host/x") == 0);
    CHECK(nu_spec(out, sizeof out, "http://h:81/base/", "/x") > 0 && strcmp(out, "N:http://h:81/base/x") == 0);

    /* bad inputs */
    CHECK(nu_spec(out, sizeof out, "host", "/x") == -1 && out[0] == 0);
    CHECK(nu_spec(out, sizeof out, "https://host", "x") == -1);
    CHECK(nu_spec(out, sizeof out, "https://host", 0) == -1);
    CHECK(nu_spec(0, 10, "https://host", "/x") == -1);

    /* length limit: result + NUL must fit cap */
    CHECK(nu_spec(tiny, sizeof tiny, "https://host", "/x") == -1 && tiny[0] == 0);
    CHECK(nu_spec(out, 17, "https://host", "/x") == 16);          /* exactly fits */
    CHECK(nu_spec(out, 16, "https://host", "/x") == -1);          /* one short */
    memcpy(big, "https://", 8);
    for (i = 8; i < 160; i++) big[i] = 'a';
    big[160] = 0;
    CHECK(nu_spec(out, sizeof out, big, "/palm/v1/login") == -1);  /* too long */
    big[140] = 0;                                                  /* 140 + 2 + 14 = 156 */
    CHECK(nu_spec(out, sizeof out, big, "/palm/v1/login") == 156);
}

static void t_list_spec(void)
{
    char out[NU_SPEC_CAP];

    CHECK(nu_spec_list(out, sizeof out, "https://h", 0, 0, 8) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/inbox?offset=0&limit=8") == 0);
    CHECK(nu_spec_list(out, sizeof out, "https://h/", 1, 16, 8) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/sent?offset=16&limit=8") == 0);
    CHECK(nu_spec_list(out, sizeof out, "https://h", 0, 32767, 50) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/inbox?offset=32767&limit=50") == 0);
    CHECK(nu_spec_list(out, sizeof out, "https://h", 0, -1, 8) == -1);
    CHECK(nu_spec_list(out, sizeof out, "https://h", 0, 0, 0) == -1);
    CHECK(nu_spec_list(out, sizeof out, "https://h", 0, 0, 51) == -1);
    CHECK(nu_spec_list(out, 20, "https://h", 0, 0, 8) == -1);
}

static void t_dir_spec(void)
{
    char out[NU_SPEC_CAP];

    CHECK(nu_spec_directory(out, sizeof out, "https://h", "", 0, 50) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/directory?offset=0&limit=50") == 0);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", 0, 0, 50) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/directory?offset=0&limit=50") == 0);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "al_1", 50, 50) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/directory?offset=50&limit=50&prefix=al_1") == 0);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "abcdefghijklmnop", 0, 1) > 0);
    /* prefix validation */
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "Al", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a&b", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a b", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a%20", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a-b", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "abcdefghijklmnopq", 0, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a", -1, 50) == -1);
    CHECK(nu_spec_directory(out, sizeof out, "https://h", "a", 0, 0) == -1);
}

static void t_note_spec(void)
{
    char out[NU_SPEC_CAP];

    CHECK(nu_spec_note(out, sizeof out, "https://h", 1UL) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/notes/1") == 0);
    CHECK(nu_spec_note(out, sizeof out, "https://h/", 4294967295UL) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/notes/4294967295") == 0);
    CHECK(nu_spec_note(out, sizeof out, "https://h", 0UL) > 0);
    CHECK(strcmp(out, "N:https://h/palm/v1/notes/0") == 0);
}

static void t_header(void)
{
    char out[NU_HDR_CAP];
    char tok[44];
    char small[25];

    memset(tok, 'A', 43);
    tok[43] = 0;
    CHECK(nu_auth_header(out, sizeof out, "abc") == 25);
    CHECK(strcmp(out, "Authorization: Bearer abc") == 0);
    CHECK(nu_auth_header(out, sizeof out, tok) == 22 + 43);
    CHECK(strncmp(out, "Authorization: Bearer AAAA", 26) == 0 && strlen(out) == 65);
    CHECK(nu_auth_header(out, sizeof out, "") == -1 && out[0] == 0);
    CHECK(nu_auth_header(out, sizeof out, 0) == -1);
    CHECK(nu_auth_header(out, sizeof out, "a b") == -1);
    CHECK(nu_auth_header(out, sizeof out, "ab\r\n") == -1);
    CHECK(nu_auth_header(out, sizeof out, "ab\n") == -1);
    CHECK(nu_auth_header(small, sizeof small, "abc") == -1);   /* needs 26 */
    CHECK(nu_auth_header(out, 26, "abc") == 25);
}

static void t_reply(void)
{
    char r[64];
    char small[4];
    const char *rest = 0;
    const char *t = "ok\n1|2|a|0\n";

    CHECK(nu_reply(t, strlen(t), &rest, r, sizeof r) == NU_OK && rest == t + 3);
    CHECK(nu_reply("ok\n", 3, 0, r, sizeof r) == NU_OK);
    CHECK(nu_reply("err bad login\n", 14, &rest, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "bad login") == 0);
    CHECK(nu_reply("err auth\n", 9, 0, r, sizeof r) == NU_AUTH && strcmp(r, "auth") == 0);
    CHECK(nu_reply("err auth", 8, 0, r, sizeof r) == NU_AUTH);
    CHECK(nu_reply("err auth\r\n", 10, 0, r, sizeof r) == NU_AUTH);
    CHECK(nu_reply("err auth\n", 9, 0, small, sizeof small) == NU_AUTH && strcmp(small, "aut") == 0);
    CHECK(nu_reply("err auth\n", 9, 0, 0, 0) == NU_AUTH);
    CHECK(nu_reply("err authority\n", 14, 0, r, sizeof r) == NU_ERR);
    CHECK(nu_reply("err rate limit\n", 15, 0, small, sizeof small) == NU_ERR && strcmp(small, "rat") == 0);
    CHECK(nu_reply("", 0, 0, r, sizeof r) == NU_ERR && strcmp(r, "no reply") == 0);
    CHECK(nu_reply("<html>", 6, 0, r, sizeof r) == NU_ERR && strcmp(r, "bad reply") == 0);
    CHECK(nu_reply(0, 0, 0, r, sizeof r) == NU_ERR);
}

static void t_note_reply(void)
{
    static char note[NU_NOTE_BYTES + 2];
    char r[64];

    memset(note, 0, sizeof note);
    note[0] = 'o'; note[1] = 'k'; note[2] = '\n';   /* bitmap bytes that look like text */
    CHECK(nu_note_reply(note, NU_NOTE_BYTES, r, sizeof r) == NU_OK);
    CHECK(nu_note_reply(note, NU_NOTE_BYTES + 1, r, sizeof r) == NU_ERR && strcmp(r, "bad reply") == 0);
    CHECK(nu_note_reply(note, NU_NOTE_BYTES - 1, r, sizeof r) == NU_ERR && strcmp(r, "bad reply") == 0);
    CHECK(nu_note_reply("err no such note\n", 17, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "no such note") == 0);
    CHECK(nu_note_reply("err auth\n", 9, r, sizeof r) == NU_AUTH);
    CHECK(nu_note_reply("ok\n", 3, r, sizeof r) == NU_ERR && strcmp(r, "bad reply") == 0);
    CHECK(nu_note_reply("", 0, r, sizeof r) == NU_ERR && strcmp(r, "no reply") == 0);
}

static void t_login_reply(void)
{
    char tok[48], h[20], r[64];
    char tinytok[4];
    const char *ok = "ok\nq3Zx-_9\nalice\n";

    strcpy(tok, "untouched");
    strcpy(h, "untouched");
    CHECK(nu_login_reply(ok, strlen(ok), tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_OK);
    CHECK(strcmp(tok, "q3Zx-_9") == 0 && strcmp(h, "alice") == 0);
    CHECK(nu_login_reply("ok\r\nT\r\nbob", 10, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_OK);
    CHECK(strcmp(tok, "T") == 0 && strcmp(h, "bob") == 0);

    strcpy(tok, "keep");
    CHECK(nu_login_reply("err bad login\n", 14, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "bad login") == 0 && strcmp(tok, "keep") == 0);
    CHECK(nu_login_reply("err too many attempts\n", 22, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "too many attempts") == 0);

    /* malformed ok replies */
    CHECK(nu_login_reply("ok\n", 3, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "bad reply") == 0 && strcmp(tok, "keep") == 0);
    CHECK(nu_login_reply("ok\ntok\n", 7, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply("ok\n\nalice\n", 10, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply("ok\nt k\nalice\n", 13, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply("ok\ntok\nAlice\n", 13, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply("ok\ntok\nabcdefghijklmnopq\n", 25, tok, sizeof tok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply(ok, strlen(ok), tinytok, sizeof tinytok, h, sizeof h, r, sizeof r) == NU_ERR);
    CHECK(nu_login_reply(ok, strlen(ok), tok, sizeof tok, h, 5, r, sizeof r) == NU_ERR);  /* "alice" needs 6 */
    CHECK(nu_login_reply(ok, strlen(ok), tok, sizeof tok, h, 6, r, sizeof r) == NU_OK);
    CHECK(strcmp(tok, "keep") != 0);
}

static void t_send_reply(void)
{
    unsigned long id = 7;
    char r[64];

    CHECK(nu_send_reply("ok\n42\n", 6, &id, r, sizeof r) == NU_OK && id == 42UL);
    CHECK(nu_send_reply("ok\r\n4294967295", 14, &id, r, sizeof r) == NU_OK && id == 4294967295UL);
    id = 7;
    CHECK(nu_send_reply("ok\n4294967296\n", 14, &id, r, sizeof r) == NU_ERR && id == 7UL);
    CHECK(strcmp(r, "bad reply") == 0);
    CHECK(nu_send_reply("ok\n\n", 4, &id, r, sizeof r) == NU_ERR);
    CHECK(nu_send_reply("ok\n12x\n", 7, &id, r, sizeof r) == NU_ERR);
    CHECK(nu_send_reply("ok\n0\n", 5, &id, r, sizeof r) == NU_ERR);
    CHECK(nu_send_reply("err unknown handle: bob\n", 24, &id, r, sizeof r) == NU_ERR);
    CHECK(strcmp(r, "unknown handle: bob") == 0 && id == 7UL);
    CHECK(nu_send_reply("err auth\n", 9, &id, r, sizeof r) == NU_AUTH);
}

int main(void)
{
    t_server_ok();
    t_cleartext();
    t_spec();
    t_list_spec();
    t_dir_spec();
    t_note_spec();
    t_header();
    t_reply();
    t_note_reply();
    t_login_reply();
    t_send_reply();
    if (fails) {
        printf("test_netutil: %d FAILED\n", fails);
        return 1;
    }
    printf("test_netutil: all passed\n");
    return 0;
}
