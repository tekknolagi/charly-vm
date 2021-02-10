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

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cassert>

#include "charly/core/compiler/location.h"
#include "charly/core/compiler/token.h"
#include "charly/core/compiler/ir/builtin.h"

#pragma once

namespace charly::core::compiler::ast {

template <typename T>
using ref = std::shared_ptr<T>;

template <typename T, typename... Args>
inline ref<T> make(Args... params) {
  return std::make_shared<T>(std::forward<Args>(params)...);
}

template <typename T, typename O>
inline ref<T> cast(ref<O> node) {
  return std::dynamic_pointer_cast<T>(node);
}

class Pass;  // forward declaration

// base class of all ast nodes
class Node : std::enable_shared_from_this<Node> {
  friend class Pass;

public:
  enum class Type : uint8_t {
    Unknown = 0,

    // Toplevel
    Program = 1,

    // Statements
    Block,
    Return,
    Break,
    Continue,
    Defer,
    Throw,
    Export,
    Import,

    // Control Expressions
    Yield,
    Spawn,
    Await,
    Typeof,

    // Literals
    Id,
    Name,
    Int,
    Float,
    Bool,
    Char,
    String,
    FormatString,
    Null,
    Self,
    Super,
    Tuple,
    List,
    DictEntry,
    Dict,
    FunctionArgument,
    Function,
    Class,
    ClassProperty,

    // Expressions
    Assignment,
    Ternary,
    BinaryOp,
    UnaryOp,
    Spread,
    CallOp,
    MemberOp,
    IndexOp,

    // Declaration
    Declaration,
    UnpackDeclaration,

    // Control structures
    If,
    While,
    Try,
    Switch,
    SwitchCase,
    For,

    // Intrinsic Operations
    BuiltinOperation
  };

  // search for a node by comparing the ast depth-first
  // with a compare function
  //
  // a second skip function can be used to skip traversal
  // of certain node types
  static ref<Node> search(const ref<Node>& node,
                                 std::function<bool(const ref<Node>&)> compare,
                                 std::function<bool(const ref<Node>&)> skip);

  operator Location() const {
    return m_location;
  }

  const Location& location() const {
    return m_location;
  }

  void set_location(const Location& loc) {
    m_location = loc;
  }

  void set_location(const ref<Node>& node) {
    m_location = node->location();
  }

  void set_location(const Location& begin, const Location& end) {
    set_begin(begin);
    set_end(end);
  }

  void set_location(const ref<Node>& begin, const ref<Node>& end) {
    set_location(begin->location(), end->location());
  }

  void set_begin(const Location& loc) {
    m_location.set_begin(loc);
  }

  void set_end(const Location& loc) {
    m_location.set_end(loc);
  }

  void set_begin(const ref<Node>& node) {
    set_begin(node->location());
  }

  void set_end(const ref<Node>& node) {
    set_end(node->location());
  }

  virtual Type type() const = 0;
  virtual const char* node_name() const = 0;

  virtual bool assignable() const {
    return false;
  }

  virtual void children(std::function<void(const ref<Node>&)>&&) const {}

  // dump a textual representation of this node into the stream
  void dump(std::ostream& out, bool print_location = false) const;

  // child classes may override and write additional info into the node output
  virtual void dump_info(std::ostream&) const {}

protected:
  virtual ~Node(){};

  Location m_location = { .valid = false };
};

template <typename T>
bool isa(const ref<Node>& node) {
  return dynamic_cast<T*>(node.get()) != nullptr;
}

#define AST_NODE(T)                                \
public:                                            \
  virtual Node::Type type() const override {       \
    return Node::Type::T;                          \
  }                                                \
                                                   \
private:                                           \
  virtual const char* node_name() const override { \
    return #T;                                     \
  }

#define CHILD_NODE(N) \
  {                   \
    if (N)            \
      callback(N);    \
  }

#define CHILD_VECTOR(N)          \
  {                              \
    for (const auto& node : N) { \
      callback(node);            \
    }                            \
  }

#define CHILDREN() \
  virtual void children([[maybe_unused]] std::function<void(const ref<Node>&)>&& callback) const override

// {
//   <statement>
// }
class Statement : public Node {};

// 1 + x, false, foo(bar)
class Expression : public Statement {};

// {
//   ...
// }
class Block final : public Statement {
  AST_NODE(Block)
public:
  template <typename... Args>
  Block(Args&&... params) : statements({ std::forward<Args>(params)... }) {
    if (this->statements.size() > 0) {
      this->set_begin(this->statements.front()->location());
      this->set_end(this->statements.back()->location());
    }
  }

  std::vector<ref<Statement>> statements;

  CHILDREN() {
    CHILD_VECTOR(statements);
  }
};

// top level node of a compiled program
class Program final : public Node {
  AST_NODE(Program)
public:
  Program(ref<Statement> body = nullptr) : body(body) {
    this->set_location(body);
  }

  ref<Statement> body;

  CHILDREN() {
    CHILD_NODE(body);
  }
};

// return <exp>
class Return final : public Statement {
  AST_NODE(Return)
public:
  Return(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// break
class Break final : public Statement {
  AST_NODE(Break)
};

// continue
class Continue final : public Statement {
  AST_NODE(Continue)
};

// defer <statement>
class Defer final : public Statement {
  AST_NODE(Defer)
public:
  Defer(ref<Statement> statement) : statement(statement) {
    this->set_location(statement);
  }

  ref<Block> body;
  ref<Statement> statement;

  CHILDREN() {
    CHILD_NODE(body);
    CHILD_NODE(statement);
  }
};

// throw <expression>
class Throw final : public Statement {
  AST_NODE(Throw)
public:
  Throw(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// export <expression>
class Export final : public Statement {
  AST_NODE(Export)
public:
  Export(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// import <identifier>
// import <identifier> as <identifier>
// import <string> as <identifier>
class Import final : public Expression {
  AST_NODE(Import)
public:
  Import(ref<Expression> source) : source(source) {
    this->set_location(source);
  }

  ref<Expression> source;

  CHILDREN() {
    CHILD_NODE(source);
  }
};

// yield <expression>
class Yield final : public Expression {
  AST_NODE(Yield)
public:
  Yield(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// spawn <statement>
class Spawn final : public Expression {
  AST_NODE(Spawn)
public:
  Spawn(ref<Statement> statement) : statement(statement) {
    this->set_location(statement);
  }

  bool execute_immediately = true; // set by desugar pass
  ref<Statement> statement;

  CHILDREN() {
    CHILD_NODE(statement);
  }
};

// await <expression>
class Await final : public Expression {
  AST_NODE(Await)
public:
  Await(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// typeof <expression>
class Typeof final : public Expression {
  AST_NODE(Typeof)
public:
  Typeof(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// <target> <operation>= <source>
class Assignment final : public Expression {
  AST_NODE(Assignment)
public:
  Assignment(ref<Expression> target, ref<Expression> source) :
    operation(TokenType::Assignment), target(target), source(source) {
    this->set_begin(target);
    this->set_end(source);
  }
  Assignment(TokenType operation, ref<Expression> target, ref<Expression> source) :
    operation(operation), target(target), source(source) {
    this->set_begin(target);
    this->set_end(source);
  }

  TokenType operation;
  ref<Expression> target;
  ref<Expression> source;

  CHILDREN() {
    CHILD_NODE(target);
    CHILD_NODE(source);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// <condition> ? <then_exp> : <else_exp>
class Ternary final : public Expression {
  AST_NODE(Ternary)
public:
  Ternary(ref<Expression> condition, ref<Expression> then_exp, ref<Expression> else_exp) :
    condition(condition), then_exp(then_exp), else_exp(else_exp) {
    this->set_begin(condition);
    this->set_end(else_exp);
  }

  ref<Expression> condition;
  ref<Expression> then_exp;
  ref<Expression> else_exp;

  CHILDREN() {
    CHILD_NODE(condition);
    CHILD_NODE(then_exp);
    CHILD_NODE(else_exp);
  }
};

// <lhs> <operation> <rhs>
class BinaryOp final : public Expression {
  AST_NODE(BinaryOp)
public:
  BinaryOp(TokenType operation, ref<Expression> lhs, ref<Expression> rhs) : operation(operation), lhs(lhs), rhs(rhs) {
    this->set_begin(lhs);
    this->set_end(rhs);
  }

  TokenType operation;
  ref<Expression> lhs;
  ref<Expression> rhs;

  CHILDREN() {
    CHILD_NODE(lhs);
    CHILD_NODE(rhs);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// <operation> <expression>
class UnaryOp final : public Expression {
  AST_NODE(UnaryOp)
public:
  UnaryOp(TokenType operation, ref<Expression> expression) : operation(operation), expression(expression) {
    this->set_location(expression);
  }

  TokenType operation;
  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// ...<exp>
class Spread final : public Expression {
  AST_NODE(Spread)
public:
  Spread(ref<Expression> expression) : expression(expression) {
    this->set_location(expression);
  }

  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }
};

// null
class Null final : public Expression {
  AST_NODE(Null)
};

// self
class Self final : public Expression {
  AST_NODE(Self)
};

// super
class Super final : public Expression {
  AST_NODE(Super)
};

template <typename T>
class Atom : public Expression {
public:
  Atom(T value) : value(value) {}
  T value;
};

// foo, bar, $_baz42
class Name;  // forward declaration
class Id final : public Atom<std::string> {
  AST_NODE(Id)
public:
  using Atom<std::string>::Atom;

  Id(const ref<Name>& name);
  Id(const std::string& name) : Id(make<Name>(name)) {}

  virtual bool assignable() const override {
    return true;
  }

  virtual void dump_info(std::ostream& out) const override;
};

// used to represent names that do not refer to a variable
class Name final : public Atom<std::string> {
  AST_NODE(Name)
public:
  using Atom<std::string>::Atom;

  Name(const ref<Id>& id) : Atom<std::string>::Atom(id->value) {
    this->set_location(id);
  }

  Name(const std::string& value) : Atom<std::string>::Atom(value) {}

  virtual bool assignable() const override {
    return false;
  }

  virtual void dump_info(std::ostream& out) const override;
};

inline Id::Id(const ref<Name>& name) : Atom<std::string>::Atom(name->value) {
  this->set_location(name);
}

// 1, 2, 42
class Int final : public Atom<int64_t> {
  AST_NODE(Int)
public:
  using Atom<int64_t>::Atom;

  virtual void dump_info(std::ostream& out) const override;
};

// 0.5, 25.25, 5000.1234
class Float final : public Atom<double> {
  AST_NODE(Float)
public:
  using Atom<double>::Atom;

  virtual void dump_info(std::ostream& out) const override;
};

// true, false
class Bool final : public Atom<bool> {
  AST_NODE(Bool)
public:
  using Atom<bool>::Atom;

  virtual void dump_info(std::ostream& out) const override;
};

// 'a', '\n', 'ä', 'π'
class Char final : public Atom<uint32_t> {
  AST_NODE(Char)
public:
  using Atom<uint32_t>::Atom;

  virtual void dump_info(std::ostream& out) const override;
};

// "hello world"
class String final : public Atom<std::string> {
  AST_NODE(String)
public:
  using Atom<std::string>::Atom;

  virtual void dump_info(std::ostream& out) const override;
};

// "name: {name} age: {age}"
class FormatString final : public Expression {
  AST_NODE(FormatString)
public:
  template <typename... Args>
  FormatString(Args&&... params) : elements({ std::forward<Args>(params)... }) {}

  std::vector<ref<Expression>> elements;

  CHILDREN() {
    CHILD_VECTOR(elements);
  }
};

// (1, 2, 3)
class Tuple final : public Expression {
  AST_NODE(Tuple)
public:
  template <typename... Args>
  Tuple(Args&&... params) : elements({ std::forward<Args>(params)... }) {}

  std::vector<ref<Expression>> elements;

  virtual bool assignable() const override {
    if (elements.size() == 0)
      return false;

    bool spread_passed = false;
    for (const ref<Expression>& node : elements) {
      if (isa<Name>(node)) {
        continue;
      }

      if (ref<Spread> spread = cast<Spread>(node)) {
        if (spread_passed)
          return false;

        spread_passed = true;

        if (!isa<Name>(spread->expression)) {
          return false;
        }

        continue;
      }

      return false;
    }

    return true;
  }

  CHILDREN() {
    CHILD_VECTOR(elements);
  }
};

// [1, 2, 3]
class List final : public Expression {
  AST_NODE(List)
public:
  template <typename... Args>
  List(Args&&... params) : elements({ std::forward<Args>(params)... }) {}

  std::vector<ref<Expression>> elements;

  CHILDREN() {
    CHILD_VECTOR(elements);
  }
};

// { a: 1, b: false, c: foo }
class DictEntry final : public Node {
  AST_NODE(DictEntry)
public:
  DictEntry(ref<Expression> key) : DictEntry(key, nullptr) {}
  DictEntry(ref<Expression> key, ref<Expression> value) : key(key), value(value) {
    if (value) {
      this->set_begin(key);
      this->set_end(value);
    } else {
      this->set_location(key);
    }
  }

  ref<Expression> key;
  ref<Expression> value;

  virtual bool assignable() const override {
    if (value.get())
      return false;

    if (isa<Name>(key))
      return true;

    if (ref<Spread> spread = cast<Spread>(key)) {
      return isa<Name>(spread->expression);
    }

    return false;
  }

  CHILDREN() {
    CHILD_NODE(key);
    CHILD_NODE(value);
  }
};

// { a: 1, b: false, c: foo }
class Dict final : public Expression {
  AST_NODE(Dict)
public:
  template <typename... Args>
  Dict(Args&&... params) : elements({ std::forward<Args>(params)... }) {}

  std::vector<ref<DictEntry>> elements;

  virtual bool assignable() const override {
    if (elements.size() == 0)
      return false;

    bool spread_passed = false;
    for (const ref<DictEntry>& node : elements) {
      if (!node->assignable())
        return false;

      if (isa<Spread>(node->key)) {
        if (spread_passed)
          return false;
        spread_passed = true;
      }
    }

    return true;
  }

  CHILDREN() {
    CHILD_VECTOR(elements);
  }
};

class FunctionArgument final : public Node {
  AST_NODE(FunctionArgument)
public:
  FunctionArgument(ref<Name> name, ref<Expression> default_value = nullptr) :
    self_initializer(false), spread_initializer(false), name(name), default_value(default_value) {
    if (default_value) {
      this->set_location(name, default_value);
    } else {
      this->set_location(name);
    }
  }
  FunctionArgument(bool self_initializer,
                   bool spread_initializer,
                   ref<Name> name,
                   ref<Expression> default_value = nullptr) :
    self_initializer(self_initializer),
    spread_initializer(spread_initializer),
    name(name),
    default_value(default_value) {
    if (default_value) {
      this->set_location(name, default_value);
    } else {
      this->set_location(name);
    }
  }

  bool self_initializer;
  bool spread_initializer;
  ref<Name> name;
  ref<Expression> default_value;

  CHILDREN() {
    CHILD_NODE(default_value);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// func foo(a, b = 1, ...rest) {}
// ->(a, b) a + b
class Function final : public Expression {
  AST_NODE(Function)
public:
  // regular functions
  template <typename... Args>
  Function(bool arrow_function, ref<Name> name, ref<Statement> body, Args&&... params) :
    arrow_function(arrow_function), name(name), body(body), arguments({ std::forward<Args>(params)... }) {
    this->set_location(name, body);
  }
  Function(bool arrow_function, ref<Name> name, ref<Statement> body, std::vector<ref<FunctionArgument>>&& params) :
    arrow_function(arrow_function), name(name), body(body), arguments(std::move(params)) {
    this->set_location(name, body);
  }

  bool arrow_function;
  ref<Name> name;
  ref<Statement> body;
  std::vector<ref<FunctionArgument>> arguments;

  CHILDREN() {
    CHILD_VECTOR(arguments);
    CHILD_NODE(body);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// property foo
// static property bar = 42
class ClassProperty final : public Expression {
  AST_NODE(ClassProperty)
public:
  ClassProperty(bool is_static, ref<Name> name, ref<Expression> value) :
    is_static(is_static), name(name), value(value) {
    this->set_location(name, value);
  }
  ClassProperty(bool is_static, const std::string& name, ref<Expression> value) :
    is_static(is_static), name(make<Name>(name)), value(value) {
    this->set_location(value);
  }

  bool is_static;
  ref<Name> name;
  ref<Expression> value;

  CHILDREN() {
    CHILD_NODE(value);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// class <name> [extends <parent>] { ... }
class Class final : public Expression {
  AST_NODE(Class)
public:
  Class(ref<Name> name, ref<Expression> parent) : name(name), parent(parent), constructor(nullptr) {}
  Class(const std::string& name, ref<Expression> parent) :
    name(make<Name>(name)), parent(parent), constructor(nullptr) {}

  ref<Name> name;
  ref<Expression> parent;
  ref<Function> constructor;
  std::vector<ref<Function>> member_functions;
  std::vector<ref<ClassProperty>> member_properties;
  std::vector<ref<ClassProperty>> static_properties;

  CHILDREN() {
    CHILD_NODE(parent);
    CHILD_NODE(constructor);
    CHILD_VECTOR(member_functions);
    CHILD_VECTOR(member_properties);
    CHILD_VECTOR(static_properties);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// <target>(<arguments>)
class CallOp final : public Expression {
  AST_NODE(CallOp)
public:
  template <typename... Args>
  CallOp(ref<Expression> target, Args&&... params) : target(target), arguments({ std::forward<Args>(params)... }) {
    this->set_begin(target);
    if (arguments.size() && arguments.back().get())
      this->set_end(arguments.back()->location());
  }

  ref<Expression> target;
  std::vector<ref<Expression>> arguments;

  CHILDREN() {
    CHILD_NODE(target);
    CHILD_VECTOR(arguments);
  }
};

// <target>.<member>
class MemberOp final : public Expression {
  AST_NODE(MemberOp)
public:
  MemberOp(ref<Expression> target, ref<Name> member) : target(target), member(member) {
    this->set_begin(target);
    this->set_end(member);
  }
  MemberOp(ref<Expression> target, const std::string& member) : target(target), member(make<Name>(member)) {
    this->set_location(target);
  }

  ref<Expression> target;
  ref<Name> member;

  virtual bool assignable() const override {
    return true;
  }

  CHILDREN() {
    CHILD_NODE(target);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// <target>[<index>]
class IndexOp final : public Expression {
  AST_NODE(IndexOp)
public:
  IndexOp(ref<Expression> target, ref<Expression> index) : target(target), index(index) {
    this->set_begin(target);
    this->set_end(index);
  }

  ref<Expression> target;
  ref<Expression> index;

  virtual bool assignable() const override {
    return true;
  }

  CHILDREN() {
    CHILD_NODE(target);
    CHILD_NODE(index);
  }
};

// let a
// let a = 2
// const b = 3
class Declaration final : public Expression {
  AST_NODE(Declaration)
public:
  Declaration(ref<Name> name, ref<Expression> expression, bool constant = false) :
    constant(constant), name(name), expression(expression) {
    this->set_begin(name);
    this->set_end(expression);
  }
  Declaration(const std::string& name, ref<Expression> expression, bool constant = false) :
    constant(constant), name(make<Name>(name)), expression(expression) {
    this->set_location(expression);
  }

  bool constant;
  ref<Name> name;
  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(expression);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// let (a, ...b, c) = 1
// const (a, ...b, c) = x
class UnpackDeclaration final : public Expression {
  AST_NODE(UnpackDeclaration)
public:
  UnpackDeclaration(ref<Expression> target, ref<Expression> expression, bool constant = false) :
    constant(constant), target(target), expression(expression) {
    this->set_begin(target);
    this->set_end(expression);
  }

  bool constant;
  ref<Expression> target;
  ref<Expression> expression;

  CHILDREN() {
    CHILD_NODE(target);
    CHILD_NODE(expression);
  }

  virtual void dump_info(std::ostream& out) const override;
};

// if <condition>
//   <then_stmt>
// else
//   <else_stmt>
class If final : public Expression {
  AST_NODE(If)
public:
  If(ref<Expression> condition, ref<Statement> then_stmt, ref<Statement> else_stmt = nullptr) :
    condition(condition), then_stmt(then_stmt), else_stmt(else_stmt) {
    this->set_begin(condition);

    if (else_stmt) {
      this->set_end(else_stmt);
    }
  }

  ref<Expression> condition;
  ref<Statement> then_stmt;
  ref<Statement> else_stmt;

  CHILDREN() {
    CHILD_NODE(condition);
    CHILD_NODE(then_stmt);
    CHILD_NODE(else_stmt);
  }
};

// while <condition>
//   <then_stmt>
class While final : public Expression {
  AST_NODE(While)
public:
  While(ref<Expression> condition, ref<Statement> then_stmt) : condition(condition), then_stmt(then_stmt) {
    this->set_begin(condition);
    this->set_end(then_stmt);
  }

  ref<Expression> condition;
  ref<Statement> then_stmt;

  CHILDREN() {
    CHILD_NODE(condition);
    CHILD_NODE(then_stmt);
  }
};

// try <try_stmt>
// [catch (<exception_name>) <catch_stmt>]
class Try final : public Expression {
  AST_NODE(Try)
public:
  Try(ref<Statement> try_stmt, ref<Name> exception_name, ref<Statement> catch_stmt) :
    try_stmt(try_stmt), exception_name(exception_name), catch_stmt(catch_stmt) {
    this->set_begin(try_stmt);
    this->set_end(catch_stmt);
  }
  Try(ref<Statement> try_stmt, const std::string& exception_name, ref<Statement> catch_stmt) :
    try_stmt(try_stmt), exception_name(make<Name>(exception_name)), catch_stmt(catch_stmt) {
    this->set_begin(try_stmt);
    this->set_end(catch_stmt);
  }

  ref<Statement> try_stmt;
  ref<Name> exception_name;
  ref<Statement> catch_stmt;

  CHILDREN() {
    CHILD_NODE(try_stmt);
    CHILD_NODE(catch_stmt);
  }

  virtual void dump_info(std::ostream& out) const override;
};

class SwitchCase final : public Expression {
  AST_NODE(SwitchCase)
public:
  SwitchCase(ref<Expression> test, ref<Statement> stmt) : test(test), stmt(stmt) {
    this->set_begin(test);
    this->set_end(stmt);
  }

  ref<Expression> test;
  ref<Statement> stmt;

  CHILDREN() {
    CHILD_NODE(test);
    CHILD_NODE(stmt);
  }
};

// switch (<test>) { case <test> <stmt> default <default_stmt> }
class Switch final : public Expression {
  AST_NODE(Switch)
public:
  Switch(ref<Expression> test) : test(test), default_stmt(nullptr) {
    this->set_location(test);
  }
  template <typename... Args>
  Switch(ref<Expression> test, ref<Statement> default_stmt, Args&&... params) :
    test(test), default_stmt(default_stmt), cases({ std::forward<Args>(params)... }) {
    this->set_begin(test);
    if (default_stmt) {
      this->set_end(default_stmt);
    } else {
      this->set_end(test);
    }
  }

  ref<Expression> test;
  ref<Statement> default_stmt;
  std::vector<ref<SwitchCase>> cases;

  CHILDREN() {
    CHILD_NODE(test);
    CHILD_NODE(default_stmt);
    CHILD_VECTOR(cases);
  }
};

// for <target> in <source> <stmt>
class For final : public Expression {
  AST_NODE(For)
public:
  For(ref<Statement> declaration, ref<Statement> stmt) : declaration(declaration), stmt(stmt) {
    this->set_begin(declaration);
    this->set_end(stmt);
  }

  ref<Statement> declaration;
  ref<Statement> stmt;

  CHILDREN() {
    CHILD_NODE(declaration);
    CHILD_NODE(stmt);
  }
};

// builtin(builtin_id, <arguments>...)
class BuiltinOperation final : public Expression {
  AST_NODE(BuiltinOperation)
public:
  template <typename... Args>
  BuiltinOperation(const std::string& name, Args&&... params) :
    name(name), arguments({ std::forward<Args>(params)... }) {
    assert(ir::kBuiltinNameMapping.count(name));

    if (arguments.size()) {
      this->set_location(arguments.front()->location(), arguments.back()->location());
    }
  }

  std::string name;
  std::vector<ref<Expression>> arguments;

  CHILDREN() {
    CHILD_VECTOR(arguments);
  }

  virtual void dump_info(std::ostream& out) const override;
};

#undef AST_NODE
#undef CHILD_NODE
#undef CHILD_VECTOR
#undef CHILDREN

}  // namespace charly::core::compiler::ast
