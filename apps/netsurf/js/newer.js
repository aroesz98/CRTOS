// CRTOS: after the polyfill library, which brings ECMAScript up to 2017: the later language
// features that pages compiled for older browsers still call - Array flat, flatMap and at,
// Object.fromEntries and hasOwn, String trimStart, trimEnd, replaceAll and at, Promise
// finally, allSettled and any, queueMicrotask - each only where it is missing; and
// Promise.all of nothing, which the library resolves with undefined instead of [].
(function (G) {
  function def(obj, name, fn) {
    if (obj && typeof obj[name] !== 'function')
      Object.defineProperty(obj, name, { value: fn, writable: true, configurable: true, enumerable: false });
  }
  var ITER = typeof Symbol === 'function' && Symbol.iterator;

  // the items of an array, an array-like or an iterable
  function items(src) {
    var out = [];
    if (src === undefined || src === null)
      return out;
    if (ITER && typeof src[ITER] === 'function' && !Array.isArray(src) && typeof src !== 'string') {
      var it = src[ITER](), r;
      while (!(r = it.next()).done)
        out.push(r.value);
      return out;
    }
    for (var i = 0; i < src.length; i++)
      out.push(src[i]);
    return out;
  }
  function relative(i, n) {
    i = Number(i);
    i = i !== i ? 0 : i < 0 ? Math.ceil(i) : Math.floor(i);
    return i < 0 ? i + n : i;
  }

  // ---- Array, Object, String ---------------------------------------------------------------
  def(Array.prototype, 'flat', function (depth) {
    var out = [];
    (function add(a, d) {
      for (var i = 0; i < a.length; i++) {
        if (!(i in a))
          continue;
        if (d > 0 && Array.isArray(a[i]))
          add(a[i], d - 1);
        else
          out.push(a[i]);
      }
    })(this, depth === undefined ? 1 : Number(depth));
    return out;
  });
  def(Array.prototype, 'flatMap', function (fn, self) {
    return Array.prototype.map.call(this, fn, self).flat(1);
  });
  def(Array.prototype, 'at', function (i) {
    var n = this.length >>> 0;
    i = relative(i, n);
    return i >= 0 && i < n ? this[i] : undefined;
  });
  def(String.prototype, 'at', function (i) {
    var s = String(this);
    i = relative(i, s.length);
    return i >= 0 && i < s.length ? s.charAt(i) : undefined;
  });
  def(Object, 'fromEntries', function (src) {
    var o = {};
    items(src).forEach(function (e) { o[e[0]] = e[1]; });
    return o;
  });
  def(Object, 'hasOwn', function (o, k) { return Object.prototype.hasOwnProperty.call(Object(o), k); });
  def(String.prototype, 'trimStart', function () { return String(this).replace(/^[\s﻿\xA0]+/, ''); });
  def(String.prototype, 'trimEnd', function () { return String(this).replace(/[\s﻿\xA0]+$/, ''); });
  def(String.prototype, 'trimLeft', String.prototype.trimStart);
  def(String.prototype, 'trimRight', String.prototype.trimEnd);
  // the replacement's $$, $&, $` and $' for a match of @m at @at in @s
  function expand(repl, m, at, s) {
    return String(repl).replace(/\$([$&`'])/g, function (x, c) {
      return c === '$' ? '$' : c === '&' ? m : c === '`' ? s.slice(0, at) : s.slice(at + m.length);
    });
  }
  def(String.prototype, 'replaceAll', function (search, repl) {
    var s = String(this);
    if (search instanceof RegExp) {
      if (!search.global)
        throw new TypeError('replaceAll must be called with a global RegExp');
      return s.replace(search, repl);
    }
    search = String(search);
    function rep(at) {
      return typeof repl === 'function' ? String(repl(search, at, s)) : expand(repl, search, at, s);
    }
    var out = '', pos = 0, i;
    if (!search.length) {
      // an empty pattern matches before every character and at the end
      for (i = 0; i < s.length; i++)
        out += rep(i) + s.charAt(i);
      return out + rep(s.length);
    }
    while ((i = s.indexOf(search, pos)) !== -1) {
      out += s.slice(pos, i) + rep(i);
      pos = i + search.length;
    }
    return out + s.slice(pos);
  });

  // ---- Promise -----------------------------------------------------------------------------
  var P = G.Promise;
  if (typeof P === 'function') {
    def(P.prototype, 'finally', function (fn) {
      if (typeof fn !== 'function')
        return this.then(fn, fn);
      return this.then(function (v) {
        return P.resolve(fn()).then(function () { return v; });
      }, function (e) {
        return P.resolve(fn()).then(function () { throw e; });
      });
    });
    var all = P.all;
    Object.defineProperty(P, 'all', {
      value: function all_(src) {
        var list = items(src);
        return list.length ? all.call(this, list) : this.resolve([]);
      },
      writable: true, configurable: true
    });
    def(P, 'allSettled', function (src) {
      var C = this;
      return C.all(items(src).map(function (x) {
        return C.resolve(x).then(function (v) {
          return { status: 'fulfilled', value: v };
        }, function (e) {
          return { status: 'rejected', reason: e };
        });
      }));
    });
    def(P, 'any', function (src) {
      var C = this, list = items(src);
      return new C(function (resolve, reject) {
        var left = list.length, errors = new Array(list.length);
        function fail() {
          var e = new Error('All promises were rejected');
          e.name = 'AggregateError';
          e.errors = errors;
          reject(e);
        }
        if (!left)
          fail();
        list.forEach(function (x, i) {
          C.resolve(x).then(resolve, function (e) {
            errors[i] = e;
            if (--left === 0)
              fail();
          });
        });
      });
    });
    // after the current script, as the library runs the promises' jobs
    def(G, 'queueMicrotask', function (fn) {
      if (typeof fn !== 'function')
        throw new TypeError('queueMicrotask needs a function');
      G.setTimeout(fn, 0);
    });
  }
})(this);
