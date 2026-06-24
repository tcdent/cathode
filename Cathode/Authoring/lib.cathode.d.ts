// lib.cathode.d.ts -- the Cathode authoring type surface (GENERATED -- do not hand-edit).
// 025 S1 (Model A): the DOM/JS member interfaces are PROJECTED from the production FBoundMember binding
// tables (signatures from each member's own field). Regenerate via the regenerate-mode test; the coherence
// test asserts this committed file == the regeneration. Authors compile against this file with a tsconfig
// that OMITS "dom" -- any unsupported web API (fetch/localStorage/innerHTML/...) is a type error.
export {};

// --- Standard globals (Cathode-provided; BOUND to the install set; an install add/remove flows through) ---
declare const console: { log(...args: unknown[]): void; warn(...args: unknown[]): void; error(...args: unknown[]): void; };
declare function setTimeout(handler: () => void, timeout?: number): number;
declare function clearTimeout(id: number): void;
declare function requestAnimationFrame(callback: (time: number) => void): number;
declare function cancelAnimationFrame(id: number): void;
declare const document: Document;

// --- Auxiliary referenced types (referenced by signatures; no binding table; RESOLVE-guarded) ---
type EventListener = (event: Event) => void;
interface NodeList { readonly length: number; item(index: number): Node | null; [index: number]: Node; }
interface NamedNodeMap { readonly length: number; }
interface DOMRect { readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly top: number; readonly right: number; readonly bottom: number; readonly left: number; }
interface CSSStyleDeclaration { getPropertyValue(property: string): string; setProperty(property: string, value: string): void; removeProperty(property: string): string; cssText: string; }

interface EventTarget {
	addEventListener(type: string, listener: EventListener, useCapture?: boolean): void;
	removeEventListener(type: string, listener: EventListener, useCapture?: boolean): void;
	dispatchEvent(event: Event): boolean;
}

interface Node extends EventTarget {
	readonly nodeType: number;
	readonly nodeName: string;
	nodeValue: string | null;
	readonly namespaceURI: string | null;
	readonly parentNode: Node | null;
	readonly firstChild: Node | null;
	readonly nextSibling: Node | null;
	readonly childNodes: NodeList;
	insertBefore(node: Node, child: Node | null): Node;
	appendChild(node: Node): Node;
	removeChild(child: Node): Node;
	remove(): void;
	textContent: string;
}

interface Element extends Node {
	readonly tagName: string;
	getAttribute(qualifiedName: string): string | null;
	setAttribute(qualifiedName: string, value: string): void;
	removeAttribute(qualifiedName: string): void;
	id: string;
	className: string;
	querySelector(selectors: string): Element | null;
	querySelectorAll(selectors: string): NodeList;
	matches(selectors: string): boolean;
	closest(selectors: string): Element | null;
	readonly attributes: NamedNodeMap;
	getBoundingClientRect(): DOMRect;
}

interface HTMLElement extends Element {
	readonly style: CSSStyleDeclaration;
	focus(): void;
	blur(): void;
}

interface HTMLInputElement extends HTMLElement {
	value: string;
	checked: boolean;
}

interface CharacterData extends Node {
	data: string;
}

interface Text extends CharacterData {
}

interface Document extends Node {
	getElementById(elementId: string): Element | null;
	querySelector(selectors: string): Element | null;
	querySelectorAll(selectors: string): NodeList;
	createElement(localName: string): Element;
	createTextNode(data: string): Text;
	createElementNS(namespace: string | null, qualifiedName: string): Element;
	readonly body: Element | null;
	readonly documentElement: Element | null;
	readonly activeElement: Element | null;
}

interface Event {
	readonly type: string;
	readonly target: EventTarget | null;
	readonly currentTarget: EventTarget | null;
	readonly eventPhase: number;
	readonly bubbles: boolean;
	readonly defaultPrevented: boolean;
	preventDefault(): void;
	stopPropagation(): void;
}

interface KeyboardEvent extends Event {
	readonly type: string;
	readonly target: EventTarget | null;
	readonly currentTarget: EventTarget | null;
	readonly eventPhase: number;
	readonly bubbles: boolean;
	readonly defaultPrevented: boolean;
	preventDefault(): void;
	stopPropagation(): void;
	readonly key: string;
	readonly code: string;
}

