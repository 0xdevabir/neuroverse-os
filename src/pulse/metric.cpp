// src/pulse/metric.cpp
//
// Telemetry — host scaffold. Provides the Registry factory + scrape()
// in Prometheus-style text format. The real scrape endpoint lands
// with the kernel network stack in Phase 1.

#include "neuro/pulse/metric.hpp"

#include <algorithm>
#include <sstream>

namespace neuro::pulse {

Counter& Registry::counter(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = counters_.find(name);
    if (it != counters_.end()) return *it->second;
    auto c = std::make_unique<Counter>(name, help);
    Counter& ref = *c;
    counters_.emplace(name, std::move(c));
    return ref;
}

Gauge& Registry::gauge(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = gauges_.find(name);
    if (it != gauges_.end()) return *it->second;
    auto g2 = std::make_unique<Gauge>(name, help);
    Gauge& ref = *g2;
    gauges_.emplace(name, std::move(g2));
    return ref;
}

Histogram& Registry::histogram(const std::string& name, const std::string& help,
                               std::vector<double> bounds) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = histograms_.find(name);
    if (it != histograms_.end()) return *it->second;
    auto h = std::make_unique<Histogram>(name, help, std::move(bounds));
    Histogram& ref = *h;
    histograms_.emplace(name, std::move(h));
    return ref;
}

std::string Registry::scrape() const {
    std::lock_guard<std::mutex> g(mu_);
    std::ostringstream s;

    // Sort metric names for stable output.
    std::vector<std::string> cnames; cnames.reserve(counters_.size());
    for (auto& kv : counters_)    cnames.push_back(kv.first);
    std::sort(cnames.begin(), cnames.end());
    for (auto& n : cnames) {
        const auto& c = *counters_.at(n);
        s << "# HELP " << c.name() << " " << c.help() << "\n";
        s << "# TYPE " << c.name() << " counter\n";
        s << c.name() << " " << c.value() << "\n";
    }

    std::vector<std::string> gnames; gnames.reserve(gauges_.size());
    for (auto& kv : gauges_)      gnames.push_back(kv.first);
    std::sort(gnames.begin(), gnames.end());
    for (auto& n : gnames) {
        const auto& gg = *gauges_.at(n);
        s << "# HELP " << gg.name() << " " << gg.help() << "\n";
        s << "# TYPE " << gg.name() << " gauge\n";
        s << gg.name() << " " << gg.value() << "\n";
    }

    std::vector<std::string> hnames; hnames.reserve(histograms_.size());
    for (auto& kv : histograms_) hnames.push_back(kv.first);
    std::sort(hnames.begin(), hnames.end());
    for (auto& n : hnames) {
        const auto& h = *histograms_.at(n);
        s << "# HELP " << h.name() << " " << h.help() << "\n";
        s << "# TYPE " << h.name() << " histogram\n";
        for (std::size_t i = 0; i < h.bounds().size(); ++i) {
            s << h.name() << "_bucket{le=\""
              << h.bounds()[i] << "\"} " << h.buckets()[i] << "\n";
        }
        s << h.name() << "_bucket{le=\"+Inf\"} "
          << h.buckets().back() << "\n";
        s << h.name() << "_count " << h.count() << "\n";
        s << h.name() << "_sum "   << h.sum()   << "\n";
    }

    return s.str();
}

Registry& host_registry() {
    static Registry r;
    return r;
}

}  // namespace neuro::pulse