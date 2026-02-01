// include/rfui/syntax.h
// Rayfall syntax highlighting — token types and tokenizer API
#ifndef RFUI_SYNTAX_H
#define RFUI_SYNTAX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TOK_DEFAULT,
    TOK_COMMENT,
    TOK_STRING,
    TOK_NUMBER,
    TOK_KEYWORD,
    TOK_SYMBOL,     // quoted 'sym
    TOK_PAREN,
    TOK_BUILTIN,
} rfui_tok_type_t;

typedef struct {
    rfui_tok_type_t type;
    int start;  // byte offset into text
    int len;
} rfui_token_t;

// Tokenize a Rayfall expression. Returns number of tokens written.
int rfui_tokenize(const char* text, rfui_token_t* tokens, int max_tokens);

// Get NULL-terminated arrays of completion candidates
const char** rfui_get_keywords(void);
const char** rfui_get_builtins(void);

#ifdef __cplusplus
}

// C++ only — ImGui color for a token type
struct ImVec4;
ImVec4 rfui_token_color(rfui_tok_type_t type);
#endif

#endif // RFUI_SYNTAX_H
