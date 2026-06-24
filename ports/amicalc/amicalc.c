#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

#define WIN_W 300
#define WIN_H 200

#define DISP_X 10
#define DISP_Y 8
#define DISP_W (WIN_W - (2 * DISP_X))
#define DISP_H 20

#define BTN_W 40
#define BTN_H 20
#define BTN_X_SP 5
#define BTN_Y_SP 5
#define GAP_X 10

#define FUNC_COLS 2
#define FUNC_X 10
#define FUNC_Y 40
#define KEY_X (FUNC_X + (FUNC_COLS * BTN_W) + ((FUNC_COLS - 1) * BTN_X_SP) + GAP_X)
#define KEY_Y FUNC_Y

#define MAX_ENTRY 64
#define MAX_EXPR 256
#define EXPR_TMP (MAX_EXPR + 64)
#define MAX_PAREN_DEPTH 8

#define CONST_PI 3.141592653589793
#define CONST_E 2.718281828459045
#define ROOT_W 12
#define ROOT_H 8

#define MENU_CONST 0
#define MENU_MODE 1
#define MENU_VIEW 2
#define ITEM_PI 0
#define ITEM_E 1
#define ITEM_RAD 0
#define ITEM_DEG 1
#define ITEM_EXPR 0

#define ANGLE_RAD 0
#define ANGLE_DEG 1

struct Button {
    const char *label;
    const char *alt_label;
    char action;
    int group;
    int row;
    int col;
};

static const struct Button buttons[] = {
    {"sin", "asin", 'N', 0, 0, 0}, {"cos", "acos", 'O', 0, 0, 1},
    {"tan", "atan", 'T', 0, 1, 0}, {"ln", "exp", 'L', 0, 1, 1},
    {"log", "10^x", 'G', 0, 2, 0}, {"sqrt", "x^2", 'Q', 0, 2, 1},
    {"x^y", NULL, 'P', 0, 3, 0}, {"e^x", "ln", 'X', 0, 3, 1},
    {"Inv", "Inv*", 'I', 0, 4, 0}, {"Exp", NULL, 'E', 0, 4, 1},
    {"(", NULL, '(', 0, 5, 0}, {")", NULL, ')', 0, 5, 1},

    {"7", NULL, '7', 1, 0, 0}, {"8", NULL, '8', 1, 0, 1}, {"9", NULL, '9', 1, 0, 2}, {"/", NULL, '/', 1, 0, 3},
    {"4", NULL, '4', 1, 1, 0}, {"5", NULL, '5', 1, 1, 1}, {"6", NULL, '6', 1, 1, 2}, {"*", NULL, '*', 1, 1, 3},
    {"1", NULL, '1', 1, 2, 0}, {"2", NULL, '2', 1, 2, 1}, {"3", NULL, '3', 1, 2, 2}, {"-", NULL, '-', 1, 2, 3},
    {"0", NULL, '0', 1, 3, 0}, {".", NULL, '.', 1, 3, 1}, {"+/-", NULL, 'S', 1, 3, 2}, {"+", NULL, '+', 1, 3, 3},
    {"C", NULL, 'C', 1, 4, 0}, {"%", NULL, '%', 1, 4, 1}, {"n!", NULL, 'F', 1, 4, 2}, {"=", NULL, '=', 1, 4, 3},
    {"<-", NULL, 'B', 1, 5, 0}
};

static const char MENU_TITLE[] = "Constantes";
static const char MENU_PI_LABEL[] = "PI";
static const char MENU_E_LABEL[] = "E";
static const char MENU_MODE_TITLE[] = "Modo";
static const char MENU_RAD_LABEL[] = "RAD";
static const char MENU_DEG_LABEL[] = "GRA";
static const char MENU_VIEW_TITLE[] = "Vista";
static const char MENU_EXPR_LABEL[] = "Expresion";

static struct Menu menu_constants;
static struct Menu menu_mode;
static struct Menu menu_view;
static struct MenuItem menu_item_pi;
static struct MenuItem menu_item_e;
static struct MenuItem menu_item_rad;
static struct MenuItem menu_item_deg;
static struct MenuItem menu_item_expr;
static struct IntuiText menu_text_pi;
static struct IntuiText menu_text_e;
static struct IntuiText menu_text_rad;
static struct IntuiText menu_text_deg;
static struct IntuiText menu_text_expr;

struct CalcState {
    char entry[MAX_ENTRY + 1];
    int entry_len;
    double accum;
    int accum_set;
    char op;
    int error;
    int just_result;
    int inv;
    int angle_mode;
    int paren_depth;
    double paren_accum[MAX_PAREN_DEPTH];
    int paren_accum_set[MAX_PAREN_DEPTH];
    char paren_op[MAX_PAREN_DEPTH];
    char expr[MAX_EXPR + 1];
    int expr_len;
    int expr_entry_start;
    int show_expr;
};

static void clear_state(struct CalcState *state)
{
    state->entry[0] = '\0';
    state->entry_len = 0;
    state->accum = 0.0;
    state->accum_set = 0;
    state->op = 0;
    state->error = 0;
    state->just_result = 0;
    state->paren_depth = 0;
    state->expr[0] = '\0';
    state->expr_len = 0;
    state->expr_entry_start = -1;
}

static void expr_reset(struct CalcState *state)
{
    state->expr[0] = '\0';
    state->expr_len = 0;
    state->expr_entry_start = -1;
}

static void expr_set(struct CalcState *state, const char *text)
{
    int len = (int)strlen(text);

    if (len > MAX_EXPR) {
        text += len - MAX_EXPR;
        len = MAX_EXPR;
    }
    memcpy(state->expr, text, (size_t)len);
    state->expr[len] = '\0';
    state->expr_len = len;
    state->expr_entry_start = (len > 0) ? 0 : -1;
}

static void expr_append_text(struct CalcState *state, const char *text)
{
    int add = (int)strlen(text);
    int overflow;

    if (add <= 0) {
        return;
    }
    if (add > MAX_EXPR) {
        text += add - MAX_EXPR;
        add = MAX_EXPR;
    }
    if (state->expr_len + add > MAX_EXPR) {
        overflow = state->expr_len + add - MAX_EXPR;
        if (overflow >= state->expr_len) {
            state->expr_len = 0;
        } else {
            memmove(state->expr, state->expr + overflow,
                    (size_t)(state->expr_len - overflow));
            state->expr_len -= overflow;
        }
        state->expr_entry_start = -1;
    }
    memcpy(state->expr + state->expr_len, text, (size_t)add);
    state->expr_len += add;
    state->expr[state->expr_len] = '\0';
}

static void expr_append_char(struct CalcState *state, char ch)
{
    char buf[2];

    buf[0] = ch;
    buf[1] = '\0';
    expr_append_text(state, buf);
}

static void expr_update_entry(struct CalcState *state)
{
    int prefix_len;
    int overflow;

    if (state->entry_len <= 0) {
        return;
    }
    if (state->expr_entry_start < 0) {
        prefix_len = state->expr_len;
        expr_append_text(state, state->entry);
        state->expr_entry_start = prefix_len;
        return;
    }
    prefix_len = state->expr_entry_start;
    if (prefix_len > state->expr_len) {
        prefix_len = state->expr_len;
    }
    if (prefix_len + state->entry_len > MAX_EXPR) {
        overflow = (prefix_len + state->entry_len) - MAX_EXPR;
        if (overflow >= prefix_len) {
            prefix_len = 0;
        } else {
            memmove(state->expr, state->expr + overflow,
                    (size_t)(prefix_len - overflow));
            prefix_len -= overflow;
        }
        state->expr_entry_start = prefix_len;
    }
    memcpy(state->expr + prefix_len, state->entry, (size_t)state->entry_len);
    state->expr_len = prefix_len + state->entry_len;
    state->expr[state->expr_len] = '\0';
}

static int expr_is_operator(char ch)
{
    return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^' || ch == 'r';
}

static int expr_find_last_value_span(const struct CalcState *state, int *start, int *end)
{
    int i;

    if (state->expr_len <= 0) {
        return 0;
    }
    i = state->expr_len - 1;
    if (state->expr[i] == ')') {
        int depth = 1;

        i--;
        while (i >= 0) {
            char c = state->expr[i];
            if (c == ')') {
                depth++;
            } else if (c == '(') {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
            i--;
        }
        if (i < 0) {
            return 0;
        }
        *end = state->expr_len;
        i--;
        while (i >= 0) {
            char c = state->expr[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '^') {
                i--;
            } else {
                break;
            }
        }
        *start = i + 1;
        return 1;
    }
    i = state->expr_len - 1;
    while (i >= 0) {
        char c = state->expr[i];
        if (c == '+' || c == '*' || c == '/' || c == '^' || c == 'r' || c == '(' || c == ')') {
            break;
        }
        if (c == '-') {
            if (i == 0) {
                i--;
                break;
            }
            if (state->expr[i - 1] == 'e' || state->expr[i - 1] == 'E') {
                i--;
                continue;
            }
            if (state->expr[i - 1] == '+' || state->expr[i - 1] == '-' ||
                state->expr[i - 1] == '*' || state->expr[i - 1] == '/' ||
                state->expr[i - 1] == '^' || state->expr[i - 1] == 'r' ||
                state->expr[i - 1] == '(') {
                i--;
            }
            break;
        }
        i--;
    }
    *start = i + 1;
    *end = state->expr_len;
    return (*start < *end);
}

static void expr_wrap_span(struct CalcState *state, int start, int end,
                           const char *prefix, const char *suffix)
{
    char temp[EXPR_TMP];
    int len = 0;
    int prefix_len = (int)strlen(prefix);
    int suffix_len = (int)strlen(suffix);

    if (start < 0) {
        start = 0;
    }
    if (end > state->expr_len) {
        end = state->expr_len;
    }
    if (start > 0) {
        memcpy(temp + len, state->expr, (size_t)start);
        len += start;
    }
    if (prefix_len > 0) {
        memcpy(temp + len, prefix, (size_t)prefix_len);
        len += prefix_len;
    }
    if (end > start) {
        memcpy(temp + len, state->expr + start, (size_t)(end - start));
        len += end - start;
    }
    if (suffix_len > 0) {
        memcpy(temp + len, suffix, (size_t)suffix_len);
        len += suffix_len;
    }
    if (state->expr_len > end) {
        memcpy(temp + len, state->expr + end, (size_t)(state->expr_len - end));
        len += state->expr_len - end;
    }
    if (len > MAX_EXPR) {
        int skip = len - MAX_EXPR;
        memmove(temp, temp + skip, (size_t)(len - skip));
        len -= skip;
    }
    memcpy(state->expr, temp, (size_t)len);
    state->expr[len] = '\0';
    state->expr_len = len;
    state->expr_entry_start = -1;
}

static void expr_wrap_last_value(struct CalcState *state, const char *prefix, const char *suffix)
{
    int start;
    int end;

    if (!expr_find_last_value_span(state, &start, &end)) {
        return;
    }
    expr_wrap_span(state, start, end, prefix, suffix);
}

static void expr_add_operator(struct CalcState *state, char op)
{
    if (state->entry_len > 0) {
        if (state->expr_entry_start >= 0 || state->expr_len == 0) {
            expr_update_entry(state);
            state->expr_entry_start = -1;
            expr_append_char(state, op);
            return;
        }
        state->expr_entry_start = -1;
        expr_append_char(state, op);
        return;
    }
    if (state->expr_len == 0) {
        expr_append_text(state, "0");
        expr_append_char(state, op);
        return;
    }
    if (expr_is_operator(state->expr[state->expr_len - 1])) {
        state->expr[state->expr_len - 1] = op;
        return;
    }
    if (state->expr[state->expr_len - 1] == '(') {
        expr_append_text(state, "0");
        expr_append_char(state, op);
        return;
    }
    expr_append_char(state, op);
}

static void expr_add_paren_open(struct CalcState *state, int implicit_mul)
{
    if (state->entry_len > 0 && (state->expr_entry_start >= 0 || state->expr_len == 0)) {
        expr_update_entry(state);
    }
    if (implicit_mul) {
        expr_append_char(state, '*');
    }
    expr_append_char(state, '(');
    state->expr_entry_start = -1;
}

static void expr_add_paren_close(struct CalcState *state)
{
    if (state->entry_len > 0 && (state->expr_entry_start >= 0 || state->expr_len == 0)) {
        expr_update_entry(state);
    }
    expr_append_char(state, ')');
    state->expr_entry_start = -1;
}

static void expr_apply_unary(struct CalcState *state, char action)
{
    const char *prefix = NULL;
    const char *suffix = NULL;
    char value_buf[64];

    if (state->entry_len > 0 && (state->expr_entry_start >= 0 || state->expr_len == 0)) {
        expr_update_entry(state);
    } else if (state->expr_len == 0 && state->accum_set) {
        sprintf(value_buf, "%.15g", state->accum);
        expr_set(state, value_buf);
    }

    switch (action) {
        case 'N':
            prefix = state->inv ? "asin(" : "sin(";
            suffix = ")";
            break;
        case 'O':
            prefix = state->inv ? "acos(" : "cos(";
            suffix = ")";
            break;
        case 'T':
            prefix = state->inv ? "atan(" : "tan(";
            suffix = ")";
            break;
        case 'L':
            prefix = state->inv ? "exp(" : "ln(";
            suffix = ")";
            break;
        case 'G':
            prefix = state->inv ? "10^(" : "log(";
            suffix = ")";
            break;
        case 'X':
            prefix = state->inv ? "ln(" : "e^(";
            suffix = ")";
            break;
        case 'Q':
            prefix = state->inv ? "(" : "sqrt(";
            suffix = state->inv ? ")^2" : ")";
            break;
        case '%':
            prefix = "";
            suffix = "%";
            break;
        case 'F':
            prefix = "";
            suffix = "!";
            break;
        default:
            break;
    }

    if (!prefix || !suffix) {
        return;
    }
    expr_wrap_last_value(state, prefix, suffix);
}
static int compute_op(double lhs, char op, double rhs, double *out)
{
    switch (op) {
        case '+':
            *out = lhs + rhs;
            return 1;
        case '-':
            *out = lhs - rhs;
            return 1;
        case '*':
            *out = lhs * rhs;
            return 1;
        case '/':
            if (rhs == 0.0) {
                return 0;
            }
            *out = lhs / rhs;
            return 1;
        case '^':
            *out = pow(lhs, rhs);
            if (*out != *out) {
                return 0;
            }
            return 1;
        case 'r':
            if (rhs == 0.0) {
                return 0;
            }
            *out = pow(lhs, 1.0 / rhs);
            if (*out != *out) {
                return 0;
            }
            return 1;
        default:
            break;
    }
    return 0;
}

static void handle_digit(struct CalcState *state, char digit)
{
    if (state->error) {
        return;
    }
    if (state->just_result) {
        state->entry_len = 0;
        state->entry[0] = '\0';
        state->just_result = 0;
    }
    if (state->entry_len >= MAX_ENTRY) {
        return;
    }
    state->entry[state->entry_len++] = digit;
    state->entry[state->entry_len] = '\0';
}

static void insert_constant(struct CalcState *state, double value)
{
    if (state->error) {
        return;
    }
    sprintf(state->entry, "%.15g", value);
    state->entry_len = (int)strlen(state->entry);
    state->just_result = 0;
}

static double deg_to_rad(double value)
{
    return value * (CONST_PI / 180.0);
}

static double rad_to_deg(double value)
{
    return value * (180.0 / CONST_PI);
}

static int get_current_value(struct CalcState *state, double *out, int *from_accum)
{
    if (state->entry_len > 0) {
        *out = strtod(state->entry, NULL);
        *from_accum = 0;
        return 1;
    }
    if (state->accum_set && state->op == 0) {
        *out = state->accum;
        *from_accum = 1;
        return 1;
    }
    return 0;
}

static void set_result(struct CalcState *state, double value)
{
    sprintf(state->entry, "%.15g", value);
    state->entry_len = (int)strlen(state->entry);
    state->just_result = 1;
}

static void handle_unary(struct CalcState *state, char action)
{
    double value;
    double result;
    double angle;
    int from_accum = 0;

    if (state->error) {
        return;
    }
    if (!get_current_value(state, &value, &from_accum)) {
        return;
    }

    switch (action) {
        case 'L':
            if (state->inv) {
                result = exp(value);
            } else {
                if (value <= 0.0) {
                    state->error = 1;
                    return;
                }
                result = log(value);
            }
            break;
        case 'G':
            if (state->inv) {
                result = pow(10.0, value);
            } else {
                if (value <= 0.0) {
                    state->error = 1;
                    return;
                }
                result = log(value) / log(10.0);
            }
            break;
        case 'X':
            if (state->inv) {
                if (value <= 0.0) {
                    state->error = 1;
                    return;
                }
                result = log(value);
            } else {
                result = exp(value);
            }
            break;
        case 'Q':
            if (state->inv) {
                result = value * value;
            } else {
                if (value < 0.0) {
                    state->error = 1;
                    return;
                }
                result = sqrt(value);
            }
            break;
        case '%':
            result = value / 100.0;
            break;
        case 'F': {
            long n;
            long i;
            double fact = 1.0;

            if (value < 0.0) {
                state->error = 1;
                return;
            }
            n = (long)value;
            if (value != (double)n) {
                state->error = 1;
                return;
            }
            if (n > 170) {
                state->error = 1;
                return;
            }
            for (i = 2; i <= n; ++i) {
                fact *= (double)i;
            }
            result = fact;
            break;
        }
        case 'N':
            if (state->inv) {
                if (value < -1.0 || value > 1.0) {
                    state->error = 1;
                    return;
                }
                result = asin(value);
                if (state->angle_mode == ANGLE_DEG) {
                    result = rad_to_deg(result);
                }
            } else {
                angle = value;
                if (state->angle_mode == ANGLE_DEG) {
                    angle = deg_to_rad(angle);
                }
                result = sin(angle);
            }
            break;
        case 'O':
            if (state->inv) {
                if (value < -1.0 || value > 1.0) {
                    state->error = 1;
                    return;
                }
                result = acos(value);
                if (state->angle_mode == ANGLE_DEG) {
                    result = rad_to_deg(result);
                }
            } else {
                angle = value;
                if (state->angle_mode == ANGLE_DEG) {
                    angle = deg_to_rad(angle);
                }
                result = cos(angle);
            }
            break;
        case 'T':
            if (state->inv) {
                result = atan(value);
                if (state->angle_mode == ANGLE_DEG) {
                    result = rad_to_deg(result);
                }
            } else {
                angle = value;
                if (state->angle_mode == ANGLE_DEG) {
                    angle = deg_to_rad(angle);
                }
                result = tan(angle);
            }
            break;
        default:
            return;
    }

    if (result != result) {
        state->error = 1;
        return;
    }
    set_result(state, result);
    if (from_accum) {
        state->accum = result;
        state->accum_set = 1;
    }
}

static char *find_exp(char *entry)
{
    char *pos = strchr(entry, 'e');
    if (!pos) {
        pos = strchr(entry, 'E');
    }
    return pos;
}

static int entry_has_exp(const char *entry)
{
    return strchr(entry, 'e') != NULL || strchr(entry, 'E') != NULL;
}

static int entry_has_decimal(const char *entry)
{
    const char *p = entry;

    while (*p != '\0' && *p != 'e' && *p != 'E') {
        if (*p == '.') {
            return 1;
        }
        ++p;
    }
    return 0;
}

static int insert_char(struct CalcState *state, int pos, char ch)
{
    if (pos < 0 || pos > state->entry_len) {
        return 0;
    }
    if (state->entry_len >= MAX_ENTRY) {
        return 0;
    }
    memmove(state->entry + pos + 1, state->entry + pos,
            (size_t)(state->entry_len - pos + 1));
    state->entry[pos] = ch;
    state->entry_len++;
    return 1;
}

static void remove_char(struct CalcState *state, int pos)
{
    if (pos < 0 || pos >= state->entry_len) {
        return;
    }
    memmove(state->entry + pos, state->entry + pos + 1,
            (size_t)(state->entry_len - pos));
    state->entry_len--;
}

static void handle_decimal(struct CalcState *state)
{
    if (state->error) {
        return;
    }
    if (state->just_result) {
        state->entry_len = 0;
        state->entry[0] = '\0';
        state->just_result = 0;
    }
    if (entry_has_exp(state->entry) || entry_has_decimal(state->entry)) {
        return;
    }
    if (state->entry_len == 0) {
        if (MAX_ENTRY < 2) {
            return;
        }
        state->entry[0] = '0';
        state->entry[1] = '.';
        state->entry[2] = '\0';
        state->entry_len = 2;
        return;
    }
    if (state->entry_len == 1 && state->entry[0] == '-') {
        if (MAX_ENTRY < 3) {
            return;
        }
        state->entry[1] = '0';
        state->entry[2] = '.';
        state->entry[3] = '\0';
        state->entry_len = 3;
        return;
    }
    if (state->entry_len >= MAX_ENTRY) {
        return;
    }
    state->entry[state->entry_len++] = '.';
    state->entry[state->entry_len] = '\0';
}

static void handle_exp(struct CalcState *state)
{
    if (state->error) {
        return;
    }
    if (state->just_result) {
        state->just_result = 0;
    }
    if (entry_has_exp(state->entry)) {
        return;
    }
    if (state->entry_len == 0) {
        state->entry[0] = '0';
        state->entry[1] = '\0';
        state->entry_len = 1;
    } else if (state->entry_len == 1 && state->entry[0] == '-') {
        if (state->entry_len < MAX_ENTRY) {
            state->entry[state->entry_len++] = '0';
            state->entry[state->entry_len] = '\0';
        }
    }
    if (state->entry_len >= MAX_ENTRY) {
        return;
    }
    state->entry[state->entry_len++] = 'e';
    state->entry[state->entry_len] = '\0';
}

static void handle_sign(struct CalcState *state)
{
    char *exp_pos;

    if (state->error) {
        return;
    }
    if (state->just_result) {
        state->just_result = 0;
    }
    if (state->entry_len == 0) {
        state->entry[0] = '-';
        state->entry[1] = '\0';
        state->entry_len = 1;
        return;
    }

    exp_pos = find_exp(state->entry);
    if (exp_pos) {
        int sign_idx = (int)(exp_pos - state->entry) + 1;
        if (sign_idx < state->entry_len &&
            (state->entry[sign_idx] == '+' || state->entry[sign_idx] == '-')) {
            if (state->entry[sign_idx] == '-') {
                remove_char(state, sign_idx);
            } else {
                state->entry[sign_idx] = '-';
            }
        } else {
            insert_char(state, sign_idx, '-');
        }
        return;
    }

    if (state->entry[0] == '-') {
        remove_char(state, 0);
    } else {
        insert_char(state, 0, '-');
    }
}

static void handle_backspace(struct CalcState *state)
{
    if (state->error) {
        return;
    }
    if (state->entry_len <= 0) {
        return;
    }
    state->entry_len--;
    state->entry[state->entry_len] = '\0';
    state->just_result = 0;
    if (state->expr_entry_start >= 0 || state->expr_len == 0) {
        if (state->entry_len > 0) {
            expr_update_entry(state);
        } else if (state->expr_entry_start >= 0) {
            if (state->expr_entry_start > state->expr_len) {
                state->expr_entry_start = state->expr_len;
            }
            state->expr_len = state->expr_entry_start;
            state->expr[state->expr_len] = '\0';
            state->expr_entry_start = -1;
        }
    }
}

static void handle_operator(struct CalcState *state, char op)
{
    double value;

    if (state->error) {
        return;
    }
    if (!state->accum_set && state->entry_len == 0) {
        state->accum = 0.0;
        state->accum_set = 1;
    }
    if (state->entry_len > 0) {
        value = strtod(state->entry, NULL);
        if (!state->accum_set) {
            state->accum = value;
            state->accum_set = 1;
        } else if (state->op != 0) {
            if (!compute_op(state->accum, state->op, value, &state->accum)) {
                state->error = 1;
                return;
            }
        } else {
            state->accum = value;
        }
        state->entry_len = 0;
        state->entry[0] = '\0';
    }
    state->op = op;
    state->just_result = 0;
}

static int eval_pending(const struct CalcState *state, double *out)
{
    double value;

    if (state->entry_len > 0) {
        value = strtod(state->entry, NULL);
    } else if (state->accum_set) {
        value = state->accum;
    } else {
        return 0;
    }

    if (state->op != 0 && state->accum_set) {
        if (!compute_op(state->accum, state->op, value, out)) {
            return 0;
        }
    } else {
        *out = value;
    }
    return 1;
}

static void handle_paren_open(struct CalcState *state)
{
    if (state->error) {
        return;
    }
    if (state->paren_depth >= MAX_PAREN_DEPTH) {
        state->error = 1;
        return;
    }

    if (state->entry_len > 0 && state->op == 0 && !state->accum_set) {
        state->accum = strtod(state->entry, NULL);
        state->accum_set = 1;
        state->op = '*';
        state->entry_len = 0;
        state->entry[0] = '\0';
    }

    state->paren_accum[state->paren_depth] = state->accum;
    state->paren_accum_set[state->paren_depth] = state->accum_set;
    state->paren_op[state->paren_depth] = state->op;
    state->paren_depth++;

    state->accum = 0.0;
    state->accum_set = 0;
    state->op = 0;
    state->entry_len = 0;
    state->entry[0] = '\0';
    state->just_result = 0;
}

static void handle_paren_close(struct CalcState *state)
{
    double value;

    if (state->error) {
        return;
    }
    if (state->paren_depth == 0) {
        state->error = 1;
        return;
    }
    if (!eval_pending(state, &value)) {
        state->error = 1;
        return;
    }

    state->paren_depth--;
    state->accum = state->paren_accum[state->paren_depth];
    state->accum_set = state->paren_accum_set[state->paren_depth];
    state->op = state->paren_op[state->paren_depth];

    set_result(state, value);
}

static void handle_equals(struct CalcState *state)
{
    double value;

    if (state->error) {
        return;
    }
    if (state->paren_depth > 0) {
        state->error = 1;
        return;
    }
    if (!state->accum_set && state->entry_len == 0) {
        state->entry_len = 1;
        state->entry[0] = '0';
        state->entry[1] = '\0';
        state->just_result = 1;
        return;
    }

    if (state->entry_len > 0) {
        value = strtod(state->entry, NULL);
    } else if (state->accum_set) {
        value = state->accum;
    } else {
        value = 0.0;
    }

    if (state->op != 0) {
        if (!state->accum_set) {
            state->accum = 0.0;
            state->accum_set = 1;
        }
        if (!compute_op(state->accum, state->op, value, &state->accum)) {
            state->error = 1;
            return;
        }
    } else {
        state->accum = value;
        state->accum_set = 1;
    }

    sprintf(state->entry, "%.15g", state->accum);
    state->entry_len = (int)strlen(state->entry);
    state->op = 0;
    state->just_result = 1;
}

static void handle_action(struct CalcState *state, char action)
{
    if (action >= '0' && action <= '9') {
        if (state->just_result) {
            expr_reset(state);
        }
        handle_digit(state, action);
        if (!state->error) {
            expr_update_entry(state);
        }
        return;
    }
    switch (action) {
        case '+':
        case '-':
        case '*':
        case '/':
            if (!state->error) {
                expr_add_operator(state, action);
            }
            handle_operator(state, action);
            break;
        case 'P':
            if (state->inv) {
                if (!state->error) {
                    expr_add_operator(state, 'r');
                }
                handle_operator(state, 'r');
            } else {
                if (!state->error) {
                    expr_add_operator(state, '^');
                }
                handle_operator(state, '^');
            }
            break;
        case '=':
            if (!state->error && state->entry_len > 0 &&
                (state->expr_entry_start >= 0 || state->expr_len == 0)) {
                expr_update_entry(state);
                state->expr_entry_start = -1;
            }
            handle_equals(state);
            if (!state->error) {
                expr_set(state, state->entry);
            }
            break;
        case '.':
            if (state->just_result) {
                expr_reset(state);
            }
            handle_decimal(state);
            if (!state->error) {
                expr_update_entry(state);
            }
            break;
        case 'E':
            if (state->just_result) {
                expr_set(state, state->entry);
            }
            handle_exp(state);
            if (!state->error && (state->expr_entry_start >= 0 || state->expr_len == 0)) {
                expr_update_entry(state);
            }
            break;
        case 'S':
            if (state->just_result) {
                expr_set(state, state->entry);
            }
            handle_sign(state);
            if (!state->error && (state->expr_entry_start >= 0 || state->expr_len == 0)) {
                expr_update_entry(state);
            }
            break;
        case 'B':
            handle_backspace(state);
            break;
        case '(':
            if (!state->error) {
                int implicit_mul = (state->entry_len > 0 && state->op == 0 && !state->accum_set);
                expr_add_paren_open(state, implicit_mul);
            }
            handle_paren_open(state);
            break;
        case ')':
            if (!state->error) {
                expr_add_paren_close(state);
            }
            handle_paren_close(state);
            break;
        case 'I':
            state->inv = !state->inv;
            break;
        case 'L':
        case 'G':
        case 'X':
        case 'Q':
        case '%':
        case 'F':
        case 'N':
        case 'O':
        case 'T':
            if (!state->error) {
                expr_apply_unary(state, action);
            }
            handle_unary(state, action);
            break;
        case 'C':
            clear_state(state);
            break;
        default:
            break;
    }
}

static void get_display_value(const struct CalcState *state, char *out)
{
    if (state->error) {
        strcpy(out, "ERR");
        return;
    }
    if (state->entry_len > 0) {
        strcpy(out, state->entry);
        return;
    }
    if (state->accum_set) {
        sprintf(out, "%.15g", state->accum);
        return;
    }
    strcpy(out, "0");
}

static int button_left(const struct Button *btn)
{
    int base = (btn->group == 0) ? FUNC_X : KEY_X;
    return base + btn->col * (BTN_W + BTN_X_SP);
}

static int button_top(const struct Button *btn)
{
    int base = (btn->group == 0) ? FUNC_Y : KEY_Y;
    return base + btn->row * (BTN_H + BTN_Y_SP);
}

static int content_left(const struct Window *win)
{
    if (win->Flags & GIMMEZEROZERO) {
        return 0;
    }
    return win->BorderLeft;
}

static int content_top(const struct Window *win)
{
    if (win->Flags & GIMMEZEROZERO) {
        return 0;
    }
    return win->BorderTop;
}

static int content_width(const struct Window *win)
{
    if (win->Flags & GIMMEZEROZERO) {
        if (win->GZZWidth > 0) {
            return win->GZZWidth;
        }
    }
    return win->Width - win->BorderLeft - win->BorderRight;
}

static int content_height(const struct Window *win)
{
    if (win->Flags & GIMMEZEROZERO) {
        if (win->GZZHeight > 0) {
            return win->GZZHeight;
        }
    }
    return win->Height - win->BorderTop - win->BorderBottom;
}

static int content_right(const struct Window *win)
{
    return content_left(win) + content_width(win) - 1;
}

static int content_bottom(const struct Window *win)
{
    return content_top(win) + content_height(win) - 1;
}

static const char *button_label(const struct Button *btn, const struct CalcState *state)
{
    if (state->inv) {
        if (btn->action == 'P') {
            return NULL;
        }
        if (btn->alt_label) {
            return btn->alt_label;
        }
    }
    return btn->label;
}

static void draw_root_symbol(struct RastPort *rp, int x, int y)
{
    static const UWORD root_bits[ROOT_H] = {
        0x07F, 0x07F, 0x0C0, 0x180,
        0x300, 0x600, 0xC00, 0x800
    };
    int tx = x + (BTN_W - ROOT_W) / 2;
    int ty = y + (BTN_H - ROOT_H) / 2;
    int row;

    SetDrMd(rp, JAM1);
    SetAPen(rp, 1);
    for (row = 0; row < ROOT_H; ++row) {
        UWORD bits = root_bits[row];
        int col;

        for (col = 0; col < ROOT_W; ++col) {
            if (bits & (1U << (ROOT_W - 1 - col))) {
                WritePixel(rp, tx + col, ty + row);
            }
        }
    }
}

static int message_inner_x(const struct Window *win, const struct IntuiMessage *msg)
{
    if (win->Flags & GIMMEZEROZERO) {
        return win->GZZMouseX;
    }
    return msg->MouseX - win->BorderLeft;
}

static int message_inner_y(const struct Window *win, const struct IntuiMessage *msg)
{
    if (win->Flags & GIMMEZEROZERO) {
        return win->GZZMouseY;
    }
    return msg->MouseY - win->BorderTop;
}

static void set_angle_mode(struct CalcState *state, int mode)
{
    state->angle_mode = mode;
    if (mode == ANGLE_RAD) {
        menu_item_rad.Flags |= CHECKED;
        menu_item_deg.Flags &= ~CHECKED;
    } else {
        menu_item_deg.Flags |= CHECKED;
        menu_item_rad.Flags &= ~CHECKED;
    }
}

static void set_show_expr(struct CalcState *state, int show)
{
    state->show_expr = show;
    if (show) {
        menu_item_expr.Flags |= CHECKED;
    } else {
        menu_item_expr.Flags &= ~CHECKED;
    }
}

static void init_menus(struct Window *win, struct CalcState *state)
{
    struct RastPort *rp = win->RPort;
    int menu_height = rp->TxHeight + 4;
    int menu_width = TextLength(rp, (UBYTE *)MENU_TITLE, (int)strlen(MENU_TITLE)) + 12;
    int mode_width = TextLength(rp, (UBYTE *)MENU_MODE_TITLE, (int)strlen(MENU_MODE_TITLE)) + 12;
    int view_width = TextLength(rp, (UBYTE *)MENU_VIEW_TITLE, (int)strlen(MENU_VIEW_TITLE)) + 12;
    int item_height = rp->TxHeight + 2;
    int pi_width = TextLength(rp, (UBYTE *)MENU_PI_LABEL, (int)strlen(MENU_PI_LABEL));
    int e_width = TextLength(rp, (UBYTE *)MENU_E_LABEL, (int)strlen(MENU_E_LABEL));
    int rad_width = TextLength(rp, (UBYTE *)MENU_RAD_LABEL, (int)strlen(MENU_RAD_LABEL));
    int deg_width = TextLength(rp, (UBYTE *)MENU_DEG_LABEL, (int)strlen(MENU_DEG_LABEL));
    int expr_width = TextLength(rp, (UBYTE *)MENU_EXPR_LABEL, (int)strlen(MENU_EXPR_LABEL));
    int item_width = (pi_width > e_width ? pi_width : e_width) + 12;
    int mode_item_width = (rad_width > deg_width ? rad_width : deg_width) + CHECKWIDTH + 8;
    int view_item_width = expr_width + CHECKWIDTH + 8;

    memset(&menu_constants, 0, sizeof(menu_constants));
    menu_constants.LeftEdge = 0;
    menu_constants.TopEdge = 0;
    menu_constants.Width = menu_width;
    menu_constants.Height = menu_height;
    menu_constants.Flags = MENUENABLED;
    menu_constants.MenuName = (BYTE *)MENU_TITLE;
    menu_constants.FirstItem = &menu_item_pi;
    menu_constants.NextMenu = &menu_mode;

    memset(&menu_item_pi, 0, sizeof(menu_item_pi));
    menu_item_pi.NextItem = &menu_item_e;
    menu_item_pi.LeftEdge = 0;
    menu_item_pi.TopEdge = 0;
    menu_item_pi.Width = item_width;
    menu_item_pi.Height = item_height;
    menu_item_pi.Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    menu_item_pi.ItemFill = (APTR)&menu_text_pi;
    menu_item_pi.SelectFill = NULL;
    menu_item_pi.Command = 0;
    menu_item_pi.SubItem = NULL;
    menu_item_pi.NextSelect = MENUNULL;
    menu_item_pi.MutualExclude = 0;

    memset(&menu_item_e, 0, sizeof(menu_item_e));
    menu_item_e.NextItem = NULL;
    menu_item_e.LeftEdge = 0;
    menu_item_e.TopEdge = item_height;
    menu_item_e.Width = item_width;
    menu_item_e.Height = item_height;
    menu_item_e.Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP;
    menu_item_e.ItemFill = (APTR)&menu_text_e;
    menu_item_e.SelectFill = NULL;
    menu_item_e.Command = 0;
    menu_item_e.SubItem = NULL;
    menu_item_e.NextSelect = MENUNULL;
    menu_item_e.MutualExclude = 0;

    menu_text_pi.FrontPen = 0;
    menu_text_pi.BackPen = 1;
    menu_text_pi.DrawMode = JAM2;
    menu_text_pi.LeftEdge = 2;
    menu_text_pi.TopEdge = 1;
    menu_text_pi.ITextFont = NULL;
    menu_text_pi.IText = (UBYTE *)MENU_PI_LABEL;
    menu_text_pi.NextText = NULL;

    menu_text_e.FrontPen = 0;
    menu_text_e.BackPen = 1;
    menu_text_e.DrawMode = JAM2;
    menu_text_e.LeftEdge = 2;
    menu_text_e.TopEdge = 1;
    menu_text_e.ITextFont = NULL;
    menu_text_e.IText = (UBYTE *)MENU_E_LABEL;
    menu_text_e.NextText = NULL;

    memset(&menu_mode, 0, sizeof(menu_mode));
    menu_mode.LeftEdge = menu_width;
    menu_mode.TopEdge = 0;
    menu_mode.Width = mode_width;
    menu_mode.Height = menu_height;
    menu_mode.Flags = MENUENABLED;
    menu_mode.MenuName = (BYTE *)MENU_MODE_TITLE;
    menu_mode.FirstItem = &menu_item_rad;
    menu_mode.NextMenu = &menu_view;

    memset(&menu_item_rad, 0, sizeof(menu_item_rad));
    menu_item_rad.NextItem = &menu_item_deg;
    menu_item_rad.LeftEdge = 0;
    menu_item_rad.TopEdge = 0;
    menu_item_rad.Width = mode_item_width;
    menu_item_rad.Height = item_height;
    menu_item_rad.Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP | CHECKIT | MENUTOGGLE;
    menu_item_rad.ItemFill = (APTR)&menu_text_rad;
    menu_item_rad.SelectFill = NULL;
    menu_item_rad.Command = 0;
    menu_item_rad.SubItem = NULL;
    menu_item_rad.NextSelect = MENUNULL;
    menu_item_rad.MutualExclude = (1 << ITEM_DEG);

    memset(&menu_item_deg, 0, sizeof(menu_item_deg));
    menu_item_deg.NextItem = NULL;
    menu_item_deg.LeftEdge = 0;
    menu_item_deg.TopEdge = item_height;
    menu_item_deg.Width = mode_item_width;
    menu_item_deg.Height = item_height;
    menu_item_deg.Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP | CHECKIT | MENUTOGGLE;
    menu_item_deg.ItemFill = (APTR)&menu_text_deg;
    menu_item_deg.SelectFill = NULL;
    menu_item_deg.Command = 0;
    menu_item_deg.SubItem = NULL;
    menu_item_deg.NextSelect = MENUNULL;
    menu_item_deg.MutualExclude = (1 << ITEM_RAD);

    menu_text_rad.FrontPen = 0;
    menu_text_rad.BackPen = 1;
    menu_text_rad.DrawMode = JAM2;
    menu_text_rad.LeftEdge = CHECKWIDTH;
    menu_text_rad.TopEdge = 1;
    menu_text_rad.ITextFont = NULL;
    menu_text_rad.IText = (UBYTE *)MENU_RAD_LABEL;
    menu_text_rad.NextText = NULL;

    menu_text_deg.FrontPen = 0;
    menu_text_deg.BackPen = 1;
    menu_text_deg.DrawMode = JAM2;
    menu_text_deg.LeftEdge = CHECKWIDTH;
    menu_text_deg.TopEdge = 1;
    menu_text_deg.ITextFont = NULL;
    menu_text_deg.IText = (UBYTE *)MENU_DEG_LABEL;
    menu_text_deg.NextText = NULL;

    memset(&menu_view, 0, sizeof(menu_view));
    menu_view.LeftEdge = menu_width + mode_width;
    menu_view.TopEdge = 0;
    menu_view.Width = view_width;
    menu_view.Height = menu_height;
    menu_view.Flags = MENUENABLED;
    menu_view.MenuName = (BYTE *)MENU_VIEW_TITLE;
    menu_view.FirstItem = &menu_item_expr;
    menu_view.NextMenu = NULL;

    memset(&menu_item_expr, 0, sizeof(menu_item_expr));
    menu_item_expr.NextItem = NULL;
    menu_item_expr.LeftEdge = 0;
    menu_item_expr.TopEdge = 0;
    menu_item_expr.Width = view_item_width;
    menu_item_expr.Height = item_height;
    menu_item_expr.Flags = ITEMTEXT | ITEMENABLED | HIGHCOMP | CHECKIT | MENUTOGGLE;
    menu_item_expr.ItemFill = (APTR)&menu_text_expr;
    menu_item_expr.SelectFill = NULL;
    menu_item_expr.Command = 0;
    menu_item_expr.SubItem = NULL;
    menu_item_expr.NextSelect = MENUNULL;
    menu_item_expr.MutualExclude = 0;

    menu_text_expr.FrontPen = 0;
    menu_text_expr.BackPen = 1;
    menu_text_expr.DrawMode = JAM2;
    menu_text_expr.LeftEdge = CHECKWIDTH;
    menu_text_expr.TopEdge = 1;
    menu_text_expr.ITextFont = NULL;
    menu_text_expr.IText = (UBYTE *)MENU_EXPR_LABEL;
    menu_text_expr.NextText = NULL;

    set_angle_mode(state, state->angle_mode);
    set_show_expr(state, state->show_expr);
}

static void handle_menu_pick(struct CalcState *state, USHORT code)
{
    while (code != MENUNULL) {
        UWORD menu_num = MENUNUM(code);
        UWORD item_num = ITEMNUM(code);
        struct MenuItem *item;

        if (menu_num == MENU_CONST) {
            if (item_num == ITEM_PI) {
                if (state->just_result) {
                    expr_reset(state);
                }
                insert_constant(state, CONST_PI);
                expr_update_entry(state);
            } else if (item_num == ITEM_E) {
                if (state->just_result) {
                    expr_reset(state);
                }
                insert_constant(state, CONST_E);
                expr_update_entry(state);
            }
        } else if (menu_num == MENU_MODE) {
            if (item_num == ITEM_RAD) {
                set_angle_mode(state, ANGLE_RAD);
            } else if (item_num == ITEM_DEG) {
                set_angle_mode(state, ANGLE_DEG);
            }
        } else if (menu_num == MENU_VIEW) {
            if (item_num == ITEM_EXPR) {
                set_show_expr(state, !state->show_expr);
            }
        }

        item = ItemAddress(&menu_constants, code);
        if (!item) {
            break;
        }
        code = item->NextSelect;
    }
}

static void draw_button(struct RastPort *rp, int x, int y, const char *label, int root_symbol)
{
    SetAPen(rp, 0);
    RectFill(rp, x + 1, y + 1, x + BTN_W - 2, y + BTN_H - 2);

    SetAPen(rp, 1);
    Move(rp, x, y);
    Draw(rp, x + BTN_W - 1, y);
    Draw(rp, x + BTN_W - 1, y + BTN_H - 1);
    Draw(rp, x, y + BTN_H - 1);
    Draw(rp, x, y);

    if (root_symbol) {
        draw_root_symbol(rp, x, y);
    } else if (label) {
        int len = (int)strlen(label);
        int text_w = TextLength(rp, (UBYTE *)label, len);
        int tx = x + (BTN_W - text_w) / 2;
        int ty = y + (BTN_H - rp->TxHeight) / 2 + rp->TxBaseline;

        Move(rp, tx, ty);
        Text(rp, (UBYTE *)label, len);
    }
}

static void draw_display(struct Window *win, const struct CalcState *state)
{
    struct RastPort *rp = win->RPort;
    char buffer[64];
    char left_buf[8];
    int ox = content_left(win) + DISP_X;
    int oy = content_top(win) + DISP_Y;
    int right = ox + DISP_W - 1;
    int bottom = oy + DISP_H - 1;
    int text_y = oy + (DISP_H - rp->TxHeight) / 2 + rp->TxBaseline;
    int len;
    int width;
    int num_x;

    SetDrMd(rp, JAM1);
    SetAPen(rp, 1);
    RectFill(rp, ox, oy, right, bottom);

    SetAPen(rp, 0);
    Move(rp, ox, oy);
    Draw(rp, right, oy);
    Draw(rp, right, bottom);
    Draw(rp, ox, bottom);
    Draw(rp, ox, oy);

    if (state->show_expr && !state->error && state->expr_len > 0) {
        int text_x = ox + 4;
        int avail = DISP_W - 8;
        int start = 0;
        const char *expr_ptr = state->expr;
        int expr_len = state->expr_len;

        if (state->inv) {
            const char *prefix = "INV ";
            int prefix_len = 4;
            int prefix_w = TextLength(rp, (UBYTE *)prefix, prefix_len);

            Move(rp, text_x, text_y);
            Text(rp, (UBYTE *)prefix, prefix_len);
            text_x += prefix_w;
            avail -= prefix_w;
            if (avail < 0) {
                avail = 0;
            }
        }

        if (avail > 0) {
            while (start < expr_len) {
                int w = TextLength(rp, (UBYTE *)(expr_ptr + start), expr_len - start);
                if (w <= avail) {
                    break;
                }
                start++;
            }
            Move(rp, text_x, text_y);
            Text(rp, (UBYTE *)(expr_ptr + start), expr_len - start);
        }
        return;
    }

    get_display_value(state, buffer);

    left_buf[0] = '\0';
    if (!state->error) {
        if (state->inv) {
            strcpy(left_buf, "INV");
        }
        if (state->op != 0) {
            size_t pos = strlen(left_buf);
            if (pos > 0) {
                left_buf[pos++] = ' ';
            }
            left_buf[pos++] = state->op;
            left_buf[pos] = '\0';
        }
    }

    if (left_buf[0] != '\0') {
        Move(rp, ox + 4, text_y);
        Text(rp, (UBYTE *)left_buf, (int)strlen(left_buf));
    }

    len = (int)strlen(buffer);
    width = TextLength(rp, (UBYTE *)buffer, len);
    num_x = ox + DISP_W - 4 - width;
    if (num_x < ox + 4) {
        num_x = ox + 4;
    }
    Move(rp, num_x, text_y);
    Text(rp, (UBYTE *)buffer, len);
}

static void draw_buttons(struct Window *win, const struct CalcState *state)
{
    struct RastPort *rp = win->RPort;
    size_t i;
    int ox = content_left(win);
    int oy = content_top(win);

    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        const char *label = button_label(&buttons[i], state);
        int root_symbol = (state->inv && buttons[i].action == 'P');
        int x = ox + button_left(&buttons[i]);
        int y = oy + button_top(&buttons[i]);
        draw_button(rp, x, y, label, root_symbol);
    }
}

static void draw_ui(struct Window *win, const struct CalcState *state)
{
    struct RastPort *rp = win->RPort;
    int left = content_left(win);
    int top = content_top(win);
    int right = content_right(win);
    int bottom = content_bottom(win);

    SetDrMd(rp, JAM1);

    SetAPen(rp, 0);
    RectFill(rp, left, top, right, bottom);

    draw_display(win, state);
    draw_buttons(win, state);
}

static const struct Button *find_button(int x, int y)
{
    size_t i;

    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        int bx = button_left(&buttons[i]);
        int by = button_top(&buttons[i]);
        if (x >= bx && x < bx + BTN_W && y >= by && y < by + BTN_H) {
            return &buttons[i];
        }
    }
    return NULL;
}

int main(void)
{
    struct Window *win = NULL;
    struct NewWindow nw;
    struct CalcState state;
    struct IntuiMessage *msg;
    int running = 1;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);
    if (!IntuitionBase) {
        return 0;
    }
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        return 0;
    }

    clear_state(&state);
    state.inv = 0;
    state.angle_mode = ANGLE_RAD;
    state.show_expr = 0;

    memset(&nw, 0, sizeof(nw));
    nw.LeftEdge = 50;
    nw.TopEdge = 50;
    nw.Width = WIN_W;
    nw.Height = WIN_H;
    nw.DetailPen = (UBYTE)-1;
    nw.BlockPen = (UBYTE)-1;
    nw.IDCMPFlags = CLOSEWINDOW | MOUSEBUTTONS | REFRESHWINDOW | MENUPICK;
    nw.Flags = WINDOWDRAG | WINDOWDEPTH | WINDOWCLOSE |
               SMART_REFRESH | ACTIVATE | GIMMEZEROZERO;
    nw.Title = (UBYTE *)"AmiCalc 1.3";
    nw.Type = WBENCHSCREEN;

    win = OpenWindow(&nw);
    if (!win) {
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        return 0;
    }

    init_menus(win, &state);
    SetMenuStrip(win, &menu_constants);

    draw_ui(win, &state);

    while (running) {
        WaitPort(win->UserPort);
        while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code;
            if (cls == REFRESHWINDOW) {
                BeginRefresh(win);
                draw_ui(win, &state);
                EndRefresh(win, TRUE);
                ReplyMsg((struct Message *)msg);
                continue;
            }

            ReplyMsg((struct Message *)msg);

            if (cls == CLOSEWINDOW) {
                running = 0;
            } else if (cls == MENUPICK) {
                handle_menu_pick(&state, (USHORT)msg->Code);
                draw_display(win, &state);
            } else if (cls == MOUSEBUTTONS) {
                if (code == SELECTUP) {
                    int local_x = message_inner_x(win, msg);
                    int local_y = message_inner_y(win, msg);
                    const struct Button *btn = NULL;

                    if (local_x >= 0 && local_y >= 0) {
                        btn = find_button(local_x, local_y);
                    }
                    if (btn) {
                        handle_action(&state, btn->action);
                        draw_display(win, &state);
                        if (btn->action == 'I') {
                            draw_buttons(win, &state);
                        }
                    }
                }
            }
        }
    }

    ClearMenuStrip(win);
    CloseWindow(win);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);

    return 0;
}
