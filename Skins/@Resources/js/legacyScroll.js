/**
 * Custom scrollbar for Trident (IE7+) and modern browsers.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @license GPL v2.0 License
 * @author F4Jonatas
 * @release 0.1.3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * @option {number}      [width=12]                       - Scrollbar width in px.
 * @option {number|null} [height=null]                    - Wrapper height in px. null = viewport height.
 * @option {string}      [colorTrack='rgba(255,255,255,.1)'] - Track background color.
 * @option {string}      [colorThumb='#dedede']           - Thumb color (normal).
 * @option {string}      [colorThumbHover='#33bbcc']      - Thumb color on hover (not used yet, CSS class).
 * @option {string}      [colorThumbActive='#1199aa']     - Thumb color when active (not used yet).
 * @option {string}      [colorUp='#33ccaa']              - Up button color.
 * @option {string}      [colorDown='#33ccaa']            - Down button color.
 * @option {boolean}     [autoHide=false]                 - Whether to hide the scrollbar when idle.
 * @option {number}      [autoHideDelay=1500]             - Idle time (ms) before hiding.
 * @option {boolean}     [buttons=true]                   - Whether to create up/down arrow buttons.
 * @option {number|null} [trackHeight=null]               - Fixed track height in px. If set, buttons are automatically disabled.
 *
 * @example
 * // Basic usage
 * legacyScroll.init();
 * legacyScroll.scrollbar('myDiv', { width: 14, buttons: false });
 *
 * @example
 * // Auto-hide scrollbar with custom track height
 * legacyScroll.scrollbar('content', {
 *     autoHide: true,
 *     trackHeight: 150
 * });
 *
 * @example
 * // After replacing the container content (e.g. via innerHTML), rebind the
 * // scrollbar without rebuilding it. Events and scroll position are kept.
 * document.getElementById('content').innerHTML = html;
 * legacyScroll.update('content');
 */
(function() {

	// Internal state
	var containers = [],              // all scrollbar instances
		activeCont = null,              // currently dragged container
		scrollTimer = null,             // timer for arrow smooth scrolling
		scrollSpeed = 0,                // direction and speed for arrows
		scrollDelay = 400,              // initial delay before acceleration
		styleId = 'legacyScrollStyles', // ID of injected <style> element

		// Default options
		_defaults = {
			width:            12,
			height:           null,  // null = use viewport height
			colorTrack:       'rgba(255,255,255,.1)',
			colorThumb:       '#dedede',
			colorThumbHover:  '#33bbcc',
			colorThumbActive: '#1199aa',
			colorUp:          '#33ccaa',
			colorDown:        '#33ccaa',
			autoHide:         false,
			autoHideDelay:    1500,
			buttons:          true,
			trackHeight:      null
		};

	/* ---------- Helpers ---------- */

	/**
	 * Shallow merge of source into target.
	 * @param {Object} target
	 * @param {Object} source
	 * @returns {Object}
	 */
	function extend(target, source) {
		target = target || {};
		if (!source) return target;
		for (var key in source) {
			if (source.hasOwnProperty(key)) {
				target[key] = source[key];
			}
		}
		return target;
	}

	/**
	 * IE7/8-safe indexOf replacement for arrays.
	 * Array.prototype.indexOf does not exist in IE7/8 — using it crashes Trident.
	 * @param {Array} arr
	 * @param {*} val
	 * @returns {number} index or -1
	 */
	function arrayIndexOf(arr, val) {
		for (var i = 0; i < arr.length; i++) {
			if (arr[i] === val) return i;
		}
		return -1;
	}

	/**
	 * Add CSS class to element (IE7+ safe).
	 * @param {HTMLElement} el
	 * @param {string} cls
	 */
	function addClass(el, cls) {
		if (el.classList) {
			el.classList.add(cls);
		} else {
			var classes = el.className.split(' ');
			if (arrayIndexOf(classes, cls) === -1) {
				el.className += ' ' + cls;
			}
		}
	}

	/**
	 * Remove CSS class from element (IE7+ safe).
	 * @param {HTMLElement} el
	 * @param {string} cls
	 */
	function removeClass(el, cls) {
		if (el.classList) {
			el.classList.remove(cls);
		} else {
			el.className = el.className.replace(
				new RegExp('(\\s|^)' + cls + '(\\s|$)','g'), ' '
			).replace(/^\s+|\s+$/g, '');
		}
	}

	/**
	 * Detect CSS transition support (used for fade effect in autoHide).
	 *
	 * The `in` operator on el.style must be wrapped in try/catch:
	 * in Trident/IE7 the style object is a COM/ActiveX object and `in`
	 * throws a native exception that crashes the host process entirely.
	 */
	var supportsTransition = false;
	(function() {
		try {
			var el = document.createElement('div');
			if ('transition' in el.style || 'webkitTransition' in el.style) {
				supportsTransition = true;
			}
		} catch(e) {
			supportsTransition = false;
		}
	})();

	/* ---------- Dynamic style injection ---------- */

	/**
	 * Create a <style> element with base classes for the scrollbar.
	 * For auto-hide, we inject different rules depending on CSS transition support.
	 * @param {HTMLElement} dom - reference element used to inject the <style> tag before it.
	 */
	function injectStyles( dom ) {
		if (document.getElementById(styleId)) return;
		if (!dom) return; // guard: element must exist before inserting adjacent HTML

		var cssText = '' +
			'.ls-track, .ls-thumb, .ls-up, .ls-down { position: absolute; right: 0; }' +
			'.ls-track { top: 0; bottom: 0; background-color: #dedede; }' +
			'.ls-up { top: 0; cursor: pointer; }' +
			'.ls-down { bottom: 0; cursor: pointer; }' +
			'.ls-thumb { cursor: pointer; }' +
			/* Base hidden state (for IE7/8 fallback) */
			'.ls-bar-hidden .ls-track, .ls-bar-hidden .ls-thumb, .ls-bar-hidden .ls-up, .ls-bar-hidden .ls-down { visibility: hidden; }';

		// If browser supports transitions, add fade classes
		if (supportsTransition) {
			cssText += '' +
				'.ls-bar-fade .ls-track, .ls-bar-fade .ls-thumb, .ls-bar-fade .ls-up, .ls-bar-fade .ls-down {' +
					'opacity: 0;' +
					'transition: opacity 0.2s;' +
					'pointer-events: none;' +
				'}';
			cssText += '.ls-track, .ls-thumb, .ls-up, .ls-down { opacity: 1; }';
		}

		dom.insertAdjacentHTML( 'beforebegin', '<style id="'+ styleId +'" type="text/css">' + cssText + '</style>' );
	}

	/**
	 * Single source of truth for the maximum scrollable pixels.
	 * Used by refreshOne, drag, wheel and arrows — keeps ratio consistent.
	 * @param {Object} data - instance data
	 * @returns {number}
	 */
	function getMaxScroll(data) {
		var c        = data.container;
		var max      = c.scrollHeight - c.clientHeight;
		return max > 0 ? max : 0;
	}

	/* ---------- Geometry refresh ---------- */

	/**
	 * Read an integer CSS property from an element (IE7+ safe).
	 * currentStyle is the IE7 equivalent of getComputedStyle.
	 * Returns 0 if the value is not a pixel value (e.g. "auto").
	 * @param {HTMLElement} el
	 * @param {string} prop - camelCase property name (e.g. "paddingTop")
	 * @returns {number}
	 */
	function getCSSInt(el, prop) {
		var style = el.currentStyle || (window.getComputedStyle ? window.getComputedStyle(el, null) : null);
		if (!style) return 0;
		var val = parseInt(style[prop], 10);
		return isNaN(val) ? 0 : val;
	}

	/**
	 * Recalculate thumb size and position for a single container.
	 * Called after any scroll change. Takes into account custom trackHeight.
	 * Accounts for padding and margin of the container so the thumb ratio
	 * reflects the true visible area regardless of CSS spacing.
	 * @param {HTMLElement} cont
	 */
	function refreshOne(cont) {
		var data = cont._lsdata;
		if (!data || !data.container) return;

		var c   = data.container;
		var sw  = data.width;

		var wrapperH = data.wrapper.clientHeight;

		if (wrapperH <= 0) return;

		var trackTop = 0, trackBottom = 0, trackH;
		if (data.trackHeight !== null) {
			trackH = data.trackHeight;
		} else {
			if (data.buttons) {
				trackTop = sw;
				trackBottom = sw;
			}
			trackH = wrapperH - trackTop - trackBottom;
		}

		var visibleH  = c.clientHeight;
		var maxScroll = getMaxScroll(data);         // single source of truth
		var totalH    = maxScroll + visibleH;

		var minThumbH = Math.ceil(sw * 0.5);
		var thumbH    = (totalH > 0)
			? Math.max(minThumbH, Math.ceil(trackH * visibleH / totalH))
			: trackH;
		data.thumb.style.height = thumbH + 'px';

		data.ratio = (maxScroll > 0)
			? (trackH - thumbH) / maxScroll
			: 1;

		var currentPos = data.scrollPos || 0;
		var thumbTop = (maxScroll > 0)
			? trackTop + (currentPos / maxScroll) * (trackH - thumbH)
			: trackTop;
		data.thumb.style.top = Math.floor(thumbTop) + 'px';

		var swpx = sw + 'px';
		data.track.style.width  = swpx;
		data.thumb.style.width  = swpx;

		if (data.trackHeight !== null) {
			data.track.style.height = data.trackHeight + 'px';
		} else {
			data.track.style.height = '';
		}

		if (data.up) {
			data.up.style.width  = swpx;
			data.up.style.height = swpx;
		}
		if (data.down) {
			data.down.style.width  = swpx;
			data.down.style.height = swpx;
		}

		if (data.trackHeight === null) {
			data.track.style.top    = trackTop + 'px';
			data.track.style.bottom = trackBottom + 'px';
		} else {
			data.track.style.top    = '0px';
			data.track.style.bottom = 'auto';
		}
	}

	/** Refreshes all instances (e.g., on window resize). */
	function refreshAll() {
		for (var i = 0; i < containers.length; i++) {
			refreshOne(containers[i]);
		}
	}

	/* ---------- Arrow button smooth scrolling ---------- */

	/**
	 * Stop arrow/drag scrolling and release any mouse capture.
	 * @param {HTMLElement|null} cont - container whose state should be reset.
	 */
	function clearScrollTimer(cont) {
		if (scrollTimer) {
			clearTimeout(scrollTimer);
			scrollTimer = null;
		}
		scrollSpeed = 0;
		if (cont && cont._lsdata) {
			removeClass(cont._lsdata.thumb, 'ls-thumb-active');
			// Restart auto-hide delay after drag ends
			if (cont._lsdata._autoHideShow) cont._lsdata._autoHideShow();
		}
		if (activeCont) {
			activeCont._lsdata.sg = false;
			if (activeCont.releaseCapture) activeCont.releaseCapture();
			activeCont = null;
		}
		if (document.onselectstart) document.onselectstart = null;
	}

	/**
	 * Begin continuous scrolling triggered by an arrow button.
	 * @param {HTMLElement} cont - container to scroll.
	 * @param {number} direction - -1 for up, 1 for down.
	 */
	function startArrowScroll(cont, direction) {
		clearScrollTimer(null);
		var data = cont._lsdata;
		addClass(data.thumb, 'ls-thumb-active');
		activeCont = cont;
		scrollSpeed = direction;
		scrollDelay = 400;
		arrowScrollStep(cont);
	}

	/**
	 * Perform one arrow scroll step and schedule the next one.
	 * @param {HTMLElement} cont - container being scrolled.
	 */
	function arrowScrollStep(cont) {
		if (scrollSpeed === 0 || !cont) {
			clearScrollTimer(cont);
			return;
		}
		var d      = cont._lsdata;
		var curPos = d.scrollPos || 0;
		var maxScroll = getMaxScroll(d);
		var newPos = Math.max(0, Math.min(curPos + Math.ceil(20 * scrollSpeed), maxScroll));
		d.inner.style.top = '-' + Math.floor(newPos) + 'px';
		d.scrollPos = newPos;
		refreshOne(cont);
		scrollTimer = setTimeout(function() {
			arrowScrollStep(cont);
		}, scrollDelay);
		scrollDelay = 32;
	}

	/* ---------- Main bar events (drag, track click, wheel, native scroll) ---------- */

	/**
	 * Attach all necessary mouse and wheel events to the scrollbar components.
	 * @param {HTMLElement} container - the original content container
	 * @param {Object} data - the instance's data object
	 */
	function setupBarEvents(container, data) {
		var thumb = data.thumb,
			track = data.track;

		// Thumb drag
		thumb.onmousedown = function(e) {
			e = e || window.event;
			if (data.sg) return false;
			activeCont           = container;
			data.sg              = true;
			data.dragStartY      = e.screenY;
			data.dragStartScroll = data.scrollPos || 0;
			addClass(thumb, 'ls-thumb-active');
			if (thumb.setCapture)     thumb.setCapture();
			if (container.setCapture) container.setCapture();
			return false;
		};

		// Click on track (jump to position)
		track.onmousedown = function(e) {
			e = e || window.event;
			if (data.sg) return false;

			var mouseY = e.clientY + (document.body.scrollTop || document.documentElement.scrollTop || 0);
			var trackTopAbs = 0;
			var el = track;
			while (el) { trackTopAbs += el.offsetTop; el = el.offsetParent; }

			var clickPos = mouseY - trackTopAbs;
			var thumbH   = thumb.offsetHeight;
			var newScrollTop = (clickPos - thumbH / 2) / data.ratio;
			var maxScroll = getMaxScroll(data);
			newScrollTop = Math.max(0, Math.min(newScrollTop, maxScroll));
			data.inner.style.top = '-' + Math.floor(newScrollTop) + 'px';
			data.scrollPos = newScrollTop;
			refreshOne(container);

			activeCont           = container;
			data.sg              = true;
			data.dragStartY      = e.screenY;
			data.dragStartScroll = data.scrollPos || 0;
			addClass(thumb, 'ls-thumb-active');
			if (thumb.setCapture)     thumb.setCapture();
			if (container.setCapture) container.setCapture();
			return false;
		};

		// Up button (if exists)
		if (data.up) {
			data.up.onmousedown = function() {
				if (scrollSpeed !== 0) return false;
				startArrowScroll(container, -1);
				return false;
			};
			data.up.ondblclick   = data.up.onmousedown;
			data.up.onmouseup    = data.up.onmouseout = function() { clearScrollTimer(container); };
		}

		// Down button (if exists)
		if (data.down) {
			data.down.onmousedown = function() {
				if (scrollSpeed !== 0) return false;
				startArrowScroll(container, 1);
				return false;
			};
			data.down.ondblclick   = data.down.onmousedown;
			data.down.onmouseup    = data.down.onmouseout = function() { clearScrollTimer(container); };
		}

		// Thumb hover state
		thumb.onmouseover = function() {
			if (!data.sg) addClass(thumb, 'ls-thumb-hover');
		};
		thumb.onmouseout = function() {
			if (!data.sg) removeClass(thumb, 'ls-thumb-hover');
		};

		// Mouse wheel (with IE7 stopPropagation fix)
		var wrapper = data.wrapper;
		if (wrapper.onmousewheel !== undefined) {
			wrapper.onmousewheel = function(e) {
				e = e || window.event;
				e.cancelBubble = true;
				e.returnValue  = false;
				if (e.stopPropagation) e.stopPropagation();
				if (e.preventDefault) e.preventDefault();

				if (data._autoHideShow) data._autoHideShow();   // reveal on wheel

				var delta     = e.wheelDelta ? e.wheelDelta / 120 : -e.detail / 3;
				var maxScroll = getMaxScroll(data);
				var oldPos    = data.scrollPos || 0;
				var newPos    = Math.max(0, Math.min(oldPos - delta * 30, maxScroll));
				if (newPos !== oldPos) {
					data.inner.style.top = '-' + Math.floor(newPos) + 'px';
					data.scrollPos = newPos;
					refreshOne(container);
				}
				return false;
			};
		}

		// Native scroll event (programmatic / touchpad)
		container.onscroll = function() {
			// sync scrollPos from native scroll if it occurs
			data.scrollPos = container.scrollTop;
			refreshOne(container);
		};
	}

	/* ---------- Auto-hide logic ---------- */

	/**
	 * Set up auto-hide behaviour: show on hover/scroll, hide after delay.
	 * @param {Object} data - instance data
	 * @param {Object} opts - current options (including autoHideDelay)
	 */
	function setupAutoHide(data, opts) {
		var wrapper = data.wrapper;
		var container = data.container;
		var hideTimer = null;
		var visible = false;

		var HIDDEN_CLASS = supportsTransition ? 'ls-bar-fade' : 'ls-bar-hidden';

		function setVisibility(show) {
			if (show === visible) return;
			visible = show;
			if (show) {
				removeClass(wrapper, HIDDEN_CLASS);
			} else {
				addClass(wrapper, HIDDEN_CLASS);
			}
		}

		function showBar() {
			setVisibility(true);
			if (hideTimer) clearTimeout(hideTimer);
			hideTimer = setTimeout(function() {
				if (activeCont) return;   // dragging or arrow-scrolling: keep visible
				setVisibility(false);
			}, opts.autoHideDelay);
		}

		// Expose showBar so clearScrollTimer can restart the delay after drag ends
		data._autoHideShow = showBar;

		addClass(wrapper, HIDDEN_CLASS);
		visible = false;


		var lastX = -1, lastY = -1;

		function onMove(e) {
			e = e || window.event;
			var x = e.screenX, y = e.screenY;
			if (x === lastX && y === lastY) return;  // ignore spurious mousemove
			lastX = x;
			lastY = y;
			showBar();
		}

		if (wrapper.addEventListener) {
			wrapper.addEventListener('mouseenter', showBar);
			wrapper.addEventListener('mousemove',  onMove);
			wrapper.addEventListener('mouseleave', function() {
				if (activeCont) return;
				if (hideTimer) clearTimeout(hideTimer);
				hideTimer = setTimeout(function() {
					if (!activeCont) setVisibility(false);
				}, opts.autoHideDelay);
			});
		} else {
			// IE7/8
			wrapper.onmouseenter = showBar;
			wrapper.onmouseover  = showBar;   // IE7 has no reliable native onmouseenter
			wrapper.onmousemove  = onMove;
			wrapper.onmouseout   = function(e) {
				if (activeCont) return;
				e = e || window.event;
				var related = e.toElement || e.relatedTarget;
				if (related && !wrapper.contains(related)) {
					if (hideTimer) clearTimeout(hideTimer);
					hideTimer = setTimeout(function() {
						if (!activeCont) setVisibility(false);
					}, opts.autoHideDelay);
				}
			};
		}

		var oldScroll = container.onscroll;
		container.onscroll = function() {
			if (oldScroll) oldScroll.call(this);
			if (opts.autoHide) showBar();
		};

		data._autoHideTimer = hideTimer;
	}

	/* ---------- Global event handlers (drag, mouseup, resize) ---------- */

	/**
	 * Fully removes the scrollbar for a container: unwraps the container,
	 * moves the inner children back, deletes all created elements and clears
	 * the instance state. Safe to call on a non-initialized container.
	 * @param {HTMLElement|string} target - container element or its id
	 * @returns {boolean} true if a scrollbar was destroyed
	 */
	function destroy( target ) {
		var container = ( typeof target === 'string' )
			? document.getElementById( target )
			: target;

		if ( !container || !container._lsdata ) return false;

		var d = container._lsdata;

		// Stop pending timers
		if ( scrollTimer ) { clearTimeout( scrollTimer ); scrollTimer = null; }
		scrollSpeed = 0;

		if ( activeCont === container ) {
			activeCont = null;
			if ( container.releaseCapture ) container.releaseCapture();
		}
		if ( document.onselectstart ) document.onselectstart = null;

		// Move children from inner back into the container
		while ( d.inner.firstChild ) {
			container.appendChild( d.inner.firstChild );
		}

		// Restore container styles
		container.style.position = '';
		container.style.left     = '';
		container.style.top      = '';
		container.style.width    = '';
		container.style.height   = '';
		container.style.overflow = '';

		// Remove scrollbar elements
		var parts = [ d.track, d.thumb, d.up, d.down ];
		for ( var i = 0; i < parts.length; i++ ) {
			if ( parts[i] && parts[i].parentNode ) {
				parts[i].parentNode.removeChild( parts[i] );
			}
		}

		// Replace wrapper with container, then remove wrapper
		var wrapper = d.wrapper;
		if ( wrapper && wrapper.parentNode ) {
			wrapper.parentNode.insertBefore( container, wrapper );
			wrapper.parentNode.removeChild( wrapper );
		}

		// Unregister
		var idx = arrayIndexOf( containers, container );
		if ( idx !== -1 ) containers.splice( idx, 1 );

		delete container._lsdata;
		return true;
	}

	/**
	 * Rebinds an existing scrollbar to the container's current content.
	 *
	 * Replacing the container content (e.g. `container.innerHTML = html`)
	 * detaches the internal `inner` element that every scrollbar handler
	 * moves, so wheel, drag and arrows would act on an orphan node. This
	 * recreates `inner` when needed, moves the new content into it, keeps the
	 * scroll position (clamped to the new range) and refreshes the geometry.
	 * The wrapper and all event handlers are preserved.
	 *
	 * @public
	 * @param {HTMLElement|string} target - container element or its id.
	 * @returns {HTMLElement|null} The container, or null when it does not exist
	 *                             or has no scrollbar yet (use scrollbar() then).
	 */
	function update( target ) {
		var container = ( typeof target === 'string' )
			? document.getElementById( target )
			: target;

		if ( !container || !container._lsdata ) return null;

		var data  = container._lsdata;
		var inner = data.inner;

		// Setting innerHTML on the container detaches `inner`: create a new one.
		if ( inner.parentNode !== container ) {
			inner = document.createElement( 'div' );
			inner.style.position = 'relative';
			data.inner = inner;
		}

		// Move any content that was added outside `inner` into it.
		var node = container.firstChild;
		while ( node ) {
			var next = node.nextSibling;
			if ( node !== inner ) inner.appendChild( node );
			node = next;
		}
		if ( inner.parentNode !== container ) container.appendChild( inner );

		// Measure with the content at the top, then restore a clamped position.
		inner.style.top = '0px';
		var maxScroll = getMaxScroll( data );
		var pos = Math.max( 0, Math.min( data.scrollPos || 0, maxScroll ));
		inner.style.top = '-' + Math.floor( pos ) + 'px';
		data.scrollPos = pos;

		refreshOne( container );
		return container;
	}


	/**
	 * Initialise global mouse/resize listeners and inject the shared stylesheet.
	 * Must be called with a valid DOM element so injectStyles can insert the <style> tag.
	 * Guarded by window._legacyScrollInit so it runs only once.
	 * @param {HTMLElement} dom - any element already in the document
	 */
	function initGlobal( dom ) {
		if (window._legacyScrollInit) return;
		window._legacyScrollInit = true;

		injectStyles( dom );

		function onMouseMove(e) {
			e = e || window.event;
			if (!activeCont) return;
			var data = activeCont._lsdata;
			if (!data.sg) return;

			var mouseY    = e.screenY;
			var deltaY    = mouseY - data.dragStartY;
			var newScrollTop = data.dragStartScroll + deltaY / data.ratio;
			var maxScroll    = getMaxScroll(data);
			newScrollTop     = Math.max(0, Math.min(newScrollTop, maxScroll));
			data.inner.style.top = '-' + Math.floor(newScrollTop) + 'px';
			data.scrollPos = newScrollTop;
			refreshOne(activeCont);
		}

		function onMouseUp() {
			if (activeCont) {
				clearScrollTimer(activeCont);
				activeCont = null;
			}
		}

		function onResize() {
			refreshAll();
		}

		if (document.addEventListener) {
			document.addEventListener('mousemove', onMouseMove, false);
			document.addEventListener('mouseup', onMouseUp, false);
			window.addEventListener('resize', onResize, false);
		} else if (document.attachEvent) {
			document.attachEvent('onmousemove', onMouseMove);
			document.attachEvent('onmouseup', onMouseUp);
			window.attachEvent('onresize', onResize);
		}
	}

	/* ---------- Public API ---------- */

	/**
	 * Create a custom scrollbar for a given container.
	 * @public
	 * @param {string} containerId - ID of the target element (must have a defined height).
	 * @param {Object} [userOptions] - Configuration object (see module documentation).
	 * @returns {HTMLElement|null} The container element or null if not found.
	 */
	function scrollbar(containerId, userOptions) {
		var container = document.getElementById(containerId);

		// Guard: element must exist before doing anything else.
		// Calling initGlobal or injectStyles with a null element crashes IE7/8.
		if (!container) return null;

		// Now it is safe to initialise globals (injectStyles requires a valid element).
		legacyScroll.init( container );

		// Rebuild from scratch: drop the old scrollbar, if any
		if ( container._lsdata ) destroy( container );

		var opts = extend({}, _defaults);
		opts = extend(opts, userOptions || {});

		// ---- 1. Create wrapper (clone of original element without content) ----
		var wrapper = container.cloneNode(false);
		wrapper.removeAttribute('id');   // id belongs to the real container only

		// Inherit margin from the original element so the wrapper sits in
		// the same position in the flow. Padding and border are reset
		// because the wrapper is purely a layout shell.
		// wrapper.style.overflow      = 'hidden';
		// wrapper.style.position      = 'relative';
		// wrapper.style.padding       = '0';
		// wrapper.style.border        = 'none';
		wrapper.style.marginTop     = getCSSInt(container, 'marginTop')    + 'px';
		wrapper.style.marginBottom  = getCSSInt(container, 'marginBottom') + 'px';
		wrapper.style.marginLeft    = getCSSInt(container, 'marginLeft')   + 'px';
		wrapper.style.marginRight   = getCSSInt(container, 'marginRight')  + 'px';
		// Width: offsetWidth already includes padding+border.
		wrapper.style.width  = container.offsetWidth + 'px';

		// Height: use the declared option, or fall back to the viewport height.
		// This means the user never needs to set height in CSS — the scrollable
		// area will naturally match what is visible in the browser window.
		var viewportH = document.documentElement.clientHeight || document.body.clientHeight || 0;
		var wrapH     = (opts.height !== null && opts.height > 0) ? opts.height : viewportH;
		wrapper.style.height = wrapH + 'px';

		container.parentNode.insertBefore(wrapper, container);
		wrapper.appendChild(container);

		// Make container absolute inside wrapper
		container.style.position = 'absolute';
		container.style.left     = '0px';
		container.style.top      = '0px';
		container.style.width    = '100%';
		container.style.height   = '100%';
		container.style.overflow = 'hidden';

		// Wrap all existing children in an inner div.
		// This is required because IE7 reports scrollHeight == clientHeight
		// when overflow:hidden is set — the inner div's offsetHeight gives the
		// true content height regardless of overflow settings.
		var inner = document.createElement('div');
		inner.style.position = 'relative';
		while (container.firstChild) {
			inner.appendChild(container.firstChild);
		}
		container.appendChild(inner);

		// ---- 2. Create scrollbar elements ----
		var track = document.createElement('div'); track.className = 'ls-track';
		var thumb = document.createElement('div'); thumb.className = 'ls-thumb';

		var up = null, down = null;
		if (opts.buttons && opts.trackHeight === null) {
			up   = document.createElement('div'); up.className   = 'ls-up';
			down = document.createElement('div'); down.className = 'ls-down';
		}

		wrapper.appendChild(track);
		wrapper.appendChild(thumb);
		if (up)   wrapper.appendChild(up);
		if (down) wrapper.appendChild(down);

		// ---- 3. Store instance data ----
		var data = {
			container:       container,
			wrapper:         wrapper,
			inner:           inner,
			track:           track,
			thumb:           thumb,
			up:              up,
			down:            down,
			width:           opts.width,
			buttons:         !!(up && down),
			trackHeight:     opts.trackHeight,
			ratio:           0,
			sg:              false,
			dragStartY:      0,
			dragStartScroll: 0,
			scrollPos:       0
		};
		container._lsdata = data;
		containers.push(container);

		// ---- 4. Apply colors from options ----
		track.style.backgroundColor = opts.colorTrack;
		thumb.style.backgroundColor = opts.colorThumb;
		if (up)   up.style.backgroundColor   = opts.colorUp;
		if (down) down.style.backgroundColor = opts.colorDown;

		// ---- 5. Attach event handlers ----
		setupBarEvents(container, data);

		// ---- 6. Setup auto-hide if requested ----
		if (opts.autoHide) {
			setupAutoHide(data, opts);
		}

		// ---- 7. Initial refresh ----
		refreshOne(container);

		return container;
	}

	// Expose public API globally
	window.legacyScroll = {
		init:      initGlobal,
		scrollbar: scrollbar,
		update:    update,
		refresh:   refreshAll,
		destroy:   destroy
	};
})();
