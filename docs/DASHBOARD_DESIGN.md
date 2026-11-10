# Dashboard design and interaction

SmartVest uses a shared instrument-style visual system across the rider and administrator dashboards. The product remains v1.0. All views work locally from Main; no font service, image CDN, cloud UI service or frontend framework is required.

## Visual direction

Warm cream surfaces, graphite typography and restrained safety-orange controls give the interface the character of motorcycle instruments. Readiness, faults and measurement age remain explicit. The rider overview places readiness and faults first, pairs the front/side position display with monitoring checks, and groups location with contacts. Administrator pages use measurement strips, equipment tables, charts and records according to their task. Sign-in uses a typographic SmartVest identity and three factual capabilities; it has no decorative vest or promotional tagline.

The dashboard groups readings by task. Rider views prioritize device readiness, emergency contacts and saved clips. Developer views expose detailed sensor and controller status.

Cream is the default appearance for both roles and sign-in. Account â†’ Appearance offers Light (cream) and warm charcoal Dim modes; `sv-theme` saves only this browser preference. Clearing browser storage restores cream. The palette applies to native controls, dialogs, graphs, battery indicators and the WebGL scene. Theme changes do not contact hardware or create another canvas. Printing uses a light palette.

## Useful interactions

- **Appearance:** choose Light or Dim under Account in either dashboard. The selected mode is saved in this browser and also applies to sign-in. The header contains no appearance control.
- **Find a page:** open the page switcher, press Ctrl/Cmd+K, or press `/` outside an input. Search only includes the current role's navigation. Arrow keys move the selection; Enter opens a page; Escape closes the switcher. Navigation retains the normal unsaved-edit warning.
- **Spacing:** choose comfortable or compact spacing. This preference is stored locally in the browser; readings and device settings are unaffected. Clearing browser storage resets it.
- **Copy coordinates:** the explicit copy button copies displayed latitude and longitude. Copy failure is reported, with a selection-and-copy fallback. Last-known coordinates keep their existing age/status labels.
- **Equipment filters:** administrators can show all modules or only warning/fault states. Filtering is immediate and local, preserves the chosen filter across updates, and does not contact or configure hardware.
- **Diagnostic sections:** eligible read-only sections remember their expansion state within the browser session. Passwords, revealed keys, forms, calibration setup and device commands are excluded. No diagnostic values are stored by this feature.
- **Mobile navigation:** rider pages use a bottom navigation bar; administrator pages use a compact drawer. The drawer supports keyboard focus, Escape, outside close and viewport changes.

Manual calibration, sensitive actions, real SMS tests and destructive operations retain their existing authentication, confirmations and readiness checks. Opening a page, changing spacing, searching or filtering never sends a device command.

## Responsive behavior and motion

Layouts adapt from narrow phones through tablets and desktop displays. Content reflows while wide diagnostic tables stay inside their own scroll containers. Controls support touch, mouse and keyboard; focus remains visible. Safe-area spacing keeps fixed navigation clear of device UI. System reduced-motion and forced-color preferences receive dedicated treatment.

Motion gives brief input feedback and maintains continuity. It does not animate every live reading or delay controls. [Apple's guidance](https://developer.apple.com/design/human-interface-guidelines/motion) supports purposeful, optional motion; [web.dev](https://web.dev/articles/animations-guide) explains why transform/opacity changes avoid expensive layout animation. [W3C reflow](https://www.w3.org/WAI/WCAG22/Understanding/reflow.html) and [target-size guidance](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html) inform the layout and controls. These references inform implementation; they do not constitute a conformance certification.

The shared interactions add no background polling, prefetch traffic or additional sensor sampling. Stopped or hidden views cancel their outstanding read requests; writes retain their existing explicit confirmation and result handling. The existing renderer retains frame-based smoothing, freshness limits, visibility/offscreen handling and GPU cleanup. See [rider position](RIDER_POSITION.md) for the measured-torso/illustrative-bike boundary and calibration behavior.

## Loading and recovery

The initial HTML includes its styles and startup supervisor, so appearance does not depend on another successful download. Exact generated SHA-256 hashes authorize these blocks in the Content Security Policy. A 15-second feedback timer reports a delayed connection without stopping startup. Failed or timed-out module downloads retry automatically with bounded backoff and a fresh, validated asset namespace; each attempt has a 35-second deadline. Successful modules are reused. Late startup entries cannot initialize a second application, and obsolete page loads cannot mount after navigation. A valid login is retained during temporary read failures. No request automatically repeats a save, calibration command or SMS.

The existing 30-minute authentication lifetime remains fixed from sign-in. Expiry returns to sign-in. Current readings retain their freshness rules; a reload or a saved appearance does not establish equipment health.

Signing in shows Loading dashboard until that page receives data. A failed initial read cannot show Connected to Vest. HTTP or invalid-response errors show Updates unavailable; rendering errors show Display unavailable. Administrators receive the available HTTP code and escaped error text, while riders receive concise recovery instructions. Successful retries restore the live label. Brief transport failures after a successful update retain the existing freshness grace period; stale data still loses its live status.

## Maintenance and verification

Edit shared CSS, shell generation and the small shared interaction module, then regenerate embedded assets. Asset URLs receive content fingerprints while the product version stays 1.0. Keep deduplicated compressed UI payloads within 100 KiB and retain 8 KiB below Main's IDE size limit, 24 KiB within its physical application slots, and the supplied partition table. Do not cache live API responses as an offline substitute for current hardware status.

The [verification guide](VERIFICATION.md) explains the software and hardware checks. The [UI manifest](../Main_and_Cameras/ui/manifest.json) records generated asset sizes. Run the browser checks with the device and browser combinations you plan to use.

Production modules are generated with pinned esbuild 0.28.2 and Terser 5.48.0. Readable JavaScript stays in `ui`; required optimized delivery modules stay in `ui-built`. Shared runtime and display bundles reduce startup requests. Administrator overview does not download the live position scene. Runtime aliases refer to one initialized API/session state, including after a retry. Two safe compression passes are enabled; unsafe transformations and property renaming are disabled. The build tests verify source hashes, exact flash payload round trips and deterministic generation. These are build-only dependencies, with no browser/CDN dependency.
