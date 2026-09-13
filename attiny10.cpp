// Independent command-line companion for the upstream ATtiny10Programmer
// Arduino sketch: https://github.com/bdpdx/ATtiny10Programmer
//
// The upstream sketch is MIT-licensed and is not included in this repository.
// This file communicates with its documented serial command interface.
// POSIX implementation: Linux, macOS, and other termios-based systems.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <vector>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

static const std::string kPrompt = "Command [d,e,i,m,u,v,?] > ";
static const std::string kUploadPrompt = "Upload hex file content to serial monitor";

class HexError : public std::runtime_error {
public:
    explicit HexError(const std::string& message) : std::runtime_error(message) {}
};

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::vector<unsigned char> decodeHex(const std::string& text, size_t line) {
    if (text.size() % 2) throw HexError("line " + std::to_string(line) + ": malformed record");
    std::vector<unsigned char> result;
    for (size_t i = 0; i < text.size(); i += 2) {
        int high = hexDigit(text[i]), low = hexDigit(text[i + 1]);
        if (high < 0 || low < 0) throw HexError("line " + std::to_string(line) + ": non-hex characters");
        result.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return result;
}

static std::string loadHex(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw HexError("cannot open HEX file: " + path);
    std::string result, line;
    bool eof = false;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (line.empty() || line[0] != ':') throw HexError("line " + std::to_string(lineNumber) + ": expected ':'");
        auto bytes = decodeHex(line.substr(1), lineNumber);
        if (bytes.size() < 5) throw HexError("line " + std::to_string(lineNumber) + ": record is too short");
        if (bytes.size() != static_cast<size_t>(bytes[0]) + 5) throw HexError("line " + std::to_string(lineNumber) + ": byte count does not match record");
        unsigned sum = 0;
        for (unsigned char byte : bytes) sum += byte;
        if ((sum & 0xffu) != 0) throw HexError("line " + std::to_string(lineNumber) + ": invalid checksum");
        if (bytes[3] == 1) {
            if (bytes[0] != 0) throw HexError("line " + std::to_string(lineNumber) + ": invalid EOF record");
            eof = true;
        } else if (eof) {
            throw HexError("line " + std::to_string(lineNumber) + ": data after EOF record");
        }
        result += ":";
        for (size_t i = 1; i < line.size(); ++i) result += static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
        result += "\n";
    }
    if (result.empty()) throw HexError("file contains no Intel HEX records");
    if (!eof) throw HexError("file has no EOF record");
    return result;
}

static speed_t baudRate(int baud) {
    switch (baud) {
        case 9600: return B9600; case 19200: return B19200; case 38400: return B38400;
        case 57600: return B57600; case 115200: return B115200;
        default: throw std::runtime_error("unsupported baud rate: " + std::to_string(baud));
    }
}

class Serial {
public:
    Serial(const std::string& port, int baud, double timeout) : timeout_(timeout) {
        fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (fd_ < 0) throw std::runtime_error("cannot open " + port + ": " + std::strerror(errno));
        termios settings{};
        if (tcgetattr(fd_, &settings) < 0) fail("cannot read serial settings");
        cfmakeraw(&settings);
        cfsetispeed(&settings, baudRate(baud));
        cfsetospeed(&settings, baudRate(baud));
        settings.c_cflag |= CLOCAL | CREAD;
        if (tcsetattr(fd_, TCSANOW, &settings) < 0) fail("cannot configure serial port");
    }
    ~Serial() { if (fd_ >= 0) close(fd_); }
    Serial(const Serial&) = delete;

    std::string readUntil(const std::string& marker, double seconds = -1) {
        if (seconds < 0) seconds = timeout_;
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        std::string data;
        char buffer[256];
        while (data.find(marker) == std::string::npos) {
            auto remaining = std::chrono::duration<double>(end - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) throw std::runtime_error("timed out waiting for " + marker);
            timeval wait{static_cast<time_t>(remaining), static_cast<suseconds_t>((remaining - static_cast<time_t>(remaining)) * 1000000)};
            fd_set readable; FD_ZERO(&readable); FD_SET(fd_, &readable);
            int ready = select(fd_ + 1, &readable, nullptr, nullptr, &wait);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) fail("serial read failed");
            if (ready == 0) continue;
            ssize_t count = read(fd_, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("serial connection closed");
            data.append(buffer, static_cast<size_t>(count));
        }
        return data;
    }
    void write(const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            ssize_t count = ::write(fd_, data.data() + offset, data.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) fail("serial write failed");
            offset += static_cast<size_t>(count);
        }
        tcdrain(fd_);
    }
    std::string command(char command, double seconds = -1) {
        // The Arduino may have printed its prompt before this process opened
        // the port. In that case it is already waiting for a command, so do
        // not wait for a prompt forever before sending one.
        try { readUntil(kPrompt, std::min(timeout_, 2.0)); } catch (const std::runtime_error&) {}
        write(std::string(1, command));
        return readUntil(kPrompt, seconds);
    }
    std::string upload(const std::string& image) {
        try { readUntil(kPrompt, std::min(timeout_, 2.0)); } catch (const std::runtime_error&) {}
        write("u");
        readUntil(kUploadPrompt);
        write(image);
        return readUntil(kPrompt, std::max(timeout_, 60.0));
    }
private:
    int fd_ = -1; double timeout_;
    [[noreturn]] void fail(const std::string& message) const { throw std::runtime_error(message + ": " + std::strerror(errno)); }
};

static std::vector<std::string> candidatePorts() {
    std::vector<std::string> ports;
    std::set<std::string> devices;
    for (const char* directoryName : {"/dev", "/dev/serial/by-id"}) {
        const bool includeAll = std::string(directoryName) == "/dev/serial/by-id";
        DIR* directory = opendir(directoryName);
        if (!directory) continue;
        while (dirent* entry = readdir(directory)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            bool serialName = name.rfind("ttyACM", 0) == 0 || name.rfind("ttyUSB", 0) == 0 ||
                name.rfind("cu.usbmodem", 0) == 0 || name.rfind("cu.usbserial", 0) == 0;
            if (includeAll || serialName) {
                std::string path = std::string(directoryName) + "/" + name;
                char resolved[PATH_MAX];
                std::string device = realpath(path.c_str(), resolved) ? resolved : path;
                if (devices.insert(device).second) ports.push_back(path);
            }
        }
        closedir(directory);
    }
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

static int probe(const std::vector<std::string>& ports, int baud, double timeout) {
    int found = 0;
    std::cerr << "Probing " << ports.size() << " serial port(s) at " << baud << " baud...\n";
    for (const std::string& port : ports) {
        std::cerr << "  " << port << ": opening and waiting for programmer prompt...\n";
        try {
            Serial serial(port, baud, timeout);
            std::string output = serial.command('v');
            if (output.find("Version") != std::string::npos) {
                size_t start = output.rfind('\n', output.size() - 2);
                std::string version = output.substr(start == std::string::npos ? 0 : start + 1);
                std::cout << port << ": ATtiny10Programmer found (" << version << ")\n";
                ++found;
            } else {
                std::cerr << "    not recognized (no Version response)\n";
            }
        } catch (const std::exception& error) {
            std::cerr << "    failed: " << error.what() << "\n";
        }
    }
    if (!found) std::cerr << "No ATtiny10Programmer found.\n";
    return found;
}

static void usage(const char* name) {
    std::cerr << "Usage: " << name << " -p PORT (-u HEX|-e|-i|-d|-m|-v) [--timeout SEC] [-b BAUD]\n"
              << "       " << name << " --probe [--port PORT] [--timeout SEC] [-b BAUD]\n"
              << "  -u, --upload FILE       upload Intel HEX\n  -e, --erase             erase flash\n"
              << "  -i, --identify         identify device\n  -d, --dump             dump memory\n"
              << "  -m, --free-memory      show Arduino free memory\n  -v, --version          show sketch version\n"
              << "  -p, --port PORT        serial port\n  -b, --baud BAUD        baud rate (default 115200)\n"
              << "  -P, --probe            find connected programmer(s)\n"
              << "      --timeout SEC      response timeout (default 10)\n";
}

int main(int argc, char** argv) {
    std::string port, hexFile; int baud = 115200, action = 0; double timeout = 10;
    option options[] = {{"upload", required_argument, nullptr, 'u'}, {"erase", no_argument, nullptr, 'e'},
        {"identify", no_argument, nullptr, 'i'}, {"dump", no_argument, nullptr, 'd'}, {"free-memory", no_argument, nullptr, 'm'},
        {"version", no_argument, nullptr, 'v'}, {"port", required_argument, nullptr, 'p'}, {"baud", required_argument, nullptr, 'b'},
        {"timeout", required_argument, nullptr, 't'}, {"probe", no_argument, nullptr, 'P'},
        {"help", no_argument, nullptr, 'h'}, {nullptr, 0, nullptr, 0}};
    int optionIndex;
    while (true) {
        int c = getopt_long(argc, argv, "u:eidmvp:b:t:Ph", options, &optionIndex);
        if (c == -1) break;
        if (c == 'h') { usage(argv[0]); return 0; }
        if (c == 'u') { hexFile = optarg; action = c; } else if (c == 'p') port = optarg; else if (c == 'b') baud = std::stoi(optarg);
        else if (c == 't') timeout = std::stod(optarg); else if (c == 'e' || c == 'i' || c == 'd' || c == 'm' || c == 'v' || c == 'P') { if (action) { usage(argv[0]); return 2; } action = c; }
        else { usage(argv[0]); return 2; }
    }
    if (action == 'P') {
        std::vector<std::string> ports = port.empty() ? candidatePorts() : std::vector<std::string>{port};
        if (ports.empty()) { std::cerr << "no candidate serial ports found\n"; return 1; }
        return probe(ports, baud, timeout) ? 0 : 1;
    }
    if (port.empty() || action == 0 || (action == 'u' && hexFile.empty())) { usage(argv[0]); return 2; }
    try {
        std::string image = action == 'u' ? loadHex(hexFile) : "";
        Serial serial(port, baud, timeout);
        std::string output = action == 'u' ? serial.upload(image) : serial.command(static_cast<char>(action), action == 'd' ? std::max(timeout, 60.0) : -1);
        std::cout << output << (output.empty() || output.back() == '\n' ? "" : "\n");
        return (output.find("Done") != std::string::npos || (action != 'u' && output.find("failed") == std::string::npos && output.find("Unable to identify") == std::string::npos)) ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << "error: " << error.what() << "\n"; return 2; }
}
