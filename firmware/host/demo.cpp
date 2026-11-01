// Mac-side ESP emulator. It uses the same render_flight_ticket() function as
// the future Arduino project, then ships the RGB framebuffer to the simulator.
#include "flight_ticket.hpp"

#include <array>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

class FrameBuffer final : public flight_display::Canvas {
 public:
  std::array<uint8_t, 128 * 64 * 3> pixels{};
  void pixel(int x, int y, flight_display::Color color) override {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    const size_t offset = static_cast<size_t>(y * 128 + x) * 3;
    pixels[offset] = color.r; pixels[offset + 1] = color.g; pixels[offset + 2] = color.b;
  }
  void fill_rect(int x, int y, int width, int height, flight_display::Color color) override {
    for (int yy = y; yy < y + height; ++yy) for (int xx = x; xx < x + width; ++xx) pixel(xx, yy, color);
  }
};

int main() {
  FrameBuffer frame;
  flight_display::FlightTicket ticket;  // Replace this fixture with backend JSON parsing later.
  flight_display::render_flight_ticket(frame, ticket);

  const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(7878); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (socket_fd < 0 || connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    std::cerr << "Could not connect to http://127.0.0.1:7878. Start the Rust simulator first.\n"; return 1;
  }
  const std::string header = "POST /api/frame HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/octet-stream\r\nContent-Length: 24576\r\nConnection: close\r\n\r\n";
  send(socket_fd, header.data(), header.size(), 0);
  send(socket_fd, frame.pixels.data(), frame.pixels.size(), 0);
  close(socket_fd);
  std::cout << "Sent a hardcoded flight-ticket frame to the simulator.\n";
}
