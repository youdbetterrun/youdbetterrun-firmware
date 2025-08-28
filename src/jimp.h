// Prototype of an Immediate Deserialization idea. Expect this API to change a lot.
// Stolen and adapted from https://github.com/tsoding/jim 2890b45
// vim: tabstop=4 shiftwidth=4 autoindent smartindent expandtab

#ifndef JIMP_H_
#define JIMP_H_

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
struct Stream {
};
#endif

// TODO: move all diagnostics reporting outside of the library
//   So the user has more options on how to report things

typedef enum {
    JIMP_INVALID,
    JIMP_EOF,

    // Puncts
    JIMP_OCURLY,
    JIMP_CCURLY,
    JIMP_OBRACKET,
    JIMP_CBRACKET,
    JIMP_COMMA,
    JIMP_COLON,

    // Symbols
    JIMP_TRUE,
    JIMP_FALSE,
    JIMP_NULL,

    // Values
    JIMP_STRING,
    JIMP_NUMBER,
} Jimp_Token;

typedef struct {
    // TODO: Add some fake stream thingie that yields chars one at a time so we
    // can remove all of the switches.
#ifdef ARDUINO
    Stream *stream;
#else
    char const *buffer;
#endif
    int last_char;
    int offset;

    Jimp_Token token;

    char *string;
    size_t string_count;
    size_t string_capacity;
    double number;
    bool boolean;
} Jimp;

// TODO: how do null-s fit into this entire system?

void jimp_begin(Jimp *jimp, Stream &stream);

/// If succeeds puts the freshly parsed boolean into jimp->boolean.
/// Any consequent calls to the jimp_* functions may invalidate jimp->boolean.
bool jimp_bool(Jimp *jimp);

/// If succeeds puts the freshly parsed number into jimp->number.
/// Any consequent calls to the jimp_* functions may invalidate jimp->number.
bool jimp_number(Jimp *jimp);

/// If succeeds puts the freshly parsed string into jimp->string as a NULL-terminated string.
/// Any consequent calls to the jimp_* functions may invalidate jimp->string.
/// strdup it if you don't wanna lose it (memory management is on you at that point).
bool jimp_string(Jimp *jimp);

/// Parses the beginning of the object `{`
bool jimp_object_begin(Jimp *jimp);

/// If succeeds puts the key of the member into jimp->string as a NULL-terminated string.
/// Any consequent calls to the jimp_* functions may invalidate jimp->string.
/// strdup it if you don't wanna lose it (memory management is on you at that point).
bool jimp_object_member(Jimp *jimp);

/// Parses the end of the object `}`
bool jimp_object_end(Jimp *jimp);

/// Reports jimp->string as an unknown member. jimp->string is expected to be populated by
/// jimp_object_member.
void jimp_unknown_member(Jimp *jimp);

/// Parses the beginning of the array `[`
bool jimp_array_begin(Jimp *jimp);

/// Checks whether there is any more items in the array.
bool jimp_array_item(Jimp *jimp);

/// Parses the end of the array `]`
bool jimp_array_end(Jimp *jimp);

bool jimp_skip_array(Jimp *jimp);
bool jimp_skip_object(Jimp *jimp);
bool jimp_skip_any(Jimp *jimp);

/// Prints diagnostic at the current position of the parser.
void jimp_diagf_(int const line, const char *fmt, ...);

#define jimp_diagf(args...) jimp_diagf_(__LINE__, args)

bool jimp_is_null_ahead(Jimp *jimp);
bool jimp_is_bool_ahead(Jimp *jimp);
bool jimp_is_number_ahead(Jimp *jimp);
bool jimp_is_string_ahead(Jimp *jimp);
bool jimp_is_array_ahead(Jimp *jimp);
bool jimp_is_object_ahead(Jimp *jimp);

#endif // JIMP_H_

#ifdef JIMP_IMPLEMENTATION

static bool jimp__expect_token(Jimp *jimp, Jimp_Token token);
static bool jimp__get_and_expect_token(Jimp *jimp, Jimp_Token token);
static const char *jimp__token_kind(Jimp_Token token);
static bool jimp__get_token(Jimp *jimp);
static bool jimp__parse_number(Jimp *jimp);
static void jimp__skip_whitespaces(Jimp *jimp);
static bool jimp__skip_open_container(Jimp *jimp, Jimp_Token openToken, Jimp_Token closeToken);
static void jimp__append_to_string(Jimp *jimp, char x);

static void jimp__append_to_string(Jimp *jimp, char x)
{
    if (jimp->string_count >= jimp->string_capacity) {
        if (jimp->string_capacity == 0) jimp->string_capacity = 1024;
        else jimp->string_capacity *= 2;
        jimp->string = (char*)realloc(jimp->string, jimp->string_capacity);
    }
    jimp->string[jimp->string_count++] = x;
}

// TODO It will just wait forever if we expect more bytes and the stream doesn't
// give tyem.
static int jimp__peek(Jimp *jimp) {
    if (jimp->last_char != -1) {
        return jimp->last_char;
    }
#ifdef ARDUINO
    while (jimp->stream->available() <= 0) {
        delay(1);
    }
    jimp->last_char = jimp->stream->read();
#else
    jimp->last_char = jimp->buffer[++jimp->offset];
#endif
    return jimp->last_char;
}

static int jimp__get(Jimp *jimp) {
    int c = jimp__peek(jimp);
    jimp->last_char = -1;
    return c;
}

static void jimp__skip_whitespaces(Jimp *jimp) {
    int c;
    while ((c = jimp__peek(jimp)) >= 0 && isspace(c)) {
        jimp__get(jimp);
    }
}

static bool jimp__parse_number(Jimp *jimp) {
    // jimp__skip_whitespaces(jimp);
    jimp->string_count = 0;
    int c = jimp__peek(jimp);

    if (-1 == c) return false;

    bool found = false;

    while (isdigit(c) || c == '-' || c == '+' || c == '.') {
        jimp__append_to_string(jimp, (char)c);
        found = true;
        jimp__get(jimp);
        c = jimp__peek(jimp);
    }

    if (found) {
        jimp__append_to_string(jimp, '\0');
        jimp->number = strtod(jimp->string, NULL);
    }

    return found;
}

static Jimp_Token jimp__puncts[256] = {};

static struct {
    Jimp_Token token;
    const char *symbol;
} jimp__symbols[] = {
    { .token = JIMP_TRUE,  .symbol = "true"  },
    { .token = JIMP_FALSE, .symbol = "false" },
    { .token = JIMP_NULL,  .symbol = "null"  },
};
#define jimp__symbols_count (sizeof(jimp__symbols)/sizeof(jimp__symbols[0]))

static bool jimp__get_token(Jimp *jimp)
{
    jimp__skip_whitespaces(jimp);

    int c = jimp__peek(jimp);

    if (-1 == c) return false;

    jimp->token = jimp__puncts[(unsigned char)c];

    if (jimp->token) {
        jimp__get(jimp);
        return true;
    }

    for (size_t i = 0; i < jimp__symbols_count; ++i) {
        const char *symbol = jimp__symbols[i].symbol;
        if (*symbol == (char)c) {
            // TODO: Error
            while (*symbol && *symbol++ == (char)jimp__get(jimp)) {}

            // Check if we looped above until we got nullptr. If not nullptr,
            // symbol mismatch.
            if (*symbol) {
                jimp->token = JIMP_INVALID;
                jimp_diagf("ERROR: invalid symbol\n");
                return false;
            } else {
                jimp->token = jimp__symbols[i].token;
                return true;
            }
        }
    }

    if ('"' == (char)c) {
        jimp__get(jimp);
        jimp->string_count = 0;

        while (true) {
            int c = jimp__get(jimp);
            if (c == -1) {
                jimp->token = JIMP_INVALID;
                jimp_diagf("ERROR: jimp__get invalid %d\n", c);
                return false;
            }
            // TODO: support all the JSON escape sequences defined in the spec
            // Yes, including those dumb suroggate pairs. Spec is spec.
            switch ((char)c) {
            case '\\': {
                jimp__get(jimp);
                if (jimp__peek(jimp) == -1)
                {
                    jimp->token = JIMP_INVALID;
                    jimp_diagf("ERROR: unfinished escape sequence\n");
                    return false;
                }
                switch ((char)jimp__peek(jimp)) {
                case 'r':
                    jimp__get(jimp);
                    jimp__append_to_string(jimp, '\r');
                    break;
                case 'n':
                    jimp__get(jimp);
                    jimp__append_to_string(jimp, '\n');
                    break;
                case 't':
                    jimp__get(jimp);
                    jimp__append_to_string(jimp, '\t');
                    break;
                case '\\':
                    jimp__get(jimp);
                    jimp__append_to_string(jimp, '\\');
                    break;
                case '"':
                    jimp__get(jimp);
                    jimp__append_to_string(jimp, '"');
                    break;
                default:
                    jimp->token = JIMP_INVALID;
                    jimp_diagf("ERROR: invalid escape sequence\n");
                    return false;
                }
                break;
            }
            case '"': {
                jimp__append_to_string(jimp, '\0');
                jimp->token = JIMP_STRING;
                return true;
            }
            default: {
                jimp__append_to_string(jimp, (char)c);
            }
            }
        }
        jimp->token = JIMP_INVALID;
        jimp_diagf("ERROR: unfinished string\n");
        return false;
    }

    if (jimp__parse_number(jimp)) {
        jimp->token = JIMP_NUMBER;
        return true;
    }

    jimp->token = JIMP_INVALID;
    return false;
}

void jimp_begin(Jimp *jimp, Stream &stream)
{
    // Initialize here for c++ support (c++ doesn't have array designated
    // initializers)
    jimp__puncts['{'] = JIMP_OCURLY,
    jimp__puncts['}'] = JIMP_CCURLY,
    jimp__puncts['['] = JIMP_OBRACKET,
    jimp__puncts[']'] = JIMP_CBRACKET,
    jimp__puncts[','] = JIMP_COMMA,
    jimp__puncts[':'] = JIMP_COLON,

#ifdef ARDUINO
    jimp->stream = &stream;
#else
    jimp->buffer = data;
    jimp->offset = -1;
#endif
    jimp->last_char = -1;
}

void jimp_diagf_(int const line, const char *fmt, ...)
{
    char buf[256]; // pick a size that fits your diagnostics
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

#ifdef ARDUINO
    Serial.print(buf);
#else
    printf("%s", buf);
#endif
}

static const char *jimp__token_kind(Jimp_Token token)
{
   switch (token) {
   case JIMP_EOF:      return "end of input";
   case JIMP_INVALID:  return "invalid";
   case JIMP_OCURLY:   return "{";
   case JIMP_CCURLY:   return "}";
   case JIMP_OBRACKET: return "[";
   case JIMP_CBRACKET: return "]";
   case JIMP_COMMA:    return ",";
   case JIMP_COLON:    return ":";
   case JIMP_TRUE:     return "true";
   case JIMP_FALSE:    return "false";
   case JIMP_NULL:     return "null";
   case JIMP_STRING:   return "string";
   case JIMP_NUMBER:   return "number";
   }
   assert(0 && "unreachable");
   return NULL;
}

bool jimp_array_begin(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_OBRACKET);
}

bool jimp_array_end(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_CBRACKET);
}

bool jimp_array_item(Jimp *jimp)
{
    jimp__skip_whitespaces(jimp);
    int c = jimp__peek(jimp);

    if (c == -1) {
        jimp->token = JIMP_INVALID;
        jimp_diagf("ERROR: peeking jimp array\n");
        return false;
    }
    
    switch ((char)c) {
        case ',': {
            jimp__get(jimp);
            return true;
        }
        case ']': {
            // DONT consume it
            return false;
        }
        default: {
            // First element?
            return true;
        }
    }

   assert(0 && "unreachable");
   return false;
}

void jimp_unknown_member(Jimp *jimp)
{
    jimp_diagf("\n\n[ERROR]: unexpected object member `%s`\n\n", jimp->string);
}

bool jimp_object_begin(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_OCURLY);
}

bool jimp_object_member(Jimp *jimp)
{
    jimp__skip_whitespaces(jimp);
    int c = jimp__peek(jimp);

    if (c == -1) {
        jimp->token = JIMP_INVALID;
        jimp_diagf("ERROR: peeking jimp object\n");
        return false;
    }
    if ((char)c == ',') {
        jimp__get(jimp);
        if (!jimp__get_and_expect_token(jimp, JIMP_STRING)) return false;
        if (!jimp__get_and_expect_token(jimp, JIMP_COLON)) return false;
        return true;
    }
    if ((char)c == '}') {
        // DONT consume. Consumed in jimp_object_end
        return false;
    }
    if (!jimp__get_token(jimp)) return false;
    if (!jimp__expect_token(jimp, JIMP_STRING)) return false;
    if (!jimp__get_and_expect_token(jimp, JIMP_COLON)) return false;
    return true;
}

bool jimp_object_end(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_CCURLY);
}

bool jimp_string(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_STRING);
}

bool jimp_bool(Jimp *jimp)
{
    jimp__get_token(jimp);
    if (jimp->token == JIMP_TRUE) {
        jimp->boolean = true;
    } else if (jimp->token == JIMP_FALSE) {
        jimp->boolean = false;
    } else {
        jimp_diagf("ERROR: expected boolean, but got `%s`\n", jimp__token_kind(jimp->token));
        return false;
    }
    return true;
}

bool jimp_number(Jimp *jimp)
{
    return jimp__get_and_expect_token(jimp, JIMP_NUMBER);
}

bool jimp_is_null_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    int c = jimp__peek(jimp);
    return (c == 'n'); 
}

bool jimp_is_bool_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    int c = jimp__peek(jimp);
    return (c == 't' || c == 'f');
}

bool jimp_is_number_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    int c = jimp__peek(jimp);
    return (c == '-' || c == '+' || isdigit(c));
}

bool jimp_is_string_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    return (jimp__peek(jimp) == '"');
}

bool jimp_is_array_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    return (jimp__peek(jimp) == '[');
}

bool jimp_is_object_ahead(Jimp *jimp) {
    jimp__skip_whitespaces(jimp);
    return (jimp__peek(jimp) == '{');
}

static bool jimp__get_and_expect_token(Jimp *jimp, Jimp_Token token)
{
    if (!jimp__get_token(jimp)) return false;
    return jimp__expect_token(jimp, token);
}

static bool jimp__expect_token(Jimp *jimp, Jimp_Token token)
{
    if (jimp->token != token) {
        jimp_diagf("ERROR: expected %s, but got %s\n", jimp__token_kind(token), jimp__token_kind(jimp->token));
        return false;
    }
    return true;
}

static bool jimp__skip_open_container(Jimp *jimp, Jimp_Token openToken, Jimp_Token closeToken) {
    int depth = 1;

    while (true) {
        if (!jimp__get_token(jimp)) return false;

        if (jimp->token == openToken) {
            depth++;
        }

        if (jimp->token == closeToken) {
            depth--;
        }

        if (depth == 0) {
            return true;
        }
    }

    return false;
}

bool jimp_skip_array(Jimp *jimp) {
    if (!jimp__get_and_expect_token(jimp, JIMP_OBRACKET)) return false;
    return jimp__skip_open_container(jimp, JIMP_OBRACKET, JIMP_CBRACKET);
}

bool jimp_skip_object(Jimp *jimp) {
    if (!jimp__get_and_expect_token(jimp, JIMP_OCURLY)) return false;
    return jimp__skip_open_container(jimp, JIMP_OCURLY, JIMP_CCURLY);
}

bool jimp_skip_any(Jimp *jimp) {
    if (!jimp__get_token(jimp)) return false;

    switch (jimp->token) {
    case JIMP_INVALID: {
        jimp_diagf("Got invalid token");
        return false;
    }
    case JIMP_OCURLY: {
        return jimp__skip_open_container(jimp, JIMP_OCURLY, JIMP_CCURLY);
    }
    case JIMP_OBRACKET: {
        return jimp__skip_open_container(jimp, JIMP_OBRACKET, JIMP_CBRACKET);
    }
    default: {
        return true;
    }
    }
}

#endif // JIMP_IMPLEMENTATION
