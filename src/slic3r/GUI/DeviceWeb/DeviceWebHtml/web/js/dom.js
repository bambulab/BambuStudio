// Minimal DOM helpers. No virtual DOM: the host pushes a full ViewModel on every
// change, so a naive innerHTML rebuild would flicker, drop scroll position and
// blur focused inputs. patchList() keeps stable nodes across pushes instead.
//
// Loaded as a classic script: ES modules are blocked by CORS under file://,
// so every unit publishes itself on the global BBL namespace.
(function (global) {
  'use strict';

  var BBL = global.BBL || (global.BBL = {});

  var SVG_NS = 'http://www.w3.org/2000/svg';
  // Tags that must be created in the SVG namespace. Missing one here is silent
  // and fatal: document.createElement() yields an HTML element that the SVG
  // renderer ignores, so the shape simply never paints.
  var SVG_TAGS = {
    svg: 1, g: 1, defs: 1, use: 1, symbol: 1, marker: 1,
    path: 1, circle: 1, ellipse: 1, rect: 1, line: 1, polyline: 1, polygon: 1,
    text: 1, tspan: 1, textPath: 1,
    mask: 1, clipPath: 1, pattern: 1, image: 1, foreignObject: 1,
    linearGradient: 1, radialGradient: 1, stop: 1,
    filter: 1, feGaussianBlur: 1, feOffset: 1, feBlend: 1, feColorMatrix: 1,
    feComposite: 1, feFlood: 1, feMerge: 1, feMergeNode: 1, feDropShadow: 1,
    animate: 1, animateTransform: 1,
  };

  function create(tag) {
    return SVG_TAGS[tag] ? document.createElementNS(SVG_NS, tag) : document.createElement(tag);
  }

  function setAttr(node, name, value) {
    if (value === null || value === undefined || value === false) {
      node.removeAttribute(name);
      return;
    }
    var next = value === true ? '' : String(value);
    // Reading before writing avoids style recalc churn on unchanged attributes,
    // which matters because the host re-pushes state several times per second.
    if (node.getAttribute(name) !== next) node.setAttribute(name, next);
  }

  function setText(node, value) {
    var next = value === null || value === undefined ? '' : String(value);
    if (node.textContent !== next) node.textContent = next;
  }

  function toggleClass(node, name, on) {
    if (!name) return;
    if (node.classList.contains(name) !== !!on) node.classList.toggle(name, !!on);
  }

  function applyProps(node, props) {
    if (!props) return;
    Object.keys(props).forEach(function (key) {
      var value = props[key];
      if (key === 'class' || key === 'className') {
        setAttr(node, 'class', value);
      } else if (key === 'text') {
        setText(node, value);
      } else if (key === 'html') {
        if (node.innerHTML !== value) node.innerHTML = value;
      } else if (key === 'style' && value && typeof value === 'object') {
        Object.keys(value).forEach(function (prop) { node.style[prop] = value[prop]; });
      } else if (key === 'dataset' && value && typeof value === 'object') {
        Object.keys(value).forEach(function (prop) { node.dataset[prop] = value[prop]; });
      } else if (key.indexOf('on') === 0 && typeof value === 'function') {
        node.addEventListener(key.slice(2).toLowerCase(), value);
      } else {
        setAttr(node, key, value);
      }
    });
  }

  function appendChildren(node, children) {
    if (children === null || children === undefined || children === false) return;
    if (Array.isArray(children)) {
      children.forEach(function (child) { appendChildren(node, child); });
      return;
    }
    if (children instanceof Node) {
      node.appendChild(children);
      return;
    }
    node.appendChild(document.createTextNode(String(children)));
  }

  // el('div', { class: 'x', onClick: fn }, [child, 'text'])
  function el(tag, props, children) {
    var node = create(tag);
    applyProps(node, props);
    appendChildren(node, children);
    return node;
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  // Keyed reconciliation for one level of children.
  //
  // keyFn MUST produce a key that is unique across the whole list. For AMS slots
  // that means combining ams_id and slot_id: display_index alone collides between
  // slots living in different AMS units.
  //
  // render(item, existingNode) returns either a fresh node (when existingNode is
  // null) or updates and returns existingNode.
  //
  // offset: number of leading children the caller owns and patchList must not
  // touch. The stage puts dryer-chamber decorations before the slot columns, and
  // without this the reordering pass would treat them as stale list entries.
  function patchList(container, items, keyFn, render, offset) {
    var lead = offset || 0;
    var existing = container.__bblKeyed || {};
    var next = {};
    var ordered = [];

    (items || []).forEach(function (item, index) {
      var key = String(keyFn(item, index));
      var node = render(item, existing[key] || null, index);
      if (!node) return;
      node.__bblKey = key;
      next[key] = node;
      ordered.push(node);
    });

    Object.keys(existing).forEach(function (key) {
      var stale = existing[key];
      // If the key is gone, or if render() returned a brand-new node rather
      // than updating the existing node in-place, the old DOM node must be
      // detached. Otherwise it stays behind in the container and the insertion
      // loop below piles new nodes in front of it, leaking N items per render.
      if (!next[key] || stale !== next[key]) {
        if (stale && stale.parentNode === container) container.removeChild(stale);
      }
    });

    // Reorder in place: only touch the DOM where the sequence actually differs.
    ordered.forEach(function (node, index) {
      var current = container.childNodes[lead + index];
      if (current !== node) container.insertBefore(node, current || null);
    });

    container.__bblKeyed = next;
    return ordered;
  }

  BBL.dom = {
    el: el,
    setAttr: setAttr,
    setText: setText,
    toggleClass: toggleClass,
    clear: clear,
    patchList: patchList,
  };
})(window);
