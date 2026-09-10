/*
 * Copyright (c) 2026 BOSC & ICT, CAS
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Minimal semihosting runtime shared by all standalone AME examples. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    SemiOpen = 0x01,
    SemiClose = 0x02,
    SemiWrite0 = 0x04,
    SemiWrite = 0x05,
    SemiGetCmdline = 0x15,
    SemiExitExtended = 0x20,
    ApplicationExit = 0x20026,
};

struct ztt_baremetal_file {
    long handle;
    int terminal;
    int in_use;
    char *buffer;
    size_t used;
    size_t capacity;
};

#define FILE_BUFFER_SIZE (512 * 1024)

static struct ztt_baremetal_file stderr_file = {-1, 1, 1, NULL, 0, 0};
static struct ztt_baremetal_file files[4];
static char file_buffers[4][FILE_BUFFER_SIZE];
FILE *stderr = &stderr_file;

static long
semihost_call(long operation, void *arguments)
{
    register long a0 __asm__("a0") = operation;
    register void *a1 __asm__("a1") = arguments;
    __asm__ volatile(
        "fence rw, rw\n"
        ".option push\n"
        ".option norvc\n"
        "slli zero, zero, 0x1f\n"
        "ebreak\n"
        "srai zero, zero, 7\n"
        ".option pop\n"
        "fence rw, rw\n"
        : "+r"(a0)
        : "r"(a1)
        : "memory");
    return a0;
}

void *
memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    for (size_t i = 0; i < size; ++i)
        out[i] = in[i];
    return destination;
}

void *
memset(void *destination, int value, size_t size)
{
    unsigned char *out = destination;
    for (size_t i = 0; i < size; ++i)
        out[i] = (unsigned char)value;
    return destination;
}

int
memcmp(const void *lhs, const void *rhs, size_t size)
{
    const unsigned char *a = lhs;
    const unsigned char *b = rhs;
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

size_t
strlen(const char *string)
{
    size_t size = 0;
    while (string[size])
        ++size;
    return size;
}

static int
write_stream(FILE *stream, const char *data, size_t size)
{
    if (!stream || !stream->in_use)
        return 0;
    if (!stream->terminal) {
        if (size > stream->capacity - stream->used)
            return 0;
        memcpy(stream->buffer + stream->used, data, size);
        stream->used += size;
        return (int)size;
    }
    if (stream->terminal) {
        char buffer[512];
        if (size >= sizeof(buffer))
            size = sizeof(buffer) - 1;
        memcpy(buffer, data, size);
        buffer[size] = '\0';
        semihost_call(SemiWrite0, buffer);
        return (int)size;
    }
    return 0;
}

FILE *
fopen(const char *path, const char *mode)
{
    if (!path || !mode || mode[0] != 'w')
        return NULL;
    uintptr_t arguments[3] = {
        (uintptr_t)path,
        4,
        (uintptr_t)strlen(path),
    };
    long handle = semihost_call(SemiOpen, arguments);
    if (handle <= 0)
        return NULL;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (!files[i].in_use) {
            files[i].handle = handle;
            files[i].terminal = 0;
            files[i].in_use = 1;
            files[i].buffer = file_buffers[i];
            files[i].used = 0;
            files[i].capacity = sizeof(file_buffers[i]);
            return &files[i];
        }
    }
    uintptr_t close_arguments[1] = {(uintptr_t)handle};
    semihost_call(SemiClose, close_arguments);
    return NULL;
}

int
fclose(FILE *stream)
{
    if (!stream || !stream->in_use || stream->terminal)
        return EOF;
    uintptr_t write_arguments[3] = {
        (uintptr_t)stream->handle,
        (uintptr_t)stream->buffer,
        (uintptr_t)stream->used,
    };
    if (semihost_call(SemiWrite, write_arguments) != 0)
        return EOF;
    uintptr_t arguments[1] = {(uintptr_t)stream->handle};
    long result = semihost_call(SemiClose, arguments);
    stream->in_use = 0;
    stream->handle = -1;
    return result == 0 ? 0 : EOF;
}

int
fputc(int character, FILE *stream)
{
    char value = (char)character;
    return write_stream(stream, &value, 1) == 1 ? (unsigned char)value : EOF;
}

struct format_buffer {
    char *data;
    size_t size;
    size_t used;
};

static void
put_char(struct format_buffer *buffer, char value)
{
    if (buffer->used + 1 < buffer->size)
        buffer->data[buffer->used++] = value;
}

static void
put_string(struct format_buffer *buffer, const char *string)
{
    if (!string)
        string = "(null)";
    while (*string)
        put_char(buffer, *string++);
}

static void
put_unsigned(struct format_buffer *buffer, uint64_t value, unsigned base,
             unsigned width, int zero_pad)
{
    char digits[32];
    unsigned count = 0;
    do {
        const unsigned digit = value % base;
        digits[count++] = digit < 10 ? (char)('0' + digit) :
                                      (char)('a' + digit - 10);
        value /= base;
    } while (value && count < sizeof(digits));
    while (count < width) {
        put_char(buffer, zero_pad ? '0' : ' ');
        --width;
    }
    while (count)
        put_char(buffer, digits[--count]);
}

static int
vfprintf_impl(FILE *stream, const char *format, va_list arguments)
{
    char output[512];
    struct format_buffer buffer = {output, sizeof(output), 0};

    while (*format) {
        if (*format != '%') {
            put_char(&buffer, *format++);
            continue;
        }
        ++format;
        if (*format == '%') {
            put_char(&buffer, *format++);
            continue;
        }

        int zero_pad = 0;
        unsigned width = 0;
        if (*format == '0') {
            zero_pad = 1;
            ++format;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (unsigned)(*format - '0');
            ++format;
        }

        unsigned length = 0;
        while (*format == 'l') {
            ++length;
            ++format;
        }

        switch (*format++) {
        case 's':
            put_string(&buffer, va_arg(arguments, const char *));
            break;
        case 'c':
            put_char(&buffer, (char)va_arg(arguments, int));
            break;
        case 'd':
        case 'i': {
            int64_t value = length > 1 ? va_arg(arguments, long long) :
                            length == 1 ? va_arg(arguments, long) :
                                          va_arg(arguments, int);
            uint64_t magnitude;
            if (value < 0) {
                put_char(&buffer, '-');
                magnitude = (uint64_t)(-(value + 1)) + 1;
            } else {
                magnitude = (uint64_t)value;
            }
            put_unsigned(&buffer, magnitude, 10, width, zero_pad);
            break;
        }
        case 'u': {
            uint64_t value = length > 1 ?
                va_arg(arguments, unsigned long long) : length == 1 ?
                va_arg(arguments, unsigned long) :
                va_arg(arguments, unsigned int);
            put_unsigned(&buffer, value, 10, width, zero_pad);
            break;
        }
        case 'x': {
            uint64_t value = length > 1 ?
                va_arg(arguments, unsigned long long) : length == 1 ?
                va_arg(arguments, unsigned long) :
                va_arg(arguments, unsigned int);
            put_unsigned(&buffer, value, 16, width, zero_pad);
            break;
        }
        case 'g':
        case 'f':
            (void)va_arg(arguments, double);
            put_string(&buffer, "<fp>");
            break;
        default:
            put_char(&buffer, '?');
            break;
        }
    }
    output[buffer.used] = '\0';
    return write_stream(stream, output, buffer.used);
}

int
fprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf_impl(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int
parse_command_line(char *command, char **argv, int capacity)
{
    int argc = 0;
    char *cursor = command;
    while (*cursor && argc < capacity) {
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (!*cursor)
            break;
        char quote = 0;
        if (*cursor == '\'' || *cursor == '"')
            quote = *cursor++;
        argv[argc++] = cursor;
        while (*cursor && ((quote && *cursor != quote) ||
                           (!quote && *cursor != ' ' && *cursor != '\t')))
            ++cursor;
        if (*cursor)
            *cursor++ = '\0';
    }
    return argc;
}

static __attribute__((noreturn)) void
baremetal_exit(int status)
{
    uintptr_t arguments[2] = {ApplicationExit, (uintptr_t)status};
    semihost_call(SemiExitExtended, arguments);
    for (;;)
        __asm__ volatile("wfi");
}

extern int main(int argc, char **argv);

__attribute__((noreturn)) void
ztt_baremetal_start(void)
{
    static char command[2048];
    uintptr_t arguments[2] = {(uintptr_t)command, sizeof(command)};
    char *argv[16];

    if (semihost_call(SemiGetCmdline, arguments) != 0)
        baremetal_exit(127);
    command[sizeof(command) - 1] = '\0';
    int argc = parse_command_line(command, argv,
                                  (int)(sizeof(argv) / sizeof(argv[0])));
    baremetal_exit(main(argc, argv));
}
