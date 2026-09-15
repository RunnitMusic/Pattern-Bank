#include "Core.h"

#include <chrono>
#include <iomanip>
#include <iostream>

int main()
{
    using namespace stepshaper;
    Model model;
    for (int i = 1; i < maxLanes; ++i) model.addLane();
    Engine engine (model);
    TickContext context;
    context.playing = true;
    context.samplesPerTick = 64;
    constexpr int iterations = 1'000'000;
    volatile float sink = 0.0f;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        context.songBeat += static_cast<double> (context.samplesPerTick) / context.sampleRate * context.tempoBpm / 60.0;
        engine.tick (context);
        sink = sink + engine.output (i % maxLanes);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::cout << std::fixed << std::setprecision (3)
              << "Eight-LFO controller tick: " << elapsed * 1.0e9 / iterations << " ns/tick, "
              << iterations / elapsed << " ticks/second (sink " << sink << ")\n";
}
