// CRTOS: the first of the scripts every page gets after NetSurf's own polyfill.js
// (tools/netsurf_prepare.py puts them into polyfill.js.inc; dukky.c runs each by itself, in
// the page's own global environment): what the Window of NetSurf's bindings lacks, or only
// declares with a stub that returns undefined, and the polyfill library that runs next
// (third_party/netsurf-libs/polyfill) needs - self and the window's other names for itself,
// console.assert - and what the library leaves alone where the name exists: performance,
// Image, btoa and atob.
(function (G) {
  // NetSurf's polyfill.js has an Array.from for array-likes only: the library's takes
  // iterables too (a Set, a Map)
  if (Array.from && !/\[native code\]/.test(String(Array.from)))
    delete Array.from;

  function own(obj, name, v) {
    Object.defineProperty(obj, name, { value: v, writable: true, configurable: true, enumerable: true });
  }

  // the page's global object: the Window prototype exposes Duktape's original global under
  // this name, and scripts that install polyfills on globalThis put them where the page
  // never looks
  own(G, 'globalThis', G);
  // one window without frames: its names for itself (self is what the library runs on)
  own(G, 'self', G);
  own(G, 'frames', G);
  own(G, 'top', G);
  own(G, 'parent', G);
  own(G, 'length', 0);
  own(G, 'closed', false);
  own(G, 'opener', null);
  own(G, 'frameElement', null);
  own(G, 'status', '');

  // console.assert: the message on the console when the condition is false
  var con = G.console;
  if (con && typeof con.assert !== 'function') {
    Object.defineProperty(con, 'assert', {
      value: function (cond) {
        if (!cond)
          con.error.apply(con, ['Assertion failed:'].concat(Array.prototype.slice.call(arguments, 1)));
      },
      writable: true, configurable: true
    });
  }

  // performance: Duktape's own stays on its original global, which is not the page's
  var start = Date.now();
  if (typeof G.performance === 'undefined') {
    own(G, 'performance', {
      timeOrigin: start,
      now: function () { return Date.now() - start; },
      timing: { navigationStart: start, fetchStart: start, requestStart: start, responseStart: start,
                domLoading: start },
      navigation: { type: 0, redirectCount: 0 },
      getEntries: function () { return []; },
      getEntriesByType: function () { return []; },
      getEntriesByName: function () { return []; },
      mark: function () {},
      measure: function () {}
    });
  }
  if (typeof G.Image === 'undefined') {
    own(G, 'Image', function Image(width, height) {
      var img = G.document.createElement('img');
      if (width !== undefined)
        img.width = width;
      if (height !== undefined)
        img.height = height;
      return img;
    });
  }

  // btoa and atob: the Window prototype has stubs of them that return undefined
  var b64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  own(G, 'btoa', function btoa(s) {
    s = String(s);
    var out = '', i, c1, c2, c3;
    for (i = 0; i < s.length; i++) {
      if (s.charCodeAt(i) > 255)
        throw new Error('InvalidCharacterError: btoa of a character over U+00FF');
    }
    for (i = 0; i < s.length; i += 3) {
      c1 = s.charCodeAt(i);
      c2 = i + 1 < s.length ? s.charCodeAt(i + 1) : NaN;
      c3 = i + 2 < s.length ? s.charCodeAt(i + 2) : NaN;
      out += b64.charAt(c1 >> 2) + b64.charAt(((c1 & 3) << 4) | (isNaN(c2) ? 0 : c2 >> 4)) +
        (isNaN(c2) ? '=' : b64.charAt(((c2 & 15) << 2) | (isNaN(c3) ? 0 : c3 >> 6))) +
        (isNaN(c3) ? '=' : b64.charAt(c3 & 63));
    }
    return out;
  });
  own(G, 'atob', function atob(s) {
    s = String(s).replace(/[\t\n\f\r ]+/g, '');
    if (s.length % 4 === 0)
      s = s.replace(/==?$/, '');
    if (s.length % 4 === 1 || /[^A-Za-z0-9+\/]/.test(s))
      throw new Error('InvalidCharacterError: atob of text that is not base64');
    var out = '', bits = 0, n = 0, i;
    for (i = 0; i < s.length; i++) {
      bits = (bits << 6) | b64.indexOf(s.charAt(i));
      n += 6;
      if (n >= 8) {
        n -= 8;
        out += String.fromCharCode((bits >> n) & 255);
      }
    }
    return out;
  });
})(this);
