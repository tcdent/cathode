// Cathode surface walk — the in-engine runtime-verification faculty's DOM snapshot mechanism (sprint 011 [b]).
//
// AUTHORED for Cathode's own verification intention (NOT lifted from any external harness): produce a
// structured, deterministic snapshot of the rendered DOM — the element tree, each node's box geometry, and the
// computed styles that matter for verifying Cathode's renderer honored the panel's intended layout + look.
//
// WALK-SPLIT: this RETURNS the snapshot object and emits NOTHING. Each consumer emits its own way — the
// in-engine faculty captures the return value through its native sink; a Chromium oracle (if one is ever
// authored, separately) would read the return value its own way. Returning (not emitting) is what lets the
// one snapshot shape serve more than one consumer without a console/window dependency baked in.
//
// Uses only ordinary, engine-neutral DOM reads (nodeName / nodeType / childNodes / nodeValue / attributes +
// getBoundingClientRect + getComputedStyle). No location/history/storage, no engine-specific surface.
//
// Loaded as TEXT and evaluated (quickjs-ng has no dynamic module loading), so it installs one global function.
(function () {
  "use strict";

  // The computed-style CAPTURE SET — Cathode's own choice of which properties to snapshot for verification,
  // grouped by the concern each serves. This is capture-scope (what to record), a DISTINCT concern from the
  // CSS capability-contract (what litehtml supports — Cathode's own litehtml-grounded declaration, authored
  // separately on the roadmap). Longhands are used where getComputedStyle resolves shorthands to them.
  var STYLE_CAPTURE = [
    // flex layout (container + item)
    "display", "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis",
    "align-items", "justify-content", "gap",
    // box model
    "box-sizing", "width", "height",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "border-top-width", "border-top-style", "border-top-color", "border-radius",
    // color + type
    "color", "background-color",
    "font-family", "font-size", "font-weight", "text-align", "text-transform", "letter-spacing", "opacity",
    // effects (parity-relevant; may be out of the eventual supported subset, captured to surface that)
    "box-shadow", "text-shadow", "animation-name", "transition-property",
  ];

  var GEOMETRY_PRECISION = 100; // 2 decimal places — enough to compare layout without float noise

  function roundCoord(value) {
    return Math.round(value * GEOMETRY_PRECISION) / GEOMETRY_PRECISION;
  }

  function readBox(el) {
    if (typeof el.getBoundingClientRect !== "function") return null;
    var r = el.getBoundingClientRect();
    return { x: roundCoord(r.x), y: roundCoord(r.y), w: roundCoord(r.width), h: roundCoord(r.height) };
  }

  function readStyles(el, props) {
    var out = {};
    if (typeof getComputedStyle !== "function") return out;
    var computed = getComputedStyle(el);
    for (var i = 0; i < props.length; i++) {
      out[props[i]] = computed.getPropertyValue(props[i]);
    }
    return out;
  }

  function readAttributes(el) {
    var out = {};
    var attrs = el.attributes;
    if (!attrs) return out;
    for (var i = 0; i < attrs.length; i++) {
      out[attrs[i].name] = attrs[i].value;
    }
    return out;
  }

  // Recursively capture one element: its tag, attributes, directly-contained text, box, styles, and element
  // children. Text nodes (nodeType 3) fold into the parent's text; element nodes (nodeType 1) recurse.
  function captureElement(el, props) {
    var text = "";
    var children = [];
    var kids = el.childNodes || [];
    for (var i = 0; i < kids.length; i++) {
      var node = kids[i];
      if (node.nodeType === 1) {
        children.push(captureElement(node, props));
      } else if (node.nodeType === 3 && node.nodeValue) {
        text += node.nodeValue;
      }
    }
    return {
      tag: (el.nodeName || "").toLowerCase(),
      attrs: readAttributes(el),
      text: text.replace(/\s+/g, " ").trim(),
      box: readBox(el),
      style: readStyles(el, props),
      children: children,
    };
  }

  // The walk. Returns { root } or null when there is no DOM to walk (a bare runtime with the document seam
  // unwired — itself a faculty-A finding, reported as domExposed=false; harmless in a real browser). Root
  // defaults to #app then <body>; the capture set defaults to STYLE_CAPTURE but is overridable.
  globalThis.CathodeSurfaceWalk = function (rootEl, props) {
    if (typeof document === "undefined" || !document) return null;
    var captureSet = props || STYLE_CAPTURE;
    var root = rootEl
      || (typeof document.getElementById === "function" ? document.getElementById("app") : null)
      || document.body;
    if (!root) return null;
    return { root: captureElement(root, captureSet) };
  };

  // Expose the capture set so a consumer can read it without re-declaring (single source for the set).
  globalThis.CathodeSurfaceWalk.STYLE_CAPTURE = STYLE_CAPTURE;
})();
