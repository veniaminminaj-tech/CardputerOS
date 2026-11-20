# Browser: supported HTML, CSS and JavaScript

CardputerOS Browser is a lightweight text renderer for the 320×240 display. It is not Chromium or a full HTML5 browser.

## Supported features

- HTTP and HTTPS GET requests, redirects, and relative links.
- Mobile User-Agent and `Accept-Encoding: identity` (no gzip/Brotli decompression).
- Basic HTML text, headings, paragraphs, lists, links, image alt text, and common entities.
- Inline `style` attributes and embedded `<style>` blocks.
- CSS selectors: tag names, `.class`, and `#id`.
- CSS properties: `color`, `background`, and `background-color`.
- A bounded JavaScript subset: `var`, `let`, and `const` string values, string concatenation with `+`, `document.write()`, and `document.title`.

## Deliberate limits

- No DOM, event handlers, timers, `eval`, fetch/XHR, modules, or external JavaScript files.
- No external CSS files, complex selectors, layout, flexbox, grid, dimensions, or full CSS cascade/specificity.
- No image decoding/rendering, forms, cookies, or modern SPA support.
- JavaScript processing is limited to at most 12 script blocks and a small statement budget. CSS parsing is capped at 48 rules and 6000 characters.

Modern social networks, webmail, and interactive web apps may not render. Prefer lightweight/mobile pages or simple HTML pages.

## Browser controls

- `G`: edit URL.
- `W/S`: select a link.
- `Enter`: open selected link.
- `A/D`: scroll.
- `Backspace`: go back.

The current implementation is a prototype. Run the PlatformIO GitHub Actions build before flashing firmware to the device.
