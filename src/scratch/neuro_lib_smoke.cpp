// src/scratch/neuro_lib_smoke.cpp
//
// Smoke test for the NeuroLib umbrella header. Includes
// <neuro/neuro.hpp> and exercises one call from each subsystem so
// that future header changes don't accidentally break the bundle.

#include "neuro/neuro.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main() {
    using namespace neuro;

    std::printf("neuro_scratch (umbrella):\n");

    // core
    core::Result<int> r{42};
    std::printf("  Result       = %d\n", r.value());

    // mem
    mem::Arena a(4096);
    auto* p = a.allocate(64, 16);
    std::printf("  Arena used   = %zu (p=%p)\n", a.used(), (void*)p);

    // sched — ctor starts workers, dtor joins them.
    {
        sched::ws::Scheduler ws;
        (void)ws.worker_count();
    }

    // ipc — pair.a().send_nowait → pair.b().try_recv()
    ipc::EndpointPair pair;
    auto a_side = pair.a();
    auto b_side = pair.b();
    std::vector<std::byte> body(5);
    std::memcpy(body.data(), "hello", 5);
    a_side.send_nowait(ipc::Message{ipc::Tag{1, 1}, std::move(body)});
    auto msg = b_side.try_recv();
    std::printf("  ipc          = %s\n",
                msg ? std::string((const char*)msg->payload.data(),
                                  msg->payload.size()).c_str() : "(null)");

    // net
    net::IpAddr ip = net::IpAddr::parse("127.0.0.1");
    std::printf("  net parse    = %s\n", ip.to_string().c_str());

    // fs
    fs::MemFS mfs;
    std::vector<std::byte> hello(2);
    std::memcpy(hello.data(), "hi", 2);
    (void)mfs.write_all("/hello.txt", hello);
    auto got = mfs.read_all("/hello.txt");
    std::printf("  fs roundtrip = %s\n",
                got ? std::string((const char*)got->data(),
                                  got->size()).c_str() : "(null)");

    // dev
    auto& bus = dev::host_bus();
    (void)bus.size();

    // ui
    ui::Scene scene;
    std::vector<ui::DrawCmd> cmds;
    scene.flatten(cmds);

    // audio
    audio::Graph g;
    g.add_node(std::make_unique<audio::PassthroughNode>());
    g.prepare_all(48000, 2);

    // fabric
    auto& cluster = fabric::host_cluster();
    cluster.local(0, "127.0.0.1:9000");
    std::printf("  fabric self  = %llu\n",
                (unsigned long long)cluster.self());

    // pkg
    auto& store = pkg::host_store();
    std::vector<std::byte> x(1, std::byte{0x78});
    auto d = store.put(x);
    std::printf("  pkg has      = %d\n", (int)store.has(d));

    // jit
    auto& engine = jit::host_engine();
    jit::Module mod;
    mod.name = "main";
    jit::Function fn;
    fn.name = "f";
    fn.code.push_back({jit::OpKind::Const, {}, {}});
    mod.functions.push_back(std::move(fn));
    auto code = engine.codegen(mod, jit::Target::Host);
    std::printf("  jit disasm   = %zu bytes\n", code.disasm.size());

    // proof (compile-only check; no runtime use)
    proof::Contract pc;
    pc.expects = "x >= 0";
    pc.ensures = "result >= 0";
    std::printf("  proof        = %zu chars\n", pc.to_string().size());

    // pulse
    auto& reg = pulse::host_registry();
    auto& ctr = reg.counter("demo_total", "demo counter");
    ctr.inc();
    std::printf("  pulse        = %zu metrics\n",
                reg.scrape().size());

    // learn
    auto& opt = learn::host_optimizer();
    opt.observe({"worker_count", std::int64_t{8}, 0});
    auto prop = opt.propose();
    std::printf("  learn prop   = kind=%d val=%lld\n",
                (int)prop.kind, (long long)prop.integer_value);

    // bridge
    auto& br = bridge::host_bridge();
    std::printf("  bridge libs  = %zu\n", br.library_count());

    // boot
    auto boot = boot::host_boot_manifest();
    std::printf("  boot version = %s\n", boot.kernel_version.c_str());

    std::printf("OK\n");
    return 0;
}
