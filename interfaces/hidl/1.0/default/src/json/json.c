/*
 * Copyright (c) 2021 anqi.huang@outlook.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>

#include "json.h"

#ifndef INT_MAX
#define INT_MAX 2147483647
#define INT_MIN (-INT_MAX - 1)
#define UINT_MAX   4294967295U
#endif

static const char *ep;

/* Parse the input text into an unescaped cstring, and populate item. */
static const unsigned char firstByteMark[7] = {0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};

static void *(*Json_malloc)(size_t sz) = malloc;

static void (*Json_free)(void *ptr) = free;

/* Predeclare these prototypes. */
static const char *parse_value(Json *item, const char *value);

static char *print_value(Json *item, int depth, int fmt);

static const char *parse_array(Json *item, const char *value);

static char *print_array(Json *item, int depth, int fmt);

static const char *parse_object(Json *item, const char *value);

static char *print_object(Json *item, int depth, int fmt);

const char *Json_GetErrorPtr() {
    return ep;
}

static int Json_strcasecmp(const char *s1, const char *s2) {
    if (!s1) {
        return (s1 == s2) ? 0 : 1;
    }

    if (!s2) {
        return 1;
    }

    for (; tolower(*s1) == tolower(*s2); ++s1, ++s2) {
        if (*s1 == 0) {
            return 0;
        }
    }

    return tolower(*(const unsigned char *) s1) - tolower(*(const unsigned char *) s2);
}

static char *Json_strdup(const char *str) {
    size_t len;
    char *copy;

    len = strlen(str) + 1;
    if (!(copy = (char *) Json_malloc(len))) {
        return 0;
    }
    memcpy(copy, str, len);

    return copy;
}

void Json_InitHooks(Json_Hooks *hooks) {
    /* Reset hooks */
    if (!hooks) {
        Json_malloc = malloc;
        Json_free = free;
        return;
    }

    Json_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
    Json_free = (hooks->free_fn) ? hooks->free_fn : free;
}

/*
 * Internal constructor.
 */
static Json *Json_New_Item() {
    Json *node = (Json *) Json_malloc(sizeof(Json));
    if (node) {
        memset(node, 0, sizeof(Json));
    }

    return node;
}

/*
 * Delete a Json structure.
 */
void Json_Delete(Json *c) {
    Json *next;
    while (c) {
        next = c->next;
        if (!(c->type & Json_IsReference) && c->child) {
            Json_Delete(c->child);
        }

        if (!(c->type & Json_IsReference) && c->valuestring) {
            Json_free(c->valuestring);
        }

        if (c->string) {
            Json_free(c->string);
        }

        Json_free(c);
        c = next;
    }
}

/*
 * Parse the input text to generate a number, and populate the result into item.
 */
static const char *parse_number(Json *item, const char *num) {
    long double n = 0, scale = 0;
    int subscale = 0, signsubscale = 1;
    item->sign = 1;

    /* Could use sscanf for this */
    if (*num == '-') {
        item->sign = -1, num++; /* Has sign */
    }

    if (*num == '0') {
        num++; /* is zero */
    }

    if (*num >= '1' && *num <= '9') {
        do {
            n = (n * 10.0) + (*num++ - '0');
        } while (*num >= '0' && *num <= '9'); /* Number */
    }

    /* Fractional part? */
    if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
        num++;
        do {
            n = (n * 10.0) + (*num++ - '0'), scale--;
        } while (*num >= '0' && *num <= '9');
    }

    /* Exponent? */
    if (*num == 'e' || *num == 'E') {
        num++;
        if (*num == '+') {
            num++;
        } else if (*num == '-') {
            signsubscale = -1, num++; /* With sign? */
        }

        while (*num >= '0' && *num <= '9') {
            subscale = (subscale * 10) + (*num++ - '0'); /* Number? */
        }
    }

    if (scale == 0 && subscale == 0) {
        item->valuedouble = (double) (item->sign * n);
        item->valueint = (uint64) (item->sign * (uint64) n);
        item->type = Json_Int;
    } else {
        n = item->sign * n *
            pow(10.0, (scale + subscale * signsubscale)); /* number = +/- number.fraction * 10^+/- exponent */
        item->valuedouble = (double) n;
        item->valueint = (uint64) n;
        item->type = Json_Double;
    }

    return num;
}

/* Render the number nicely from the given item into a string. */
static char *print_double(Json *item) {
    char *str;
    double d = item->valuedouble;
    str = (char *) Json_malloc(64); /* This is a nice tradeoff. */
    if (str) {
        if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9) {
            sprintf(str, "%lf", d);
        } else {
            sprintf(str, "%f", d);
        }
    }

    return str;
}

static char *print_int(Json *item) {
    char *str;
    str = (char *) Json_malloc(22); /* 2^64+1 can be represented in 21 chars. */

    if (str) {
        if (item->sign == -1) {
            if ((int64) item->valueint <= (int64) INT_MAX
                && (int64) item->valueint >= (int64) INT_MIN) {
                sprintf(str, "%d", (int32) item->valueint);
            } else {
                sprintf(str, "%lld", (int64) item->valueint);
            }
        } else {
            if (item->valueint <= (uint64) UINT_MAX) {
                sprintf(str, "%u", (uint32) item->valueint);
            } else {
                sprintf(str, "%llu", item->valueint);
            }
        }
    }

    return str;
}

static const char *parse_string(Json *item, const char *str) {
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    unsigned uc, uc2;
    if (*str != '\"') {
        ep = str;
        return 0;
    } /* not a string! */

    while (*ptr != '\"' && *ptr && ++len) {
        if (*ptr++ == '\\') {
            ptr++; /* Skip escaped quotes. */
        }
    }

    out = (char *) Json_malloc(len + 1); /* This is how long we need for the string, roughly. */
    if (!out) {
        return 0;
    }

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '\"' && *ptr) {
        if (*ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
                case 'b':
                    *ptr2++ = '\b';
                    break;

                case 'f':
                    *ptr2++ = '\f';
                    break;

                case 'n':
                    *ptr2++ = '\n';
                    break;

                case 'r':
                    *ptr2++ = '\r';
                    break;

                case 't':
                    *ptr2++ = '\t';
                    break;

                case 'u': /* transcode utf16 to utf8. */
                    sscanf(ptr + 1, "%4x", &uc);
                    ptr += 4; /* get the unicode char. */

                    if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0) {
                        break;    // check for invalid.
                    }

                    // UTF16 surrogate pairs.
                    if (uc >= 0xD800 && uc <= 0xDBFF) {
                        if (ptr[1] != '\\' || ptr[2] != 'u') {
                            break;    // missing second-half of surrogate.
                        }

                        sscanf(ptr + 3, "%4x", &uc2);
                        ptr += 6;
                        if (uc2 < 0xDC00 || uc2 > 0xDFFF) {
                            break;    // invalid second-half of surrogate.
                        }

                        uc = 0x10000 | ((uc & 0x3FF) << 10) | (uc2 & 0x3FF);
                    }

                    len = 4;
                    if (uc < 0x80) {
                        len = 1;
                    } else if (uc < 0x800) {
                        len = 2;
                    } else if (uc < 0x10000) {
                        len = 3;
                    }

                    ptr2 += len;

                    switch (len) {
                        case 4:
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;

                        case 3:
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;

                        case 2:
                            *--ptr2 = ((uc | 0x80) & 0xBF);
                            uc >>= 6;

                        case 1:
                            *--ptr2 = (uc | firstByteMark[len]);
                    }
                    ptr2 += len;
                    break;

                default:
                    *ptr2++ = *ptr;
                    break;
            }
            ptr++;
        }
    }

    *ptr2 = 0;
    if (*ptr == '\"') {
        ptr++;
    }

    item->valuestring = out;
    item->type = Json_String;

    return ptr;
}

/*
 * Render the cstring provided to an escaped version that can be printed.
 */
static char *print_string_ptr(const char *str) {
    const char *ptr;
    char *ptr2, *out;
    int len = 0;
    unsigned char token;

    if (!str) {
        return Json_strdup("");
    }

    ptr = str;
    while ((token = *ptr) && ++len) {
        if (strchr("\"\\\b\f\n\r\t", token)) {
            len++;
        } else if (token < 32) {
            len += 5;
        }

        ptr++;
    }

    out = (char *) Json_malloc(len + 3);
    if (!out) {
        return 0;
    }

    ptr2 = out;
    ptr = str;
    *ptr2++ = '\"';
    while (*ptr) {
        if ((unsigned char) *ptr > 31 && *ptr != '\"' && *ptr != '\\') {
            *ptr2++ = *ptr++;
        } else {
            *ptr2++ = '\\';
            switch (token = *ptr++) {
                case '\\':
                    *ptr2++ = '\\';
                    break;

                case '\"':
                    *ptr2++ = '\"';
                    break;

                case '\b':
                    *ptr2++ = 'b';
                    break;

                case '\f':
                    *ptr2++ = 'f';
                    break;

                case '\n':
                    *ptr2++ = 'n';
                    break;

                case '\r':
                    *ptr2++ = 'r';
                    break;

                case '\t':
                    *ptr2++ = 't';
                    break;

                default:
                    sprintf(ptr2, "u%04x", token);
                    ptr2 += 5;
                    break; /* escape and print */
            }
        }
    }

    *ptr2++ = '\"';
    *ptr2++ = 0;

    return out;
}

/*
 * Invote print_string_ptr (which is useful) on an item.
 */
static char *print_string(Json *item) {
    return print_string_ptr(item->valuestring);
}

/*
 * Utility to jump whitespace and cr/lf
 */
static const char *skip(const char *in) {
    while (in && *in && (unsigned char) *in <= 32) {
        in++;
    }
    return in;
}

/*
 * Parse an object - create a new root, and populate.
 */
Json *Json_Parse(const char *value) {
    Json *c = Json_New_Item();
    ep = 0;
    if (!c) {
        return 0; /* memory fail */
    }

    if (!parse_value(c, skip(value))) {
        Json_Delete(c);
        return 0;
    }

    return c;
}

/*
 * Render a Json item/entity/structure to text.
 */
char *Json_Print(Json *item) {
    return print_value(item, 0, 1);
}

char *Json_PrintUnformatted(Json *item) {
    return print_value(item, 0, 0);
}

/*
 * Parser core - when encountering text, process appropriately.
 */
static const char *parse_value(Json *item, const char *value) {
    if (!value) {
        return 0; /* Fail on null. */
    }

    if (!strncmp(value, "null", 4)) {
        item->type = Json_NULL;
        return value + 4;
    }

    if (!strncmp(value, "false", 5)) {
        item->type = Json_False;
        return value + 5;
    }

    if (!strncmp(value, "true", 4)) {
        item->type = Json_True;
        item->valueint = 1;
        return value + 4;
    }

    if (*value == '\"') {
        return parse_string(item, value);
    }

    if (*value == '-' || (*value >= '0' && *value <= '9')) {
        return parse_number(item, value);
    }

    if (*value == '[') {
        return parse_array(item, value);
    }

    if (*value == '{') {
        return parse_object(item, value);
    }

    ep = value;
    return 0; /* failure. */
}

/*
 * Render a value to text.
 */
static char *print_value(Json *item, int depth, int fmt) {
    char *out = 0;
    if (!item) {
        return 0;
    }

    switch ((item->type) & 255) {
        case Json_NULL:
            out = Json_strdup("null");
            break;

        case Json_False:
            out = Json_strdup("false");
            break;

        case Json_True:
            out = Json_strdup("true");
            break;

        case Json_Int:
            out = print_int(item);
            break;

        case Json_Double:
            out = print_double(item);
            break;

        case Json_String:
            out = print_string(item);
            break;

        case Json_Array:
            out = print_array(item, depth, fmt);
            break;

        case Json_Object:
            out = print_object(item, depth, fmt);
            break;
    }

    return out;
}

/*
 * Build an array from input text.
 */
static const char *parse_array(Json *item, const char *value) {
    Json *child;
    if (*value != '[') {
        ep = value;
        return 0;
    } /* not an array! */

    item->type = Json_Array;
    value = skip(value + 1);
    if (*value == ']') {
        return value + 1; /* empty array. */
    }

    item->child = child = Json_New_Item();
    if (!item->child) {
        return 0; /* memory fail */
    }

    value = skip(parse_value(child, skip(value))); /* skip any spacing, get the value. */
    if (!value) {
        return 0;
    }

    while (*value == ',') {
        Json *new_item;
        if (!(new_item = Json_New_Item())) {
            return 0; /* memory fail */
        }

        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) {
            return 0; /* memory fail */
        }
    }

    if (*value == ']') {
        return value + 1; /* end of array */
    }

    ep = value;
    return 0; /* malformed. */
}

/*
 * Render an array to text
 */
static char *print_array(Json *item, int depth, int fmt) {
    char **entries;
    char *out = 0, *ptr, *ret;
    int len = 5;
    Json *child = item->child;
    int numentries = 0, i = 0, fail = 0;

    /*
     * How many entries in the array?
     */
    while (child) {
        numentries++, child = child->next;
    }

    /*
     * Allocate an array to hold the values for each
     */
    entries = (char **) Json_malloc(numentries * sizeof(char *));
    if (!entries) {
        return 0;
    }

    memset(entries, 0, numentries * sizeof(char *));
    /*
     * Retrieve all the results:
     */
    child = item->child;
    while (child && !fail) {
        ret = print_value(child, depth + 1, fmt);
        entries[i++] = ret;
        if (ret) {
            len += strlen(ret) + 2 + (fmt ? 1 : 0);
        } else {
            fail = 1;
        }
        child = child->next;
    }

    /*
     * If we didn't fail, try to malloc the output string
     */
    if (!fail) {
        out = (char *) Json_malloc(len);
    }

    /*
     * If that fails, we fail.
     */
    if (!out) {
        fail = 1;
    }

    /*
     * Handle failure.
     */
    if (fail) {
        for (i = 0; i < numentries; i++) {
            if (entries[i]) {
                Json_free(entries[i]);
            }
        }

        Json_free(entries);
        return 0;
    }

    /*
     * Compose the output array.
     */
    *out = '[';
    ptr = out + 1;
    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) {
            *ptr++ = ',';
            if (fmt) {
                *ptr++ = ' ';
            }
            *ptr = 0;
        }
        Json_free(entries[i]);
    }
    Json_free(entries);
    *ptr++ = ']';
    *ptr++ = 0;

    return out;
}

/*
 * Build an object from the text.
 */
static const char *parse_object(Json *item, const char *value) {
    Json *child;
    if (*value != '{') {
        ep = value;
        return 0;
    } /* not an object! */

    item->type = Json_Object;
    value = skip(value + 1);
    if (*value == '}') {
        return value + 1; /* empty array. */
    }

    item->child = child = Json_New_Item();
    if (!item->child) {
        return 0;
    }

    value = skip(parse_string(child, skip(value)));
    if (!value) {
        return 0;
    }

    child->string = child->valuestring;
    child->valuestring = 0;
    if (*value != ':') {
        ep = value;
        return 0;
    } /* fail! */

    value = skip(parse_value(child, skip(value + 1))); /* skip any spacing, get the value. */
    if (!value) {
        return 0;
    }

    while (*value == ',') {
        Json *new_item;
        if (!(new_item = Json_New_Item())) {
            return 0; /* memory fail */
        }

        child->next = new_item;
        new_item->prev = child;
        child = new_item;
        value = skip(parse_string(child, skip(value + 1)));
        if (!value) {
            return 0;
        }

        child->string = child->valuestring;
        child->valuestring = 0;
        if (*value != ':') {
            ep = value;
            return 0;
        } /* fail! */

        value = skip(parse_value(child, skip(value + 1))); /* skip any spacing, get the value. */
        if (!value) {
            return 0;
        }
    }

    if (*value == '}') {
        return value + 1; /* end of array */
    }

    ep = value;

    return 0; /* malformed. */
}

/*
 * Render an object to text.
 */
static char *print_object(Json *item, int depth, int fmt) {
    char **entries = 0, **names = 0;
    char *out = 0, *ptr, *ret, *str;
    int len = 7, i = 0, j;
    Json *child = item->child;
    int numentries = 0, fail = 0;
    /*
     * Count the number of entries.
     */
    while (child) {
        numentries++, child = child->next;
    }

    /* Allocate space for the names and the objects */
    entries = (char **) Json_malloc(numentries * sizeof(char *));
    if (!entries) {
        return 0;
    }

    names = (char **) Json_malloc(numentries * sizeof(char *));
    if (!names) {
        Json_free(entries);
        return 0;
    }

    memset(entries, 0, sizeof(char *) * numentries);
    memset(names, 0, sizeof(char *) * numentries);

    /* Collect all the results into our arrays: */
    child = item->child;
    depth++;
    if (fmt) {
        len += depth;
    }

    while (child) {
        names[i] = str = print_string_ptr(child->string);
        entries[i++] = ret = print_value(child, depth, fmt);
        if (str && ret) {
            len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
        } else {
            fail = 1;
        }
        child = child->next;
    }

    /* Try to allocate the output string */
    if (!fail) {
        out = (char *) Json_malloc(len);
    }

    if (!out) {
        fail = 1;
    }

    /* Handle failure */
    if (fail) {
        for (i = 0; i < numentries; i++) {
            if (names[i]) {
                Json_free(names[i]);
            }

            if (entries[i]) {
                Json_free(entries[i]);
            }
        }
        Json_free(names);
        Json_free(entries);
        return 0;
    }

    /* Compose the output: */
    *out = '{';
    ptr = out + 1;
    if (fmt) {
        *ptr++ = '\n';
    }

    *ptr = 0;
    for (i = 0; i < numentries; i++) {
        if (fmt) {
            for (j = 0; j < depth; j++) {
                *ptr++ = '\t';
            }
        }

        strcpy(ptr, names[i]);
        ptr += strlen(names[i]);
        *ptr++ = ':';
        if (fmt) {
            *ptr++ = '\t';
        }

        strcpy(ptr, entries[i]);
        ptr += strlen(entries[i]);
        if (i != numentries - 1) {
            *ptr++ = ',';
        }

        if (fmt) {
            *ptr++ = '\n';
        }

        *ptr = 0;
        Json_free(names[i]);
        Json_free(entries[i]);
    }

    Json_free(names);
    Json_free(entries);
    if (fmt) {
        for (i = 0; i < depth - 1; i++) {
            *ptr++ = '\t';
        }
    }

    *ptr++ = '}';
    *ptr++ = 0;

    return out;
}

/*
 * Get Array size/item / object item.
 */
int Json_GetArraySize(Json *array) {
    Json *c = array->child;
    int i = 0;
    while (c) {
        i++, c = c->next;
    }

    return i;
}

Json *Json_GetArrayItem(Json *array, int item) {
    Json *c = array->child;
    while (c && item > 0) {
        item--, c = c->next;
    }

    return c;
}

Json *Json_GetObjectItem(Json *object, const char *string) {
    Json *c = object->child;
    while (c && Json_strcasecmp(c->string, string)) {
        c = c->next;
    }

    return c;
}

/*
 * Utility for array list handling.
 */
static void suffix_object(Json *prev, Json *item) {
    prev->next = item;
    item->prev = prev;
}

/*
 * Utility for handling references.
 */
static Json *create_reference(Json *item) {
    Json *ref = Json_New_Item();
    if (!ref) {
        return 0;
    }

    memcpy(ref, item, sizeof(Json));
    ref->string = 0;
    ref->type |= Json_IsReference;
    ref->next = ref->prev = 0;

    return ref;
}

/*
 * Add item to array/object.
 */
void Json_AddItemToArray(Json *array, Json *item) {
    Json *c = array->child;
    if (!item) {
        return;
    }

    if (!c) {
        array->child = item;
    } else {
        while (c && c->next) {
            c = c->next;
        }
        suffix_object(c, item);
    }
}

void Json_AddItemToArrayHead(Json *array, Json *item) {
    Json *c = array->child;
    if (!item) {
        return;
    }

    if (!c) {
        array->child = item;
    } else {
        item->prev = c->prev;
        item->next = c;
        c->prev = item;
        array->child = item;
    }
}

void Json_AddItemToObject(Json *object, const char *string, Json *item) {
    if (!item) {
        return;
    }

    if (item->string) {
        Json_free(item->string);
    }

    item->string = Json_strdup(string);
    Json_AddItemToArray(object, item);
}

void Json_AddItemReferenceToArray(Json *array, Json *item) {
    Json_AddItemToArray(array, create_reference(item));
}

void Json_AddItemReferenceToObject(Json *object, const char *string, Json *item) {
    Json_AddItemToObject(object, string, create_reference(item));
}

Json *Json_DetachItemFromArray(Json *array, int which) {
    Json *c = array->child;
    while (c && which > 0) {
        c = c->next, which--;
    }

    if (!c) {
        return 0;
    }

    if (c->prev) {
        c->prev->next = c->next;
    }

    if (c->next) {
        c->next->prev = c->prev;
    }

    if (c == array->child) {
        array->child = c->next;
    }

    c->prev = c->next = 0;

    return c;
}

void Json_DeleteItemFromArray(Json *array, int which) {
    Json_Delete(Json_DetachItemFromArray(array, which));
}

Json *Json_DetachItemFromObject(Json *object, const char *string) {
    int i = 0;
    Json *c = object->child;
    while (c && Json_strcasecmp(c->string, string)) {
        i++, c = c->next;
    }

    if (c) {
        return Json_DetachItemFromArray(object, i);
    }

    return 0;
}

void Json_DeleteItemFromObject(Json *object, const char *string) {
    Json_Delete(Json_DetachItemFromObject(object, string));
}

/*
 * Replace array/object items with new ones.
 */
void Json_ReplaceItemInArray(Json *array, int which, Json *newitem) {
    Json *c = array->child;
    while (c && which > 0) {
        c = c->next, which--;
    }

    if (!c) {
        return;
    }

    newitem->next = c->next;
    newitem->prev = c->prev;
    if (newitem->next) {
        newitem->next->prev = newitem;
    }

    if (c == array->child) {
        array->child = newitem;
    } else {
        newitem->prev->next = newitem;
    }

    c->next = c->prev = 0;
    Json_Delete(c);
}

void Json_ReplaceItemInObject(Json *object, const char *string, Json *newitem) {
    int i = 0;
    Json *c = object->child;
    while (c && Json_strcasecmp(c->string, string)) {
        i++, c = c->next;
    }

    if (c) {
        newitem->string = Json_strdup(string);
        Json_ReplaceItemInArray(object, i, newitem);
    }
}

/*
 * Create basic types:
 */
Json *Json_CreateNull() {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_NULL;
    }

    return item;
}

Json *Json_CreateTrue() {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_True;
    }

    return item;
}

Json *Json_CreateFalse() {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_False;
    }

    return item;
}

Json *Json_CreateBool(int b) {
    Json *item = Json_New_Item();
    if (item) {
        item->type = b ? Json_True : Json_False;
    }

    return item;
}

Json *Json_CreateDouble(double num, int sign) {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_Double;
        item->valuedouble = num;
        item->valueint = (uint64) num;
        item->sign = sign;
    }

    return item;
}

Json *Json_CreateInt(uint64 num, int sign) {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_Int;
        item->valuedouble = (double) num;
        item->valueint = (uint64) num;
        item->sign = sign;
    }

    return item;
}

Json *Json_CreateString(const char *string) {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_String;
        item->valuestring = Json_strdup(string);
    }

    return item;
}

Json *Json_CreateArray() {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_Array;
    }

    return item;
}

Json *Json_CreateObject() {
    Json *item = Json_New_Item();
    if (item) {
        item->type = Json_Object;
    }

    return item;
}

/*
 * Create Arrays:
 */
Json *Json_CreateIntArray(int *numbers, int sign, int count) {
    int i;
    Json *n = 0, *p = 0, *a = Json_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = Json_CreateDouble((long double) ((unsigned int) numbers[i]), sign);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

Json *Json_CreateFloatArray(float *numbers, int count) {
    int i;
    Json *n = 0, *p = 0, *a = Json_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = Json_CreateDouble((long double) numbers[i], -1);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

Json *Json_CreateDoubleArray(double *numbers, int count) {
    int i;
    Json *n = 0, *p = 0, *a = Json_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = Json_CreateDouble((long double) numbers[i], -1);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }

    return a;
}

Json *Json_CreateStringArray(const char **strings, int count) {
    int i;
    Json *n = 0, *p = 0, *a = Json_CreateArray();
    for (i = 0; a && i < count; i++) {
        n = Json_CreateString(strings[i]);
        if (!i) {
            a->child = n;
        } else {
            suffix_object(p, n);
        }
        p = n;
    }
    return a;
}

