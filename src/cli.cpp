#include <naval_sdk/cli.hpp>
#include "transport.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace naval_sdk {
namespace {
std::string env(const char* name) { auto p = std::getenv(name); return p ? p : ""; }
bool blank(const std::string& s) { return s.find_first_not_of(" \r\n\t") == std::string::npos; }
std::map<std::string,std::string> participant_environment(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::invalid_argument("could not read participant --env-file");
    std::string content(16385, '\0');
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(file.gcount()));
    if (file.bad()) throw std::invalid_argument("could not read participant --env-file");
    if (content.size() > 16384) throw std::invalid_argument("participant --env-file must be at most 16 KiB");
    try { (void)Json(content).dump(); } catch (const Json::exception&) { throw std::invalid_argument("participant --env-file must use UTF-8"); }
    std::map<std::string,std::string> values;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines,line)) {
        std::vector<std::string> words;
        std::string word;
        char quote = 0;
        bool started = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (!quote && c == '#') break;
            if (c == '\\' && quote != '\'') {
                if (++i == line.size()) throw std::invalid_argument("invalid escaping in participant --env-file");
                char next = line[i];
                if (quote == '"' && next != '"' && next != '\\') word += '\\';
                word += next; started = true; continue;
            }
            if (quote) { if (c == quote) quote = 0; else word += c; }
            else if (c == '\'' || c == '"') { quote = c; started = true; }
            else if (std::isspace(static_cast<unsigned char>(c))) {
                if (started) { words.push_back(word); word.clear(); started = false; }
            } else { word += c; started = true; }
        }
        if (quote) throw std::invalid_argument("invalid quoting in participant --env-file");
        if (started) words.push_back(word);
        if (words.empty()) continue;
        if (words.front() == "export") words.erase(words.begin());
        if (words.size() != 1 || words[0].find('=') == std::string::npos)
            throw std::invalid_argument("expected KEY=value in participant --env-file");
        auto equal = words[0].find('=');
        auto key = words[0].substr(0,equal), value = words[0].substr(equal+1);
        if (key != "BATTLE_SERVER_URL" && key != "BATTLE_BOT_TOKEN") throw std::invalid_argument("unknown setting in participant --env-file");
        if (values.count(key)) throw std::invalid_argument("duplicate setting in participant --env-file");
        values[key] = value;
    }
    if (blank(values["BATTLE_SERVER_URL"]) || blank(values["BATTLE_BOT_TOKEN"]))
        throw std::invalid_argument("participant --env-file must define BATTLE_SERVER_URL and BATTLE_BOT_TOKEN");
    return values;
}
int count(const std::string& s) {
    if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("expected a nonnegative integer option");
    return std::stoi(s);
}
}
RunOptions connection_options(const ConnectionArguments& args, const std::string& default_url) {
    if (args.url && (args.host || args.port)) throw std::invalid_argument("use --url or --host/--port, not both");
    auto settings = args.env_file ? participant_environment(*args.env_file)
        : std::map<std::string,std::string>{{"BATTLE_SERVER_URL",env("BATTLE_SERVER_URL")}, {"BATTLE_BOT_TOKEN",env("BATTLE_BOT_TOKEN")}};
    std::string url;
    if (args.host || args.port) {
        std::string host = args.host.value_or("localhost");
        if (host.find(':') != std::string::npos && !host.empty() && host.front() != '[') host = '[' + host + ']';
        url = "ws://" + host + ':' + std::to_string(args.port.value_or(7878)) + "/bot";
    } else url = args.url.value_or(settings["BATTLE_SERVER_URL"].empty() ? default_url : settings["BATTLE_SERVER_URL"]);
    auto endpoint = detail::parse_url(url);
    if (endpoint.target != "/bot") throw std::invalid_argument("bot endpoint must end in /bot");
    if (endpoint.tls && blank(settings["BATTLE_BOT_TOKEN"]))
        throw std::invalid_argument("a participant credential is required: use --env-file or BATTLE_BOT_TOKEN");
    RunOptions out; out.url = url; out.token = settings["BATTLE_BOT_TOKEN"];
    return out;
}
CliOptions parse_command_line(int argc, char* argv[], const std::string& default_name) {
    ConnectionArguments args;
    CliOptions out;
    std::string name = default_name;
    std::optional<std::string> ca_file;
    int retries = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { out.help = true; return out; }
        if (arg != "--url" && arg != "--host" && arg != "--port" && arg != "--env-file"
            && arg != "--name" && arg != "--record" && arg != "--matches" && arg != "--ca-file" && arg != "--reconnect-attempts")
            throw std::invalid_argument("unknown command-line option; use --help");
        if (++i >= argc) throw std::invalid_argument("missing command-line option value");
        std::string value = argv[i];
        if (arg == "--url") args.url = value;
        else if (arg == "--host") args.host = value;
        else if (arg == "--port") args.port = count(value);
        else if (arg == "--env-file") args.env_file = value;
        else if (arg == "--name") name = value;
        else if (arg == "--record") out.record = value;
        else if (arg == "--matches") out.matches = count(value);
        else if (arg == "--ca-file") ca_file = value;
        else retries = count(value);
    }
    out.connection = connection_options(args);
    out.connection.name = name; out.connection.ca_file = ca_file; out.connection.reconnect_attempts = retries;
    return out;
}
std::string connection_help() {
    return "Options:\n  --url WS_URL | --host HOST --port PORT\n  --env-file PATH     Participant URL and token (never executed)\n"
        "  --name NAME         Bot name\n  --matches N         Disconnect after N matches (0 = unlimited)\n"
        "  --record PATH       Create a new bot-view JSONL recording\n  --ca-file PATH      Additional trusted TLS CA certificates\n"
        "  --reconnect-attempts N  Retry outside an active match\n  --help              Show help\n";
}
}
