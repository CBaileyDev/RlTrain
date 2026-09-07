#include "sim/PracticeArena.h"
#include "learner/Trainer.h"
#include <exception>
#include <iostream>
#include <string_view>
#include <thread>
#include <mutex>
#include <queue>
#include <memory>
#include <csignal>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void Interrupt(int) { interrupted = 1; }
void Help() {
    std::cout << "RL Studio engine\n\n"
        "rl-engine make-practice-arena [--output <directory>]\n"
        "rl-engine train [--config <json>] [--run <directory>] [--checkpoint <directory>] [--interactive]\n"
        "rl-engine play --checkpoint <directory> [--opponent <directory>] [--matches <count>] [--interactive]\n"
        "rl-engine eval --a <directory> --b <directory> [--matches <count>]\n"
        "rl-engine bench [--config <json>]\n"
        "rl-engine defaults\n\n"
        "Practice geometry is approximate. Training plays inside RocketSim only.\n"
        "Events are JSON lines on stdout; --interactive accepts control JSON on stdin.\n";
}
struct Inbox { std::mutex mutex; std::queue<rls::Json> queue; };
}
int main(int argc, char** argv) {
    std::ostream events(std::cout.rdbuf());
    auto emit = [&events](const rls::Json& event) { events << event.dump() << std::endl; };
    try {
        if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) { Help(); return 0; }
        const std::string command = argv[1];
        std::map<std::string, std::string> options;
        bool interactive = false;
        for (int i = 2; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--interactive") { interactive = true; continue; }
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--")) throw std::invalid_argument("Missing value for " + key);
            if (options.contains(key)) throw std::invalid_argument("Duplicate option " + key);
            options[key] = argv[++i];
        }
        std::vector<std::string> allowed;
        if (command == "make-practice-arena") allowed = {"--output"};
        else if (command == "train") allowed = {"--config", "--run", "--checkpoint"};
        else if (command == "play") allowed = {"--checkpoint", "--opponent", "--matches"};
        else if (command == "eval") allowed = {"--a", "--b", "--matches"};
        else if (command == "bench") allowed = {"--config"};
        else if (command != "defaults") throw std::invalid_argument("Unknown command: " + command);
        for (const auto& [key, value] : options)
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) throw std::invalid_argument("Unknown option: " + key);
        if (command == "defaults") { emit(rls::DefaultConfig()); return 0; }
        if (command == "make-practice-arena") {
            const std::filesystem::path output = options.contains("--output") ? options["--output"] : "engine/assets/practice_meshes";
            if (std::filesystem::is_directory(output / "soccar"))
                for (const auto& entry : std::filesystem::directory_iterator(output / "soccar"))
                    if (entry.path().extension() == ".cmf") throw std::runtime_error("Output already contains collision meshes. Choose an empty directory.");
            std::string error;
            if (!rls::WritePracticeArena(output, error)) throw std::runtime_error(error);
            emit({{"type", "arena"}, {"path", (std::filesystem::absolute(output) / "soccar/practice_arena.cmf").string()}});
            return 0;
        }
        // Simulator diagnostics cannot corrupt the JSON event stream.
        std::cout.rdbuf(std::cerr.rdbuf());
        std::signal(SIGINT, Interrupt);
        auto inbox = std::make_shared<Inbox>();
        if (interactive) std::thread([inbox] {
            std::string line;
            while (std::getline(std::cin, line)) {
                auto value = rls::Json::parse(line, nullptr, false);
                if (!value.is_discarded()) { std::lock_guard lock(inbox->mutex); inbox->queue.push(value); }
            }
            std::lock_guard lock(inbox->mutex); inbox->queue.push({{"type", "stop"}});
        }).detach();
        auto control = [inbox]() -> rls::Json {
            if (interrupted) return {{"type", "stop"}};
            std::lock_guard lock(inbox->mutex);
            if (inbox->queue.empty()) return nullptr;
            auto result = inbox->queue.front(); inbox->queue.pop(); return result;
        };
        auto config = options.contains("--config") ? rls::ValidateConfig(rls::ReadJson(options["--config"])) : rls::DefaultConfig();
        if (command == "train") {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            const auto run = options.contains("--run") ? options["--run"] : "runs/run-" + std::to_string(stamp);
            rls::Train(config, run, options["--checkpoint"], emit, control);
        } else if (command == "bench") rls::Bench(config, emit);
        else {
            const auto checkpoint = options[command == "eval" ? "--a" : "--checkpoint"];
            const auto opponent = options[command == "eval" ? "--b" : "--opponent"];
            if (checkpoint.empty() || (command == "eval" && opponent.empty())) throw std::invalid_argument("Checkpoint paths are required");
            int matches = 20;
            if (options.contains("--matches")) {
                size_t used = 0; matches = std::stoi(options["--matches"], &used);
                if (used != options["--matches"].size()) throw std::invalid_argument("Invalid match count");
            }
            if (matches < 1 || matches > 100000) throw std::invalid_argument("matches must be between 1 and 100000");
            rls::Play(checkpoint, opponent, matches, command == "play", emit, control);
        }
        return 0;
    } catch (const std::invalid_argument& error) { emit({{"type", "error"}, {"message", error.what()}}); return 2; }
      catch (const std::exception& error) { emit({{"type", "error"}, {"message", error.what()}}); return 1; }
}
