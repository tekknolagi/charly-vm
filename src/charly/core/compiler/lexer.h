/*
 * This file is part of the Charly Virtual Machine (https://github.com/KCreate/charly-vm)
 *
 * MIT License
 *
 * Copyright (c) 2017 - 2021 Leonard Schütz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <memory>
#include <stdexcept>

#include "charly/utils/string.h"
#include "charly/utils/buffer.h"
#include "charly/utils/vector.h"
#include "charly/core/compiler/token.h"

#pragma once

namespace charly::core::compiler {

// base class of all compiler errors
class LexerException : public std::runtime_error {
public:
  LexerException(const utils::string& message, const Location& location)
      : std::runtime_error(message), m_location(location) {
  }

  // write a formatted version of this error to the stream:
  //
  // <filename>:<row>:<col>: <message>
  virtual void dump(std::ostream& io) {
    io << *m_location.filename;
    io << ':';
    io << m_location.row;
    io << ':';
    io << m_location.column;
    io << ':';
    io << ' ';
    io << what();
  }

private:
  Location m_location;
};

// splits source input into individual tokens for parsing
class Lexer {
public:
  Lexer(const utils::string& filename,
        const utils::string& source,
        uint32_t row = 1,
        uint32_t column = 1)
    : m_filename(filename), m_source(source), m_row(row), m_column(column) {
  }

  // read the next token from the source
  Token read_token();

  // reads the next token, skipping over whitespace and newlines
  Token read_token_skip_whitespace();

  // returns the last read token
  Token last_token();

private:
  Lexer(Lexer&) = delete;
  Lexer(Lexer&&) = delete;

  void increment_row();
  void increment_column(size_t delta);

  uint32_t peek_char(uint32_t nth = 0); // peek the next char
  uint32_t read_char(); // read the next char
  uint32_t last_char(); // return the last read char or \0

  // character identification
  bool is_whitespace(uint32_t cp); // \r \t ' '
  bool is_decimal(uint32_t cp); // 0-9
  bool is_hex(uint32_t cp); // 0-9a-fA-F
  bool is_binary(uint32_t cp); // 0-1
  bool is_octal(uint32_t cp); // 0-7
  bool is_alpha_lower(uint32_t cp); // a-z
  bool is_alpha_upper(uint32_t cp); // A-Z
  bool is_alpha(uint32_t cp); // a-zA-Z
  bool is_alphanumeric(uint32_t cp); // a-zA-Z0-9
  bool is_id_begin(uint32_t cp); // alpha $ _
  bool is_id_part(uint32_t cp); // alpha $ _ numeric

  // consume some token type
  void consume_whitespace(Token& token);
  void consume_decimal(Token& token);
  void consume_hex(Token& token);
  void consume_octal(Token& token);
  void consume_binary(Token& token);
  void consume_identifier(Token& token);

  // current source row / column
  uint32_t m_row;
  uint32_t m_column;

  utils::string m_filename;   // full path of the source file (or empty buffer)
  utils::Buffer m_source;     // source code

  uint32_t m_last_character; // the last character that was read from the source

  // list of parsed tokens
  utils::vector<Token> m_tokens;
};

}