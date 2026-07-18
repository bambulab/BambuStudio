/*
 * mini-dc.js
 *
 * A deliberately small, dependency-free runtime for the Design Component
 * template dialect used by the Bambu Studio design documents. Templates are
 * compiled once to a static AST, evaluated to keyed VNodes, and patched in
 * place. The in-place patch is essential: it preserves focused inputs, caret
 * positions, and imported component instances across renders.
 */
(function initMiniDC(global) {
  'use strict';

  const registry = new Map();
  const EVENT_STORE = Symbol('dcEvents');
  const HOVER_STORE = Symbol('dcHover');
  const STYLE_CACHE = Symbol('dcStyleText');
  const BOOLEAN_PROPERTIES = new Set([
    'autofocus', 'checked', 'disabled', 'hidden', 'multiple', 'readonly',
    'required', 'selected'
  ]);

  let nextTemplateNodeId = 0;

  // -------------------------------------------------------------------------
  // Bindings and template compilation
  // -------------------------------------------------------------------------

  function compileBinding(rawValue) {
    const raw = String(rawValue == null ? '' : rawValue);
    // A whole binding contains exactly one interpolation. The path-only
    // dialect never uses braces inside an expression, so rejecting them here
    // prevents `{{ a }} / {{ b }}` from being swallowed as one invalid path.
    const whole = /^\s*\{\{\s*([^{}]*?)\s*\}\}\s*$/.exec(raw);
    if (whole) {
      return { kind: 'whole', expression: whole[1].trim() };
    }

    const parts = [];
    const interpolation = /\{\{([\s\S]*?)\}\}/g;
    let cursor = 0;
    let match;
    while ((match = interpolation.exec(raw))) {
      if (match.index > cursor) {
        parts.push({ kind: 'literal', value: raw.slice(cursor, match.index) });
      }
      parts.push({ kind: 'expression', value: match[1].trim() });
      cursor = match.index + match[0].length;
    }

    if (!parts.length) return { kind: 'static', value: raw };
    if (cursor < raw.length) {
      parts.push({ kind: 'literal', value: raw.slice(cursor) });
    }
    return { kind: 'mixed', parts };
  }

  function resolveExpression(values, scope, rawExpression) {
    const expression = String(rawExpression || '').trim();
    if (expression === 'true') return true;
    if (expression === 'false') return false;

    // This runtime intentionally accepts paths only; it is not an evaluator.
    if (!/^[A-Za-z_$][A-Za-z0-9_$]*(?:\.(?:[A-Za-z_$][A-Za-z0-9_$]*|\d+))*$/.test(expression)) {
      return undefined;
    }

    const segments = expression.split('.');
    const head = segments.shift();
    let currentScope = scope;
    let value;
    let foundInLoop = false;

    while (currentScope) {
      if (currentScope.name === head) {
        value = currentScope.value;
        foundInLoop = true;
        break;
      }
      currentScope = currentScope.parent;
    }

    if (!foundInLoop) {
      value = values == null ? undefined : values[head];
    }

    for (const segment of segments) {
      if (value == null) return undefined;
      value = value[segment];
    }
    return value;
  }

  function stringValue(value) {
    if (value == null || typeof value === 'function') return '';
    return String(value);
  }

  function evaluateBinding(binding, values, scope) {
    if (binding.kind === 'static') return binding.value;
    if (binding.kind === 'whole') {
      const value = resolveExpression(values, scope, binding.expression);
      return value == null ? '' : value;
    }
    return binding.parts.map((part) => (
      part.kind === 'literal'
        ? part.value
        : stringValue(resolveExpression(values, scope, part.value))
    )).join('');
  }

  function kebabToCamel(name) {
    return name.replace(/-([a-z])/g, (_, letter) => letter.toUpperCase());
  }

  function compileNode(node) {
    if (node.nodeType === Node.TEXT_NODE) {
      return {
        kind: 'text',
        id: nextTemplateNodeId++,
        value: compileBinding(node.nodeValue || '')
      };
    }

    if (node.nodeType !== Node.ELEMENT_NODE) return null;

    const id = nextTemplateNodeId++;
    const tag = node.localName.toLowerCase();
    const children = () => Array.from(node.childNodes)
      .map(compileNode)
      .filter(Boolean);

    if (tag === 'sc-for') {
      return {
        kind: 'for',
        id,
        list: compileBinding(node.getAttribute('list') || ''),
        as: node.getAttribute('as') || 'item',
        children: children()
      };
    }

    if (tag === 'sc-if') {
      return {
        kind: 'if',
        id,
        value: compileBinding(node.getAttribute('value') || ''),
        children: children()
      };
    }

    if (tag === 'dc-import') {
      const props = [];
      for (const attribute of Array.from(node.attributes)) {
        const lowerName = attribute.name.toLowerCase();
        if (lowerName === 'name' || lowerName.startsWith('hint-')) continue;
        props.push({
          name: kebabToCamel(lowerName),
          value: compileBinding(attribute.value)
        });
      }
      return {
        kind: 'component',
        id,
        name: node.getAttribute('name') || '',
        props
      };
    }

    const attributes = [];
    const events = [];
    let hover = null;

    for (const attribute of Array.from(node.attributes)) {
      const lowerName = attribute.name.toLowerCase();
      if (lowerName.startsWith('hint-')) continue;
      if (lowerName === 'style-hover') {
        hover = compileBinding(attribute.value);
        continue;
      }
      if (lowerName === 'onclick' || lowerName === 'oninput') {
        events.push({
          type: lowerName === 'onclick' ? 'click' : 'input',
          value: compileBinding(attribute.value)
        });
        continue;
      }
      attributes.push({ name: lowerName, value: compileBinding(attribute.value) });
    }

    return {
      kind: 'element',
      id,
      tag,
      attributes,
      events,
      hover,
      children: children()
    };
  }

  function compileTemplate(source) {
    let fragment;
    if (source instanceof HTMLTemplateElement) {
      fragment = source.content;
    } else if (source instanceof DocumentFragment) {
      fragment = source;
    } else if (typeof source === 'string') {
      const template = document.createElement('template');
      template.innerHTML = source;
      fragment = template.content;
    } else if (source && source.nodeType === Node.ELEMENT_NODE) {
      const template = document.createElement('template');
      template.innerHTML = source.innerHTML;
      fragment = template.content;
    } else {
      throw new TypeError('DC.register(): templateSource must be a template element or HTML string');
    }

    return Array.from(fragment.childNodes).map(compileNode).filter(Boolean);
  }

  // -------------------------------------------------------------------------
  // AST evaluation -> concrete keyed VNodes
  // -------------------------------------------------------------------------

  function vnodeKey(nodeId, loopKeys) {
    return loopKeys.length ? `${nodeId}@${loopKeys.join('/')}` : String(nodeId);
  }

  function safeKeyPart(value) {
    try {
      return encodeURIComponent(String(value));
    } catch (_) {
      return 'unknown';
    }
  }

  function itemKey(item, index) {
    if (item != null && (typeof item === 'object' || typeof item === 'function')) {
      for (const field of ['id', 'hash', 'name']) {
        if (item[field] != null) return `${field}:${safeKeyPart(item[field])}`;
      }
    }
    return `index:${index}`;
  }

  function evaluateNodes(nodes, values, scope, loopKeys, output) {
    const result = output || [];

    for (const node of nodes) {
      if (node.kind === 'for') {
        const list = evaluateBinding(node.list, values, scope);
        if (!Array.isArray(list)) continue;

        const duplicateCounts = new Map();
        list.forEach((item, index) => {
          const base = itemKey(item, index);
          const occurrence = duplicateCounts.get(base) || 0;
          duplicateCounts.set(base, occurrence + 1);
          const loopKey = `${node.id}:${base}:${occurrence}`;
          const childScope = { name: node.as, value: item, parent: scope };
          evaluateNodes(
            node.children,
            values,
            childScope,
            loopKeys.concat(loopKey),
            result
          );
        });
        continue;
      }

      if (node.kind === 'if') {
        if (evaluateBinding(node.value, values, scope)) {
          evaluateNodes(node.children, values, scope, loopKeys, result);
        }
        continue;
      }

      const key = vnodeKey(node.id, loopKeys);
      if (node.kind === 'text') {
        result.push({
          type: 'text',
          key,
          text: stringValue(evaluateBinding(node.value, values, scope)),
          el: null
        });
        continue;
      }

      if (node.kind === 'component') {
        const props = {};
        for (const prop of node.props) {
          props[prop.name] = evaluateBinding(prop.value, values, scope);
        }
        result.push({ type: 'component', key, name: node.name, props, el: null, instance: null });
        continue;
      }

      const attributes = Object.create(null);
      const events = Object.create(null);
      for (const attribute of node.attributes) {
        attributes[attribute.name] = evaluateBinding(attribute.value, values, scope);
      }
      for (const event of node.events) {
        const handler = evaluateBinding(event.value, values, scope);
        events[event.type] = typeof handler === 'function' ? handler : null;
      }

      result.push({
        type: 'element',
        key,
        tag: node.tag,
        attributes,
        events,
        hover: node.hover ? stringValue(evaluateBinding(node.hover, values, scope)) : '',
        children: evaluateNodes(node.children, values, scope, loopKeys, []),
        el: null
      });
    }

    return result;
  }

  // -------------------------------------------------------------------------
  // DOM patcher
  // -------------------------------------------------------------------------

  function sameVNodeType(oldVNode, newVNode) {
    if (!oldVNode || oldVNode.type !== newVNode.type) return false;
    if (newVNode.type === 'element') return oldVNode.tag === newVNode.tag;
    if (newVNode.type === 'component') return oldVNode.name === newVNode.name;
    return true;
  }

  function normalizeAttributeValue(value) {
    return stringValue(value);
  }

  function setStyleText(element, cssText, keepEmptyAttribute) {
    const text = String(cssText || '');
    if (element[STYLE_CACHE] !== text) {
      element.style.cssText = text;
      element[STYLE_CACHE] = text;
    }
    if (!text && !keepEmptyAttribute) element.removeAttribute('style');
  }

  function mergeStyles(base, hover) {
    const cleanBase = String(base || '').trim();
    const cleanHover = String(hover || '').trim();
    if (!cleanBase) return cleanHover;
    if (!cleanHover) return cleanBase;
    return `${cleanBase}${cleanBase.endsWith(';') ? '' : ';'}${cleanHover}`;
  }

  function patchHoverStyle(element, attributes, hoverCss) {
    const hasBaseStyle = Object.prototype.hasOwnProperty.call(attributes, 'style');
    const baseStyle = hasBaseStyle ? normalizeAttributeValue(attributes.style) : '';
    let record = element[HOVER_STORE];

    if (hoverCss) {
      if (!record) {
        record = {
          active: false,
          base: baseStyle,
          hover: hoverCss,
          enter: null,
          leave: null
        };
        record.enter = () => {
          record.active = true;
          setStyleText(element, mergeStyles(record.base, record.hover), true);
        };
        record.leave = () => {
          record.active = false;
          setStyleText(element, record.base, hasBaseStyle);
        };
        element.addEventListener('mouseenter', record.enter);
        element.addEventListener('mouseleave', record.leave);
        element[HOVER_STORE] = record;
      }
      record.base = baseStyle;
      record.hover = hoverCss;
      setStyleText(element, record.active ? mergeStyles(baseStyle, hoverCss) : baseStyle, true);
      return;
    }

    if (record) {
      element.removeEventListener('mouseenter', record.enter);
      element.removeEventListener('mouseleave', record.leave);
      delete element[HOVER_STORE];
    }
    setStyleText(element, baseStyle, hasBaseStyle);
  }

  function patchEvents(element, nextEvents) {
    const store = element[EVENT_STORE] || (element[EVENT_STORE] = Object.create(null));
    const eventTypes = new Set([...Object.keys(store), ...Object.keys(nextEvents)]);

    for (const type of eventTypes) {
      const nextHandler = nextEvents[type];
      let record = store[type];
      if (typeof nextHandler === 'function') {
        if (!record) {
          record = {
            handler: nextHandler,
            listener(event) {
              return record.handler.call(element, event);
            }
          };
          store[type] = record;
          element.addEventListener(type, record.listener);
        } else {
          // renderVals commonly creates a fresh closure each render. Redirect
          // the one installed listener so it is current without accumulating.
          record.handler = nextHandler;
        }
      } else if (record) {
        element.removeEventListener(type, record.listener);
        delete store[type];
      }
    }
  }

  function patchSpecialProperty(element, name, value, present) {
    if (name === 'value' && 'value' in element) {
      const desired = present ? normalizeAttributeValue(value) : '';
      // This equality check is the caret-preservation rule. Never assign a
      // focused input's value when the browser already holds the right text.
      if (String(element.value) !== desired) element.value = desired;
      if (present) {
        if (element.getAttribute('value') !== desired) element.setAttribute('value', desired);
      } else {
        element.removeAttribute('value');
      }
      return true;
    }

    if ((name === 'checked' || name === 'selected') && name in element) {
      const enabled = present && value !== false;
      if (element[name] !== enabled) element[name] = enabled;
      if (enabled) element.setAttribute(name, '');
      else element.removeAttribute(name);
      return true;
    }

    return false;
  }

  function patchAttributes(element, oldAttributes, nextAttributes, hoverCss) {
    patchHoverStyle(element, nextAttributes, hoverCss);

    const names = new Set([
      ...Object.keys(oldAttributes || {}),
      ...Object.keys(nextAttributes)
    ]);

    for (const name of names) {
      if (name === 'style') continue;
      const present = Object.prototype.hasOwnProperty.call(nextAttributes, name);
      const value = present ? nextAttributes[name] : undefined;

      if (patchSpecialProperty(element, name, value, present)) continue;

      if (!present) {
        element.removeAttribute(name);
        continue;
      }

      if (BOOLEAN_PROPERTIES.has(name) && typeof value === 'boolean') {
        if (value) element.setAttribute(name, '');
        else element.removeAttribute(name);
        if (name in element && element[name] !== value) element[name] = value;
        continue;
      }

      const desired = normalizeAttributeValue(value);
      if (element.getAttribute(name) !== desired) element.setAttribute(name, desired);
    }
  }

  function cleanupElement(element) {
    const eventStore = element[EVENT_STORE];
    if (eventStore) {
      for (const [type, record] of Object.entries(eventStore)) {
        element.removeEventListener(type, record.listener);
      }
      delete element[EVENT_STORE];
    }

    const hover = element[HOVER_STORE];
    if (hover) {
      element.removeEventListener('mouseenter', hover.enter);
      element.removeEventListener('mouseleave', hover.leave);
      delete element[HOVER_STORE];
    }
  }

  function removeVNode(vnode) {
    if (!vnode) return;
    if (vnode.type === 'component') {
      if (vnode.instance) vnode.instance.destroy();
      if (vnode.el) vnode.el.remove();
      return;
    }
    if (vnode.type === 'element') {
      for (const child of vnode.children || []) removeVNode(child);
      cleanupElement(vnode.el);
    }
    if (vnode.el) vnode.el.remove();
  }

  function mountVNode(vnode) {
    if (vnode.type === 'text') {
      vnode.el = document.createTextNode(vnode.text);
      return vnode;
    }

    if (vnode.type === 'element') {
      const element = document.createElement(vnode.tag);
      vnode.el = element;
      patchAttributes(element, {}, vnode.attributes, vnode.hover);
      patchEvents(element, vnode.events);
      vnode.children = patchChildren(element, [], vnode.children);
      return vnode;
    }

    const host = document.createElement('dc-component');
    host.style.display = 'contents';
    host.setAttribute('data-dc-component', vnode.name);
    vnode.el = host;

    const entry = registry.get(vnode.name);
    if (!entry) {
      console.error(`[mini-dc] Component "${vnode.name}" is not registered.`);
      return vnode;
    }

    vnode.instance = new ComponentInstance(entry, host, vnode.props);
    vnode.instance.render();
    return vnode;
  }

  function patchVNode(oldVNode, nextVNode) {
    if (!oldVNode || !sameVNodeType(oldVNode, nextVNode)) {
      if (oldVNode) removeVNode(oldVNode);
      return mountVNode(nextVNode);
    }

    nextVNode.el = oldVNode.el;

    if (nextVNode.type === 'text') {
      if (oldVNode.text !== nextVNode.text) nextVNode.el.nodeValue = nextVNode.text;
      return nextVNode;
    }

    if (nextVNode.type === 'element') {
      patchAttributes(nextVNode.el, oldVNode.attributes, nextVNode.attributes, nextVNode.hover);
      patchEvents(nextVNode.el, nextVNode.events);
      nextVNode.children = patchChildren(nextVNode.el, oldVNode.children, nextVNode.children);
      return nextVNode;
    }

    nextVNode.instance = oldVNode.instance;
    if (nextVNode.instance) nextVNode.instance.updateProps(nextVNode.props);
    return nextVNode;
  }

  function patchChildren(parent, oldChildren, nextChildren) {
    const buckets = new Map();
    for (const oldVNode of oldChildren || []) {
      const bucket = buckets.get(oldVNode.key) || [];
      bucket.push(oldVNode);
      buckets.set(oldVNode.key, bucket);
    }

    const patched = nextChildren.map((nextVNode) => {
      const bucket = buckets.get(nextVNode.key);
      const oldVNode = bucket && bucket.length ? bucket.shift() : null;
      if (bucket && !bucket.length) buckets.delete(nextVNode.key);
      return patchVNode(oldVNode, nextVNode);
    });

    for (const bucket of buckets.values()) {
      for (const oldVNode of bucket) removeVNode(oldVNode);
    }

    // Reconcile from the end. Stable nodes are not moved at all; avoiding a
    // redundant DOM move is another part of preserving focus.
    let anchor = null;
    for (let index = patched.length - 1; index >= 0; index -= 1) {
      const vnode = patched[index];
      if (vnode.el.parentNode !== parent || vnode.el.nextSibling !== anchor) {
        parent.insertBefore(vnode.el, anchor);
      }
      anchor = vnode.el;
    }

    return patched;
  }

  // -------------------------------------------------------------------------
  // Component instances and public base class
  // -------------------------------------------------------------------------

  function shallowEqual(left, right) {
    if (left === right) return true;
    const leftKeys = Object.keys(left || {});
    const rightKeys = Object.keys(right || {});
    if (leftKeys.length !== rightKeys.length) return false;
    return leftKeys.every((key) => (
      Object.prototype.hasOwnProperty.call(right, key) && Object.is(left[key], right[key])
    ));
  }

  class ComponentInstance {
    constructor(entry, host, props) {
      this.entry = entry;
      this.host = host;
      this.vnodes = [];
      this.destroyed = false;
      this.renderQueued = false;
      this.logic = new entry.ComponentClass(props || {});
      if (!this.logic.state || typeof this.logic.state !== 'object') this.logic.state = {};
      this.logic.props = props || {};
      this.logic.__dcInstance = this;
    }

    scheduleRender() {
      if (this.destroyed || this.renderQueued) return;
      this.renderQueued = true;
      queueMicrotask(() => {
        if (this.destroyed || !this.renderQueued) return;
        this.renderQueued = false;
        this.render();
      });
    }

    render() {
      if (this.destroyed) return;
      this.renderQueued = false;
      const values = typeof this.logic.renderVals === 'function'
        ? (this.logic.renderVals() || {})
        : {};
      const nextVNodes = evaluateNodes(this.entry.ast, values, null, [], []);
      this.vnodes = patchChildren(this.host, this.vnodes, nextVNodes);
    }

    updateProps(nextProps) {
      const normalized = nextProps || {};
      if (shallowEqual(this.logic.props, normalized)) return;

      const previousProps = this.logic.props;
      this.logic.props = normalized;
      this.render();
      if (typeof this.logic.componentDidUpdate === 'function') {
        this.logic.componentDidUpdate(previousProps);
      }
    }

    destroy() {
      if (this.destroyed) return;
      this.destroyed = true;
      this.renderQueued = false;
      for (const vnode of this.vnodes) removeVNode(vnode);
      this.vnodes = [];
      if (this.logic) this.logic.__dcInstance = null;
    }
  }

  class DCLogic {
    constructor(props) {
      this.props = props || {};
      this.state = {};
      this.__dcInstance = null;
    }

    setState(objectOrUpdater) {
      const partial = typeof objectOrUpdater === 'function'
        ? objectOrUpdater(this.state)
        : objectOrUpdater;

      if (partial && typeof partial === 'object' && !Array.isArray(partial)) {
        this.state = Object.assign({}, this.state, partial);
        if (this.__dcInstance) this.__dcInstance.scheduleRender();
      }
    }

    renderVals() {
      return this.props;
    }
  }

  function register(name, templateSource, ComponentClass) {
    if (!name || typeof name !== 'string') {
      throw new TypeError('DC.register(): name must be a non-empty string');
    }
    if (typeof ComponentClass !== 'function') {
      throw new TypeError(`DC.register("${name}"): ComponentClass must be a class`);
    }

    registry.set(name, {
      name,
      ast: compileTemplate(templateSource),
      ComponentClass
    });
    return DC;
  }

  function mount(name, hostElement, props) {
    const entry = registry.get(name);
    if (!entry) throw new Error(`DC.mount(): component "${name}" is not registered`);
    if (!(hostElement instanceof Element)) {
      throw new TypeError('DC.mount(): hostElement must be a DOM Element');
    }

    if (hostElement.__dcRootInstance) {
      hostElement.__dcRootInstance.destroy();
    }
    hostElement.replaceChildren();

    const instance = new ComponentInstance(entry, hostElement, props || {});
    hostElement.__dcRootInstance = instance;
    instance.render();
    return instance.logic;
  }

  function unmount(hostElement) {
    if (!hostElement || !hostElement.__dcRootInstance) return;
    hostElement.__dcRootInstance.destroy();
    hostElement.__dcRootInstance = null;
    hostElement.replaceChildren();
  }

  const DC = {
    register,
    mount,
    mountComponent: mount,
    unmount,
    registry
  };

  global.DCLogic = DCLogic;
  global.DC = DC;
  global.mountComponent = mount;
})(window);
