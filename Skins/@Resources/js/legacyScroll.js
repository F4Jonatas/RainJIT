/**
 * Custom scrollbar for Trident (IE7+) and modern browsers.
 *
 * Features: draggable thumb, track click, optional arrow buttons, mouse wheel,
 * keyboard navigation, smooth (animated) scrolling and auto-hide.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * @license GPL v2.0 License
 * @author F4Jonatas
 * @release 0.1.4
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
 * @option {boolean}     [smooth=true]                    - Smooth (animated) scrolling for wheel, keyboard and track clicks.
 *                                                          Enabled only when the browser supports animation frames and the user
 *                                                          has not requested reduced motion; otherwise scrolling is instant.
 *                                                          Dragging the thumb and the arrow buttons are always instant.
 * @option {number}      [smoothFactor=0.18]              - Easing speed per 16.7 ms frame, from 0.01 (slow) to 1 (instant).
 * @option {boolean}     [keyboard=true]                  - Keyboard navigation: Up/Down (line), PageUp/PageDown and
 *                                                          Space/Shift+Space (page), Home/End (limits). Acts while the
 *                                                          scrollbar has focus or is under the mouse; ignored while typing
 *                                                          in form fields.
 * @option {number}      [wheelStep=30]                   - Pixels scrolled per mouse wheel notch.
 * @option {number}      [keyStep=40]                     - Pixels scrolled per Up/Down key press.
 * @option {number}      [pageOverlap=40]                 - Pixels kept visible between pages (PageUp/PageDown/Space).
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
 * // Slower smooth scrolling, keyboard disabled
 * legacyScroll.scrollbar('content', { smoothFactor: 0.1, keyboard: false });
 *
 * @example
 * // Instant scrolling (no animation)
 * legacyScroll.scrollbar('content', { smooth: false });
 *
 * @example
 * // After replacing the container content (e.g. via innerHTML), rebind the
 * // scrollbar without rebuilding it. Events and scroll position are kept.
 * document.getElementById('content').innerHTML = html;
 * legacyScroll.update('content');
 *
 * @example
 * // Feature detection results
 * if ( legacyScroll.supports.animation ) { ... }
 */
(function() {

	// Internal state
	var containers = [],              // all scrollbar instances
		activeCont = null,              // currently dragged container
		hoverCont = null,               // container under the mouse (keyboard target)
		focusCont = null,               // container that owns the focus (keyboard target)
		scrollTimer = null,             // timer for arrow smooth scrolling
		scrollSpeed = 0,                // direction and speed for arrows
		scrollDelay = 400,              // initial delay before acceleration
		styleId = 'legacyScrollStyles', // ID of injected <style> element
		FRAME_MS = 16.67,               // reference frame duration used by the easing

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
			trackHeight:      null,
			smooth:           true,
			smoothFactor:     0.18,
			keyboard:         true,
			wheelStep:        30,
			keyStep:          40,
			pageOverlap:      40
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
	 * Cross-browser event binding (addEventListener / attachEvent).
	 * Unlike the `onxxx` properties, several handlers can coexist.
	 * @param {HTMLElement|Document|Window} el
	 * @param {string} name - event name without the "on" prefix.
	 * @param {Function} fn
	 */
	function addEvent(el, name, fn) {
		if (el.addEventListener) {
			el.addEventListener(name, fn, false);
		} else if (el.attachEvent) {
			el.attachEvent('on' + name, fn);
		}
	}

	/**
	 * Current time in milliseconds (IE7+ safe).
	 * @returns {number}
	 */
	function now() {
		return (new Date()).getTime();
	}

	/**
	 * Tells whether the pointer moved from inside a wrapper to outside of it.
	 * @param {HTMLElement} wrapper
	 * @param {Event} e - mouseout event.
	 * @returns {boolean} true when the related target is still inside the wrapper.
	 */
	function stillInside(wrapper, e) {
		var rel = e.relatedTarget || e.toElement;
		return !!(rel && (rel === wrapper || (wrapper.contains && wrapper.contains(rel))));
	}

	/**
	 * Tells whether an element handles keyboard input by itself.
	 * @param {HTMLElement} el
	 * @returns {boolean}
	 */
	function isEditable(el) {
		if (!el) return false;
		var tag = (el.tagName || '').toUpperCase();
		return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' ||
			tag === 'BUTTON' || el.isContentEditable === true;
	}

	/* ---------- Feature detection ---------- */

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

	/**
	 * Detect animation frame support (required by the smooth scrolling) and
	 * the "reduce motion" user preference. Without requestAnimationFrame
	 * (IE7/8/9) scrolling silently falls back to instant jumps.
	 */
	var frameRequest = null,
		prefersReducedMotion = false;
	(function() {
		try {
			frameRequest = window.requestAnimationFrame ||
				window.webkitRequestAnimationFrame ||
				window.mozRequestAnimationFrame ||
				window.msRequestAnimationFrame || null;
		} catch(e) {
			frameRequest = null;
		}

		try {
			if (window.matchMedia) {
				prefersReducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)').matches === true;
			}
		} catch(e) {
			prefersReducedMotion = false;
		}
	})();

	var supportsAnimation = !!frameRequest;

	/**
	 * Schedule a callback for the next animation frame.
	 * @param {Function} fn
	 */
	function requestFrame(fn) {
		frameRequest.call(window, fn);
	}

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
	 * Used by refreshOne, drag, wheel, keyboard and arrows — keeps ratio consistent.
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

	/* ---------- Scroll position engine (instant + smooth) ---------- */

	/**
	 * Clamp a scroll position to the valid range of an instance.
	 * @param {Object} data - instance data
	 * @param {number} pos - desired position in px.
	 * @returns {number}
	 */
	function clampPos(data, pos) {
		if (isNaN(pos)) pos = 0;
		return Math.max(0, Math.min(pos, getMaxScroll(data)));
	}

	/**
	 * Apply a scroll position immediately (moves content and thumb).
	 * @param {Object} data - instance data
	 * @param {number} pos - position in px (already clamped).
	 */
	function applyPos(data, pos) {
		data.scrollPos = pos;
		data.inner.style.top = '-' + Math.floor(pos) + 'px';
		refreshOne(data.container);
	}

	/**
	 * Cancel any running smooth animation and drop its pending target.
	 * @param {Object} data - instance data
	 */
	function stopAnimation(data) {
		data.animating = false;
		data.animToken++;               // invalidates any frame already scheduled
		data.target    = data.scrollPos;
	}

	/**
	 * Position the scroll is heading to: the animation target while animating,
	 * otherwise the current position. New relative scrolls accumulate on it.
	 * @param {Object} data - instance data
	 * @returns {number}
	 */
	function getTarget(data) {
		return data.animating ? data.target : (data.scrollPos || 0);
	}

	/**
	 * One frame of the easing animation (frame-rate independent).
	 * @param {Object} data - instance data
	 * @param {number} token - animation id; stale frames are ignored.
	 */
	function animateStep(data, token) {
		if (token !== data.animToken || !data.animating) return;

		data.target = clampPos(data, data.target);

		var t  = now();
		var dt = t - data.animLast;
		data.animLast = t;
		if (dt <= 0)  dt = FRAME_MS;
		if (dt > 64)  dt = 64;          // avoid a huge jump after a stalled frame

		var diff = data.target - data.scrollPos;
		if (Math.abs(diff) <= 0.5) {
			applyPos(data, data.target);
			data.animating = false;
			return;
		}

		var k    = 1 - Math.pow(1 - data.smoothFactor, dt / FRAME_MS);
		var step = diff * k;
		if (Math.abs(step) < 1) {
			step = (diff > 0 ? 1 : -1) * Math.min(1, Math.abs(diff));
		}

		applyPos(data, data.scrollPos + step);
		requestFrame(function() { animateStep(data, token); });
	}

	/**
	 * Scroll an instance to a position, animated when smooth scrolling is on.
	 * @param {Object} data - instance data
	 * @param {number} pos - target position in px.
	 * @param {boolean} [instant=false] - force an immediate jump.
	 */
	function scrollToPos(data, pos, instant) {
		pos = clampPos(data, pos);

		if (instant || !data.smooth) {
			stopAnimation(data);
			applyPos(data, pos);
			return;
		}

		data.target = pos;

		if (!data.animating) {
			data.animating = true;
			data.animLast  = now();
			var token = ++data.animToken;
			requestFrame(function() { animateStep(data, token); });
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
		stopAnimation(data);
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
		applyPos(d, clampPos(d, curPos + Math.ceil(20 * scrollSpeed)));
		scrollTimer = setTimeout(function() {
			arrowScrollStep(cont);
		}, scrollDelay);
		scrollDelay = 32;
	}

	/* ---------- Main bar events (drag, track click, wheel, native scroll) ---------- */

	/**
	 * Give the keyboard focus to the scrollbar wrapper (when keyboard is on).
	 * The thumb/track cancel the default mousedown, so focus is set manually.
	 * @param {Object} data - instance data
	 */
	function focusWrapper(data) {
		if (!data.keyboard) return;
		try { data.wrapper.focus(); } catch(e) {}
	}

	/**
	 * Attach all necessary mouse and wheel events to the scrollbar components.
	 * @param {HTMLElement} container - the original content container
	 * @param {Object} data - the instance's data object
	 */
	function setupBarEvents(container, data) {
		var thumb = data.thumb,
			track = data.track;

		// Thumb drag (always instant: the thumb must follow the mouse)
		thumb.onmousedown = function(e) {
			e = e || window.event;
			if (data.sg) return false;
			stopAnimation(data);
			focusWrapper(data);
			activeCont           = container;
			data.sg              = true;
			data.dragStartY      = e.screenY;
			data.dragStartScroll = data.scrollPos || 0;
			addClass(thumb, 'ls-thumb-active');
			if (thumb.setCapture)     thumb.setCapture();
			if (container.setCapture) container.setCapture();
			return false;
		};

		// Click on track (jump to position, animated when smooth is on)
		track.onmousedown = function(e) {
			e = e || window.event;
			if (data.sg) return false;

			focusWrapper(data);

			var mouseY = e.clientY + (document.body.scrollTop || document.documentElement.scrollTop || 0);
			var trackTopAbs = 0;
			var el = track;
			while (el) { trackTopAbs += el.offsetTop; el = el.offsetParent; }

			var clickPos = mouseY - trackTopAbs;
			var thumbH   = thumb.offsetHeight;
			var newScrollTop = clampPos(data, (clickPos - thumbH / 2) / data.ratio);

			scrollToPos(data, newScrollTop);

			// A drag started from the click continues from the clicked position.
			activeCont           = container;
			data.sg              = true;
			data.dragStartY      = e.screenY;
			data.dragStartScroll = newScrollTop;
			addClass(thumb, 'ls-thumb-active');
			if (thumb.setCapture)     thumb.setCapture();
			if (container.setCapture) container.setCapture();
			return false;
		};

		// Up button (if exists)
		if (data.up) {
			data.up.onmousedown = function() {
				if (scrollSpeed !== 0) return false;
				focusWrapper(data);
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
				focusWrapper(data);
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

				var delta = e.wheelDelta ? e.wheelDelta / 120 : -e.detail / 3;
				scrollToPos(data, getTarget(data) - delta * data.wheelStep);
				return false;
			};
		}

		// Native scroll event (programmatic / touchpad)
		container.onscroll = function() {
			// sync scrollPos from native scroll if it occurs
			stopAnimation(data);
			data.scrollPos = container.scrollTop;
			data.target    = data.scrollPos;
			refreshOne(container);
		};

		// Track which instance is under the mouse / focused (keyboard target)
		addEvent(wrapper, 'mouseover', function() {
			hoverCont = container;
		});
		addEvent(wrapper, 'mouseout', function(e) {
			e = e || window.event;
			if (stillInside(wrapper, e)) return;
			if (hoverCont === container) hoverCont = null;
		});

		if (data.keyboard) {
			wrapper.tabIndex = 0;                 // focusable by click or script
			wrapper.style.outline = 'none';       // no focus ring
			wrapper.hideFocus = true;             // IE7/8 equivalent of outline: none

			addEvent(wrapper, 'focus', function() { focusCont = container; });
			addEvent(wrapper, 'blur',  function() {
				if (focusCont === container) focusCont = null;
			});
		}
	}

	/* ---------- Keyboard ---------- */

	/**
	 * Document-level key handler: scrolls the focused or hovered instance.
	 * Up/Down: one line. PageUp/PageDown and Space/Shift+Space: one page.
	 * Home/End: first/last position.
	 * @param {KeyboardEvent} e
	 */
	function onKeyDown(e) {
		e = e || window.event;

		var cont = focusCont || hoverCont;
		if (!cont || !cont._lsdata) return;

		var data = cont._lsdata;
		if (!data.keyboard) return;
		if (e.altKey || e.ctrlKey || e.metaKey) return;
		if (isEditable(e.target || e.srcElement)) return;

		var page   = Math.max(data.keyStep, cont.clientHeight - data.pageOverlap);
		var target = null;

		switch (e.keyCode) {
			case 38: target = getTarget(data) - data.keyStep; break;                   // Up
			case 40: target = getTarget(data) + data.keyStep; break;                   // Down
			case 33: target = getTarget(data) - page;         break;                   // PageUp
			case 34: target = getTarget(data) + page;         break;                   // PageDown
			case 32: target = getTarget(data) + (e.shiftKey ? -page : page); break;    // Space
			case 36: target = 0;                              break;                   // Home
			case 35: target = getMaxScroll(data);             break;                   // End
			default: return;
		}

		// Keep the page itself from scrolling together with the instance.
		e.cancelBubble = true;
		e.returnValue  = false;
		if (e.stopPropagation) e.stopPropagation();
		if (e.preventDefault)  e.preventDefault();

		if (data._autoHideShow) data._autoHideShow();   // reveal on key scroll
		scrollToPos(data, target);
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

	/* ---------- Lifecycle (destroy, update) ---------- */

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

		// Stop pending timers and animations
		stopAnimation( d );
		if ( scrollTimer ) { clearTimeout( scrollTimer ); scrollTimer = null; }
		scrollSpeed = 0;

		if ( activeCont === container ) {
			activeCont = null;
			if ( container.releaseCapture ) container.releaseCapture();
		}
		if ( hoverCont === container ) hoverCont = null;
		if ( focusCont === container ) focusCont = null;
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
	 * The wrapper and all event handlers are preserved. Any running smooth
	 * animation is cancelled.
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

		stopAnimation( data );

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
		applyPos( data, clampPos( data, data.scrollPos || 0 ));
		data.target = data.scrollPos;

		return container;
	}

	/* ---------- Global event handlers (drag, mouseup, resize, keyboard) ---------- */

	/**
	 * Initialise global mouse/keyboard/resize listeners and inject the shared stylesheet.
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
			stopAnimation(data);
			applyPos(data, clampPos(data, newScrollTop));
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
			document.addEventListener('keydown', onKeyDown, false);
			window.addEventListener('resize', onResize, false);
		} else if (document.attachEvent) {
			document.attachEvent('onmousemove', onMouseMove);
			document.attachEvent('onmouseup', onMouseUp);
			document.attachEvent('onkeydown', onKeyDown);
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
		var factor = parseFloat(opts.smoothFactor);
		if (isNaN(factor)) factor = _defaults.smoothFactor;
		factor = Math.max(0.01, Math.min(factor, 1));

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
			scrollPos:       0,

			// Smooth scrolling: requested AND supported AND not reduced motion
			smooth:          !!opts.smooth && supportsAnimation && !prefersReducedMotion,
			smoothFactor:    factor,
			target:          0,
			animating:       false,
			animToken:       0,
			animLast:        0,

			// Keyboard and step sizes
			keyboard:        !!opts.keyboard,
			wheelStep:       opts.wheelStep,
			keyStep:         opts.keyStep,
			pageOverlap:     opts.pageOverlap
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
		destroy:   destroy,

		/**
		 * Feature detection results.
		 * @type {{animation: boolean, transition: boolean, reducedMotion: boolean}}
		 */
		supports: {
			animation:     supportsAnimation,
			transition:    supportsTransition,
			reducedMotion: prefersReducedMotion
		}
	};
})();
