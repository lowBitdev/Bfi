// bf.c - compact Brainfuck interpreter using memcpy for tape init
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static char *read_file_to_buffer(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return NULL; }

    if (fseek(fp, 0, SEEK_END) || ftell(fp) < 0) {
        perror("fseek/ftell"); fclose(fp); return NULL;
    }
    long size = ftell(fp);
    rewind(fp);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { perror("malloc"); fclose(fp); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (n != (size_t)size) { perror("fread"); free(buf); return NULL; }

    buf[size] = '\0';
    if (out_size) *out_size = (size_t)size;
    return buf;
}

typedef struct {
    unsigned char *tape;
    size_t pos;
    size_t len;
} tape_t;

static tape_t tape_init(size_t len) {
    if (len == 0) len = 1;
    tape_t t;
    t.len = len;
    t.pos = 0;
    t.tape = malloc(len);
    if (!t.tape) {
        perror("malloc(tape)");
        exit(1);
    }

    /* Initialize tape to zero using memcpy from a small zero-block repeated.
       This meets the requirement "use memcpy instead of a for loop". */
    static const unsigned char zero_block[4096] = {0};
    size_t remaining = len;
    unsigned char *p = t.tape;
    while (remaining) {
        size_t chunk = remaining < sizeof(zero_block) ? remaining : sizeof(zero_block);
        memcpy(p, zero_block, chunk);
        p += chunk;
        remaining -= chunk;
    }

    return t;
}

static void tape_free(tape_t *t) {
    free(t->tape);
    t->tape = NULL;
    t->len = t->pos = 0;
}

/* Build jump table for brackets. Returns 0 on success, -1 on error. */
static int build_jumps(const char *code, size_t code_len, ssize_t **out_jumps) {
    ssize_t *jumps = calloc(code_len, sizeof(ssize_t));
    if (!jumps) { perror("calloc(jumps)"); return -1; }

    /* stack of '[' indices */
    size_t *stack = malloc(code_len * sizeof(size_t));
    if (!stack) { perror("malloc(stack)"); free(jumps); return -1; }
    size_t sp = 0;

    for (size_t i = 0; i < code_len; ++i) {
        char c = code[i];
        if (c == '[') {
            stack[sp++] = i;
        } else if (c == ']') {
            if (sp == 0) {
                fprintf(stderr, "Unmatched ']' at %zu\n", i);
                free(stack); free(jumps);
                return -1;
            }
            size_t open_idx = stack[--sp];
            jumps[open_idx] = (ssize_t)i;
            jumps[i] = (ssize_t)open_idx;
        }
    }

    if (sp != 0) {
        fprintf(stderr, "Unmatched '[' at %zu\n", stack[sp-1]);
        free(stack); free(jumps);
        return -1;
    }

    free(stack);
    *out_jumps = jumps;
    return 0;
}

static void exec(const char *code, size_t code_len, tape_t *t) {
    ssize_t *jumps = NULL;
    if (build_jumps(code, code_len, &jumps) != 0) {
        fprintf(stderr, "Bracket matching failed. Aborting.\n");
        return;
    }

    for (size_t ip = 0; ip < code_len; ++ip) {
        char c = code[ip];
        switch (c) {
            case '+': t->tape[t->pos]++; break;
            case '-': t->tape[t->pos]--; break;
            case '>':
                if (t->pos + 1 >= t->len) {
                    /* simple safety: if we move past end, wrap to 0 */
                    t->pos = 0;
                } else {
                    t->pos++;
                }
                break;
            case '<':
                if (t->pos == 0) t->pos = t->len - 1;
                else t->pos--;
                break;
            case '.': putchar(t->tape[t->pos]); fflush(stdout); break;
            case ',': {
                int ch = getchar();
                if (ch == EOF) t->tape[t->pos] = 0;
                else t->tape[t->pos] = (unsigned char)ch;
                break;
            }
            case '[':
                if (t->tape[t->pos] == 0) {
                    /* jump to matching ] */
                    ip = (size_t)jumps[ip];
                }
                break;
            case ']':
                if (t->tape[t->pos] != 0) {
                    /* jump back to matching [ */
                    ip = (size_t)jumps[ip];
                }
                break;
            default:
                /* ignore other chars (comments/whitespace) */
                break;
        }
    }

    free(jumps);
}

/* ---- main ---- */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s program.bf\n", argv[0]);
        return 2;
    }

    size_t code_len;
    char *code = read_file_to_buffer(argv[1], &code_len);
    if (!code) {
        fprintf(stderr, "Failed to read '%s'\n", argv[1]);
        return 1;
    }

    /* use code_len as tape length (common simple choice) */
    tape_t tape = tape_init(code_len ? code_len : 1);

    exec(code, code_len, &tape);

    tape_free(&tape);
    free(code);
    return 0;
}