/* Host test for the Scribble recipient selection. cc -std=c89 -Wall -Wextra */
#include <stdio.h>
#include <string.h>
#include "selection.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static Selection s;

static void test_handle_ok(void)
{
    CHECK(sel_handle_ok("alice"));
    CHECK(sel_handle_ok("a"));
    CHECK(sel_handle_ok("bob_2"));
    CHECK(sel_handle_ok("abcdefghijklmnop"));       /* 16 */
    CHECK(!sel_handle_ok("abcdefghijklmnopq"));     /* 17 */
    CHECK(!sel_handle_ok(""));
    CHECK(!sel_handle_ok(NULL));
    CHECK(!sel_handle_ok("Alice"));
    CHECK(!sel_handle_ok("a,b"));
    CHECK(!sel_handle_ok("a b"));
    CHECK(!sel_handle_ok("a\n"));
    CHECK(!sel_handle_ok("a|b"));
}

static void test_toggle(void)
{
    sel_clear(&s);
    CHECK(sel_count(&s) == 0);
    CHECK(!sel_contains(&s, "alice"));
    CHECK(sel_toggle(&s, "alice") == SEL_ADDED);
    CHECK(sel_count(&s) == 1);
    CHECK(sel_contains(&s, "alice"));
    CHECK(sel_toggle(&s, "bob") == SEL_ADDED);
    CHECK(sel_toggle(&s, "carol") == SEL_ADDED);
    CHECK(sel_count(&s) == 3);
    CHECK(sel_toggle(&s, "bob") == SEL_REMOVED);
    CHECK(sel_count(&s) == 2);
    CHECK(!sel_contains(&s, "bob"));
    CHECK(sel_contains(&s, "alice"));
    CHECK(sel_contains(&s, "carol"));
    /* order kept after a removal */
    CHECK(strcmp(s.h[0], "alice") == 0);
    CHECK(strcmp(s.h[1], "carol") == 0);
    CHECK(sel_toggle(&s, "Bad!") == SEL_BAD);
    CHECK(sel_toggle(&s, NULL) == SEL_BAD);
    CHECK(sel_count(&s) == 2);
    /* prefix of a selected handle is a different handle */
    CHECK(!sel_contains(&s, "ali"));
    CHECK(!sel_contains(&s, "alicex"));
    sel_clear(&s);
    CHECK(sel_count(&s) == 0);
    CHECK(!sel_contains(&s, "alice"));
}

static void test_max(void)
{
    char h[8];
    int i;

    sel_clear(&s);
    for (i = 0; i < SEL_MAX; i++) {
        sprintf(h, "u%d", i);
        CHECK(sel_toggle(&s, h) == SEL_ADDED);
    }
    CHECK(sel_count(&s) == SEL_MAX);
    CHECK(sel_toggle(&s, "eleven") == SEL_FULL);
    CHECK(sel_count(&s) == SEL_MAX);
    CHECK(!sel_contains(&s, "eleven"));
    CHECK(sel_add(&s, "eleven") == SEL_FULL);
    /* removing still works when full, and makes room */
    CHECK(sel_toggle(&s, "u3") == SEL_REMOVED);
    CHECK(sel_toggle(&s, "eleven") == SEL_ADDED);
    CHECK(sel_count(&s) == SEL_MAX);
}

static void test_add(void)
{
    sel_clear(&s);
    CHECK(sel_add(&s, "alice") == SEL_ADDED);
    CHECK(sel_add(&s, "alice") == SEL_PRESENT);
    CHECK(sel_count(&s) == 1);
    CHECK(sel_add(&s, "ALICE") == SEL_BAD);
    CHECK(sel_count(&s) == 1);
}

static void test_join(void)
{
    char out[SEL_CSV_CAP];
    char small[6];
    char h[20];
    int i;

    sel_clear(&s);
    CHECK(sel_join(&s, ",", out, sizeof out) == 0);
    CHECK(out[0] == 0);
    sel_add(&s, "alice");
    CHECK(sel_join(&s, ",", out, sizeof out) == 5);
    CHECK(strcmp(out, "alice") == 0);
    sel_add(&s, "bob");
    CHECK(sel_join(&s, ",", out, sizeof out) == 9);
    CHECK(strcmp(out, "alice,bob") == 0);
    CHECK(sel_join(&s, ", ", out, sizeof out) == 10);
    CHECK(strcmp(out, "alice, bob") == 0);
    /* does not fit: -1 and "" */
    CHECK(sel_join(&s, ",", small, sizeof small) == -1);
    CHECK(small[0] == 0);
    CHECK(sel_join(&s, ",", out, 9) == -1);     /* needs 10 incl NUL */
    CHECK(sel_join(&s, ",", out, 10) == 9);
    CHECK(sel_join(&s, ",", NULL, 10) == -1);
    CHECK(sel_join(&s, ",", out, 0) == -1);

    /* worst case: 10 x 16 chars + 9 commas = 169 fits SEL_CSV_CAP, <= 400 */
    sel_clear(&s);
    for (i = 0; i < SEL_MAX; i++) {
        sprintf(h, "abcdefghijklmn%02d", i);
        CHECK(sel_add(&s, h) == SEL_ADDED);
    }
    CHECK(sel_join(&s, ",", out, sizeof out) == 169);
    CHECK(strlen(out) == 169);
    CHECK(SEL_CSV_CAP - 1 <= 400);
    CHECK(out[16] == ',');
    CHECK(out[168] == '9');
}

static void test_prefix(void)
{
    char out[SEL_HCAP];

    CHECK(sel_prefix("", out, sizeof out) == 0 && out[0] == 0);
    CHECK(sel_prefix(NULL, out, sizeof out) == 0 && out[0] == 0);
    CHECK(sel_prefix("Al", out, sizeof out) == 2 && strcmp(out, "al") == 0);
    CHECK(sel_prefix("  bob_1 \n", out, sizeof out) == 5 && strcmp(out, "bob_1") == 0);
    CHECK(sel_prefix("b o", out, sizeof out) == 2 && strcmp(out, "bo") == 0);
    CHECK(sel_prefix("a-b", out, sizeof out) == -1 && out[0] == 0);
    CHECK(sel_prefix("a&x=1", out, sizeof out) == -1);
    CHECK(sel_prefix("a%20", out, sizeof out) == -1);
    CHECK(sel_prefix("\xe9t\xe9", out, sizeof out) == -1);
    CHECK(sel_prefix("ABCDEFGHIJKLMNOP", out, sizeof out) == 16
          && strcmp(out, "abcdefghijklmnop") == 0);
    CHECK(sel_prefix("abcdefghijklmnopq", out, sizeof out) == -1);
    CHECK(sel_prefix("abc", out, 3) == -1);      /* cap too small */
    CHECK(sel_prefix("ab", out, 3) == 2);
}

static void test_drop(void)
{
    char rows[8][17];
    int n;

    strcpy(rows[0], "alice");
    strcpy(rows[1], "me");
    strcpy(rows[2], "bob");
    strcpy(rows[3], "me");
    strcpy(rows[4], "mex");
    n = sel_drop(rows, 5, "me");
    CHECK(n == 3);
    CHECK(strcmp(rows[0], "alice") == 0);
    CHECK(strcmp(rows[1], "bob") == 0);
    CHECK(strcmp(rows[2], "mex") == 0);
    CHECK(sel_drop(rows, 3, "") == 3);
    CHECK(sel_drop(rows, 3, NULL) == 3);
    CHECK(sel_drop(rows, 0, "alice") == 0);
}

int main(void)
{
    test_handle_ok();
    test_toggle();
    test_max();
    test_add();
    test_join();
    test_prefix();
    test_drop();
    printf("test_selection: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
