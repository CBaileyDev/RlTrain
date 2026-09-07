# Assistant provider and model choice

Verified September 7, 2026. Default: **`claude-sonnet-4-6` through NeoToken V2**.

The requested pasted model list was not included in the conversation payload. Selection therefore used the authenticated `/v1/models` catalog from the provider already configured in the user's OpenCode file. No API key was copied into the project, browser, logs, or this document.

## Decision

The job is occasional, conservative PPO diagnosis from a small numeric context, with reliable structured settings changes. Sonnet 4.6 has an unambiguous model ID across the live catalog and documentation, an economical advertised price, and strong evidence for following technical instructions. Anthropic reports a roughly 70% preference over Sonnet 4.5 in early Claude Code testing and improved context reading and instruction following. This is supporting evidence, not an RL tuning benchmark. [Anthropic announcement](https://www.anthropic.com/news/claude-sonnet-4-6)

A live, minimal chat-completions smoke request returned valid JSON from `claude-sonnet-4-6`: 85 prompt tokens, 22 completion tokens, 107 total (63 prompt tokens reported cached). This verifies routing and response format, not training quality. No training settings were applied by that smoke request.

## Cost and alternatives

Amounts below are the provider's advertised USD per million input/output tokens. Output rates are calculated from its stated 4× input rule. These are estimates, not independently verified account charges. V2 advertises credit multipliers separately; do not apply another multiplier to these figures. The live model catalog did not return pricing. [NeoToken V2 pricing](https://v2.neokens.com/pricing)

| Model | Input / output | Evidence and suitability |
|---|---:|---|
| **Claude Sonnet 4.6** | **$0.30 / $1.20** | Selected; available and verified end to end. Good fit for interpretable technical interventions. |
| Claude Haiku 4.5 | $0.08 / $0.32 | Available; budget alternative. Anthropic reports 73.3% SWE-bench Verified, under its stated evaluation setup. [Haiku announcement](https://www.anthropic.com/news/claude-haiku-4-5) |
| Gemini 3.1 Pro | $0.18 / $0.72 | Available; credible alternative for mathematical diagnosis. Google reports 77.1% ARC-AGI-2; this measures novel logic, not PPO tuning. [Model card](https://deepmind.google/models/model-cards/gemini-3-1-pro/) |
| GPT-5.4 mini | Not listed on V2 pricing page | Available. Official benchmarks include 54.4% SWE-Bench Pro and 88.0% GPQA Diamond; direct OpenAI pricing is $0.75 / $4.50, which is not a NeoToken quote. [OpenAI announcement](https://openai.com/index/introducing-gpt-5-4-mini-and-nano/) |
| GPT-5.6 Terra | $0.15 / $0.60 in docs | Available, but NeoToken labels that ID “GPT-5.3 Codex.” Cannot confidently map advertised model identity to benchmarks. [NeoToken docs](https://v2.neokens.com/docs) |
| Gemini 3 Flash | $0.01 / $0.04 | Advertised in docs but absent from the live catalog. Google reports 78% SWE-bench Verified for this exact older model; that score cannot be assigned to the catalog's 3.5–3.8 Flash IDs. [Google announcement](https://blog.google/products-and-platforms/products/gemini/gemini-3-flash/) |
| Claude Opus 4.7 | $1.50 / $6.00 | Available; 5× Sonnet's advertised token price. No evidence gathered here justifies that premium for this small bounded task. |

For a 4,000-input / 1,000-output-token tuning call, the advertised Sonnet estimate is **$0.0024**, or **$0.24 per 100 calls**, excluding caching and any account-specific accounting. Haiku would be about $0.00064 and Gemini 3.1 Pro $0.00144 with the same token counts. Actual requests vary. The application exposes returned token usage and makes no claim of measured currency spend.

Different benchmark harnesses, thinking budgets and tasks make these scores unsuitable for a single numerical ranking. In particular, SWE-Bench Pro and SWE-bench Verified are different tests. No model has been shown here to improve this bot's win rate. A future useful evaluation would replay fixed healthy, unstable and reward-hacking telemetry cases, assess valid JSON and appropriate abstention, then compare separate training seeds against an unchanged baseline.

## Runtime behavior

- The Rust backend reads `~/.config/opencode/opencode.json` on demand. It accepts the configured NeoToken V2 HTTPS endpoint and supports literal API keys or `{env:VARIABLE}` keys. Redirects are disabled so credentials cannot be redirected to another host.
- Settings discovers the current catalog without spending inference tokens. If discovery fails, the known default remains available and catalog status reports the fallback.
- The backend automatically builds a prompt from current configuration, metrics, schema descriptions, and an optional note. Run names and mesh paths are excluded from the outgoing configuration. Telemetry and the note are sent to NeoToken when tuning is requested.
- Responses can change at most five explicit optimizer/reward settings. Every value must pass the existing schema and a conservative per-call delta limit. Architecture, seed, action timing, hardware, and training duration cannot be changed by the assistant.
- Invalid JSON, unsupported keys, oversized changes, authentication errors, and timeouts return an error before any proposal is handed to the UI. No raw upstream response bodies or credential-bearing transport errors are shown.
- The assistant is instructed to abstain when evidence is insufficient, avoid inferring MMR from reward, and distinguish live reward adjustments from next-session optimizer settings.

Tests cover valid/no-op proposals, malformed and unsafe settings, exact prompt bounds, and credential destination restrictions. `cargo test --lib` passes. An explicitly ignored paid integration test calls the production `ask_assistant` function with real recorded configuration and metrics from `runs/integration-cpu-1`. Run it deliberately with `cargo test --lib live_tuning_with_recorded_metrics -- --ignored --nocapture` after creating the engine integration fixtures.

The production-flow test initially caught an out-of-limit model proposal, which the backend correctly rejected. The prompt now supplies exact per-request numeric ranges, including rounded integer limits, matching backend enforcement. The subsequent live test passed with four validated changes and 1,877 total tokens (1,545 input, 332 output). Tests only return proposals; they never apply them to a trainer. This verifies the complete request, parsing and validation path. Longer-running learning improvements still require controlled training experiments.
