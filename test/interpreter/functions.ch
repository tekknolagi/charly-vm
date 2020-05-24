/*
 * This file is part of the Charly Virtual Machine (https://github.com/KCreate/charly-vm)
 *
 * MIT License
 *
 * Copyright (c) 2017 - 2020 Leonard Schütz
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

import "math"

export = ->(describe, it, assert) {

  describe("lambda notation", ->{

    it("allows the parens to be omitted", ->{
      let foo = ->{}
      foo()
    })

    it("allows the curly braces to be ommited", ->{
      let foo = ->(a, b) a + b
      foo(1, 2)
    })

    it("allows both parens and curly braces to be ommited", ->{
      let bar = ->{}
      let foo = ->bar()
      foo()
    })

  })

  it("calls functions", ->{
    func p(x) {
      math.sqrt(x)
    }

    assert(p(25), 5)
    assert(p(625), 25)
    assert(p(100), 10)
    assert(p(4), 2)
    assert(p(16), 4)
    assert(p(64), 8)
  })

  it("inserts the argument variable", ->{
    let received

    func foo() {
      received = arguments
    }

    foo(1, 2, 3, 4, 5)

    assert(received, [1, 2, 3, 4, 5])
  })

  it("inserts quick access identifiers", ->{
    let received

    func foo() {
      received = [
        $0, $1, $2, $3, $4
      ]
    }

    foo(1, 2, 3, 4, 5)

    assert(received, [1, 2, 3, 4, 5])
  })

  it("calls callbacks", ->{
    func g(n, c) {
      c("Got " + n)
    }

    g("f1", ->(m) assert(m, "Got f1"))
    g(500, ->(m) assert(m, "Got 500"))
    g([1, 2, 3], ->(m) assert(m, "Got [1, 2, 3]"))
  })

  it("correctly sets self value inside callbacks", ->{
    class A {
      property a1

      func c(a, cb) {
        cb(a, ->(a1) {
          self.a1 = a1
        })
      }
    }

    const o = new A(1)

    o.c(20, ->$1($0))
    assert(o.a1, 20)

    o.c(40, ->$1($0))
    assert(o.a1, 40)

    o.c(80, ->$1($0))
    assert(o.a1, 80)
  })

  it("does consecutive call expressions", ->{
    func call_me() {
      func() {
        func() {
          25
        }
      }
    }

    assert(call_me()()(), 25)
  })

  it("changes the name of a function", ->{
    func foo {}

    assert(foo.name, "foo")

    foo.name = "bar"

    assert(foo.name, "bar")
  })

  it("changes the name of a cfunction", ->{
    const foo = Charly.internals.get_method("write")

    assert(foo.name, "write")

    foo.name = "bar"

    assert(foo.name, "bar")
  })

  describe("dynamic calls", ->{
    it("calls a function with a context and arguments", ->{
      const ctx = { v: 20 }
      func foo(a, b) = @v + a + b
      assert(foo.call(ctx, [1, 2]), 23)
    })
  })

}
