#include "blocks/blocks.h"
#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_INSTR 512
#define MAX_HANDLERS 16
#define MAX_PROGRAMS 32
#define MAX_VARS 64
#define MAX_NEST 16
#define MAX_STEPS 20000

typedef enum {
    OP_IF, OP_ELSE, OP_END, OP_REPEAT,
    OP_WALK, OP_TURN, OP_MOUSE_LOOK, OP_JUMP,
    OP_SET_VAR, OP_CHANGE_VAR, OP_PRINT, OP_SET_COLOR, OP_SET_POS
} Op;
typedef enum { C_KEY_DOWN, C_KEY_PRESSED, C_GROUNDED, C_GT, C_LT, C_EQ } Cond;
typedef enum { H_START, H_UPDATE, H_KEY } HKind;

typedef struct { int var; float val; } Arg;
typedef struct {
    int op, cond, neg, key, target;
    Arg a[3];
    char text[96];
} Instr;
typedef struct { int kind, key, begin, end; } Handler;
typedef struct {
    char name[32];
    Instr code[MAX_INSTR]; int n;
    Handler h[MAX_HANDLERS]; int nh;
} Program;

static Program programs[MAX_PROGRAMS];
static int program_count;
static char var_names[MAX_VARS][32];
static float var_vals[MAX_VARS];
static int var_count;

static int var_intern(const char *name) {
    for (int i = 0; i < var_count; i++) if (!strcmp(var_names[i], name)) return i;
    if (var_count >= MAX_VARS) return MAX_VARS - 1;
    strncpy(var_names[var_count], name, 31);
    return var_count++;
}
void blocks_reset_vars(void) { memset(var_vals, 0, sizeof var_vals); }
static float av(const Arg *a) { return a->var >= 0 ? var_vals[a->var] : a->val; }

static int parse_arg(const char *t, Arg *a) {
    if (!t) { a->var = -1; a->val = 0; return 1; }
    if (t[0] == '$') { a->var = var_intern(t + 1); a->val = 0; return 1; }
    char *end; a->val = strtof(t, &end); a->var = -1;
    return end != t && *end == 0;
}

static int tokenize(char *line, char **tok, int max) {
    int n = 0; char *s = line;
    while (*s && n < max) {
        while (isspace((unsigned char)*s)) s++;
        if (!*s) break;
        tok[n++] = s;
        while (*s && !isspace((unsigned char)*s)) s++;
        if (*s) *s++ = 0;
    }
    return n;
}

#define FAIL(msg) do { VLOG("%s:%d: %s", path, ln, msg); return 0; } while (0)

static int compile(Program *p, FILE *f, const char *path) {
    char line[256], raw[256]; int ln = 0, stack[MAX_NEST], sp = 0, in_handler = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *s = line; while (isspace((unsigned char)*s)) s++;
        size_t L = strlen(s); while (L && isspace((unsigned char)s[L - 1])) s[--L] = 0;
        if (!L) continue;
        strncpy(raw, s, sizeof raw - 1); raw[sizeof raw - 1] = 0;
        char *tok[8]; int nt = tokenize(s, tok, 8);
        const char *cmd = tok[0];
        if (!strcmp(cmd, "on")) {
            if (sp) FAIL("unclosed block before 'on'");
            if (nt < 2) FAIL("'on' needs an event");
            if (in_handler) p->h[p->nh - 1].end = p->n;
            if (p->nh >= MAX_HANDLERS) FAIL("too many event handlers");
            Handler *h = &p->h[p->nh++]; h->begin = p->n; h->key = 0;
            if (!strcmp(tok[1], "start")) h->kind = H_START;
            else if (!strcmp(tok[1], "update")) h->kind = H_UPDATE;
            else if (!strcmp(tok[1], "key_pressed")) {
                if (nt < 3 || (h->key = input_key_from_name(tok[2])) < 0) FAIL("unknown key");
                h->kind = H_KEY;
            } else FAIL("unknown event");
            in_handler = 1;
            continue;
        }
        if (!in_handler) FAIL("block outside of an 'on <event>' handler");
        if (p->n >= MAX_INSTR) FAIL("script too long");
        Instr *in = &p->code[p->n]; memset(in, 0, sizeof *in); in->target = -1; in->key = -1;
        int idx = p->n;
        if (!strcmp(cmd, "if")) {
            int i = 1;
            if (i < nt && !strcmp(tok[i], "not")) { in->neg = 1; i++; }
            if (i >= nt) FAIL("'if' needs a condition");
            const char *c = tok[i++];
            if (!strcmp(c, "key_down") || !strcmp(c, "key_pressed")) {
                in->cond = c[4] == 'd' ? C_KEY_DOWN : C_KEY_PRESSED;
                if (i >= nt || (in->key = input_key_from_name(tok[i])) < 0) FAIL("unknown key");
            } else if (!strcmp(c, "grounded")) in->cond = C_GROUNDED;
            else if (!strcmp(c, "gt") || !strcmp(c, "lt") || !strcmp(c, "eq")) {
                in->cond = !strcmp(c, "gt") ? C_GT : (!strcmp(c, "lt") ? C_LT : C_EQ);
                if (i + 1 >= nt || !parse_arg(tok[i], &in->a[0]) || !parse_arg(tok[i + 1], &in->a[1]))
                    FAIL("comparison needs two numbers or $variables");
            } else FAIL("unknown condition");
            in->op = OP_IF;
            if (sp >= MAX_NEST) FAIL("blocks nested too deeply");
            stack[sp++] = idx;
        } else if (!strcmp(cmd, "else")) {
            if (!sp || p->code[stack[sp - 1]].op != OP_IF) FAIL("'else' without 'if'");
            in->op = OP_ELSE;
            p->code[stack[sp - 1]].target = idx + 1;
            stack[sp - 1] = idx;
        } else if (!strcmp(cmd, "end")) {
            if (!sp) FAIL("'end' without a block");
            in->op = OP_END;
            int top = stack[--sp];
            if (p->code[top].op == OP_REPEAT) { p->code[top].target = idx; in->target = top; }
            else p->code[top].target = idx; /* IF without else, or ELSE */
        } else if (!strcmp(cmd, "repeat")) {
            in->op = OP_REPEAT;
            if (nt < 2 || !parse_arg(tok[1], &in->a[0])) FAIL("'repeat' needs a count");
            if (sp >= MAX_NEST) FAIL("blocks nested too deeply");
            stack[sp++] = idx;
        } else {
            static const struct { const char *name; Op op; int nargs; } ACTIONS[] = {
                {"walk", OP_WALK, 3}, {"turn", OP_TURN, 1}, {"mouse_look", OP_MOUSE_LOOK, 1},
                {"jump", OP_JUMP, 1}, {"set_var", OP_SET_VAR, 2}, {"change_var", OP_CHANGE_VAR, 2},
                {"set_color", OP_SET_COLOR, 3}, {"set_pos", OP_SET_POS, 3}, {"print", OP_PRINT, 0}
            };
            int found = 0;
            for (size_t k = 0; k < sizeof ACTIONS / sizeof ACTIONS[0]; k++) {
                if (strcmp(cmd, ACTIONS[k].name)) continue;
                found = 1; in->op = ACTIONS[k].op;
                if (in->op == OP_PRINT) {
                    const char *t = raw + 5; while (isspace((unsigned char)*t)) t++;
                    strncpy(in->text, t, sizeof in->text - 1);
                } else if (in->op == OP_SET_VAR || in->op == OP_CHANGE_VAR) {
                    if (nt < 3) FAIL("variable blocks need a name and a value");
                    Arg tmp; in->a[0].var = -1; in->a[0].val = (float)var_intern(tok[1]);
                    if (!parse_arg(tok[2], &tmp)) FAIL("bad number");
                    in->a[1] = tmp;
                } else {
                    if (nt - 1 < ACTIONS[k].nargs) FAIL("missing arguments");
                    for (int j = 0; j < ACTIONS[k].nargs; j++)
                        if (!parse_arg(tok[1 + j], &in->a[j])) FAIL("bad number or $variable");
                }
                break;
            }
            if (!found) FAIL("unknown block");
        }
        p->n++;
    }
    if (sp) { VLOG("%s: unclosed block at end of file", path); return 0; }
    if (in_handler) p->h[p->nh - 1].end = p->n;
    return 1;
}

int blocks_load(const char *name) {
    for (int i = 0; i < program_count; i++) if (!strcmp(programs[i].name, name)) return i;
    if (program_count >= MAX_PROGRAMS) { VLOG("too many scripts"); return -1; }
    char path[128]; snprintf(path, sizeof path, "scripts/%s.vblocks", name);
    FILE *f = fopen(path, "r");
    if (!f) { VLOG("script not found: %s", path); return -1; }
    Program *p = &programs[program_count];
    memset(p, 0, sizeof *p); strncpy(p->name, name, sizeof p->name - 1);
    int ok = compile(p, f, path);
    fclose(f);
    if (!ok) return -1;
    return program_count++;
}

void blocks_bind_scene(Scene *s) {
    for (int i = 0; i < s->count; i++)
        s->e[i].script_id = s->e[i].script[0] ? blocks_load(s->e[i].script) : -1;
}

typedef struct { Entity *self; const Input *in; float dt; } Ctx;

static int eval_cond(const Instr *in, const Ctx *c) {
    int r = 0;
    switch (in->cond) {
    case C_KEY_DOWN: r = c->in->down[in->key]; break;
    case C_KEY_PRESSED: r = c->in->pressed[in->key]; break;
    case C_GROUNDED: r = c->self->grounded; break;
    case C_GT: r = av(&in->a[0]) > av(&in->a[1]); break;
    case C_LT: r = av(&in->a[0]) < av(&in->a[1]); break;
    case C_EQ: r = fabsf(av(&in->a[0]) - av(&in->a[1])) < 1e-6f; break;
    }
    return in->neg ? !r : r;
}

static void exec(const Program *p, int begin, int end, Ctx *c) {
    int counters[MAX_INSTR]; memset(counters, 0xFF, sizeof counters); /* all -1 */
    int pc = begin, steps = 0;
    Entity *e = c->self;
    while (pc < end && steps++ < MAX_STEPS) {
        const Instr *in = &p->code[pc];
        switch (in->op) {
        case OP_IF: pc = eval_cond(in, c) ? pc + 1 : in->target; continue;
        case OP_ELSE: pc = in->target; continue;
        case OP_END: pc = in->target >= 0 ? in->target : pc + 1; continue;
        case OP_REPEAT:
            if (counters[pc] < 0) counters[pc] = (int)av(&in->a[0]);
            if (counters[pc] > 0) { counters[pc]--; pc++; }
            else { counters[pc] = -1; pc = in->target + 1; }
            continue;
        case OP_WALK: {
            float yaw = DEG2RAD(e->rot.y), sp = av(&in->a[2]);
            Vec3 fwd = v3(-sinf(yaw), 0, -cosf(yaw)), right = v3(cosf(yaw), 0, -sinf(yaw));
            Vec3 d = v3_add(v3_mul(fwd, av(&in->a[0])), v3_mul(right, av(&in->a[1])));
            e->input_vel = v3_add(e->input_vel, v3_mul(d, sp));
            if (fabsf(sp) > e->input_max) e->input_max = fabsf(sp);
            break; }
        case OP_TURN: e->rot.y += av(&in->a[0]) * c->dt; break;
        case OP_MOUSE_LOOK: e->rot.y -= c->in->mouse_dx * av(&in->a[0]); break;
        case OP_JUMP: if (e->grounded) { e->vel.y = av(&in->a[0]); e->grounded = 0; } break;
        case OP_SET_VAR: var_vals[(int)in->a[0].val] = av(&in->a[1]); break;
        case OP_CHANGE_VAR: var_vals[(int)in->a[0].val] += av(&in->a[1]); break;
        case OP_PRINT: VLOG("%s: %s", e->name, in->text); break;
        case OP_SET_COLOR: e->color = v3(av(&in->a[0]), av(&in->a[1]), av(&in->a[2])); break;
        case OP_SET_POS: e->pos = v3(av(&in->a[0]), av(&in->a[1]), av(&in->a[2])); e->vel = v3(0, 0, 0); break;
        }
        pc++;
    }
    if (steps >= MAX_STEPS) VLOG("%s: script step limit hit (infinite loop?)", e->name);
}

void blocks_update_scene(Scene *s, const Input *in, float dt, int event) {
    for (int i = 0; i < s->count; i++) {
        Entity *e = &s->e[i];
        if (!e->active || e->script_id < 0) continue;
        const Program *p = &programs[e->script_id];
        Ctx c = {e, in, dt};
        for (int h = 0; h < p->nh; h++) {
            const Handler *H = &p->h[h];
            int run = (event == BLOCKS_EV_START && H->kind == H_START) ||
                      (event == BLOCKS_EV_UPDATE && H->kind == H_UPDATE) ||
                      (event == BLOCKS_EV_KEYS && H->kind == H_KEY && in->pressed[H->key]);
            if (run) exec(p, H->begin, H->end, &c);
        }
    }
}
