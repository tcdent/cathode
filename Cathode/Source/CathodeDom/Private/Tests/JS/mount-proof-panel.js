// Sprint 012 — first-mount proof: a small, REAL, STATIC Preact panel (Cathode-owned test fixture; NOT the
// 3rd-party Preact bundle). Authored with preact.h (no JSX compiler in the harness). STATIC by design — no on*
// event handlers, no style props, no form inputs, no hooks — so the mount path stays within the bound DOM
// surface (012 S1-S5: createElementNS, createTextNode, insertBefore, reflected id/className, setAttribute, text
// .data). Exercises: createElementNS (div/h1/p), createTextNode (2 text nodes), insertBefore (nesting), reflected
// id + className (S3), setAttribute (data-kind, a non-reflected attr -> S2), and the getElementById mount lookup
// (the oracle). The harness loads Preact (globalThis.preact) first, then this fixture, then calls __mountPanel.
(function () {
	var h = globalThis.preact.h;

	function Panel() {
		return h('div', { id: 'app', className: 'panel', 'data-kind': 'demo' },
			h('h1', { id: 'title', className: 'heading' }, 'Cathode'),
			h('p', { id: 'msg', className: 'body' }, 'Preact mounted')
		);
	}

	// Mount the panel into the given container element (a real DOM node, already in the document tree).
	globalThis.__mountPanel = function (container) {
		globalThis.preact.render(h(Panel), container);
	};
})();
