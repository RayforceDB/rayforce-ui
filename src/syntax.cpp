// src/syntax.cpp
// Single-pass Rayfall tokenizer for syntax highlighting

#include <string.h>
#include <ctype.h>
#include "imgui.h"
#include "../include/rfui/syntax.h"

// Keyword table (control flow / binding)
static const char* keywords[] = {
    "fn", "set", "let", "select", "from", "where", "by", "if", "do",
    "while", "each", "total", "asc", "desc", "xasc", "xdesc", "update",
    "delete", "insert", "exec", "load", "save", NULL
};

// Builtin function table
static const char* builtins[] = {
    "widget", "draw", "timer", "hopen", "hclose", "write", "read",
    "count", "sum", "avg", "min", "max", "first", "last", "type",
    "string", "int", "float", "til", "show", "tables", "cols",
    "meta", "key", "value", "enlist", "raze", "flip", "group", NULL
};

static bool is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '?' || c == '!';
}

static bool match_table(const char* word, int len, const char** table) {
    for (int i = 0; table[i]; i++) {
        if ((int)strlen(table[i]) == len && memcmp(word, table[i], len) == 0)
            return true;
    }
    return false;
}

int rfui_tokenize(const char* text, rfui_token_t* tokens, int max_tokens) {
    if (!text || !tokens || max_tokens <= 0) return 0;

    int count = 0;
    int i = 0;
    int len = (int)strlen(text);

    while (i < len && count < max_tokens) {
        // Skip whitespace
        if (isspace((unsigned char)text[i])) { i++; continue; }

        rfui_token_t tok;
        tok.start = i;

        // 1. Comment: ; to EOL
        if (text[i] == ';') {
            while (i < len && text[i] != '\n') i++;
            tok.type = TOK_COMMENT;
            tok.len = i - tok.start;
            tokens[count++] = tok;
            continue;
        }

        // 2. String: "..." with \" escape
        if (text[i] == '"') {
            i++; // skip opening "
            while (i < len && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < len) i++; // skip escaped char
                i++;
            }
            if (i < len) i++; // skip closing "
            tok.type = TOK_STRING;
            tok.len = i - tok.start;
            tokens[count++] = tok;
            continue;
        }

        // 3. Quoted symbol: 'word
        if (text[i] == '\'' && i + 1 < len && (isalpha((unsigned char)text[i + 1]) || text[i + 1] == '_')) {
            i++; // skip '
            while (i < len && is_word_char(text[i])) i++;
            tok.type = TOK_SYMBOL;
            tok.len = i - tok.start;
            tokens[count++] = tok;
            continue;
        }

        // 4. Number: digit, or - followed by digit
        if (isdigit((unsigned char)text[i]) ||
            (text[i] == '-' && i + 1 < len && isdigit((unsigned char)text[i + 1]))) {
            if (text[i] == '-') i++;
            // hex
            if (text[i] == '0' && i + 1 < len && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                i += 2;
                while (i < len && isxdigit((unsigned char)text[i])) i++;
            } else {
                while (i < len && isdigit((unsigned char)text[i])) i++;
                if (i < len && text[i] == '.') {
                    i++;
                    while (i < len && isdigit((unsigned char)text[i])) i++;
                }
            }
            // optional suffix (e.g. f, i, j)
            if (i < len && isalpha((unsigned char)text[i])) i++;
            tok.type = TOK_NUMBER;
            tok.len = i - tok.start;
            tokens[count++] = tok;
            continue;
        }

        // 5. Parens/brackets/colon
        if (text[i] == '(' || text[i] == ')' || text[i] == '[' || text[i] == ']' ||
            text[i] == '{' || text[i] == '}' || text[i] == ':') {
            tok.type = TOK_PAREN;
            tok.len = 1;
            i++;
            tokens[count++] = tok;
            continue;
        }

        // 6. Word (identifier/keyword/builtin)
        if (isalpha((unsigned char)text[i]) || text[i] == '_') {
            while (i < len && is_word_char(text[i])) i++;
            int wlen = i - tok.start;
            if (match_table(text + tok.start, wlen, keywords))
                tok.type = TOK_KEYWORD;
            else if (match_table(text + tok.start, wlen, builtins))
                tok.type = TOK_BUILTIN;
            else
                tok.type = TOK_DEFAULT;
            tok.len = wlen;
            tokens[count++] = tok;
            continue;
        }

        // 7. Operator / other single char
        tok.type = TOK_DEFAULT;
        tok.len = 1;
        i++;
        tokens[count++] = tok;
    }

    return count;
}

const char** rfui_get_keywords(void) { return (const char**)keywords; }
const char** rfui_get_builtins(void) { return (const char**)builtins; }

ImVec4 rfui_token_color(rfui_tok_type_t type) {
    switch (type) {
        case TOK_COMMENT: return ImVec4(0.545f, 0.580f, 0.620f, 1.0f); // #8B949E
        case TOK_STRING:  return ImVec4(0.824f, 0.600f, 0.133f, 1.0f); // #D29922
        case TOK_NUMBER:  return ImVec4(0.224f, 0.824f, 0.753f, 1.0f); // #39D2C0
        case TOK_KEYWORD: return ImVec4(0.737f, 0.549f, 1.000f, 1.0f); // #BC8CFF
        case TOK_SYMBOL:  return ImVec4(0.247f, 0.725f, 0.314f, 1.0f); // #3FB950
        case TOK_PAREN:   return ImVec4(0.545f, 0.580f, 0.620f, 1.0f); // #8B949E
        case TOK_BUILTIN: return ImVec4(0.345f, 0.651f, 1.000f, 1.0f); // #58A6FF
        default:          return ImVec4(0.902f, 0.929f, 0.953f, 1.0f); // #E6EDF3
    }
}
