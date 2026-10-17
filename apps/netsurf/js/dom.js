// CRTOS: the last of the scripts every page gets (after prelude.js, the polyfill library and
// newer.js): the standard parts of the window, the document and the nodes that NetSurf's
// bindings only declare - stubs that return undefined, which pages and the library alike take
// for working features - or lack. The bindings' properties are configurable, as Web IDL has
// them (nsgenbind.patch), so these replace the stubs where they are declared; every page runs
// this in a global environment of its own (dukky.c), so what it changes stays with the page.
// The layout is not reachable from scripts: sizes and positions of elements read 0.
(function (G) {
  var doc = G.document;
  var hasOwn = Object.prototype.hasOwnProperty;
  function noop() {}
  function proto(name) {
    var c = G[name];
    return c && c.prototype;
  }
  function def(obj, name, desc) {
    if (!obj)
      return;
    desc.enumerable = true;
    desc.configurable = true;
    if (!('get' in desc))
      desc.writable = true;
    try {
      Object.defineProperty(obj, name, desc);
    } catch (e) {}
  }
  function method(obj, name, fn) { def(obj, name, { value: fn }); }
  function value(obj, name, v) { def(obj, name, { value: v }); }
  function getter(obj, name, get, set) { def(obj, name, set ? { get: get, set: set } : { get: get }); }
  function own(obj, name) {
    var d = obj && Object.getOwnPropertyDescriptor(obj, name);
    return d && d.get;
  }
  function list(items) {
    items.item = function (i) { return i >= 0 && i < this.length ? this[i] : null; };
    items.namedItem = function (name) {
      for (var i = 0; i < this.length; i++)
        if (this[i].getAttribute('id') === name || this[i].getAttribute('name') === name)
          return this[i];
      return null;
    };
    return items;
  }
  // the attribute as a property: a string, a flag or a number (@dflt when it is not there)
  function reflect(p, prop, attr, kind, dflt) {
    getter(p, prop, function () {
      var v = this.getAttribute(attr);
      if (kind === 'bool')
        return v !== null;
      if (kind === 'int') {
        var n = parseInt(v, 10);
        return isNaN(n) ? (typeof dflt === 'function' ? dflt.call(this) : dflt || 0) : n;
      }
      return v === null ? (dflt || '') : v;
    }, function (v) {
      if (kind === 'bool') {
        if (v)
          this.setAttribute(attr, '');
        else
          this.removeAttribute(attr);
      } else {
        this.setAttribute(attr, String(kind === 'int' ? parseInt(v, 10) || 0 : v));
      }
    });
  }

  // ---- Window (its names for itself: prelude.js) -----------------------------------------
  value(G, 'devicePixelRatio', 1);
  value(G, 'scrollX', 0);
  value(G, 'scrollY', 0);
  value(G, 'pageXOffset', 0);
  value(G, 'pageYOffset', 0);
  ['focus', 'blur', 'stop', 'print', 'close', 'captureEvents', 'releaseEvents', 'scroll', 'scrollTo',
   'scrollBy'].forEach(function (n) {
    method(G, n, noop);
  });
  // no other windows (as with pop-ups blocked), no dialogs to answer
  method(G, 'open', function () { return null; });
  method(G, 'confirm', function () { return false; });
  method(G, 'prompt', function () { return null; });
  method(G, 'getSelection', function () {
    return { rangeCount: 0, isCollapsed: true, type: 'None', toString: function () { return ''; },
             removeAllRanges: noop, addRange: noop, getRangeAt: function () { return null; } };
  });

  // Event and CustomEvent can be constructed (the bindings' constructors throw)
  var OrigEvent = G.Event;
  function makeEvent(type, init, detail) {
    var e = doc.createEvent('Event');
    e.initEvent(String(type), !!(init && init.bubbles), !!(init && init.cancelable));
    if (detail !== undefined)
      value(e, 'detail', detail);
    return e;
  }
  if (OrigEvent && OrigEvent.prototype) {
    var Event = function Event(type, init) { return makeEvent(type, init); };
    Event.prototype = OrigEvent.prototype;
    value(G, 'Event', Event);
    var CustomEvent = function CustomEvent(type, init) {
      return makeEvent(type, init, init && init.detail !== undefined ? init.detail : null);
    };
    CustomEvent.prototype = OrigEvent.prototype;
    value(G, 'CustomEvent', CustomEvent);
  }
  getter(proto('Event'), 'timeStamp', function () {
    if (!hasOwn.call(this, '__crtos_ts'))
      Object.defineProperty(this, '__crtos_ts', { value: G.performance.now() });
    return this.__crtos_ts;
  });
  // the legacy key codes, from the key
  var keyCodes = { Enter: 13, Backspace: 8, Tab: 9, Escape: 27, ' ': 32, PageUp: 33, PageDown: 34,
                   End: 35, Home: 36, ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40,
                   Insert: 45, Delete: 46, Shift: 16, Control: 17, Alt: 18 };
  function keyCode(e) {
    var k = e.key;
    if (hasOwn.call(keyCodes, k))
      return keyCodes[k];
    return typeof k === 'string' && k.length === 1 ? k.toUpperCase().charCodeAt(0) : 0;
  }
  var KP = proto('KeyboardEvent');
  getter(KP, 'keyCode', function () { return this.type === 'keypress' ? this.charCode : keyCode(this); });
  getter(KP, 'charCode', function () {
    return this.type === 'keypress' && typeof this.key === 'string' && this.key.length === 1 ?
      this.key.charCodeAt(0) : 0;
  });
  getter(KP, 'which', function () { return this.keyCode; });
  getter(KP, 'repeat', function () { return false; });
  getter(KP, 'isComposing', function () { return false; });

  // messages to the window, delivered after the current script (setTimeout 0) to its message
  // listeners and onmessage; MessageChannel in the same way between its two ports (the
  // bindings' constructor only throws)
  function messageEvent(data, source) {
    var e = makeEvent('message');
    value(e, 'data', data);
    value(e, 'origin', G.location.origin);
    value(e, 'source', source);
    value(e, 'ports', []);
    value(e, 'lastEventId', '');
    return e;
  }
  method(G, 'postMessage', function (data) {
    G.setTimeout(function () {
      var e = messageEvent(data, G);
      G.dispatchEvent(e);
      if (typeof G.onmessage === 'function')
        G.onmessage.call(G, e);
    }, 0);
  });
  function Port() {
    this.onmessage = null;
    this._other = null;
    this._listeners = [];
  }
  Port.prototype.postMessage = function (data) {
    var to = this._other;
    G.setTimeout(function () {
      var e = messageEvent(data, null);
      if (typeof to.onmessage === 'function')
        to.onmessage.call(to, e);
      to._listeners.slice().forEach(function (f) { f.call(to, e); });
    }, 0);
  };
  Port.prototype.addEventListener = function (type, f) {
    if (type === 'message' && this._listeners.indexOf(f) < 0)
      this._listeners.push(f);
  };
  Port.prototype.removeEventListener = function (type, f) {
    var i = this._listeners.indexOf(f);
    if (type === 'message' && i >= 0)
      this._listeners.splice(i, 1);
  };
  Port.prototype.start = noop;
  Port.prototype.close = noop;
  value(G, 'MessageChannel', function MessageChannel() {
    this.port1 = new Port();
    this.port2 = new Port();
    this.port1._other = this.port2;
    this.port2._other = this.port1;
  });

  // Storage: kept while the page is open (not across pages or sessions)
  function Storage() {
    Object.defineProperty(this, '_d', { value: {} });
    Object.defineProperty(this, '_k', { value: [] });
  }
  Storage.prototype.getItem = function (k) {
    k = '$' + k;
    return hasOwn.call(this._d, k) ? this._d[k] : null;
  };
  Storage.prototype.setItem = function (k, v) {
    k = String(k);
    if (!hasOwn.call(this._d, '$' + k))
      this._k.push(k);
    this._d['$' + k] = String(v);
  };
  Storage.prototype.removeItem = function (k) {
    k = String(k);
    if (hasOwn.call(this._d, '$' + k)) {
      delete this._d['$' + k];
      this._k.splice(this._k.indexOf(k), 1);
    }
  };
  Storage.prototype.clear = function () {
    for (var i = 0; i < this._k.length; i++)
      delete this._d['$' + this._k[i]];
    this._k.length = 0;
  };
  Storage.prototype.key = function (n) { return n >= 0 && n < this._k.length ? this._k[n] : null; };
  Object.defineProperty(Storage.prototype, 'length', { get: function () { return this._k.length; } });
  value(G, 'Storage', Storage);
  value(G, 'localStorage', new Storage());
  value(G, 'sessionStorage', new Storage());

  // History: the page's own entries (the state; the address and going back stay the browser's)
  value(G, 'history', {
    length: 1, state: null, scrollRestoration: 'auto',
    back: noop, forward: noop, go: noop,
    pushState: function (state) { this.state = state; this.length++; },
    replaceState: function (state) { this.state = state; }
  });

  // animation frames at about the display's rate
  var frames = {}, frameId = 0;
  method(G, 'requestAnimationFrame', function (cb) {
    var id = ++frameId;
    frames[id] = G.setTimeout(function () {
      delete frames[id];
      cb(G.performance.now());
    }, 16);
    return id;
  });
  method(G, 'cancelAnimationFrame', function (id) {
    if (frames[id] !== undefined) {
      G.clearTimeout(frames[id]);
      delete frames[id];
    }
  });

  // media queries: width and height against the viewport, screen and all true, the rest false
  method(G, 'matchMedia', function (query) {
    var q = String(query), ok = true;
    var w = G.innerWidth, h = G.innerHeight;
    q.toLowerCase().split(/\s*,\s*/)[0].split(/\s+and\s+/).forEach(function (part) {
      var m = /\(\s*(min-|max-)?(width|height)\s*:\s*([\d.]+)(px|em|rem)?\s*\)/.exec(part), o;
      if (m) {
        var v = m[2] === 'width' ? w : h, n = parseFloat(m[3]) * (m[4] === 'em' || m[4] === 'rem' ? 16 : 1);
        if (m[1] === 'min-' ? v < n : m[1] === 'max-' ? v > n : v !== n)
          ok = false;
      } else if ((o = /\(\s*orientation\s*:\s*(\w+)\s*\)/.exec(part))) {
        if (o[1] !== (w >= h ? 'landscape' : 'portrait'))
          ok = false;
      } else if (/\(|print|speech/.test(part.replace(/^(only|not)\s+/, ''))) {
        ok = false;
      }
      if (/^not\s/.test(part))
        ok = !ok;
    });
    return { media: q, matches: ok, onchange: null, addListener: noop, removeListener: noop,
             addEventListener: noop, removeEventListener: noop, dispatchEvent: function () { return false; } };
  });

  // ---- Navigator: what this browser has - no plugins, not automated, no touch events ------
  var NP = proto('Navigator');
  function emptyList() { return list([]); }
  getter(NP, 'plugins', emptyList);
  getter(NP, 'mimeTypes', emptyList);
  getter(NP, 'webdriver', function () { return false; });
  getter(NP, 'hardwareConcurrency', function () { return 1; });
  getter(NP, 'maxTouchPoints', function () { return 0; });
  getter(NP, 'doNotTrack', function () { return null; });

  // ---- CSS selectors: querySelector(All), matches, closest, getElementsByClassName --------
  // Types, #id, .class, [attr], [attr=v] (and ~= |= ^= $= *=, with i), the combinators ' ' > +
  // ~, lists; :first-child, :last-child, :only-child, :nth-child(), :nth-last-child(),
  // :first-of-type, :last-of-type, :nth(-last)-of-type(), :not(), :root, :empty, :checked,
  // :disabled, :enabled, :link (any other pseudo-class or pseudo-element matches nothing)
  var cache = {};
  function nth(arg) {
    var a = String(arg).replace(/\s+/g, '').toLowerCase(), m;
    if (a === 'odd')
      return [2, 1];
    if (a === 'even')
      return [2, 0];
    if ((m = /^([+-]?\d*)n([+-]\d+)?$/.exec(a)))
      return [m[1] === '' || m[1] === '+' ? 1 : m[1] === '-' ? -1 : parseInt(m[1], 10), m[2] ? parseInt(m[2], 10) : 0];
    if (/^[+-]?\d+$/.test(a))
      return [0, parseInt(a, 10)];
    throw new SyntaxError("'" + arg + "' is not a valid nth argument");
  }
  function parse(text) {
    var s = String(text).replace(/^\s+|\s+$/g, '');
    if (hasOwn.call(cache, s))
      return cache[s];
    var sel = [], steps = [], c = null, comb = null, i = 0, m;
    function fail() { throw new SyntaxError("'" + text + "' is not a valid selector"); }
    function take(re) {
      m = re.exec(s.slice(i));
      if (m)
        i += m[0].length;
      return m;
    }
    function compound() {
      if (!c) {
        c = { tag: null, id: null, cls: [], attrs: [], pseudo: [] };
        steps.push({ comb: comb, c: c });
        comb = null;
      }
      return c;
    }
    if (!s)
      fail();
    while (i < s.length) {
      if (take(/^\s*,\s*/)) {
        if (!steps.length || comb)
          fail();
        sel.push(steps);
        steps = [];
        c = null;
      } else if (take(/^\s*([>+~])\s*/) || take(/^\s+/)) {
        if (!c || comb)
          fail();
        comb = m[1] || ' ';
        c = null;
      } else if (take(/^(\*|[a-zA-Z][\w-]*)/)) {
        if (c)
          fail();
        compound().tag = m[1].toLowerCase();
      } else if (take(/^#((?:[\w-]|\\.)+)/)) {
        compound().id = m[1].replace(/\\(.)/g, '$1');
      } else if (take(/^\.((?:[\w-]|\\.)+)/)) {
        compound().cls.push(m[1].replace(/\\(.)/g, '$1'));
      } else if (take(/^\[\s*([\w:-]+)\s*(?:([~|^$*]?=)\s*(?:"([^"]*)"|'([^']*)'|([^\]\s]+))\s*([iIsS])?\s*)?\]/)) {
        compound().attrs.push({ name: m[1], op: m[2],
                                v: m[3] !== undefined ? m[3] : m[4] !== undefined ? m[4] : m[5],
                                ci: m[6] === 'i' || m[6] === 'I' });
      } else if (take(/^(::?)([\w-]+)(?:\(((?:[^()]|\([^()]*\))*)\))?/)) {
        var p = { name: m[1] === '::' ? '::' + m[2] : m[2].toLowerCase(), arg: m[3] };
        if (p.name === 'not')
          p.sel = parse(p.arg);
        else if (/^nth-(last-)?(child|of-type)$/.test(p.name))
          p.nth = nth(p.arg);
        compound().pseudo.push(p);
      } else {
        fail();
      }
    }
    if (!steps.length || comb)
      fail();
    sel.push(steps);
    cache[s] = sel;
    return sel;
  }
  function tagOf(el) { return String(el.tagName).toLowerCase(); }
  function parentEl(el) {
    var p = el.parentNode;
    return p && p.nodeType === 1 ? p : null;
  }
  function prevEl(el) {
    var p = el.previousSibling;
    while (p && p.nodeType !== 1)
      p = p.previousSibling;
    return p;
  }
  function nextEl(el) {
    var p = el.nextSibling;
    while (p && p.nodeType !== 1)
      p = p.nextSibling;
    return p;
  }
  function attrOk(el, a) {
    var v = el.getAttribute(a.name);
    if (v === null || v === undefined)
      return false;
    if (!a.op)
      return true;
    var want = a.v;
    if (a.ci) {
      v = v.toLowerCase();
      want = want.toLowerCase();
    }
    switch (a.op) {
    case '=': return v === want;
    case '~=': return want !== '' && (' ' + v.replace(/\s+/g, ' ') + ' ').indexOf(' ' + want + ' ') >= 0;
    case '|=': return v === want || v.indexOf(want + '-') === 0;
    case '^=': return want !== '' && v.indexOf(want) === 0;
    case '$=': return want !== '' && v.slice(-want.length) === want;
    case '*=': return want !== '' && v.indexOf(want) >= 0;
    }
    return false;
  }
  function position(el, name) {
    var k = 1, n = el, last = /last/.test(name), type = /of-type/.test(name) ? tagOf(el) : null;
    while ((n = last ? nextEl(n) : prevEl(n)))
      if (!type || tagOf(n) === type)
        k++;
    return k;
  }
  function nthOk(k, ab) {
    if (ab[0] === 0)
      return k === ab[1];
    return (k - ab[1]) / ab[0] >= 0 && (k - ab[1]) % ab[0] === 0;
  }
  function pseudoOk(el, p) {
    switch (p.name) {
    case 'first-child': return !prevEl(el);
    case 'last-child': return !nextEl(el);
    case 'only-child': return !prevEl(el) && !nextEl(el);
    case 'first-of-type': return position(el, 'nth-of-type') === 1;
    case 'last-of-type': return position(el, 'nth-last-of-type') === 1;
    case 'nth-child':
    case 'nth-last-child':
    case 'nth-of-type':
    case 'nth-last-of-type':
      return nthOk(position(el, p.name), p.nth);
    case 'not': return !matchSel(el, p.sel);
    case 'root': return !parentEl(el);
    case 'empty': return !el.firstChild;
    case 'checked': return !!el.checked || (tagOf(el) === 'option' && !!el.selected);
    case 'disabled': return !!el.disabled;
    case 'enabled': return !el.disabled && /^(input|button|select|textarea|option|fieldset)$/.test(tagOf(el));
    case 'link':
    case 'any-link': return /^(a|area)$/.test(tagOf(el)) && el.getAttribute('href') !== null;
    }
    return false;
  }
  function compoundOk(el, c) {
    var i;
    if (!el || el.nodeType !== 1)
      return false;
    if (c.tag && c.tag !== '*' && tagOf(el) !== c.tag)
      return false;
    if (c.id !== null && el.getAttribute('id') !== c.id)
      return false;
    if (c.cls.length) {
      var cls = ' ' + String(el.getAttribute('class') || '').replace(/\s+/g, ' ') + ' ';
      for (i = 0; i < c.cls.length; i++)
        if (cls.indexOf(' ' + c.cls[i] + ' ') < 0)
          return false;
    }
    for (i = 0; i < c.attrs.length; i++)
      if (!attrOk(el, c.attrs[i]))
        return false;
    for (i = 0; i < c.pseudo.length; i++)
      if (!pseudoOk(el, c.pseudo[i]))
        return false;
    return true;
  }
  // does @el match steps[0..k], steps[k] being @el itself
  function stepsOk(el, steps, k) {
    if (!compoundOk(el, steps[k].c))
      return false;
    if (k === 0)
      return true;
    var n, comb = steps[k].comb;
    if (comb === '>')
      return !!(n = parentEl(el)) && stepsOk(n, steps, k - 1);
    if (comb === '+')
      return !!(n = prevEl(el)) && stepsOk(n, steps, k - 1);
    for (n = comb === ' ' ? parentEl(el) : prevEl(el); n; n = comb === ' ' ? parentEl(n) : prevEl(n))
      if (stepsOk(n, steps, k - 1))
        return true;
    return false;
  }
  function matchSel(el, sel) {
    for (var i = 0; i < sel.length; i++)
      if (stepsOk(el, sel[i], sel[i].length - 1))
        return true;
    return false;
  }
  function inside(root, el) {
    for (var n = el.parentNode; n; n = n.parentNode)
      if (n === root)
        return true;
    return false;
  }
  // the elements below @root of type @tag ('*': all), in document order (a fragment has no
  // getElementsByTagName)
  function below(root, tag) {
    if (root.getElementsByTagName)
      return root.getElementsByTagName(tag);
    var out = [];
    (function walk(n) {
      for (n = n.firstChild; n; n = n.nextSibling) {
        if (n.nodeType === 1) {
          if (tag === '*' || tagOf(n) === tag)
            out.push(n);
          walk(n);
        }
      }
    })(root);
    return out;
  }
  function select(root, text, first) {
    var sel = parse(text), out = [], all, i, n, el, tags = {}, names;
    // one #id in the document: its index
    if (sel.length === 1 && sel[0].length === 1 && sel[0][0].c.id !== null && (root === doc || inside(doc, root))) {
      el = doc.getElementById(sel[0][0].c.id);
      if (el && (root === doc || inside(root, el)) && compoundOk(el, sel[0][0].c))
        out.push(el);
      return first ? out[0] || null : list(out);
    }
    // the last steps' types narrow the candidates
    for (i = 0; i < sel.length; i++)
      tags[sel[i][sel[i].length - 1].c.tag || '*'] = true;
    names = Object.keys(tags);
    all = below(root, names.length === 1 ? names[0] : '*');
    for (i = 0, n = all.length; i < n; i++) {
      el = all.item ? all.item(i) : all[i];
      if (el && matchSel(el, sel)) {
        if (first)
          return el;
        out.push(el);
      }
    }
    return first ? null : list(out);
  }
  function byClass(root, names) {
    var cls = String(names).replace(/^\s+|\s+$/g, '');
    return cls ? select(root, '.' + cls.split(/\s+/).join('.'), false) : list([]);
  }
  function childElements(node) {
    var out = [];
    for (var n = node.firstChild; n; n = n.nextSibling)
      if (n.nodeType === 1)
        out.push(n);
    return list(out);
  }
  // the node arguments of append() and the others: strings become text
  function nodes(args) {
    var out = [];
    for (var i = 0; i < args.length; i++)
      out.push(typeof args[i] === 'object' && args[i] !== null ? args[i] : doc.createTextNode(String(args[i])));
    return out;
  }
  function append() {
    var ns = nodes(arguments);
    for (var i = 0; i < ns.length; i++)
      this.appendChild(ns[i]);
  }
  function prepend() {
    var ns = nodes(arguments), first = this.firstChild;
    for (var i = 0; i < ns.length; i++)
      this.insertBefore(ns[i], first);
  }
  // HTML text parsed into nodes (through the innerHTML of an element of the context's kind)
  function fragment(html, context) {
    var holder = doc.createElement(context && context.nodeType === 1 ? tagOf(context) : 'div'), out = [];
    holder.innerHTML = html;
    while (holder.firstChild)
      out.push(holder.removeChild(holder.firstChild));
    return out;
  }
  function insertAdjacent(el, where, ns) {
    var p = el.parentNode, i, ref;
    where = String(where).toLowerCase();
    if (where === 'beforebegin') {
      for (i = 0; p && i < ns.length; i++)
        p.insertBefore(ns[i], el);
    } else if (where === 'afterend') {
      for (ref = el.nextSibling, i = 0; p && i < ns.length; i++)
        p.insertBefore(ns[i], ref);
    } else if (where === 'afterbegin') {
      for (ref = el.firstChild, i = 0; i < ns.length; i++)
        el.insertBefore(ns[i], ref);
    } else if (where === 'beforeend') {
      for (i = 0; i < ns.length; i++)
        el.appendChild(ns[i]);
    } else {
      throw new SyntaxError("insertAdjacent: '" + where + "' is not a position");
    }
  }

  var EP = proto('Element');
  function matches(s) { return matchSel(this, parse(s)); }
  method(EP, 'querySelector', function (s) { return select(this, s, true); });
  method(EP, 'querySelectorAll', function (s) { return select(this, s, false); });
  method(EP, 'matches', matches);
  method(EP, 'webkitMatchesSelector', matches);
  method(EP, 'msMatchesSelector', matches);
  method(EP, 'closest', function (s) {
    var sel = parse(s);
    for (var n = this; n; n = parentEl(n))
      if (matchSel(n, sel))
        return n;
    return null;
  });
  method(EP, 'getElementsByClassName', function (names) { return byClass(this, names); });
  method(EP, 'hasAttributes', function () { return this.attributes.length > 0; });
  getter(EP, 'children', function () { return childElements(this); });
  method(EP, 'append', append);
  method(EP, 'prepend', prepend);
  method(EP, 'before', function () { insertAdjacent(this, 'beforebegin', nodes(arguments)); });
  method(EP, 'after', function () { insertAdjacent(this, 'afterend', nodes(arguments)); });
  method(EP, 'replaceWith', function () {
    var p = this.parentNode;
    if (p) {
      insertAdjacent(this, 'afterend', nodes(arguments));
      p.removeChild(this);
    }
  });
  method(EP, 'insertAdjacentHTML', function (where, html) {
    var outside = /^(beforebegin|afterend)$/i.test(where);
    insertAdjacent(this, where, fragment(html, outside ? this.parentNode : this));
  });
  method(EP, 'insertAdjacentElement', function (where, el) {
    insertAdjacent(this, where, [el]);
    return el;
  });
  method(EP, 'insertAdjacentText', function (where, text) { insertAdjacent(this, where, [doc.createTextNode(text)]); });
  getter(EP, 'outerHTML', function () {
    var holder = doc.createElement('div');
    holder.appendChild(this.cloneNode(true));
    return holder.innerHTML;
  }, function (html) {
    var p = this.parentNode;
    if (p) {
      insertAdjacent(this, 'afterend', fragment(html, p));
      p.removeChild(this);
    }
  });
  // no layout for scripts: an empty box
  method(EP, 'getBoundingClientRect', function () {
    return { x: 0, y: 0, width: 0, height: 0, top: 0, right: 0, bottom: 0, left: 0 };
  });
  method(EP, 'getClientRects', function () { return list([]); });
  method(EP, 'scrollIntoView', noop);
  ['scrollTop', 'scrollLeft'].forEach(function (n) { getter(EP, n, function () { return 0; }, noop); });
  ['clientTop', 'clientLeft'].forEach(function (n) { getter(EP, n, function () { return 0; }); });
  // the root element's client area is the viewport (pages measure the window by it)
  function isRoot(el) { return el === doc.documentElement || el === doc.body; }
  getter(EP, 'clientWidth', function () { return isRoot(this) ? G.innerWidth : 0; });
  getter(EP, 'clientHeight', function () { return isRoot(this) ? G.innerHeight : 0; });
  getter(EP, 'scrollWidth', function () { return isRoot(this) ? G.innerWidth : 0; });
  getter(EP, 'scrollHeight', function () { return isRoot(this) ? G.innerHeight : 0; });

  // ---- HTML elements -------------------------------------------------------------------
  var HP = proto('HTMLElement');
  method(HP, 'click', function () {
    if (this.disabled)
      return;
    var e = makeEvent('click', { bubbles: true, cancelable: true });
    this.dispatchEvent(e);
    // a link is followed as on a click, unless a listener prevented it
    if (!e.defaultPrevented && tagOf(this) === 'a' && this.getAttribute('href') !== null)
      G.location.href = this.href;
  });
  method(HP, 'focus', noop);
  method(HP, 'blur', noop);
  method(HP, 'forceSpellCheck', noop);
  reflect(HP, 'hidden', 'hidden', 'bool');
  reflect(HP, 'accessKey', 'accesskey');
  reflect(HP, 'tabIndex', 'tabindex', 'int', function () {
    return /^(a|area|button|input|select|textarea|iframe)$/.test(tagOf(this)) ? 0 : -1;
  });
  getter(HP, 'draggable', function () { return this.getAttribute('draggable') === 'true'; },
         function (v) { this.setAttribute('draggable', v ? 'true' : 'false'); });
  getter(HP, 'spellcheck', function () { return this.getAttribute('spellcheck') !== 'false'; },
         function (v) { this.setAttribute('spellcheck', v ? 'true' : 'false'); });
  getter(HP, 'translate', function () { return this.getAttribute('translate') !== 'no'; },
         function (v) { this.setAttribute('translate', v ? 'yes' : 'no'); });
  getter(HP, 'contentEditable', function () {
    var v = this.getAttribute('contenteditable');
    return v === null ? 'inherit' : v === '' ? 'true' : v;
  }, function (v) { this.setAttribute('contenteditable', String(v)); });
  getter(HP, 'isContentEditable', function () { return false; });
  ['offsetTop', 'offsetLeft'].forEach(function (n) { getter(HP, n, function () { return 0; }); });
  getter(HP, 'offsetWidth', function () { return isRoot(this) ? G.innerWidth : 0; });
  getter(HP, 'offsetHeight', function () { return isRoot(this) ? G.innerHeight : 0; });
  getter(HP, 'offsetParent', function () { return null; });
  function dataAttr(k) { return 'data-' + String(k).replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); }); }
  getter(HP, 'dataset', function () {
    var el = this;
    return new Proxy({}, {
      get: function (t, k) {
        if (typeof k !== 'string')
          return undefined;
        var v = el.getAttribute(dataAttr(k));
        return v === null ? undefined : v;
      },
      set: function (t, k, v) {
        el.setAttribute(dataAttr(k), String(v));
        return true;
      },
      has: function (t, k) { return el.getAttribute(dataAttr(k)) !== null; },
      deleteProperty: function (t, k) {
        el.removeAttribute(dataAttr(k));
        return true;
      }
    });
  });

  // the style attribute as a CSSStyleDeclaration: properties by their CSS names or in camel
  // case, cssText, the methods; a change rewrites the attribute (which the layout reads when
  // the page is converted - not later, NetSurf's layout being static)
  function cssName(k) {
    if (k === 'cssFloat')
      return 'float';
    return String(k).replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); })
      .replace(/^(webkit|moz|ms|o)-/, '-$1-');
  }
  function readStyle(el) {
    var out = { names: [], values: {}, prio: {} };
    String(el.getAttribute('style') || '').split(';').forEach(function (d) {
      var i = d.indexOf(':');
      if (i < 0)
        return;
      var n = d.slice(0, i).replace(/^\s+|\s+$/g, '').toLowerCase();
      var v = d.slice(i + 1).replace(/^\s+|\s+$/g, '');
      var imp = /\s*!\s*important$/i.exec(v);
      if (imp)
        v = v.slice(0, imp.index);
      if (!n || !v)
        return;
      if (!hasOwn.call(out.values, n))
        out.names.push(n);
      out.values[n] = v;
      out.prio[n] = imp ? 'important' : '';
    });
    return out;
  }
  function writeStyle(el, st) {
    var text = st.names.map(function (n) {
      return n + ': ' + st.values[n] + (st.prio[n] ? ' !important' : '');
    }).join('; ');
    if (text)
      el.setAttribute('style', text + ';');
    else
      el.removeAttribute('style');
  }
  // what 'name' in style finds: the properties the browser lays out by
  var cssProps = {};
  ('background background-attachment background-color background-image background-position ' +
   'background-repeat border border-collapse border-color border-spacing border-style border-top ' +
   'border-right border-bottom border-left border-top-color border-right-color border-bottom-color ' +
   'border-left-color border-top-style border-right-style border-bottom-style border-left-style ' +
   'border-top-width border-right-width border-bottom-width border-left-width border-width bottom ' +
   'box-sizing caption-side clear clip color columns column-count column-gap column-width content ' +
   'counter-increment counter-reset cursor direction display empty-cells flex flex-basis ' +
   'flex-direction flex-flow flex-grow flex-shrink flex-wrap align-content align-items align-self ' +
   'justify-content order float font font-family font-size font-style font-variant font-weight ' +
   'height left letter-spacing line-height list-style list-style-image list-style-position ' +
   'list-style-type margin margin-top margin-right margin-bottom margin-left max-height max-width ' +
   'min-height min-width opacity outline outline-color outline-style outline-width overflow ' +
   'overflow-x overflow-y padding padding-top padding-right padding-bottom padding-left ' +
   'page-break-after page-break-before page-break-inside position quotes right table-layout ' +
   'text-align text-decoration text-indent text-transform top unicode-bidi vertical-align ' +
   'visibility white-space width word-spacing word-wrap overflow-wrap writing-mode z-index')
    .split(' ').forEach(function (n) { cssProps[n] = true; });
  function styleOf(el) {
    var methods = {
      getPropertyValue: function (n) {
        var st = readStyle(el);
        n = String(n).toLowerCase();
        return hasOwn.call(st.values, n) ? st.values[n] : '';
      },
      getPropertyPriority: function (n) { return readStyle(el).prio[String(n).toLowerCase()] || ''; },
      setProperty: function (n, v, prio) {
        var st = readStyle(el);
        n = String(n).toLowerCase();
        v = v === null || v === undefined ? '' : String(v);
        if (v === '') {
          methods.removeProperty(n);
          return;
        }
        if (!hasOwn.call(st.values, n))
          st.names.push(n);
        st.values[n] = v;
        st.prio[n] = prio === 'important' ? 'important' : '';
        writeStyle(el, st);
      },
      removeProperty: function (n) {
        var st = readStyle(el), old;
        n = String(n).toLowerCase();
        if (!hasOwn.call(st.values, n))
          return '';
        old = st.values[n];
        st.names.splice(st.names.indexOf(n), 1);
        delete st.values[n];
        writeStyle(el, st);
        return old;
      },
      item: function (i) { return readStyle(el).names[i] || ''; }
    };
    return new Proxy({}, {
      get: function (t, k) {
        if (typeof k !== 'string')
          return undefined;
        if (hasOwn.call(methods, k))
          return methods[k];
        if (k === 'cssText') {
          var text = el.getAttribute('style');
          return text === null ? '' : text;
        }
        if (k === 'length')
          return readStyle(el).names.length;
        if (k === 'parentRule')
          return null;
        if (/^\d+$/.test(k))
          return methods.item(+k);
        return methods.getPropertyValue(cssName(k));
      },
      set: function (t, k, v) {
        if (k === 'cssText')
          el.setAttribute('style', String(v));
        else if (typeof k === 'string')
          methods.setProperty(cssName(k), v);
        return true;
      },
      has: function (t, k) {
        return typeof k === 'string' && (hasOwn.call(methods, k) || k === 'cssText' || k === 'length' ||
                                         hasOwn.call(cssProps, cssName(k)));
      }
    });
  }
  getter(HP, 'style', function () {
    if (!hasOwn.call(this, '__crtos_style'))
      Object.defineProperty(this, '__crtos_style', { value: styleOf(this) });
    return this.__crtos_style;
  }, function (v) { this.setAttribute('style', String(v)); });
  // the style the page gives an element, as far as scripts can tell: its style attribute
  method(G, 'getComputedStyle', function (el) {
    return el && el.nodeType === 1 && el.style ? el.style : styleOf(doc.createElement('div'));
  });

  // links: the parts of the address (href is the bindings', resolved)
  var urlRe = /^([a-z][a-z0-9+.-]*:)(?:\/\/(?:([^:@\/]*)(?::([^@\/]*))?@)?([^:\/?#]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/i;
  function urlParts(href) {
    var m = urlRe.exec(href || '');
    if (!m)
      return null;
    return { protocol: m[1].toLowerCase(), username: m[2] || '', password: m[3] || '',
             hostname: (m[4] || '').toLowerCase(), port: m[5] || '', pathname: m[6] || (m[4] ? '/' : ''),
             search: m[7] && m[7] !== '?' ? m[7] : '', hash: m[8] && m[8] !== '#' ? m[8] : '' };
  }
  function urlJoin(u) {
    var auth = u.username ? u.username + (u.password ? ':' + u.password : '') + '@' : '';
    var host = u.hostname + (u.port ? ':' + u.port : '');
    return u.protocol + (host || u.protocol === 'file:' ? '//' + auth + host : '') + u.pathname + u.search + u.hash;
  }
  // the URL parts of the address @get gives (@set changes it) as properties of @p
  function urlProps(p, get, set) {
    function part(name, read, write) {
      getter(p, name, function () {
        var u = urlParts(get.call(this));
        return u ? read(u) : '';
      }, function (v) {
        var u = urlParts(get.call(this));
        if (u) {
          write(u, String(v));
          set.call(this, urlJoin(u));
        }
      });
    }
    part('protocol', function (u) { return u.protocol; }, function (u, v) { u.protocol = v.replace(/:?$/, ':'); });
    part('username', function (u) { return u.username; }, function (u, v) { u.username = v; });
    part('password', function (u) { return u.password; }, function (u, v) { u.password = v; });
    part('host', function (u) { return u.hostname + (u.port ? ':' + u.port : ''); }, function (u, v) {
      var m = /^([^:]*)(?::(\d*))?$/.exec(v);
      if (m) {
        u.hostname = m[1];
        u.port = m[2] || '';
      }
    });
    part('hostname', function (u) { return u.hostname; }, function (u, v) { u.hostname = v; });
    part('port', function (u) { return u.port; }, function (u, v) { u.port = v.replace(/\D.*$/, ''); });
    part('pathname', function (u) { return u.pathname; }, function (u, v) { u.pathname = v.charAt(0) === '/' ? v : '/' + v; });
    part('search', function (u) { return u.search; }, function (u, v) {
      u.search = v && v !== '?' ? (v.charAt(0) === '?' ? v : '?' + v) : '';
    });
    part('hash', function (u) { return u.hash; }, function (u, v) {
      u.hash = v && v !== '#' ? (v.charAt(0) === '#' ? v : '#' + v) : '';
    });
    getter(p, 'origin', function () {
      var u = urlParts(get.call(this));
      return u && u.hostname ? u.protocol + '//' + u.hostname + (u.port ? ':' + u.port : '') : 'null';
    });
  }
  var AP = proto('HTMLAnchorElement');
  urlProps(AP, function () { return this.href; }, function (v) { this.href = v; });
  getter(AP, 'text', function () { return this.textContent; }, function (v) { this.textContent = v; });
  reflect(AP, 'type', 'type');
  reflect(AP, 'download', 'download');
  reflect(AP, 'ping', 'ping');
  // the location: the bindings' getters, setters that go to the new address
  var LP = proto('Location'), hrefGet = own(LP, 'href');
  var hrefSet = LP && Object.getOwnPropertyDescriptor(LP, 'href').set;
  if (hrefGet && hrefSet) {
    var parts = {};
    urlProps(parts, hrefGet, hrefSet);
    ['protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash'].forEach(function (n) {
      getter(LP, n, own(LP, n) || own(parts, n), Object.getOwnPropertyDescriptor(parts, n).set);
    });
  }
  // new URL(url, base): the library resolves against a base only through a second document,
  // which cannot resolve addresses here; the reference is resolved first (RFC 3986), then the
  // library parses the absolute address (through a link element, whose href the bindings
  // resolve)
  function removeDots(path) {
    var out = [], segs = path.split('/');
    segs.forEach(function (seg, i) {
      var last = i === segs.length - 1;
      if (seg === '..') {
        if (out.length > 1)
          out.pop();
        if (last)
          out.push('');
      } else if (seg === '.') {
        if (last)
          out.push('');
      } else {
        out.push(seg);
      }
    });
    return out.join('/') || '/';
  }
  function resolveUrl(ref, base) {
    if (/^[a-z][a-z0-9+.-]*:/i.test(ref))
      return ref;
    var b = urlParts(base);
    if (!b)
      throw new TypeError("Failed to construct 'URL': Invalid base URL");
    var auth = b.username ? b.username + (b.password ? ':' + b.password : '') + '@' : '';
    var origin = b.protocol + '//' + auth + b.hostname + (b.port ? ':' + b.port : '');
    if (ref.slice(0, 2) === '//')
      return b.protocol + ref;
    if (ref === '' || ref.charAt(0) === '#')
      return origin + b.pathname + b.search + ref;
    if (ref.charAt(0) === '?')
      return origin + b.pathname + ref;
    var m = /^([^?#]*)(.*)$/.exec(ref);
    var path = m[1].charAt(0) === '/' ? m[1] : b.pathname.replace(/[^\/]*$/, '') + m[1];
    return origin + removeDots(path) + m[2];
  }
  var LibURL = G.URL;
  if (typeof LibURL === 'function') {
    var URLWithBase = function URL(url, base) {
      if (!(this instanceof URLWithBase))
        throw new TypeError("Failed to construct 'URL': Please use the 'new' operator.");
      url = String(url);
      if (base !== undefined)
        url = resolveUrl(url, new LibURL(String(base)).href);
      else if (!/^[a-z][a-z0-9+.-]*:/i.test(url))
        throw new TypeError("Failed to construct 'URL': Invalid URL");
      return new LibURL(url);
    };
    URLWithBase.prototype = LibURL.prototype;
    Object.keys(LibURL).forEach(function (k) { URLWithBase[k] = LibURL[k]; });
    value(G, 'URL', URLWithBase);
  }

  // forms and their controls
  var FP = proto('HTMLFormElement');
  var controls = 'button, fieldset, input, object, output, select, textarea';
  getter(FP, 'elements', function () { return select(this, controls, false); });
  getter(FP, 'length', function () { return select(this, controls, false).length; });
  reflect(FP, 'name', 'name');
  reflect(FP, 'noValidate', 'novalidate', 'bool');
  reflect(FP, 'autocomplete', 'autocomplete', 'string', 'on');
  getter(FP, 'encoding', function () { return this.enctype; }, function (v) { this.enctype = v; });
  method(FP, 'reset', function () {
    select(this, 'input, textarea', false).forEach(function (c) {
      if (/^(checkbox|radio)$/i.test(c.type))
        c.checked = c.defaultChecked;
      else if (tagOf(c) === 'textarea' || !/^(submit|reset|button|image|file|hidden)$/i.test(c.type))
        c.value = c.defaultValue;
    });
  });
  method(FP, 'checkValidity', function () { return true; });
  method(FP, 'reportValidity', function () { return true; });
  function formOf() { return this.closest('form'); }
  function validity() {
    return { valid: true, valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false,
             tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false,
             customError: false };
  }
  ['HTMLInputElement', 'HTMLSelectElement', 'HTMLTextAreaElement', 'HTMLButtonElement', 'HTMLFieldSetElement',
   'HTMLOutputElement', 'HTMLObjectElement', 'HTMLLabelElement', 'HTMLLegendElement'].forEach(function (n) {
    var p = proto(n);
    getter(p, 'form', formOf);
    if (n === 'HTMLLabelElement' || n === 'HTMLLegendElement')
      return;
    method(p, 'checkValidity', function () { return true; });
    method(p, 'reportValidity', function () { return true; });
    method(p, 'setCustomValidity', noop);
    getter(p, 'willValidate', function () { return false; });
    getter(p, 'validationMessage', function () { return ''; });
    getter(p, 'validity', validity);
    getter(p, 'labels', function () {
      var id = this.getAttribute('id'), el = this;
      return list(select(doc, 'label', false).filter(function (l) {
        return (id && l.getAttribute('for') === id) || inside(l, el);
      }));
    });
  });
  var IP = proto('HTMLInputElement');
  getter(IP, 'type', own(IP, 'type'), function (v) { this.setAttribute('type', String(v)); });
  ['placeholder', 'pattern', 'min', 'max', 'step', 'autocomplete', 'dirName', 'inputMode',
   'formAction', 'formEnctype', 'formMethod', 'formTarget'].forEach(function (n) {
    reflect(IP, n, n.toLowerCase());
  });
  ['required', 'multiple', 'autofocus', 'formNoValidate'].forEach(function (n) {
    reflect(IP, n, n.toLowerCase(), 'bool');
  });
  reflect(IP, 'minLength', 'minlength', 'int', -1);
  reflect(IP, 'width', 'width', 'int');
  reflect(IP, 'height', 'height', 'int');
  getter(IP, 'list', function () {
    var id = this.getAttribute('list');
    return id ? doc.getElementById(id) : null;
  });
  getter(IP, 'indeterminate', function () { return !!this.__crtos_indeterminate; }, function (v) {
    Object.defineProperty(this, '__crtos_indeterminate', { value: !!v, configurable: true });
  });
  ['select', 'setSelectionRange', 'setRangeText', 'stepUp', 'stepDown'].forEach(function (n) { method(IP, n, noop); });
  ['selectionStart', 'selectionEnd'].forEach(function (n) {
    getter(IP, n, function () { return String(this.value).length; }, noop);
  });
  getter(IP, 'selectionDirection', function () { return 'none'; }, noop);
  getter(IP, 'files', function () { return null; });
  var SP = proto('HTMLSelectElement');
  function optionsOf(s) { return select(s, 'option', false); }
  getter(SP, 'options', function () { return optionsOf(this); });
  getter(SP, 'length', function () { return optionsOf(this).length; }, noop);
  getter(SP, 'selectedOptions', function () {
    return list(optionsOf(this).filter(function (o) { return o.selected; }));
  });
  getter(SP, 'selectedIndex', function () {
    var o = optionsOf(this);
    for (var i = 0; i < o.length; i++)
      if (o[i].selected)
        return i;
    return o.length && !this.multiple ? 0 : -1;
  }, function (v) {
    var o = optionsOf(this);
    for (var i = 0; i < o.length; i++)
      o[i].selected = i === +v;
  });
  method(SP, 'item', function (i) { return optionsOf(this).item(i); });
  method(SP, 'namedItem', function (n) { return optionsOf(this).namedItem(n); });
  method(SP, 'add', function (el, before) {
    var ref = typeof before === 'number' ? optionsOf(this)[before] : before;
    if (ref && ref.parentNode)
      ref.parentNode.insertBefore(el, ref);
    else
      this.appendChild(el);
  });
  method(SP, 'remove', function (i) {
    if (arguments.length === 0) {
      if (this.parentNode)
        this.parentNode.removeChild(this);
      return;
    }
    var o = optionsOf(this)[i];
    if (o && o.parentNode)
      o.parentNode.removeChild(o);
  });
  ['required', 'autofocus'].forEach(function (n) { reflect(SP, n, n, 'bool'); });
  reflect(SP, 'size', 'size', 'int', function () { return this.multiple ? 4 : 1; });
  reflect(SP, 'autocomplete', 'autocomplete');
  var OP = proto('HTMLOptionElement');
  getter(OP, 'index', function () {
    var s = this.closest('select');
    return s ? optionsOf(s).indexOf(this) : 0;
  });
  getter(OP, 'text', own(OP, 'text'), function (v) { this.textContent = v; });
  var TP = proto('HTMLTextAreaElement');
  ['placeholder', 'autocomplete', 'dirName', 'inputMode', 'wrap'].forEach(function (n) { reflect(TP, n, n.toLowerCase()); });
  ['required', 'autofocus'].forEach(function (n) { reflect(TP, n, n, 'bool'); });
  ['select', 'setSelectionRange', 'setRangeText'].forEach(function (n) { method(TP, n, noop); });
  reflect(proto('HTMLButtonElement'), 'autofocus', 'autofocus', 'bool');
  var ScP = proto('HTMLScriptElement');
  reflect(ScP, 'async', 'async', 'bool');
  reflect(ScP, 'crossOrigin', 'crossorigin');
  reflect(ScP, 'nonce', 'nonce');
  var ImP = proto('HTMLImageElement');
  ['srcset', 'sizes', 'crossOrigin', 'lowsrc'].forEach(function (n) { reflect(ImP, n, n.toLowerCase()); });
  getter(ImP, 'currentSrc', function () { return this.src; });
  getter(ImP, 'complete', function () { return true; });
  getter(ImP, 'naturalWidth', function () { return this.width || 0; });
  getter(ImP, 'naturalHeight', function () { return this.height || 0; });

  // ---- the document ----------------------------------------------------------------------
  var DP = proto('Document');
  method(DP, 'querySelector', function (s) { return select(this, s, true); });
  method(DP, 'querySelectorAll', function (s) { return select(this, s, false); });
  method(DP, 'getElementsByClassName', function (names) { return byClass(this, names); });
  method(DP, 'getElementsByName', function (name) {
    return list(select(this, '[name]', false).filter(function (el) { return el.getAttribute('name') === String(name); }));
  });
  getter(DP, 'children', function () { return childElements(this); });
  getter(DP, 'firstElementChild', function () { return this.documentElement; });
  getter(DP, 'lastElementChild', function () { return this.documentElement; });
  getter(DP, 'childElementCount', function () { return this.documentElement ? 1 : 0; });
  method(DP, 'append', append);
  method(DP, 'prepend', prepend);
  getter(DP, 'forms', function () { return select(this, 'form', false); });
  getter(DP, 'images', function () { return select(this, 'img', false); });
  getter(DP, 'embeds', function () { return select(this, 'embed', false); });
  getter(DP, 'plugins', function () { return select(this, 'embed', false); });
  getter(DP, 'scripts', function () { return select(this, 'script', false); });
  getter(DP, 'links', function () { return select(this, 'a[href], area[href]', false); });
  getter(DP, 'anchors', function () { return select(this, 'a[name]', false); });
  getter(DP, 'applets', function () { return list([]); });
  getter(DP, 'activeElement', function () { return this.body; });
  method(DP, 'hasFocus', function () { return true; });
  // while the page is parsed, the script that runs is the last one in the document
  getter(DP, 'currentScript', function () {
    if (this.readyState !== 'loading')
      return null;
    var s = this.getElementsByTagName('script');
    return s.length ? s.item(s.length - 1) : null;
  });
  getter(DP, 'origin', function () { return G.location.origin; });
  getter(DP, 'dir', function () {
    return this.documentElement ? this.documentElement.getAttribute('dir') || '' : '';
  }, function (v) {
    if (this.documentElement)
      this.documentElement.setAttribute('dir', String(v));
  });
  getter(DP, 'lastModified', function () {
    var d = new Date();
    function two(n) { return (n < 10 ? '0' : '') + n; }
    return two(d.getMonth() + 1) + '/' + two(d.getDate()) + '/' + d.getFullYear() + ' ' +
      two(d.getHours()) + ':' + two(d.getMinutes()) + ':' + two(d.getSeconds());
  });
  getter(DP, 'designMode', function () { return 'off'; }, noop);
  method(DP, 'execCommand', function () { return false; });
  method(DP, 'queryCommandSupported', function () { return false; });
  method(DP, 'queryCommandEnabled', function () { return false; });
  method(DP, 'queryCommandState', function () { return false; });
  method(DP, 'queryCommandValue', function () { return ''; });
  method(DP, 'captureEvents', noop);
  method(DP, 'releaseEvents', noop);
  method(DP, 'clear', noop);

  // ---- text and comments, the doctype, fragments ------------------------------------------
  // (a text node's data was undefined: it is its node value)
  function childNode(p) {
    method(p, 'before', function () { insertAdjacent(this, 'beforebegin', nodes(arguments)); });
    method(p, 'after', function () { insertAdjacent(this, 'afterend', nodes(arguments)); });
    method(p, 'replaceWith', function () {
      var parent = this.parentNode;
      if (parent) {
        insertAdjacent(this, 'afterend', nodes(arguments));
        parent.removeChild(this);
      }
    });
    method(p, 'remove', function () {
      if (this.parentNode)
        this.parentNode.removeChild(this);
    });
  }
  var CP = proto('CharacterData');
  getter(CP, 'data', function () { return String(this.nodeValue); }, function (v) {
    this.nodeValue = v === null ? '' : String(v);
  });
  getter(CP, 'length', function () { return this.data.length; });
  method(CP, 'substringData', function (at, n) { return this.data.substr(at, n); });
  method(CP, 'appendData', function (s) { this.data += String(s); });
  method(CP, 'insertData', function (at, s) {
    var d = this.data;
    this.data = d.slice(0, at) + String(s) + d.slice(at);
  });
  method(CP, 'deleteData', function (at, n) {
    var d = this.data;
    this.data = d.slice(0, at) + d.slice(at + n);
  });
  method(CP, 'replaceData', function (at, n, s) {
    var d = this.data;
    this.data = d.slice(0, at) + String(s) + d.slice(at + n);
  });
  getter(CP, 'previousElementSibling', function () { return prevEl(this); });
  getter(CP, 'nextElementSibling', function () { return nextEl(this); });
  childNode(CP);
  var DTP = proto('DocumentType');
  getter(DTP, 'name', function () { return this.nodeName; });
  getter(DTP, 'publicId', function () { return ''; });
  getter(DTP, 'systemId', function () { return ''; });
  childNode(DTP);
  var FrP = proto('DocumentFragment');
  method(FrP, 'querySelector', function (s) { return select(this, s, true); });
  method(FrP, 'querySelectorAll', function (s) { return select(this, s, false); });
  method(FrP, 'getElementById', function (id) {
    return select(this, '[id]', false).filter(function (el) { return el.getAttribute('id') === String(id); })[0] || null;
  });
  getter(FrP, 'children', function () { return childElements(this); });
  getter(FrP, 'firstElementChild', function () { return childElements(this)[0] || null; });
  getter(FrP, 'lastElementChild', function () {
    var c = childElements(this);
    return c.length ? c[c.length - 1] : null;
  });
  getter(FrP, 'childElementCount', function () { return childElements(this).length; });
  method(FrP, 'append', append);
  method(FrP, 'prepend', prepend);
})(this);
