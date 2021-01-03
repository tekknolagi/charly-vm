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

#include <sstream>

#include <catch2/catch_all.hpp>

#include "charly/core/compiler/lexer.h"

using Catch::Matchers::Contains;

using namespace charly::core::compiler;

TEST_CASE("Lexer") {

  SECTION("tokenizes integers") {
    Lexer lexer("test", "0 1 25 0b1111 0o777 0777 0xffff 0xFF 0");

    CHECK(lexer.read_token_skip_whitespace().intval == 0);
    CHECK(lexer.read_token_skip_whitespace().intval == 1);
    CHECK(lexer.read_token_skip_whitespace().intval == 25);
    CHECK(lexer.read_token_skip_whitespace().intval == 15);
    CHECK(lexer.read_token_skip_whitespace().intval == 511);
    CHECK(lexer.read_token_skip_whitespace().intval == 511);
    CHECK(lexer.read_token_skip_whitespace().intval == 65535);
    CHECK(lexer.read_token_skip_whitespace().intval == 255);
    CHECK(lexer.read_token_skip_whitespace().intval == 0);
  }

  SECTION("throws an error on incomplete number literals") {
    {
      Lexer lexer("test", "0x");
      CHECK_THROWS_WITH(lexer.read_token_skip_whitespace(), "Hex number literal expected at least one digit");
    }
    {
      Lexer lexer("test", "0b");
      CHECK_THROWS_WITH(lexer.read_token_skip_whitespace(), "Binary number literal expected at least one digit");
    }
    {
      Lexer lexer("test", "0o");
      CHECK_THROWS_WITH(lexer.read_token_skip_whitespace(), "Octal number literal expected at least one digit");
    }
  }

  SECTION("tokenizes floats") {
    Lexer lexer("test", "1.0 2.0 0.0 0.1 0.5 2.5 25.25 1234.12345678");

    CHECK(lexer.read_token_skip_whitespace().floatval == 1.0);
    CHECK(lexer.read_token_skip_whitespace().floatval == 2.0);
    CHECK(lexer.read_token_skip_whitespace().floatval == 0.0);
    CHECK(lexer.read_token_skip_whitespace().floatval == 0.1);
    CHECK(lexer.read_token_skip_whitespace().floatval == 0.5);
    CHECK(lexer.read_token_skip_whitespace().floatval == 2.5);
    CHECK(lexer.read_token_skip_whitespace().floatval == 25.25);
    CHECK(lexer.read_token_skip_whitespace().floatval == 1234.12345678);
  }

  SECTION("tokenizes identifiers") {
    Lexer lexer("test", "foo foo25 $foo $_foobar foo$bar");

    CHECK(lexer.read_token_skip_whitespace().source.compare("foo") == 0);
    CHECK(lexer.read_token_skip_whitespace().source.compare("foo25") == 0);
    CHECK(lexer.read_token_skip_whitespace().source.compare("$foo") == 0);
    CHECK(lexer.read_token_skip_whitespace().source.compare("$_foobar") == 0);
    CHECK(lexer.read_token_skip_whitespace().source.compare("foo$bar") == 0);
  }

  SECTION("tokenizes whitespace and newlines") {
    Lexer lexer("test", "  \n\r\n\t\n");

    CHECK(lexer.read_token().type == TokenType::Whitespace);
    CHECK(lexer.read_token().type == TokenType::Newline);
    CHECK(lexer.read_token().type == TokenType::Whitespace);
    CHECK(lexer.read_token().type == TokenType::Newline);
    CHECK(lexer.read_token().type == TokenType::Whitespace);
    CHECK(lexer.read_token().type == TokenType::Newline);
  }

  SECTION("returns eof token after last token parsed") {
    Lexer lexer("test", "25");

    CHECK(lexer.read_token().type == TokenType::Int);
    CHECK(lexer.read_token().type == TokenType::Eof);
    CHECK(lexer.read_token().type == TokenType::Eof);
    CHECK(lexer.read_token().type == TokenType::Eof);
    CHECK(lexer.read_token().type == TokenType::Eof);
  }

  SECTION("writes location information to tokens") {
    Lexer lexer("test", "\n\n\n   hello_world");
    lexer.read_token_skip_whitespace();

    CHECK(lexer.last_token().type == TokenType::Identifier);
    CHECK(lexer.last_token().location.offset == 6);
    CHECK(lexer.last_token().location.length == 11);
    CHECK(lexer.last_token().location.row == 4);
    CHECK(lexer.last_token().location.column == 4);
    CHECK(lexer.last_token().source.compare("hello_world") == 0);
  }

  SECTION("throws on unexpected characters") {
    Lexer lexer("test", "π");
    CHECK_THROWS_WITH(lexer.read_token_skip_whitespace(), "Unexpected character");
  }

  SECTION("formats a token") {
    Lexer lexer("test", "foobarbaz\n  25\n     25.25");

    {
      std::stringstream stream;
      lexer.read_token_skip_whitespace().dump(stream);
      CHECK(stream.str().compare("(Identifier, foobarbaz) test:1:1") == 0);
    }

    {
      std::stringstream stream;
      lexer.read_token_skip_whitespace().dump(stream);
      CHECK(stream.str().compare("(Int, 25) test:2:3") == 0);
    }

    {
      std::stringstream stream;
      lexer.read_token_skip_whitespace().dump(stream);
      CHECK(stream.str().compare("(Float, 25.25) test:3:6") == 0);
    }
  }

  SECTION("recognizes keywords") {
    Lexer lexer("test", (
      "false\n"
      "NaN\n"
      "null\n"
      "self\n"
      "super\n"
      "true\n"
      "and\n"
      "as\n"
      "await\n"
      "break\n"
      "case\n"
      "catch\n"
      "class\n"
      "const\n"
      "continue\n"
      "default\n"
      "defer\n"
      "do\n"
      "else\n"
      "export\n"
      "extends\n"
      "finally\n"
      "for\n"
      "func\n"
      "guard\n"
      "if\n"
      "import\n"
      "in\n"
      "let\n"
      "loop\n"
      "match\n"
      "module\n"
      "new\n"
      "operator\n"
      "or\n"
      "property\n"
      "return\n"
      "spawn\n"
      "static\n"
      "switch\n"
      "throw\n"
      "try\n"
      "typeof\n"
      "unless\n"
      "until\n"
      "while\n"
      "yield\n"
    ));

    CHECK(lexer.read_token_skip_whitespace().type == TokenType::False);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::NaN);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Null);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Self);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Super);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::True);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::And);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::As);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Await);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Break);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Case);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Catch);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Class);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Const);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Continue);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Default);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Defer);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Do);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Else);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Export);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Extends);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Finally);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::For);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Func);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Guard);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::If);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Import);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::In);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Let);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Loop);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Match);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Module);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::New);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Operator);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Or);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Property);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Return);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Spawn);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Static);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Switch);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Throw);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Try);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Typeof);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Unless);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Until);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::While);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Yield);
    CHECK(lexer.read_token_skip_whitespace().type == TokenType::Eof);
  }

  SECTION("formats a LexerException") {
    Lexer lexer("test", "0x");

    try {
      lexer.read_token();
    } catch(LexerException& exc) {
      std::stringstream stream;
      exc.dump(stream);

      CHECK(stream.str().compare("test:1:1: Hex number literal expected at least one digit") == 0);
    }
  }
}