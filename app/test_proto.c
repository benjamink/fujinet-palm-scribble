/* Host tests for proto.c. Build: make test_proto */
#include <stdio.h>
#include <string.h>
#include "proto.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void t_status(void)
{
    const char *rest = 0;
    char r[64];
    char small[5];
    const char *t = "ok\n1|2|a|0\n";

    CHECK(proto_status(t, &rest, r, sizeof r) == 1);
    CHECK(rest == t + 3);
    CHECK(proto_status("ok", &rest, r, sizeof r) == 1);
    CHECK(*rest == 0);
    CHECK(proto_status("ok\r\nx", &rest, r, sizeof r) == 1 && strcmp(rest, "x") == 0);

    CHECK(proto_status("err rate limit\n", &rest, r, sizeof r) == 0);
    CHECK(strcmp(r, "rate limit") == 0);
    CHECK(proto_status("err bad login\r\nmore\n", 0, r, sizeof r) == 0);
    CHECK(strcmp(r, "bad login") == 0);
    CHECK(proto_status("err unknown handle: bob", 0, r, sizeof r) == 0);
    CHECK(strcmp(r, "unknown handle: bob") == 0);
    CHECK(proto_status("err rate limit\n", 0, small, sizeof small) == 0);
    CHECK(strcmp(small, "rate") == 0);

    CHECK(proto_status("", 0, r, sizeof r) == 0 && strcmp(r, "bad reply") == 0);
    CHECK(proto_status(0, 0, r, sizeof r) == 0 && strcmp(r, "bad reply") == 0);
    CHECK(proto_status("okay\n", 0, r, sizeof r) == 0 && strcmp(r, "bad reply") == 0);
    CHECK(proto_status("ok \n", 0, r, sizeof r) == 0 && strcmp(r, "bad reply") == 0);
    CHECK(proto_status("err\n", 0, r, sizeof r) == 0 && strcmp(r, "bad reply") == 0);
    CHECK(proto_status("xyz\n", 0, r, sizeof r) == 0);
    CHECK(proto_status("xyz\n", 0, 0, 0) == 0);
    CHECK(proto_status("bogus", 0, small, sizeof small) == 0 && strcmp(small, "bad ") == 0);
}

static void t_inbox(void)
{
    InboxRow rows[8];
    char a[] = "1|1730000000|alice|1\n2|1730000100|bob|0\n";
    char b[] = "1|1730000000|alice|1\ngarbage\n2|1730000100|bob|0\n";
    char c[] = "1|1730000000|alice|1\r\n2|1730000100|bob|0";
    char d[] = "123456789012345678901234567890|1|x|1\n5|6|y|0\n";
    char e[] = "1|1|abcdefghijklmnopq|1\n2|2|abcdefghijklmnop|1\n";
    char f[] = "1|1|a|1\n2|2|b|0\n3|3|c|1\n";
    char g[] = "1|1||1\n1|x|a|1\n1|1|a|2\n1|1|a\n1|1|a|1|9\n|1|a|1\n4294967296|1|a|1\n4294967295|4294967295|z|0\n";
    int n;

    n = proto_parse_inbox(a, rows, 8);
    CHECK(n == 2);
    CHECK(rows[0].id == 1UL && rows[0].created == 1730000000UL);
    CHECK(strcmp(rows[0].sender, "alice") == 0 && rows[0].unread == 1);
    CHECK(rows[1].id == 2UL && rows[1].created == 1730000100UL);
    CHECK(strcmp(rows[1].sender, "bob") == 0 && rows[1].unread == 0);

    n = proto_parse_inbox(b, rows, 8);
    CHECK(n == 2 && rows[1].id == 2UL);

    n = proto_parse_inbox(c, rows, 8);
    CHECK(n == 2 && strcmp(rows[0].sender, "alice") == 0 && strcmp(rows[1].sender, "bob") == 0);
    CHECK(rows[1].unread == 0);

    n = proto_parse_inbox(d, rows, 8);
    CHECK(n == 1 && rows[0].id == 5UL);

    n = proto_parse_inbox(e, rows, 8);
    CHECK(n == 1 && strcmp(rows[0].sender, "abcdefghijklmnop") == 0);

    n = proto_parse_inbox(f, rows, 2);
    CHECK(n == 2 && rows[1].id == 2UL);

    n = proto_parse_inbox(g, rows, 8);
    CHECK(n == 1 && rows[0].id == 4294967295UL && rows[0].created == 4294967295UL);

    CHECK(proto_parse_inbox((char *)"", rows, 8) == 0);
    {
        char h[] = "1|1|a|1\n";
        CHECK(proto_parse_inbox(h, rows, 0) == 0);
    }
}

static void t_sent(void)
{
    SentRow rows[4];
    char a[] = "2|1730000100|bob,carol\n3|1730000200|dave\n";
    char b[] = "x\n4|5|\n7|8|e,f\r\n";
    int n;

    n = proto_parse_sent(a, rows, 4);
    CHECK(n == 2);
    CHECK(rows[0].id == 2UL && rows[0].created == 1730000100UL);
    CHECK(strcmp(rows[0].to, "bob,carol") == 0);
    CHECK(strcmp(rows[1].to, "dave") == 0);

    n = proto_parse_sent(b, rows, 4);
    CHECK(n == 1 && rows[0].id == 7UL && strcmp(rows[0].to, "e,f") == 0);

    /* long recipient lists are kept (R10) */
    {
        static char in[1200];
        struct { SentRow r; char canary[8]; } w;
        char want[300];
        int k, j, pos;

        /* 10 x 16-char handles = 169 chars */
        pos = sprintf(in, "1|1|");
        for (k = 0; k < 10; k++) {
            if (k) in[pos++] = ',';
            for (j = 0; j < 16; j++) in[pos++] = (char)('a' + k);
        }
        in[pos++] = '\n'; in[pos] = 0;
        memset(&w, 'C', sizeof w);
        CHECK(proto_parse_sent(in, &w.r, 1) == 1);
        CHECK(strlen(w.r.to) == 169);
        CHECK(w.r.to[16] == ',' && w.r.to[0] == 'a' && w.r.to[168] == 'j');
        CHECK(w.canary[0] == 'C');

        /* exactly 175 chars kept intact */
        memset(want, 'x', 175); want[175] = 0;
        sprintf(in, "1|1|%s\n", want);
        memset(&w, 'C', sizeof w);
        CHECK(proto_parse_sent(in, &w.r, 1) == 1);
        CHECK(strcmp(w.r.to, want) == 0 && w.canary[0] == 'C');

        /* 176+ chars: cut at last comma that fits (index 171 here) */
        memset(want, 'y', 171); want[171] = ',';
        memset(want + 172, 'z', 40); want[212] = 0;
        sprintf(in, "1|1|%s\n", want);
        memset(&w, 'C', sizeof w);
        CHECK(proto_parse_sent(in, &w.r, 1) == 1);
        want[171] = 0;
        CHECK(strcmp(w.r.to, want) == 0);
        CHECK(w.canary[0] == 'C' && w.canary[7] == 'C');

        /* empty recipient field is still skipped */
        sprintf(in, "1|1|\n");
        CHECK(proto_parse_sent(in, rows, 4) == 0);
    }
    {
        char d[] = "99999999999999999999|1|a\n1|2|a\n3|4|b\n";
        n = proto_parse_sent(d, rows, 1);
        CHECK(n == 1 && rows[0].id == 1UL);
    }
}

static void t_handles(void)
{
    char out[4][17];
    char a[] = "alice\nbob_2\r\nBad\nwaytoolonghandle_xx\n\nx y\ncarol";
    char b[] = "a\nb\nc\n";
    int n = proto_parse_handles(a, out, 4);

    CHECK(n == 3);
    CHECK(strcmp(out[0], "alice") == 0 && strcmp(out[1], "bob_2") == 0 && strcmp(out[2], "carol") == 0);
    n = proto_parse_handles(b, out, 2);
    CHECK(n == 2 && strcmp(out[1], "b") == 0);
    {
        char c[] = "abcdefghijklmnop\n";
        CHECK(proto_parse_handles(c, out, 4) == 1 && strcmp(out[0], "abcdefghijklmnop") == 0);
    }
}

static void t_login(void)
{
    char o[32];
    memset(o, 'Z', sizeof o);
    CHECK(proto_build_login(o, sizeof o, "alice", "pw") == 9);
    CHECK(memcmp(o, "alice\npw\n", 9) == 0);
    CHECK(proto_build_login(o, 9, "alice", "pw") == 9);
    CHECK(proto_build_login(o, 8, "alice", "pw") == -1);
    CHECK(proto_build_login(o, 32, "", "pw") == -1);
    CHECK(proto_build_login(o, 32, "a", "") == -1);
    CHECK(proto_build_login(o, 32, "a\nb", "pw") == -1);
    CHECK(proto_build_login(o, 32, "a", "p\nw") == -1);
    CHECK(proto_build_login(o, 32, 0, "pw") == -1);
}

static void t_send(void)
{
    static sk_u8 bmp[SK_BYTES];
    static sk_u8 out[3000];
    char longh[500];
    int i;

    for (i = 0; i < SK_BYTES; i++) bmp[i] = (sk_u8)(i * 7 + 1);
    bmp[0] = 0x0A; bmp[1] = 0x00; bmp[2399] = 0x0A;
    memset(out, 0x55, sizeof out);

    CHECK(proto_build_send(out, sizeof out, "bob,carol", bmp) == 2410);
    CHECK(memcmp(out, "bob,carol\n", 10) == 0);
    CHECK(memcmp(out + 10, bmp, SK_BYTES) == 0);
    CHECK(out[2410] == 0x55);
    CHECK(proto_build_send(out, 2410, "bob,carol", bmp) == 2410);
    CHECK(proto_build_send(out, 2409, "bob,carol", bmp) == -1);
    CHECK(proto_build_send(out, sizeof out, "", bmp) == -1);
    CHECK(proto_build_send(out, sizeof out, 0, bmp) == -1);
    CHECK(proto_build_send(out, sizeof out, "a\nb", bmp) == -1);
    memset(longh, 'a', 401); longh[401] = 0;
    CHECK(proto_build_send(out, sizeof out, longh, bmp) == -1);
    longh[400] = 0;
    CHECK(proto_build_send(out, sizeof out, longh, bmp) == 2801);
}

static void tm(unsigned long t, const char *want)
{
    char s[17];
    proto_format_time(t, s);
    if (strcmp(s, want) != 0) { printf("FAIL time %lu: got %s want %s\n", t, s, want); fails++; }
}

static void t_time(void)
{
    /* expected values cross-checked with python3 datetime (UTC) */
    tm(1730000000UL, "2024-10-27 03:33");
    tm(951782400UL, "2000-02-29 00:00");
    tm(0UL, "1970-01-01 00:00");
    tm(86399UL, "1970-01-01 23:59");
    tm(4102444800UL, "2100-01-01 00:00");
    tm(1709164800UL, "2024-02-29 00:00");
    tm(1735689600UL, "2025-01-01 00:00");
    tm(4107542400UL, "2100-03-01 00:00");
}

int main(void)
{
    t_status(); t_inbox(); t_sent(); t_handles(); t_login(); t_send(); t_time();
    if (fails) { printf("test_proto: %d FAILED\n", fails); return 1; }
    printf("test_proto: all passed\n");
    return 0;
}
