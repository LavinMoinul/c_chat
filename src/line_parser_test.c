#include <assert.h>
#include <stdio.h>
#include <string.h>

static int find_newline(const char *buf, int len) {
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') return i;
    }
    return -1;
}

static void remove_consumed(char *buf, int *len, int consumed) {
    int remaining = *len - consumed;
    if (remaining > 0) {
        memmove(buf, buf + consumed, (size_t)remaining);
    }
    *len = remaining;
}

int main(void) {
    char buf[64];
    int len = 0;

    memcpy(buf + len, "hel", 3);
    len += 3;
    assert(find_newline(buf, len) == -1);

    memcpy(buf + len, "lo\nwo", 5);
    len += 5;

    int idx = find_newline(buf, len);
    assert(idx == 5);

    char line[64];
    memcpy(line, buf, (size_t)idx);
    line[idx] = '\0';
    assert(strcmp(line, "hello") == 0);

    remove_consumed(buf, &len, idx + 1);
    assert(len == 2);
    assert(strncmp(buf, "wo", 2) == 0);

    memcpy(buf + len, "rld\n", 4);
    len += 4;

    idx = find_newline(buf, len);
    assert(idx == 5);

    memcpy(line, buf, (size_t)idx);
    line[idx] = '\0';
    assert(strcmp(line, "world") == 0);

    remove_consumed(buf, &len, idx + 1);
    assert(len == 0);

    printf("Line parser tests passed\n");
    return 0;
}
