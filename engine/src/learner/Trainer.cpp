#include "Trainer.h"
#include "env/Reward.h"
#include "sim/RocketSimBackend.h"
#include "ppo/Gae.h"
#include <torch/torch.h>
#include <omp.h>
#include <chrono>
#include <fstream>
#include <thread>

namespace rls {
namespace {
using Clock = std::chrono::steady_clock;
/// Shared feature trunk with separate categorical policy and value heads.
struct NetworkImpl : torch::nn::Module {
    torch::nn::Sequential trunk;
    torch::nn::Linear policy{nullptr}, value{nullptr};
    NetworkImpl(int input, int hidden) {
        trunk = register_module("trunk", torch::nn::Sequential(
            torch::nn::Linear(input, hidden), torch::nn::LayerNorm(torch::nn::LayerNormOptions({hidden})), torch::nn::ReLU(),
            torch::nn::Linear(hidden, hidden), torch::nn::ReLU()));
        policy = register_module("policy", torch::nn::Linear(hidden, 90));
        value = register_module("value", torch::nn::Linear(hidden, 1));
    }
    std::pair<torch::Tensor, torch::Tensor> Forward(torch::Tensor input) {
        auto features = trunk->forward(input);
        return {policy(features), value(features).squeeze(-1)};
    }
};
TORCH_MODULE(Network);

torch::Device Device(const Json& config) {
    const std::string requested = config["device"];
    if (requested == "cuda" && !torch::cuda::is_available()) throw std::runtime_error("CUDA is unavailable. Select CPU or install a compatible NVIDIA driver.");
    return (requested != "cpu" && torch::cuda::is_available()) ? torch::Device(torch::kCUDA) : torch::Device(torch::kCPU);
}
Json Vector(const Vec3& v) { return Json::array({v.x, v.y, v.z}); }
Json Frame(const GameState& state) {
    Json cars = Json::array();
    for (int i = 0; i < state.carCount; ++i) {
        const auto& c = state.cars[i];
        cars.push_back({{"id", i}, {"team", static_cast<int>(c.team)}, {"pos", Vector(c.pos)},
            {"forward", Vector(c.forward)}, {"up", Vector(c.up)}, {"boost", c.boost}, {"demoed", bool(c.flags & CarFlag::kIsDemoed)}});
    }
    return {{"type", "frame"}, {"tick", state.tick}, {"ball", Vector(state.ball.pos)}, {"cars", cars}, {"score", state.score}};
}
std::filesystem::path Meshes(const Json& config) {
    return config["arena"] == "practice" ? std::filesystem::path("engine/assets/practice_meshes")
        : std::filesystem::path(config["meshPath"].get<std::string>());
}
void Save(Network& network, torch::optim::Adam& optimizer, const Json& config,
          const std::filesystem::path& run, int iteration, int64_t steps, const Emit& emit) {
    auto target = run / "checkpoints" / std::to_string(iteration);
    auto staging = run / "checkpoints" / (std::to_string(iteration) + ".pending");
    if (std::filesystem::exists(target)) return;
    std::filesystem::create_directories(staging);
    {
        torch::save(network, (staging / "model.pt").string());
        torch::serialize::OutputArchive archive;
        optimizer.save(archive);
        archive.save_to((staging / "optimizer.pt").string());
        WriteJson(staging / "config.json", config);
        WriteJson(staging / "metadata.json", {{"formatVersion", 1}, {"iteration", iteration}, {"steps", steps}, {"actionVersion", "discrete90_v1"}});
    }
    std::error_code ec;
    for (int retry = 0; retry < 10; ++retry) {
        std::filesystem::rename(staging, target, ec);
        if (!ec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (ec) throw std::filesystem::filesystem_error("rename failed", staging, target, ec);
    emit({{"type", "checkpoint"}, {"path", target.string()}, {"iteration", iteration}});
}
/// Commands apply at decision boundaries. Pausing never changes rollout contents.
bool Commands(Json& config, const Emit& emit, const Control& control) {
    bool paused = false;
    do {
        Json command = control();
        if (command.is_object()) {
            const std::string type = command.value("type", "");
            if (type == "stop") return false;
            if (type == "pause") { paused = true; emit({{"type", "status"}, {"status", "paused"}}); }
            if (type == "resume") { paused = false; emit({{"type", "status"}, {"status", "running"}}); }
            if (type == "rewards") {
                try {
                    Json candidate = config;
                    for (auto& [key, value] : command.at("values").items()) {
                        if (key != "velocityToBall" && key != "faceBall" && key != "ballToGoal" && key != "saveBoost"
                            && key != "airTime" && key != "touch" && key != "boostPickup" && key != "goal")
                            throw std::invalid_argument("Only reward weights can change during a run");
                        candidate[key] = value;
                    }
                    config = ValidateConfig(candidate);
                    emit({{"type", "config"}, {"config", config}});
                } catch (const std::exception& e) { emit({{"type", "error"}, {"message", e.what()}}); }
            }
        }
        if (paused) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    } while (paused);
    return true;
}
}

void Train(Json config, const std::filesystem::path& run, const std::filesystem::path& checkpoint,
           const Emit& emit, const Control& control) {
    config = ValidateConfig(config);
    const auto device = Device(config);
    torch::manual_seed(config["seed"].get<int>());
    torch::set_num_threads(1);
    omp_set_num_threads(config["threads"].get<int>());
    RocketSimBackend::Initialize(Meshes(config), config["arena"] == "practice");
    const auto spec = SpecFromConfig(config);
    auto obs = MakeObsBuilder(config["observation"].get<std::string>());
    const int width = obs->ObsSize(spec), cars = spec.CarsPerArena(), count = config["arenas"];
    const int agents = count * cars, horizon = config["rolloutSteps"], batch = agents * horizon;
    Network network(width, config["hiddenSize"]);
    network->to(device);
    torch::optim::Adam optimizer(network->parameters(), torch::optim::AdamOptions(config["learningRate"].get<double>()));
    int iteration = 0;
    int64_t steps = 0;
    if (!checkpoint.empty()) {
        const auto saved = ReadJson(checkpoint / "config.json");
        for (const char* key : {"observation", "teamSize", "hiddenSize", "tickSkip", "actionDelayTicks"})
            if (saved[key] != config[key]) throw std::runtime_error(std::string("Checkpoint mismatch: ") + key);
        const auto metadata = ReadJson(checkpoint / "metadata.json");
        if (metadata.value("formatVersion", 0) != 1 || metadata.value("actionVersion", "") != "discrete90_v1")
            throw std::runtime_error("Unsupported checkpoint format");
        torch::load(network, (checkpoint / "model.pt").string(), device);
        torch::serialize::InputArchive archive;
        archive.load_from((checkpoint / "optimizer.pt").string(), device); optimizer.load(archive);
        for (auto& group : optimizer.param_groups())
            static_cast<torch::optim::AdamOptions&>(group.options()).lr(config["learningRate"].get<double>());
        iteration = metadata["iteration"]; steps = metadata["steps"];
    }
    if (std::filesystem::exists(run)) throw std::runtime_error("Run directory already exists; choose a new run name/path");
    std::filesystem::create_directories(run);
    WriteJson(run / "config.json", config);
    std::ofstream metricsFile(run / "metrics.jsonl");
    std::vector<std::unique_ptr<RocketSimBackend>> arenas;
    for (int i = 0; i < count; ++i) arenas.push_back(std::make_unique<RocketSimBackend>(spec, SplitMix64(config["seed"].get<u64>() + i)));
    auto current = torch::empty({agents, width}, torch::kFloat32);
    auto encode = [&] {
        float* data = current.data_ptr<float>();
        #pragma omp parallel for schedule(static)
        for (int a = 0; a < count; ++a)
            obs->BuildBatch(arenas[a]->State(), arenas[a]->State(), arenas[a]->Pads(), spec, {data + a * cars * width, width, cars});
    };
    encode();
    emit({{"type", "started"}, {"run", run.string()}, {"device", device.str()}, {"config", config}, {"obsSize", width}});
    const auto start = Clock::now();
    auto lastFrame = start;
    const int finalIteration = iteration + config["iterations"].get<int>();
    bool running = true;
    while (iteration < finalIteration && running) {
        const auto iterationStart = Clock::now();
        auto observations = torch::empty({horizon, agents, width});
        auto actions = torch::empty({horizon, agents}, torch::kInt64);
        auto logprobs = torch::empty({horizon, agents});
        auto values = torch::empty({horizon, agents});
        auto rewards = torch::empty({horizon, agents});
        auto nextValues = torch::empty({horizon, agents});
        auto continues = torch::ones({horizon, agents});
        int touches = 0, goals = 0, episodes = 0, completed = 0;
        for (int t = 0; t < horizon; ++t) {
            if (!Commands(config, emit, control)) { running = false; break; }
            torch::NoGradGuard noGrad;
            observations[t].copy_(current);
            auto [logits, value] = network->Forward(current.to(device));
            auto logp = torch::log_softmax(logits, -1);
            auto chosen = torch::multinomial(logp.exp(), 1).squeeze(-1);
            actions[t].copy_(chosen.cpu()); values[t].copy_(value.cpu());
            logprobs[t].copy_(logp.gather(1, chosen.unsqueeze(-1)).squeeze(-1).cpu());
            const int64_t* actionData = actions[t].data_ptr<int64_t>();
            float* rewardData = rewards[t].data_ptr<float>();
            float* continueData = continues[t].data_ptr<float>();
            std::vector<uint8_t> reset(count), terminal(count);
            #pragma omp parallel for schedule(static) reduction(+:touches,goals,episodes)
            for (int a = 0; a < count; ++a) {
                arenas[a]->Step({actionData + a * cars, static_cast<size_t>(cars)});
                const auto& state = arenas[a]->State();
                terminal[a] = state.GoalScored();
                const float sinceTouch = state.ball.lastTouchCarId ? state.TimeSinceTouch() : state.episodeTime;
                reset[a] = terminal[a] || state.episodeTime >= config["episodeSeconds"].get<float>() || sinceTouch >= config["noTouchSeconds"].get<float>();
                goals += terminal[a]; episodes += reset[a];
                for (int c = 0; c < cars; ++c) {
                    rewardData[a * cars + c] = Reward(state, c, config);
                    continueData[a * cars + c] = reset[a] ? 0.f : 1.f;
                    touches += bool(state.cars[c].flags & CarFlag::kTouchedBall);
                }
            }
            // Bootstrap the actual transition endpoint BEFORE reset. Time limits
            // bootstrap value, goals do not. Both cut the recursive GAE trace.
            encode();
            nextValues[t].copy_(network->Forward(current.to(device)).second.cpu());
            float* nextData = nextValues[t].data_ptr<float>();
            for (int a = 0; a < count; ++a) {
                if (terminal[a]) for (int c = 0; c < cars; ++c) nextData[a * cars + c] = 0;
            }
            if (Clock::now() - lastFrame > std::chrono::milliseconds(100)) {
                emit(Frame(arenas[0]->State())); lastFrame = Clock::now();
            }
            for (int a = 0; a < count; ++a) if (reset[a]) arenas[a]->Reset();
            encode();
            ++completed; steps += agents;
        }
        if (completed != horizon) break; // Never optimize a partially filled buffer.
        auto advantages = torch::zeros_like(rewards);
        const float gamma = config["gamma"], lambda = config["gaeLambda"];
        const auto size = static_cast<size_t>(batch);
        ComputeGae({rewards.data_ptr<float>(), size}, {values.data_ptr<float>(), size},
            {nextValues.data_ptr<float>(), size}, {continues.data_ptr<float>(), size},
            horizon, agents, gamma, lambda, {advantages.data_ptr<float>(), size});
        auto returns = (advantages + values).reshape({batch}).to(device);
        auto adv = advantages.reshape({batch}).to(device);
        adv = (adv - adv.mean()) / (adv.std(false) + 1e-8);
        auto x = observations.reshape({batch, width}).to(device);
        auto act = actions.reshape({batch}).to(device);
        auto oldLogp = logprobs.reshape({batch}).to(device);
        double policyLoss = 0, valueLoss = 0, entropy = 0, kl = 0, clipped = 0;
        int updates = 0;
        const int minibatch = config["minibatchSize"], epochs = config["epochs"];
        const float clip = config["clipRange"];
        for (int epoch = 0; epoch < epochs; ++epoch) {
            auto order = torch::randperm(batch, torch::TensorOptions().dtype(torch::kInt64).device(device));
            for (int offset = 0; offset < batch; offset += minibatch) {
                auto idx = order.slice(0, offset, std::min(batch, offset + minibatch));
                auto [logits, predicted] = network->Forward(x.index_select(0, idx));
                auto lp = torch::log_softmax(logits, -1);
                auto selected = lp.gather(1, act.index_select(0, idx).unsqueeze(-1)).squeeze(-1);
                auto logRatio = selected - oldLogp.index_select(0, idx);
                auto ratio = logRatio.exp();
                auto a = adv.index_select(0, idx);
                auto pl = -torch::minimum(ratio * a, ratio.clamp(1 - clip, 1 + clip) * a).mean();
                auto vl = (predicted - returns.index_select(0, idx)).square().mean();
                auto ent = -(lp.exp() * lp).sum(-1).mean();
                auto loss = pl + config["valueCoef"].get<float>() * vl - config["entropyCoef"].get<float>() * ent;
                if (!torch::isfinite(loss).item<bool>()) throw std::runtime_error("Non-finite PPO loss; reduce reward magnitudes or learning rate");
                optimizer.zero_grad(); loss.backward();
                torch::nn::utils::clip_grad_norm_(network->parameters(), config["maxGradNorm"].get<double>());
                optimizer.step();
                policyLoss += pl.item<double>(); valueLoss += vl.item<double>(); entropy += ent.item<double>();
                kl += ((ratio - 1) - logRatio).mean().item<double>();
                clipped += (torch::abs(ratio - 1) > clip).to(torch::kFloat32).mean().item<double>();
                ++updates;
            }
        }
        ++iteration;
        const double seconds = std::chrono::duration<double>(Clock::now() - iterationStart).count();
        const auto target = advantages + values;
        const double variance = target.var(false).item<double>();
        const double explained = variance > 1e-12 ? 1 - (target - values).var(false).item<double>() / variance : 0;
        Json metrics{{"type", "metrics"}, {"iteration", iteration}, {"steps", steps}, {"reward", rewards.mean().item<double>()},
            {"policyLoss", policyLoss / updates}, {"valueLoss", valueLoss / updates}, {"entropy", entropy / updates},
            {"kl", kl / updates}, {"clipFraction", clipped / updates}, {"explainedVariance", explained},
            {"stepsPerSecond", batch / seconds}, {"touches", touches}, {"goals", goals}, {"episodes", episodes},
            {"elapsedSeconds", std::chrono::duration<double>(Clock::now() - start).count()}};
        emit(metrics); metricsFile << metrics.dump() << '\n'; metricsFile.flush();
        WriteJson(run / "config.json", config);
        if (iteration % config["checkpointEvery"].get<int>() == 0) Save(network, optimizer, config, run, iteration, steps, emit);
    }
    Save(network, optimizer, config, run, iteration, steps, emit);
    WriteJson(run / "summary.json", {{"status", running ? "completed" : "stopped"}, {"iteration", iteration}, {"steps", steps}});
    emit({{"type", "status"}, {"status", running ? "completed" : "stopped"}});
}

void Play(const std::filesystem::path& checkpoint, const std::filesystem::path& opponent,
          int matches, bool realtime, const Emit& emit, const Control& control) {
    Json config = ValidateConfig(ReadJson(checkpoint / "config.json"));
    torch::set_num_threads(1); torch::manual_seed(config["seed"].get<int>());
    const auto spec = SpecFromConfig(config);
    auto obs = MakeObsBuilder(config["observation"].get<std::string>());
    const int width = obs->ObsSize(spec), cars = spec.CarsPerArena();
    Network blue(width, config["hiddenSize"]), orange(width, config["hiddenSize"]);
    torch::load(blue, (checkpoint / "model.pt").string(), torch::kCPU);
    const auto opponentPath = opponent.empty() ? checkpoint : opponent;
    const auto other = ReadJson(opponentPath / "config.json");
    for (const char* key : {"observation", "teamSize", "hiddenSize"})
        if (other[key] != config[key]) throw std::runtime_error(std::string("Opponent mismatch: ") + key);
    torch::load(orange, (opponentPath / "model.pt").string(), torch::kCPU);
    RocketSimBackend::Initialize(Meshes(config), config["arena"] == "practice");
    RocketSimBackend arena(spec, config["seed"]);
    auto input = torch::empty({cars, width});
    int blueWins = 0, orangeWins = 0, draws = 0;
    torch::NoGradGuard noGrad;
    emit({{"type", "status"}, {"status", "playing"}});
    for (int match = 0; match < matches; ++match) {
        arena.Reset();
        while (true) {
            if (!Commands(config, emit, control)) { emit({{"type", "status"}, {"status", "stopped"}}); return; }
            const auto tickStart = Clock::now();
            obs->BuildBatch(arena.State(), arena.State(), arena.Pads(), spec, {input.data_ptr<float>(), width, cars});
            auto a = torch::multinomial(torch::softmax(blue->Forward(input).first, -1), 1).squeeze(-1);
            auto b = torch::multinomial(torch::softmax(orange->Forward(input).first, -1), 1).squeeze(-1);
            a.slice(0, spec.teamSize, cars).copy_(b.slice(0, spec.teamSize, cars));
            arena.Step({a.data_ptr<int64_t>(), static_cast<size_t>(cars)});
            if (realtime) emit(Frame(arena.State()));
            if (arena.State().GoalScored() || arena.State().episodeTime >= config["episodeSeconds"].get<float>()) {
                if (!arena.State().GoalScored()) ++draws;
                else if (arena.State().GoalScoredBy() == Team::Blue) ++blueWins;
                else ++orangeWins;
                break;
            }
            if (realtime) std::this_thread::sleep_until(tickStart + std::chrono::microseconds(static_cast<int>(spec.SecondsPerStep() * 1e6)));
        }
        emit({{"type", "evaluation"}, {"matches", match + 1}, {"blueWins", blueWins}, {"orangeWins", orangeWins}, {"draws", draws}});
    }
    emit({{"type", "status"}, {"status", "completed"}});
}
void Bench(Json config, const Emit& emit) {
    config = ValidateConfig(config);
    RocketSimBackend::Initialize(Meshes(config), config["arena"] == "practice");
    const auto spec = SpecFromConfig(config);
    RocketSimBackend arena(spec, 42);
    std::vector<int64_t> actions(spec.CarsPerArena(), 0);
    const auto start = Clock::now();
    for (int i = 0; i < 10000; ++i) { arena.Step(actions); if (arena.State().GoalScored()) arena.Reset(); }
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    emit({{"type", "benchmark"}, {"physicsTicksPerSecond", 10000 * spec.tickSkip / seconds}, {"agentStepsPerSecond", 10000 * spec.CarsPerArena() / seconds}});
}
}
