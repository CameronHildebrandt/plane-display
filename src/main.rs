//! Tiny local HUB75 simulator.  It serves a canvas viewer and accepts RGB888 frames.
//! No crates are used so it remains easy to build on older machines.

mod presentation;

use std::{
    env,
    fs,
    io::{self, Read, Write},
    net::{TcpListener, TcpStream},
    path::PathBuf,
    sync::{Arc, Mutex},
    thread,
    time::Instant,
};

const WIDTH: usize = 128;
const HEIGHT: usize = 64;
const FRAME_BYTES: usize = WIDTH * HEIGHT * 3;

struct State {
    pixels: Vec<u8>,
    frames_received: u64,
    last_frame: Option<Instant>,
}

fn main() -> io::Result<()> {
    let port = env::args()
        .skip_while(|arg| arg != "--port")
        .nth(1)
        .and_then(|value| value.parse().ok())
        .unwrap_or(7878);
    let listener = TcpListener::bind(("127.0.0.1", port))?;
    // Start with the same deterministic fixture used by the host-side ESP emulator.
    // This means the viewer is useful before a backend exists.
    let starting_frame = presentation::render(include_str!("../examples/westjet-ticket.json"))
        .unwrap_or_else(|_| demo_frame());
    let state = Arc::new(Mutex::new(State { pixels: starting_frame, frames_received: 0, last_frame: None }));

    println!("HUB75 simulator running at http://127.0.0.1:{port}");
    println!("Display: {WIDTH}x{HEIGHT}, RGB888, POST endpoint: /api/frame");

    for connection in listener.incoming() {
        match connection {
            Ok(stream) => {
                let state = Arc::clone(&state);
                thread::spawn(move || {
                    if let Err(error) = handle(stream, state) {
                        eprintln!("request error: {error}");
                    }
                });
            }
            Err(error) => eprintln!("connection error: {error}"),
        }
    }
    Ok(())
}

fn handle(mut stream: TcpStream, state: Arc<Mutex<State>>) -> io::Result<()> {
    stream.set_read_timeout(Some(std::time::Duration::from_secs(3)))?;
    let mut request = Vec::new();
    let mut chunk = [0; 8192];
    let header_end;
    loop {
        let count = stream.read(&mut chunk)?;
        if count == 0 { return Ok(()); }
        request.extend_from_slice(&chunk[..count]);
        if let Some(position) = request.windows(4).position(|window| window == b"\r\n\r\n") {
            header_end = position + 4;
            break;
        }
        if request.len() > 16 * 1024 { return respond(&mut stream, 413, "headers too large", "text/plain"); }
    }

    // Own the parsed header values before extending `request` with more body
    // bytes. Keeping string slices into `request` here would block that append.
    let headers = String::from_utf8_lossy(&request[..header_end]).into_owned();
    let mut lines = headers.lines();
    let request_line = lines.next().unwrap_or_default();
    let mut parts = request_line.split_whitespace();
    let method = parts.next().unwrap_or_default().to_owned();
    let path = parts.next().unwrap_or_default().split('?').next().unwrap_or_default().to_owned();
    let content_length = headers.lines()
        .find_map(|line| line.strip_prefix("Content-Length:").or_else(|| line.strip_prefix("content-length:")))
        .and_then(|value| value.trim().parse::<usize>().ok())
        .unwrap_or(0);
    if content_length > FRAME_BYTES * 2 { return respond(&mut stream, 413, "frame too large", "text/plain"); }
    while request.len() - header_end < content_length {
        let count = stream.read(&mut chunk)?;
        if count == 0 { break; }
        request.extend_from_slice(&chunk[..count]);
    }
    let body = &request[header_end..];

    match (method.as_str(), path.as_str()) {
        ("GET", "/") => respond(&mut stream, 200, &asset("web/index.html"), "text/html; charset=utf-8"),
        ("GET", "/api/frame") => {
            let pixels = state.lock().unwrap().pixels.clone();
            respond_bytes(&mut stream, 200, &pixels, "application/octet-stream")
        }
        ("GET", "/api/status") => {
            let display = state.lock().unwrap();
            let age_ms = display.last_frame.map(|time| time.elapsed().as_millis()).unwrap_or(0);
            let body = format!("{{\"width\":{WIDTH},\"height\":{HEIGHT},\"frames_received\":{},\"last_frame_age_ms\":{age_ms}}}", display.frames_received);
            respond(&mut stream, 200, &body, "application/json")
        }
        ("POST", "/api/frame") => {
            if body.len() != FRAME_BYTES {
                return respond(&mut stream, 400, &format!("expected exactly {FRAME_BYTES} RGB bytes, received {}", body.len()), "text/plain");
            }
            let mut display = state.lock().unwrap();
            display.pixels.copy_from_slice(body);
            display.frames_received += 1;
            display.last_frame = Some(Instant::now());
            respond(&mut stream, 204, "", "text/plain")
        }
        ("POST", "/api/presentation") => {
            let json = match std::str::from_utf8(body) {
                Ok(json) => json,
                Err(_) => return respond(&mut stream, 400, "presentation payload must be UTF-8 JSON", "text/plain"),
            };
            match presentation::render(json) {
                Ok(pixels) => {
                    let mut display = state.lock().unwrap();
                    display.pixels = pixels;
                    display.frames_received += 1;
                    display.last_frame = Some(Instant::now());
                    respond(&mut stream, 204, "", "text/plain")
                }
                Err(error) => respond(&mut stream, 400, &error, "text/plain"),
            }
        }
        _ => respond(&mut stream, 404, "not found", "text/plain"),
    }
}

fn asset(relative: &str) -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(relative);
    fs::read_to_string(path).expect("web asset must be present")
}

fn respond(stream: &mut TcpStream, status: u16, body: &str, content_type: &str) -> io::Result<()> {
    respond_bytes(stream, status, body.as_bytes(), content_type)
}

fn respond_bytes(stream: &mut TcpStream, status: u16, body: &[u8], content_type: &str) -> io::Result<()> {
    let reason = match status { 200 => "OK", 204 => "No Content", 400 => "Bad Request", 404 => "Not Found", 413 => "Payload Too Large", _ => "Error" };
    write!(stream, "HTTP/1.1 {status} {reason}\r\nContent-Type: {content_type}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", body.len())?;
    stream.write_all(body)
}

fn demo_frame() -> Vec<u8> {
    let mut pixels = vec![0; FRAME_BYTES];
    for y in 0..HEIGHT {
        for x in 0..WIDTH {
            let index = (y * WIDTH + x) * 3;
            pixels[index] = (x * 2) as u8;
            pixels[index + 1] = (y * 4) as u8;
            pixels[index + 2] = 60;
        }
    }
    pixels
}
