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

#include "charly/core/compiler/parser.h"

using namespace charly::core::compiler::ast;

namespace charly::core::compiler {

ref<Program> Parser::parse_program(utils::Buffer& source, DiagnosticConsole& console) {
  try {
    Parser parser = Parser(source, console);

    parser.m_keyword_context._return = true;
    parser.m_keyword_context._break = false;
    parser.m_keyword_context._continue = false;
    parser.m_keyword_context._yield = false;
    parser.m_keyword_context._export = true;
    parser.m_keyword_context._import = true;
    parser.m_keyword_context._super = false;

    return parser.parse_program();
  } catch (DiagnosticException&) { return nullptr; }
}

ref<Statement> Parser::parse_statement(utils::Buffer& source, DiagnosticConsole& console) {
  try {
    Parser parser = Parser(source, console);

    parser.m_keyword_context._return = true;
    parser.m_keyword_context._break = true;
    parser.m_keyword_context._continue = true;
    parser.m_keyword_context._yield = true;
    parser.m_keyword_context._export = true;
    parser.m_keyword_context._import = true;
    parser.m_keyword_context._super = true;

    return parser.parse_statement();
  } catch (DiagnosticException&) { return nullptr; }
}

ref<Expression> Parser::parse_expression(utils::Buffer& source, DiagnosticConsole& console) {
  try {
    Parser parser = Parser(source, console);

    parser.m_keyword_context._return = true;
    parser.m_keyword_context._break = true;
    parser.m_keyword_context._continue = true;
    parser.m_keyword_context._yield = true;
    parser.m_keyword_context._export = true;
    parser.m_keyword_context._import = true;
    parser.m_keyword_context._super = true;

    return parser.parse_expression();
  } catch (DiagnosticException&) { return nullptr; }
}

ref<Program> Parser::parse_program() {
  ref<Block> body = make<Block>();
  parse_block_body(body);
  ref<Program> program = make<Program>(body);
  program->set_location(body);
  return program;
}

ref<Block> Parser::parse_block() {
  ref<Block> block = make<Block>();
  begin(block);
  eat(TokenType::LeftCurly);

  auto kwcontext = m_keyword_context;
  m_keyword_context._export = false;
  m_keyword_context._import = false;
  parse_block_body(block);
  m_keyword_context = kwcontext;

  end(block);
  eat(TokenType::RightCurly);
  return block;
}

void Parser::parse_block_body(const ref<Block>& block) {
  uint32_t parsed_statements = 0;

  while (!(type(TokenType::RightCurly) || type(TokenType::Eof))) {
    ref<Statement> stmt = parse_statement();

    // wrap function and class literals inside const declaration nodes
    switch (stmt->type()) {
      case Node::Type::Class:
      case Node::Type::Function: {
        std::string variable_name;

        if (ref<Function> func = cast<Function>(stmt)) {
          if (func->arrow_function) {
            break;
          }
          variable_name = func->name;
        }

        if (ref<Class> klass = cast<Class>(stmt)) {
          variable_name = klass->name;
        }

        ref<Id> name = make<Id>(variable_name);
        name->set_location(stmt);
        ref<Declaration> declaration = make<Declaration>(name, cast<Expression>(stmt), true);
        declaration->set_location(stmt);

        stmt = declaration;
        break;
      }
      default: {
        break;
      }
    }

    skip(TokenType::Semicolon);

    if (parsed_statements == 0 && !block->location().valid) {
      block->set_begin(stmt);
    }

    block->statements.push_back(stmt);
    block->set_end(stmt);

    parsed_statements++;
  }
}

ref<Statement> Parser::parse_block_or_statement() {
  auto kwcontext = m_keyword_context;
  m_keyword_context._export = false;
  m_keyword_context._import = false;

  ref<Statement> stmt;
  if (type(TokenType::LeftCurly)) {
    stmt = parse_block();
  } else {
    stmt = parse_jump_statement();
  }

  m_keyword_context = kwcontext;

  return stmt;
}

ref<Statement> Parser::parse_statement() {
  switch (m_token.type) {
    case TokenType::Import: {
      return parse_import();
    }
    case TokenType::LeftCurly: {
      return parse_block();
    }
    case TokenType::If: {
      return parse_if();
    }
    case TokenType::While: {
      return parse_while();
    }
    case TokenType::Loop: {
      return parse_loop();
    }
    case TokenType::Try: {
      return parse_try();
    }
    case TokenType::Switch: {
      return parse_switch();
    }
    case TokenType::For: {
      return parse_for();
    }
    case TokenType::Let:
    case TokenType::Const: {
      return parse_declaration();
    }
    default: {
      return parse_jump_statement();
    }
  }
}

ref<Statement> Parser::parse_jump_statement() {
  switch (m_token.type) {
    case TokenType::Return: {
      return parse_return();
    }
    case TokenType::Break: {
      return parse_break();
    }
    case TokenType::Continue: {
      return parse_continue();
    }
    case TokenType::Defer: {
      return parse_defer();
    }
    case TokenType::Throw: {
      return parse_throw();
    }
    case TokenType::Export: {
      return parse_export();
    }
    default: {
      return parse_throw_statement();
    }
  }
}

ref<Statement> Parser::parse_throw_statement() {
  switch (m_token.type) {
    case TokenType::Throw: {
      return parse_throw();
    }
    default: {
      return parse_expression();
    }
  }
}

ref<Return> Parser::parse_return() {
  Location begin = m_token.location;
  eat(TokenType::Return);

  ref<Expression> return_value;
  if (m_token.could_start_expression()) {
    return_value = parse_expression();
  } else {
    return_value = make<Null>();
    return_value->set_location(begin);
  }

  ref<Return> node = make<Return>(return_value);
  node->set_begin(begin);

  if (!m_keyword_context._return)
    m_console.error("return statement not allowed at this point", node->location());

  return node;
}

ref<Break> Parser::parse_break() {
  ref<Break> node = make<Break>();
  at(node);
  eat(TokenType::Break);

  if (!m_keyword_context._break)
    m_console.error("break statement not allowed at this point", node->location());

  return node;
}

ref<Continue> Parser::parse_continue() {
  ref<Continue> node = make<Continue>();
  at(node);
  eat(TokenType::Continue);

  if (!m_keyword_context._continue)
    m_console.error("continue statement not allowed at this point", node->location());

  return node;
}

ref<Defer> Parser::parse_defer() {
  Location begin = m_token.location;
  eat(TokenType::Defer);

  auto kwcontext = m_keyword_context;
  m_keyword_context._return = false;
  m_keyword_context._break = false;
  m_keyword_context._continue = false;
  ref<Statement> stmt = parse_block_or_statement();
  m_keyword_context = kwcontext;

  ref<Defer> node = make<Defer>(stmt);
  node->set_begin(begin);
  validate_defer(node);
  return node;
}

ref<Throw> Parser::parse_throw() {
  Location begin = m_token.location;
  eat(TokenType::Throw);
  ref<Expression> exp = parse_expression();
  ref<Throw> node = make<Throw>(exp);
  node->set_begin(begin);
  return node;
}

ref<Export> Parser::parse_export() {
  Location begin = m_token.location;
  eat(TokenType::Export);
  ref<Expression> exp = parse_expression();
  ref<Export> node = make<Export>(exp);
  node->set_begin(begin);

  if (!m_keyword_context._export)
    m_console.error("export statement not allowed at this point", node->location());

  return node;
}

ref<Import> Parser::parse_import() {
  Location begin = m_token.location;
  eat(TokenType::Import);
  ref<Expression> source_exp = parse_as_expression();
  ref<Import> node = make<Import>(source_exp);
  node->set_begin(begin);
  validate_import(node);

  if (!m_keyword_context._import)
    m_console.error("import statement not allowed at this point", node->location());

  return node;
}

ref<If> Parser::parse_if() {
  Location begin = m_token.location;
  eat(TokenType::If);

  ref<Expression> condition = parse_expression();
  ref<Statement> then_stmt = parse_block_or_statement();
  ref<Statement> else_stmt = nullptr;

  if (skip(TokenType::Else)) {
    if (type(TokenType::If)) {
      else_stmt = parse_if();
    } else {
      else_stmt = parse_block_or_statement();
    }
  }

  ref<If> node = make<If>(condition, then_stmt, else_stmt);
  node->set_begin(begin);
  return node;
}

ref<While> Parser::parse_while() {
  Location begin = m_token.location;
  eat(TokenType::While);

  ref<Expression> condition = parse_expression();

  auto kwcontext = m_keyword_context;
  m_keyword_context._break = true;
  m_keyword_context._continue = true;
  ref<Statement> then_stmt = parse_block_or_statement();
  m_keyword_context = kwcontext;

  ref<While> node = make<While>(condition, then_stmt);
  node->set_begin(begin);
  return node;
}

ref<While> Parser::parse_loop() {
  Location begin = m_token.location;
  eat(TokenType::Loop);
  ref<Expression> condition = make<Bool>(true);
  condition->set_location(begin);

  auto kwcontext = m_keyword_context;
  m_keyword_context._break = true;
  m_keyword_context._continue = true;
  ref<Statement> then_stmt = parse_block_or_statement();
  m_keyword_context = kwcontext;

  ref<While> node = make<While>(condition, then_stmt);
  node->set_begin(begin);
  return node;
}

ref<Statement> Parser::parse_try() {
  Location begin = m_token.location;
  eat(TokenType::Try);

  ref<Statement> try_stmt = parse_block_or_statement();

  eat(TokenType::Catch);
  std::string exception_name;
  if (skip(TokenType::LeftParen)) {
    exception_name = parse_identifier_token()->value;
    eat(TokenType::RightParen);
  } else {
    exception_name = "exception";
  }
  ref<Statement> catch_stmt = parse_block_or_statement();

  ref<Statement> finally_stmt = nullptr;
  if (skip(TokenType::Finally)) {
    auto kwcontext = m_keyword_context;
    m_keyword_context._return = false;
    m_keyword_context._break = false;
    m_keyword_context._continue = false;
    finally_stmt = parse_block_or_statement();
    m_keyword_context = kwcontext;
  }

  ref<Try> try_node = make<Try>(try_stmt, exception_name, catch_stmt);
  try_node->set_begin(begin);

  if (finally_stmt) {
    return make<Block>(make<Defer>(finally_stmt), try_node);
  } else {
    return try_node;
  }
}

ref<Switch> Parser::parse_switch() {
  Location begin = m_token.location;
  eat(TokenType::Switch);

  ref<Switch> node = make<Switch>(parse_expression());
  node->set_begin(begin);

  eat(TokenType::LeftCurly);

  while (!type(TokenType::RightCurly)) {
    Location case_begin = m_token.location;

    switch (m_token.type) {
      case TokenType::Case: {
        advance();
        ref<Expression> case_test = parse_expression();

        auto kwcontext = m_keyword_context;
        m_keyword_context._break = true;
        ref<Statement> case_stmt = parse_block_or_statement();
        m_keyword_context = kwcontext;

        ref<SwitchCase> case_node = make<SwitchCase>(case_test, case_stmt);
        case_node->set_begin(case_begin);
        node->cases.push_back(make<SwitchCase>(case_test, case_stmt));
        break;
      }
      case TokenType::Default: {
        advance();

        auto kwcontext = m_keyword_context;
        m_keyword_context._break = true;
        ref<Statement> stmt = parse_block_or_statement();
        m_keyword_context = kwcontext;

        stmt->set_begin(case_begin);
        node->default_stmt = stmt;
        break;
      }
      default: {
        unexpected_token("case or default");
      }
    }
  }

  end(node);
  eat(TokenType::RightCurly);

  return node;
}

ref<For> Parser::parse_for() {
  Location begin = m_token.location;
  eat(TokenType::For);

  bool constant_value = false;
  if (skip(TokenType::Const)) {
    constant_value = true;
  } else {
    skip(TokenType::Let);
  }

  ref<Expression> target;
  switch (m_token.type) {
    case TokenType::Identifier: {
      target = parse_identifier_token();
      break;
    }
    case TokenType::LeftParen: {
      target = parse_tuple(false);
      break;
    }
    case TokenType::LeftCurly: {
      target = parse_dict();
      break;
    }
    default: {
      unexpected_token("expected identifier or unpack expression");
    }
  }

  eat(TokenType::In);

  ref<Expression> source = parse_expression();

  ref<Statement> stmt = parse_block_or_statement();

  ref<For> node = make<For>(constant_value, target, source, stmt);
  node->set_begin(begin);

  validate_for(node);

  return node;
}

ref<Statement> Parser::parse_declaration() {
  if (!(type(TokenType::Let) || type(TokenType::Const)))
    unexpected_token("let or const");

  Location begin = m_token.location;

  bool const_declaration = type(TokenType::Const);
  advance();

  // parse the left-hand side of the declaration
  ref<Expression> target;
  bool requires_assignment = const_declaration;
  switch (m_token.type) {
    // regular local variable
    case TokenType::Identifier: {
      target = parse_identifier_token();
      break;
    }

    // sequence unpack declaration
    case TokenType::LeftParen: {
      target = parse_tuple(false);  // disable paren conversion
      requires_assignment = true;
      break;
    }

    // object unpack declaration
    case TokenType::LeftCurly: {
      target = parse_dict();
      requires_assignment = true;
      break;
    }

    default: {
      unexpected_token("expected variable declaration");
    }
  }

  if (requires_assignment)
    match(TokenType::Assignment);

  ref<Declaration> declaration_node;

  if (skip(TokenType::Assignment)) {
    ref<Expression> value = parse_expression();
    declaration_node = make<Declaration>(target, value, const_declaration);
  } else {
    ref<Null> null = make<Null>();
    null->set_location(target);
    declaration_node = make<Declaration>(target, null, const_declaration);
  }

  declaration_node->set_begin(begin);

  validate_declaration(declaration_node);

  return declaration_node;
}

void Parser::parse_call_arguments(std::vector<ref<Expression>>& result) {
  if (!m_token.could_start_expression())
    return;

  do {
    result.push_back(parse_possible_spread_expression());
  } while (skip(TokenType::Comma));
}

ref<Expression> Parser::parse_as_expression() {
  ref<Expression> exp = parse_expression();

  if (skip(TokenType::As)) {
    return make<As>(exp, parse_identifier_token());
  }

  return exp;
}

ref<Expression> Parser::parse_expression() {
  switch (m_token.type) {
    case TokenType::Yield: {
      return parse_yield();
    }
    case TokenType::Import: {
      return parse_import_expression();
    }
    default: {
      return parse_assignment();
    }
  }
}

ref<Yield> Parser::parse_yield() {
  Location begin = m_token.location;
  eat(TokenType::Yield);
  ref<Yield> node = make<Yield>(parse_expression());
  node->set_begin(begin);

  if (!m_keyword_context._yield)
    m_console.error("yield expression not allowed at this point", node->location());

  return node;
}

ref<ImportExpression> Parser::parse_import_expression() {
  Location begin = m_token.location;
  eat(TokenType::Import);
  ref<ImportExpression> import_node = make<ImportExpression>(parse_expression());
  import_node->set_begin(begin);
  return import_node;
}

ref<Expression> Parser::parse_assignment() {
  ref<Expression> target = parse_ternary();

  if (type(TokenType::Assignment)) {
    TokenType assignment_operator = m_token.assignment_operator;
    eat(TokenType::Assignment);
    ref<Assignment> node = make<Assignment>(assignment_operator, target, parse_expression());
    validate_assignment(node);
    return node;
  }

  return target;
}

ref<Expression> Parser::parse_ternary() {
  ref<Expression> condition = parse_binaryop();

  if (skip(TokenType::QuestionMark)) {
    ref<Expression> then_exp = parse_expression();
    eat(TokenType::Colon);
    ref<Expression> else_exp = parse_expression();
    return make<Ternary>(condition, then_exp, else_exp);
  }

  return condition;
}

ref<Expression> Parser::parse_binaryop() {
  return parse_binaryop_1(parse_unaryop(), 0);
}

ref<Expression> Parser::parse_binaryop_1(ref<Expression> lhs, uint32_t min_precedence) {
  for (;;) {
    if (kBinaryOpPrecedenceLevels.count(m_token.type) == 0)
      break;

    TokenType operation = m_token.type;
    uint32_t precedence = kBinaryOpPrecedenceLevels.at(operation);
    if (precedence < min_precedence)
      break;

    advance();
    ref<Expression> rhs = parse_unaryop();

    // higher precedence operators or right associative operators
    for (;;) {
      if (kBinaryOpPrecedenceLevels.count(m_token.type) == 0)
        break;

      uint32_t next_precedence = kBinaryOpPrecedenceLevels.at(m_token.type);
      if ((next_precedence > precedence) ||
          (kRightAssociativeOperators.count(m_token.type) && next_precedence == precedence)) {
        rhs = parse_binaryop_1(rhs, next_precedence);
      } else {
        break;
      }
    }

    lhs = make<BinaryOp>(operation, lhs, rhs);
  }

  return lhs;
}

ref<Expression> Parser::parse_possible_spread_expression() {
  if (type(TokenType::TriplePoint)) {
    Location start_loc = m_token.location;
    advance();
    ref<Spread> op = make<Spread>(parse_call_member_index());
    op->set_begin(start_loc);
    return op;
  }

  return parse_expression();
}

ref<Expression> Parser::parse_unaryop() {
  if (m_token.is_unary_operator()) {
    TokenType operation = m_token.type;
    Location start_loc = m_token.location;
    advance();
    ref<UnaryOp> op = make<UnaryOp>(operation, parse_unaryop());
    op->set_begin(start_loc);
    return op;
  }

  return parse_control_expression();
}

ref<Expression> Parser::parse_control_expression() {
  switch (m_token.type) {
    case TokenType::Spawn: {
      return parse_spawn();
    }
    case TokenType::Await: {
      return parse_await();
    }
    case TokenType::Typeof: {
      return parse_typeof();
    }
    default: {
      return parse_call_member_index();
    }
  }
}

ref<Expression> Parser::parse_spawn() {
  Location begin = m_token.location;
  eat(TokenType::Spawn);
  auto kwcontext = m_keyword_context;
  m_keyword_context._return = true;
  m_keyword_context._break = false;
  m_keyword_context._continue = false;
  m_keyword_context._yield = true;
  m_keyword_context._super = false;
  ref<Statement> stmt = parse_block_or_statement();
  m_keyword_context = kwcontext;

  ref<Spawn> node = make<Spawn>(stmt);
  node->set_begin(begin);
  validate_spawn(node);
  return node;
}

ref<Expression> Parser::parse_await() {
  Location begin = m_token.location;
  eat(TokenType::Await);
  ref<Await> node = make<Await>(parse_control_expression());
  node->set_begin(begin);
  return node;
}

ref<Expression> Parser::parse_typeof() {
  Location begin = m_token.location;
  eat(TokenType::Typeof);
  ref<Typeof> node = make<Typeof>(parse_control_expression());
  node->set_begin(begin);
  return node;
}

ref<Expression> Parser::parse_call_member_index() {
  ref<Expression> target = parse_literal();

  for (;;) {
    bool newline_passed_since_base = target->location().end_row != m_token.location.row;

    switch (m_token.type) {
      case TokenType::LeftParen: {
        if (newline_passed_since_base)
          return target;
        target = parse_call(target);
        break;
      }
      case TokenType::LeftBracket: {
        if (newline_passed_since_base)
          return target;
        target = parse_index(target);
        break;
      }
      case TokenType::Point: {
        target = parse_member(target);
        break;
      }
      default: {
        return target;
      }
    }
  }
}

ref<CallOp> Parser::parse_call(ref<Expression> target) {
  eat(TokenType::LeftParen);
  ref<CallOp> callop = make<CallOp>(target);
  parse_call_arguments(callop->arguments);
  end(callop);
  eat(TokenType::RightParen);
  return callop;
}

ref<MemberOp> Parser::parse_member(ref<Expression> target) {
  eat(TokenType::Point);
  return make<MemberOp>(target, parse_identifier_token());
}

ref<IndexOp> Parser::parse_index(ref<Expression> target) {
  eat(TokenType::LeftBracket);
  ref<IndexOp> indexop = make<IndexOp>(target, parse_expression());
  end(indexop);
  eat(TokenType::RightBracket);
  return indexop;
}

ref<Expression> Parser::parse_literal() {
  switch (m_token.type) {
    case TokenType::Int: {
      return parse_int_token();
    }
    case TokenType::Float: {
      return parse_float_token();
    }
    case TokenType::True:
    case TokenType::False: {
      return parse_bool_token();
    }
    case TokenType::Identifier: {
      return parse_identifier_token();
    }
    case TokenType::Character: {
      return parse_char_token();
    }
    case TokenType::String: {
      return parse_string_token();
    }
    case TokenType::FormatString: {
      return parse_format_string();
    }
    case TokenType::RightArrow: {
      return parse_arrow_function();
    }
    case TokenType::Func: {
      return parse_function();
    }
    case TokenType::Class: {
      return parse_class();
    }
    case TokenType::LeftParen: {
      return parse_tuple();
    }
    case TokenType::LeftCurly: {
      return parse_dict();
    }
    case TokenType::LeftBracket: {
      return parse_list();
    }
    case TokenType::Null: {
      return parse_null_token();
    }
    case TokenType::Self: {
      return parse_self_token();
    }
    case TokenType::Super: {
      return parse_super_token();
    }
    default: {
      unexpected_token("expected an expression");
    }
  }
}

ref<FormatString> Parser::parse_format_string() {
  ref<FormatString> format_string = make<FormatString>();

  match(TokenType::FormatString);
  begin(format_string);
  end(format_string);

  ref<String> element = parse_string_token();
  if (element->value.size() > 0)
    format_string->elements.push_back(element);

  for (;;) {
    // parse interpolated expression
    ref<Expression> exp = parse_expression();
    format_string->elements.push_back(exp);
    format_string->set_end(exp);

    eat(TokenType::RightCurly);

    // lexer should only generate string or formatstring tokens at this point
    if (!(type(TokenType::FormatString) || type(TokenType::String))) {
      unexpected_token(TokenType::String);
    }

    // if the expression is followed by another FormatString token the loop
    // repeats and we parse another interpolated expression
    //
    // a regular string token signals the end of the format string
    bool final_element = type(TokenType::String);

    ref<String> element = parse_string_token();
    format_string->set_end(element);

    if (element->value.size() > 0)
      format_string->elements.push_back(element);

    if (final_element)
      return format_string;
  };
}

ref<Expression> Parser::parse_tuple(bool paren_conversion) {
  ref<Tuple> tuple = make<Tuple>();
  begin(tuple);

  eat(TokenType::LeftParen);

  do {
    // parse the empty tuple ()
    if (type(TokenType::RightParen)) {
      if (tuple->elements.size() <= 1) {
        break;
      }
    }

    ref<Expression> exp = parse_possible_spread_expression();

    // (x) is parsed as just x
    // if x is a spread expression (...x) then the tuple
    // shouldn't be omitted
    if (paren_conversion && tuple->elements.size() == 0 && type(TokenType::RightParen)) {
      if (!isa<Spread>(exp)) {
        advance();
        return exp;
      }
    }

    tuple->elements.push_back(exp);
  } while (skip(TokenType::Comma));

  end(tuple);
  eat(TokenType::RightParen);

  return tuple;
}

ref<List> Parser::parse_list() {
  ref<List> list = make<List>();
  begin(list);

  eat(TokenType::LeftBracket);

  if (!type(TokenType::RightBracket)) {
    do {
      list->elements.push_back(parse_possible_spread_expression());
    } while (skip(TokenType::Comma));
  }

  end(list);
  eat(TokenType::RightBracket);

  return list;
}

ref<Dict> Parser::parse_dict() {
  ref<Dict> dict = make<Dict>();
  begin(dict);

  eat(TokenType::LeftCurly);

  if (!type(TokenType::RightCurly)) {
    do {
      ref<Expression> key = parse_possible_spread_expression();
      ref<Expression> value = nullptr;

      if (isa<Spread>(key) && type(TokenType::Colon)) {
        unexpected_token(TokenType::Comma);
      }

      if (skip(TokenType::Colon)) {
        value = parse_expression();
      }

      dict->elements.push_back(make<DictEntry>(key, value));
    } while (skip(TokenType::Comma));
  }

  end(dict);
  eat(TokenType::RightCurly);

  validate_dict(dict);

  return dict;
}

ref<Function> Parser::parse_function(bool class_function) {
  if (!class_function && type(TokenType::RightArrow))
    return parse_arrow_function();

  Location begin = m_token.location;
  skip(TokenType::Func);

  // function name
  std::string function_name = parse_identifier_token()->value;

  // argument list
  std::vector<ref<Expression>> argument_list;
  parse_function_arguments(argument_list);

  ref<Statement> body;

  auto kwcontext = m_keyword_context;
  m_keyword_context._return = true;
  m_keyword_context._break = false;
  m_keyword_context._continue = false;
  m_keyword_context._yield = true;
  m_keyword_context._export = false;
  m_keyword_context._import = false;
  m_keyword_context._super = class_function;
  if (skip(TokenType::Assignment)) {
    body = parse_jump_statement();
  } else {
    body = parse_block();
  }
  m_keyword_context = kwcontext;

  ref<Function> node = make<Function>(function_name, body, argument_list);
  node->set_begin(begin);

  validate_function(node);

  return node;
}

ref<Function> Parser::parse_arrow_function() {
  Location begin = m_token.location;
  eat(TokenType::RightArrow);

  // argument list
  std::vector<ref<Expression>> argument_list;
  parse_function_arguments(argument_list);

  ref<Statement> body;

  auto kwcontext = m_keyword_context;
  m_keyword_context._return = true;
  m_keyword_context._break = false;
  m_keyword_context._continue = false;
  m_keyword_context._yield = true;
  m_keyword_context._export = false;
  m_keyword_context._import = false;
  m_keyword_context._super = false;
  if (type(TokenType::LeftCurly)) {
    body = parse_block();
  } else {
    body = parse_jump_statement();
  }
  m_keyword_context = kwcontext;

  ref<Function> node = make<Function>(body, argument_list);
  node->set_begin(begin);

  validate_function(node);

  return node;
}

void Parser::parse_function_arguments(std::vector<ref<Expression>>& result) {
  if (skip(TokenType::LeftParen)) {
    if (!type(TokenType::RightParen)) {
      do {
        bool spread_passed = false;
        Location spread_token_location = m_token.location;
        if (skip(TokenType::TriplePoint))
          spread_passed = true;

        ref<Expression> argument = parse_identifier_token();

        if (spread_passed) {
          argument = make<Spread>(argument);
          argument->set_begin(spread_token_location);
        }

        if (skip(TokenType::Assignment)) {
          argument = make<Assignment>(argument, parse_expression());
        }

        result.push_back(argument);
      } while (skip(TokenType::Comma));
    }

    eat(TokenType::RightParen);
  }
}

ref<Class> Parser::parse_class() {
  Location begin = m_token.location;
  eat(TokenType::Class);

  // class name
  std::string class_name = parse_identifier_token()->value;

  ref<Expression> parent;
  if (skip(TokenType::Extends)) {
    parent = parse_expression();
  }

  ref<Class> node = make<Class>(class_name, parent);
  node->set_begin(begin);

  // parse class body
  eat(TokenType::LeftCurly);

  while (!type(TokenType::RightCurly)) {
    bool static_property = false;

    if (skip(TokenType::Static))
      static_property = true;

    if (type(TokenType::Property)) {
      advance();

      std::string name = parse_identifier_token()->value;

      ref<Expression> value;
      if (skip(TokenType::Assignment)) {
        value = parse_expression();
      } else {
        value = make<Null>();
      }

      if (static_property) {
        node->static_properties.push_back(make<ClassProperty>(true, name, value));
      } else {
        node->member_properties.push_back(make<ClassProperty>(false, name, value));
      }
    } else {
      ref<Function> function = parse_function(true);

      if (static_property) {
        node->static_properties.push_back(make<ClassProperty>(true, function->name, function));
      } else {
        if (function->name.compare("constructor") == 0) {
          if (node->constructor) {
            m_console.error("duplicate constructor", function->location());
          } else {
            node->constructor = function;
          }
        } else {
          node->member_functions.push_back(function);
        }
      }
    }
  }

  end(node);
  eat(TokenType::RightCurly);

  return node;
}

ref<Int> Parser::parse_int_token() {
  match(TokenType::Int);
  ref<Int> node = make<Int>(m_token.intval);
  at(node);
  advance();
  return node;
}

ref<Float> Parser::parse_float_token() {
  match(TokenType::Float);
  ref<Float> node = make<Float>(m_token.floatval);
  at(node);
  advance();
  return node;
}

ref<Bool> Parser::parse_bool_token() {
  if (type(TokenType::True) || type(TokenType::False)) {
    ref<Bool> node = make<Bool>(type(TokenType::True));
    at(node);
    advance();
    return node;
  } else {
    unexpected_token("true or false");
  }
}

ref<Id> Parser::parse_identifier_token() {
  match(TokenType::Identifier);
  ref<Id> node = make<Id>(m_token.source);
  at(node);
  advance();
  return node;
}

ref<Char> Parser::parse_char_token() {
  match(TokenType::Character);
  ref<Char> node = make<Char>(m_token.charval);
  at(node);
  advance();
  return node;
}

ref<String> Parser::parse_string_token() {
  if (type(TokenType::String) || type(TokenType::FormatString)) {
    ref<String> node = make<String>(m_token.source);
    at(node);
    advance();
    return node;
  } else {
    unexpected_token(TokenType::String);
  }
}

ref<Null> Parser::parse_null_token() {
  match(TokenType::Null);
  ref<Null> node = make<Null>();
  at(node);
  advance();
  return node;
}

ref<Self> Parser::parse_self_token() {
  match(TokenType::Self);
  ref<Self> node = make<Self>();
  at(node);
  advance();
  return node;
}

ref<Super> Parser::parse_super_token() {
  match(TokenType::Super);
  ref<Super> node = make<Super>();
  at(node);
  advance();

  if (!m_keyword_context._super)
    m_console.error("super is not allowed at this point", node->location());

  return node;
}

void Parser::validate_defer(const ref<Defer>& node) {
  if (!isa<Block>(node->statement) && !isa<CallOp>(node->statement)) {
    m_console.error("expected a call expression", node->statement->location());
  }
}

void Parser::validate_import(const ref<Import>& node) {
  if (ref<As> as_node = cast<As>(node->source)) {
    const ref<Expression>& exp = as_node->expression;
    if (!(isa<Id>(exp) || isa<String>(exp) || isa<FormatString>(exp))) {
      m_console.error("expected an identifier or a string literal", exp->location());
    }
  } else {
    if (!isa<Id>(node->source)) {
      m_console.error("expected an identifier", node->source->location());
    }
  }
}

void Parser::validate_declaration(const ref<Declaration>& node) {
  switch (node->target->type()) {
    case Node::Type::Id: {
      return;
    }
    case Node::Type::Tuple:
    case Node::Type::Dict: {
      if (!node->target->assignable())
        m_console.error("left-hand side of declaration is not assignable", node->target->location());
      return;
    }
    default: {
      assert(false && "unexpected node");
      break;
    }
  }
}

void Parser::validate_assignment(const ref<Assignment>& node) {
  // tuple or dict assignment not allowed if the assignment
  // operator is anything else than regular assignment
  if (node->operation != TokenType::Assignment) {
    switch (node->target->type()) {
      case Node::Type::Tuple:
      case Node::Type::Dict: {
        m_console.error("this type of expression cannot be used as the left-hand side of an operator assignment",
                        node->target->location());
        return;
      }
      default: {
        break;
      }
    }
  }

  if (!node->target->assignable()) {
    m_console.error("left-hand side of assignment is not assignable", node->target->location());
  }
}

void Parser::validate_spawn(const ref<Spawn>& node) {
  if (!isa<Block>(node->statement) && !isa<CallOp>(node->statement)) {
    m_console.error("expected a call expression", node->statement->location());
  }
}

void Parser::validate_dict(const ref<Dict>& node) {
  for (ref<DictEntry>& entry : node->elements) {
    ref<Expression>& key = entry->key;
    ref<Expression>& value = entry->value;

    // key-only elements
    if (value.get() == nullptr) {
      if (isa<Id>(key))
        continue;

      if (isa<MemberOp>(key))
        continue;

      // { ...other }
      if (ref<Spread> spread = cast<Spread>(key)) {
        continue;
      }

      m_console.error("expected identifier, member access or spread expression", key->location());
      continue;
    }

    if (ref<String> string = cast<String>(key)) {
      key = make<Id>(string->value);
      continue;
    }

    // check key expression
    if (isa<Id>(key) || isa<FormatString>(key))
      continue;

    m_console.error("expected identifier or string literal", key->location());
  }
}

void Parser::validate_function(const ref<Function>& node) {
  bool default_argument_passed = false;
  bool spread_argument_passed = false;
  for (const ref<Expression>& arg : node->arguments) {
    if (spread_argument_passed) {
      // combine the location of the remaining arguments
      Location excess_arguments_location = arg->location();
      excess_arguments_location.set_end(node->arguments.back()->location());

      m_console.error("excess parameter(s)", excess_arguments_location);
      break;
    }

    if (isa<Id>(arg)) {
      if (default_argument_passed) {
        m_console.error("missing default argument", arg->location());
      }
      continue;
    }

    if (ref<Assignment> assignment = cast<Assignment>(arg)) {
      if (isa<Spread>(assignment->target)) {
        m_console.error("spread argument cannot have a default value", assignment->location());
      } else if (!isa<Id>(assignment->target)) {
        m_console.error("expected identifier", assignment->target->location());
      }

      default_argument_passed = true;
      continue;
    }

    if (ref<Spread> spread = cast<Spread>(arg)) {
      spread_argument_passed = true;
      continue;
    }

    assert(false && "unexpected node type");
  }
}

void Parser::validate_for(const ref<For>& node) {
  if (!node->target->assignable()) {
    m_console.error("expression is not assignable", node->target->location());
  }
}

void Parser::unexpected_token() {
  std::string& real_type = kTokenTypeStrings[static_cast<uint8_t>(m_token.type)];

  utils::Buffer formatbuf;
  if (m_token.type == TokenType::Eof) {
    formatbuf.append_string("unexpected end of file");
  } else {
    formatbuf.append_string("unexpected token '");
    formatbuf.append_string(real_type);
    formatbuf.append_string("'");
  }

  m_console.fatal(formatbuf.buffer_string(), m_token.location);
}

void Parser::unexpected_token(const std::string& message) {
  std::string& real_type = kTokenTypeStrings[static_cast<uint8_t>(m_token.type)];

  utils::Buffer formatbuf;

  switch (m_token.type) {
    case TokenType::Eof: {
      formatbuf.append_string("unexpected end of file");
      formatbuf.append_string(", ");
      formatbuf.append_string(message);
      break;
    }
    case TokenType::Int:
    case TokenType::Float: {
      formatbuf.append_string("unexpected numerical constant");
      formatbuf.append_string(", ");
      formatbuf.append_string(message);
      break;
    }
    case TokenType::String: {
      formatbuf.append_string("unexpected string literal");
      formatbuf.append_string(", ");
      formatbuf.append_string(message);
      break;
    }
    case TokenType::FormatString: {
      formatbuf.append_string("unexpected format string");
      formatbuf.append_string(", ");
      formatbuf.append_string(message);
      break;
    }
    default: {
      formatbuf.append_string("unexpected '");
      formatbuf.append_string(real_type);
      formatbuf.append_string("' token, ");
      formatbuf.append_string(message);
      break;
    }
  }

  m_console.fatal(formatbuf.buffer_string(), m_token.location);
}

void Parser::unexpected_token(TokenType expected) {
  std::string& real_type = kTokenTypeStrings[static_cast<uint8_t>(m_token.type)];
  std::string& expected_type = kTokenTypeStrings[static_cast<uint8_t>(expected)];

  utils::Buffer formatbuf;

  switch (m_token.type) {
    case TokenType::Eof: {
      formatbuf.append_string("unexpected end of file");
      formatbuf.append_string(", expected a '");
      formatbuf.append_string(expected_type);
      formatbuf.append_string("' token");
      break;
    }
    case TokenType::Int:
    case TokenType::Float: {
      formatbuf.append_string("unexpected numerical constant");
      formatbuf.append_string(", expected a '");
      formatbuf.append_string(expected_type);
      formatbuf.append_string("' token");
      break;
    }
    case TokenType::String: {
      formatbuf.append_string("unexpected string literal");
      formatbuf.append_string(", expected a '");
      formatbuf.append_string(expected_type);
      formatbuf.append_string("' token");
      break;
    }
    case TokenType::FormatString: {
      formatbuf.append_string("unexpected format string");
      formatbuf.append_string(", expected a '");
      formatbuf.append_string(expected_type);
      formatbuf.append_string("' token");
      break;
    }
    default: {
      formatbuf.append_string("unexpected '");
      formatbuf.append_string(real_type);
      formatbuf.append_string("' token, expected a '");
      formatbuf.append_string(expected_type);
      formatbuf.append_string("' token");
      break;
    }
  }

  m_console.fatal(formatbuf.buffer_string(), m_token.location);
}

}  // namespace charly::core::compiler
