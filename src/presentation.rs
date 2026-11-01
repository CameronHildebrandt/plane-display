//! Compatibility layer for the normalized `flight-ticket` presentation model.
//! It is intentionally dependency-free so the same small contract can be used
//! by an ESP32 adapter later.

const W: usize = 128;
const H: usize = 64;
type Rgb = (u8, u8, u8);

pub fn render(json: &str) -> Result<Vec<u8>, String> {
    if string(json, "type").as_deref() != Some("flight-ticket") {
        return Err("expected a presentation with type: flight-ticket".into());
    }
    let version = number(json, "version").unwrap_or(1.0);
    if version as u32 != 1 { return Err("only flight-ticket version 1 is supported".into()); }

    let flight_number = string(json, "number").unwrap_or_else(|| "FLIGHT".into());
    let airline = string(json, "airline").unwrap_or_else(|| "AIRLINE".into());
    let origin = string(json, "origin").unwrap_or_else(|| "---".into());
    let destination = string(json, "destination").unwrap_or_else(|| "---".into());
    let aircraft = string(json, "aircraftType").unwrap_or_else(|| "----".into());
    let altitude = number(json, "altitudeFt").unwrap_or(0.0).max(0.0) as u32;
    let speed = number(json, "groundSpeedKt").unwrap_or(0.0).max(0.0) as u32;
    let distance = number(json, "distanceFromViewerKm").unwrap_or(0.0).max(0.0);
    let progress = number(json, "progress").unwrap_or(0.0).clamp(0.0, 1.0);
    let approaching = boolean(json, "approaching").unwrap_or(false);
    let blue = if string(json, "theme").as_deref() == Some("westjet") { (22, 91, 160) } else { (45, 72, 110) };

    let mut image = Image::new();
    image.rect(0, 0, 128, 64, (0, 0, 0));
    image.rect(0, 0, 128, 19, blue);
    image.rect(4, 4, 16, 11, (240, 248, 255));
    plane(&mut image, 6, 6, (20, 110, 175));
    image.text(24, 5, &airline, 1, (255, 255, 255), 16);
    image.text(24, 13, &flight_number, 1, (198, 231, 255), 16);
    plane(&mut image, 103, 5, (255, 255, 255));
    image.text(124, 14, &aircraft, 1, (219, 234, 254), 7);

    let route_known = origin != "---" && destination != "---";
    if route_known {
        image.text(5, 23, "FROM", 1, (145, 154, 170), 5);
        image.text(123, 23, "TO", 1, (145, 154, 170), 3);
        image.text(4, 31, &origin, 2, (255, 255, 255), 9);
        image.text(123, 31, &destination, 2, (255, 255, 255), 9);
        image.text(5, 43, &format!("{}FT", altitude), 1, (85, 205, 255), 12);
        image.text(73, 43, &format!("{}KT", speed), 1, (85, 205, 255), 11);
        image.rect(4, 53, 120, 5, (42, 49, 60));
        image.rect(4, 53, (120.0 * progress) as usize, 5, (63, 191, 242));
        let x = 4 + (120.0 * progress) as i32;
        image.circle(x, 55, 3, (255, 255, 255));
        image.text(4, 60, "DEPARTED", 1, (125, 137, 154), 8);
        image.text(123, 60, "ARRIVAL", 1, (125, 137, 154), 7);
    } else {
        image.text(4, 24, "AIRCRAFT", 1, (145, 154, 170), 8);
        image.text(4, 32, &aircraft, 2, (255, 255, 255), 9);
        image.text(4, 46, &format!("{} FT  {} KT", altitude, speed), 1, (85, 205, 255), 20);
    }
    let proximity = if distance < 1.0 { "OVERHEAD" } else if approaching { "APPROACH" } else { "DEPARTING" };
    image.text(4, 48, &format!("{distance:.0}KM {proximity}"), 1, (184, 228, 137), 20);
    Ok(image.data)
}

fn string(json: &str, key: &str) -> Option<String> {
    let marker = format!("\"{key}\"");
    let rest = json.get(json.find(&marker)? + marker.len()..)?;
    let rest = rest.trim_start().strip_prefix(':')?.trim_start();
    let rest = rest.strip_prefix('"')?;
    let end = rest.find('"')?;
    Some(rest[..end].to_string())
}
fn number(json: &str, key: &str) -> Option<f32> {
    let marker = format!("\"{key}\"");
    let rest = json.get(json.find(&marker)? + marker.len()..)?.trim_start().strip_prefix(':')?.trim_start();
    let end = rest.find(|c: char| !(c.is_ascii_digit() || c == '.' || c == '-')).unwrap_or(rest.len());
    rest[..end].parse().ok()
}
fn boolean(json: &str, key: &str) -> Option<bool> {
    let marker = format!("\"{key}\"");
    let rest = json.get(json.find(&marker)? + marker.len()..)?.trim_start().strip_prefix(':')?.trim_start();
    if rest.starts_with("true") { Some(true) } else if rest.starts_with("false") { Some(false) } else { None }
}

struct Image { data: Vec<u8> }
impl Image {
    fn new() -> Self { Self { data: vec![0; W * H * 3] } }
    fn pixel(&mut self, x: i32, y: i32, color: Rgb) {
        if x < 0 || y < 0 || x >= W as i32 || y >= H as i32 { return; }
        let offset = (y as usize * W + x as usize) * 3;
        self.data[offset..offset + 3].copy_from_slice(&[color.0, color.1, color.2]);
    }
    fn rect(&mut self, x: i32, y: i32, width: usize, height: usize, color: Rgb) {
        for yy in y..y + height as i32 { for xx in x..x + width as i32 { self.pixel(xx, yy, color); } }
    }
    fn circle(&mut self, x: i32, y: i32, radius: i32, color: Rgb) {
        for yy in -radius..=radius { for xx in -radius..=radius { if xx * xx + yy * yy <= radius * radius { self.pixel(x + xx, y + yy, color); } } }
    }
    fn text(&mut self, x: i32, y: i32, text: &str, scale: i32, color: Rgb, max_chars: usize) {
        for (i, character) in text.to_ascii_uppercase().chars().take(max_chars).enumerate() {
            for (row, bits) in glyph(character).iter().enumerate() {
                for col in 0..5 { if (*bits & (1u8 << (4 - col))) != 0 { self.rect(x + (i as i32 * 6 + col) * scale, y + row as i32 * scale, scale as usize, scale as usize, color); } }
            }
        }
    }
}

fn plane(image: &mut Image, x: i32, y: i32, color: Rgb) {
    for (dx, dy) in [(0,3),(1,3),(2,3),(3,2),(4,1),(5,0),(5,3),(6,3),(7,4),(8,5),(6,4),(5,4),(4,6),(3,6),(3,4),(1,4)] { image.pixel(x + dx, y + dy, color); }
}

fn glyph(c: char) -> [u8; 7] {
    match c {
        'A'=>[14,17,17,31,17,17,17], 'B'=>[30,17,17,30,17,17,30], 'C'=>[14,17,16,16,16,17,14], 'D'=>[30,17,17,17,17,17,30], 'E'=>[31,16,16,30,16,16,31], 'F'=>[31,16,16,30,16,16,16], 'G'=>[14,17,16,23,17,17,14], 'H'=>[17,17,17,31,17,17,17], 'I'=>[31,4,4,4,4,4,31], 'J'=>[7,2,2,2,2,18,12], 'K'=>[17,18,20,24,20,18,17], 'L'=>[16,16,16,16,16,16,31], 'M'=>[17,27,21,21,17,17,17], 'N'=>[17,25,21,19,17,17,17], 'O'=>[14,17,17,17,17,17,14], 'P'=>[30,17,17,30,16,16,16], 'Q'=>[14,17,17,17,21,18,13], 'R'=>[30,17,17,30,20,18,17], 'S'=>[15,16,16,14,1,1,30], 'T'=>[31,4,4,4,4,4,4], 'U'=>[17,17,17,17,17,17,14], 'V'=>[17,17,17,17,17,10,4], 'W'=>[17,17,17,21,21,21,10], 'X'=>[17,17,10,4,10,17,17], 'Y'=>[17,17,10,4,4,4,4], 'Z'=>[31,1,2,4,8,16,31],
        '0'=>[14,17,19,21,25,17,14], '1'=>[4,12,4,4,4,4,14], '2'=>[14,17,1,2,4,8,31], '3'=>[30,1,1,14,1,1,30], '4'=>[2,6,10,18,31,2,2], '5'=>[31,16,16,30,1,1,30], '6'=>[14,16,16,30,17,17,14], '7'=>[31,1,2,4,8,8,8], '8'=>[14,17,17,14,17,17,14], '9'=>[14,17,17,15,1,1,14],
        '-'=>[0,0,0,31,0,0,0], '.'=>[0,0,0,0,0,6,6], ':'=>[0,6,6,0,6,6,0], ' '=>[0;7], _=>[31,17,21,21,21,17,31],
    }
}

#[cfg(test)]
mod tests {
    use super::render;

    #[test]
    fn ticket_renderer_produces_a_full_panel() {
        let panel = render(r#"{"type":"flight-ticket","version":1,"flight":{"number":"WS123","origin":"YVR","destination":"YEG"},"telemetry":{"altitudeFt":10000},"journey":{"progress":0.5}}"#).unwrap();
        assert_eq!(panel.len(), 128 * 64 * 3);
        assert!(panel.iter().any(|value| *value != 0));
    }

    #[test]
    fn unknown_layout_is_rejected() {
        assert!(render(r#"{"type":"radar","version":1}"#).is_err());
    }
}
