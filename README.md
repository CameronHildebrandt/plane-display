# 128×64 HUB75 simulator

A deliberately small local simulator for a 128×64 (horizontal × vertical) RGB HUB75 panel. It has no Rust dependencies and runs a browser canvas locally, so it is friendly to an older Intel Mac.

## Run it

Install the stable Rust toolchain if it is not already installed, then run:

```sh
cargo run --release
```

Open `http://127.0.0.1:7878`. A hardcoded WestJet-style ticket confirms the viewer is running.

Do not open `web/index.html` directly or host only the `web` folder: that page is just the viewer and needs the Rust server for its frame endpoint. It does show a small static boarding-pass preview in that situation, but live frames require the address above.

Choose a different local port with `cargo run --release -- --port 9000`.

## Frame contract

`POST http://127.0.0.1:7878/api/frame` replaces the entire panel image.

- Exact payload length: `128 × 64 × 3 = 24,576` bytes
- Order: pixels left-to-right, then top-to-bottom
- Each pixel: `R, G, B`, one 8-bit byte each (RGB888)

For example, any program/library adapter can send its rendered framebuffer with:

```text
HTTP POST /api/frame
Content-Type: application/octet-stream

[24,576 raw RGB bytes]
```

`GET /api/frame` returns the current raw framebuffer, and `GET /api/status` exposes dimensions and input-frame statistics.

## Flight-ticket compatibility layer

The simulator now also accepts the normalized presentation model proposed for the ESP32. Send the JSON payload to `POST /api/presentation`; the built-in renderer turns it into the same 128×64 pixel image that an embedded `flight-ticket` renderer would draw.

Try the included WestJet example after starting the simulator:

```sh
curl --data-binary @examples/westjet-ticket.json \
  -H 'Content-Type: application/json' \
  http://127.0.0.1:7878/api/presentation
```

The version-1 compatibility contract currently renders `flight.number`, `flight.airline`, `origin`, `destination`, `aircraftType`, altitude, ground speed, viewer distance, approach state, journey progress, and a WestJet colour theme. Missing origin/destination automatically produces a technical fallback card rather than a misleading route.

This is deliberately the application-level compatibility layer. It gives the Mac simulator and future ESP32 firmware one stable input format while the raw ADS-B/route enrichment remains in the backend.

## Work like the ESP32 today

You do **not** need to post JSON by hand while developing the layout. The page starts with a hardcoded WestJet ticket. More importantly, `firmware/include/flight_ticket.hpp` is the platform-neutral renderer: it receives a `FlightTicket` struct and only needs a `Canvas` that can set pixels and fill rectangles.

The Mac-side ESP emulator uses that exact renderer, sends its resulting RGB framebuffer to the local simulator, and has no Arduino dependency:

```sh
c++ -std=c++17 -Ifirmware/include firmware/host/demo.cpp -o /tmp/flight-ticket-demo
/tmp/flight-ticket-demo
```

For the actual board we will provide a small `Canvas` adapter whose `pixel()`/`fill_rect()` call the chosen HUB75 library. Then the firmware flow is simply: fetch JSON → fill `FlightTicket` → `render_flight_ticket(panel, ticket)`. The host flow is the same until its `Canvas` submits the frame to this website.

## Hardware library selected

The ESP32-S3 target is now set up for **ESP32-HUB75-MatrixPanel-DMA**. Its adapter is in `firmware/esp32/hub75_canvas.hpp`; the starter sketch is `firmware/esp32/flight_display.ino`. The only remaining hardware-specific decision is the exact S3 board/adapter, because the HUB75 GPIO mapping must match it exactly.

## What is simulated now

The viewer keeps the exact 128×64 framebuffer and models a 1/32-scan HUB75 panel in the UI. The browser draws the pixels with no smoothing and reports its render rate, true incoming-frame count, and age of the most recent received frame. It is a functional framebuffer/timing visualizer, rather than an electrical simulation of LED PWM, panel ghosting, or the ESP32 DMA engine. The HTTP boundary makes it possible to attach an adapter without changing the display viewer.

## Next integration step

The prior shared chat could not be loaded from its link here, so the specific display library/API is not yet known. Paste the sketch/library call pattern (or its repository URL) and the next step is a small adapter that captures those drawing calls and submits frames automatically. That is the piece that lets the existing display code run unchanged or nearly unchanged on the Mac.
