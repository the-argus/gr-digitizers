#include <boost/ut.hpp>
#include <fair/picoscope/Picoscope.hpp>
#include <fair/picoscope/Picoscope4000a.hpp>
#include <fair/picoscope/Picoscope5000a.hpp>
#include <format>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/PerformanceMonitor.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>
#include <string>

namespace {
using namespace std::chrono_literals;
template<typename Scheduler>
auto createWatchdog(Scheduler& sched, std::chrono::seconds timeOut = 2s, std::chrono::milliseconds pollingPeriod = 40ms) {
    auto externalInterventionNeeded = std::make_shared<std::atomic_bool>(false);

    std::thread watchdogThread([&sched, externalInterventionNeeded, timeOut, pollingPeriod]() {
        auto timeout = std::chrono::steady_clock::now() + timeOut;
        while (std::chrono::steady_clock::now() < timeout) {
            if (sched.state() == gr::lifecycle::State::STOPPED) {
                return;
            }
            std::this_thread::sleep_for(pollingPeriod);
        }
        std::println("watchdog kicked in");
        externalInterventionNeeded->store(true, std::memory_order_relaxed);
        sched.requestStop();
        std::println("requested scheduler to stop");
    });

    return std::make_pair(std::move(watchdogThread), externalInterventionNeeded);
}
} // namespace

void registerDefaultThreadPool() {
    using namespace gr::thread_pool;
    auto threadPool = std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, 1U, 1U), "CPU");
    threadPool->setThreadBounds(2U, 2U);
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::move(threadPool));
}

int main(int argc, char* argv[]) {
    using namespace boost::ut;
    using namespace gr;
    using namespace gr::testing;
    using namespace fair::picoscope;

    int runTime = 60; // in seconds

    if (argc >= 2) {
        runTime = std::atoi(argv[1]);
    }

    using SampleType = float;

    // Replace with your connected Picoscope device
    using PicoscopeT = Picoscope5000a;

    constexpr float      kSampleRate      = 1'000'000.f;
    constexpr gr::Size_t evaluatePerfRate = 1'000'000;
    constexpr float      publishRate      = 1.f;

    registerDefaultThreadPool();

    Graph graph;

    gr::Tensor<std::pmr::string> channelIds    = {"A", "B", "C", "D"};
    gr::Tensor<float>            channelRanges = {5.f, 5.f, 5.f, 5.f};
    if constexpr (PicoscopeT::N_ANALOG_CHANNELS == 8) {
        std::ranges::copy(std::array{"E", "F", "G", "H"}, std::back_inserter(channelIds));
        std::ranges::copy(std::array{5.f, 5.f, 5.f, 5.f}, std::back_inserter(channelRanges));
    }

    auto& ps = graph.emplaceBlock<Picoscope<SampleType, PicoscopeT>>({{
        {"sample_rate", gr::pmt::Value{kSampleRate}},
        {"auto_arm", gr::pmt::Value{true}}, //
        {"channel_ids", gr::pmt::Value{channelIds}},
        {"channel_ranges", gr::pmt::Value{channelRanges}},
    }});

    auto& perfMonitorA = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf A"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});
    auto& perfMonitorB = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf B"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});
    auto& perfMonitorC = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf C"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});
    auto& perfMonitorD = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf D"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});

    expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 0>(ps).template to<"in">(perfMonitorA)));
    expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 1>(ps).template to<"in">(perfMonitorB)));
    expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 2>(ps).template to<"in">(perfMonitorC)));
    expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 3>(ps).template to<"in">(perfMonitorD)));

    auto& sinkDigital = graph.emplaceBlock<testing::TagSink<uint16_t, testing::ProcessFunction::USE_PROCESS_BULK>>({{{"log_samples", gr::pmt::Value{false}}, {"log_tags", gr::pmt::Value{false}}}});
    expect(eq(ConnectionResult::SUCCESS, graph.connect<"digitalOut">(ps).template to<"in">(sinkDigital)));

    if constexpr (PicoscopeT::N_ANALOG_CHANNELS == 8) {
        auto& perfMonitorE = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf E"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});
        auto& perfMonitorF = graph.emplaceBlock<PerformanceMonitor<SampleType>>({{"name", gr::pmt::Value{"Perf F"}}, {"evaluate_perf_rate", gr::pmt::Value{evaluatePerfRate}}, {"publish_rate", gr::pmt::Value{publishRate}}});
        auto& sinkG        = graph.emplaceBlock<testing::TagSink<SampleType, testing::ProcessFunction::USE_PROCESS_BULK>>({{{"log_samples", gr::pmt::Value{false}}, {"log_tags", gr::pmt::Value{false}}}});
        auto& sinkH        = graph.emplaceBlock<testing::TagSink<SampleType, testing::ProcessFunction::USE_PROCESS_BULK>>({{{"log_samples", gr::pmt::Value{false}}, {"log_tags", gr::pmt::Value{false}}}});

        expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 4>(ps).template to<"in">(perfMonitorE)));
        expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 5>(ps).template to<"in">(perfMonitorF)));
        expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 6>(ps).template to<"in">(sinkG)));
        expect(eq(ConnectionResult::SUCCESS, graph.connect<"out", 7>(ps).template to<"in">(sinkH)));
    }

    auto sched                                        = scheduler::Simple{};
    std::ignore                                       = sched.exchange(std::move(graph));
    auto [watchdogThread, externalInterventionNeeded] = createWatchdog(sched, runTime > 0 ? std::chrono::seconds(runTime) : 20s);
    expect(sched.runAndWait().has_value());

    if (watchdogThread.joinable()) {
        watchdogThread.join();
    }
}
