from pathlib import Path

try:
    Import("env")
    PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
except NameError:
    PROJECT_DIR = Path(__file__).resolve().parent.parent

target = PROJECT_DIR / "freeink-sdk" / "libs" / "network" / "SecureNet" / "src" / "SecureClient.cpp"

if target.is_file():
    text = target.read_text(encoding="utf-8")
    if "getaddrinfo" not in text:
        # Add headers if not present
        if "#include <netdb.h>" not in text:
            text = text.replace('#include <Network.h>', '#include <Network.h>\n#include <netdb.h>\n#include <sys/socket.h>')
        
        # Replace hostResolves
        old_pattern = """bool hostResolves(const char* host) {
  IPAddress ip;
  if (Network.hostByName(host, ip) == 1) return true;
  if (Serial) Serial.printf("[SecureNet] DNS lookup failed: %s\\n", host);
  return false;
}"""
        new_pattern = """bool hostResolves(const char* host) {
  if (!host || host[0] == '\\0') return false;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  int err = getaddrinfo(host, nullptr, &hints, &res);
  if (err == 0 && res != nullptr) {
    freeaddrinfo(res);
    return true;
  }
  if (Serial) Serial.printf("[SecureNet] DNS lookup failed (%d): %s\\n", err, host);
  return false;
}"""
        if old_pattern in text:
            text = text.replace(old_pattern, new_pattern)
            target.write_text(text, encoding="utf-8")
            print(f"Patched SecureClient.cpp: {target.relative_to(PROJECT_DIR)}")
