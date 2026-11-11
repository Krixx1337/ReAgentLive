// reagentlive loopback: reads, writes, modules, search, xrefs, pointer chains, region, host thread.
#include "support.h"

TEST_CASE("read returns the agent's memory") {
    Client client = Attach();
    std::vector<uint8_t> buffer(32);
    for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = static_cast<uint8_t>(i * 7);
    const auto bytes = client.Read(reinterpret_cast<uint64_t>(buffer.data()), 32);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == buffer);
}

TEST_CASE("read batch returns one host tick and preserves order across an unreadable range") {
    Client client = Attach();
    const uint8_t buffer[] = {1, 2, 3, 4, 5};
    const auto address = reinterpret_cast<uint64_t>(buffer);
    const ReadSpan reads[] = {{address, 2}, {0x10, 1}, {address + 2, 3}};
    const auto before = client.Heartbeat();
    const auto batch = client.ReadBatch(reads);
    REQUIRE(batch.has_value());
    CHECK(batch->tick >= before);
    CHECK(batch->tick <= client.Heartbeat());
    REQUIRE(batch->items.size() == 3);
    CHECK(batch->items[0].status == Status::Ok);
    CHECK(batch->items[0].bytes == std::vector<uint8_t>{1, 2});
    CHECK(batch->items[1].status == Status::NotReadable);
    CHECK(batch->items[1].bytes.empty());
    CHECK(batch->items[2].status == Status::Ok);
    CHECK(batch->items[2].bytes == std::vector<uint8_t>{3, 4, 5});
}

TEST_CASE("agent rejects an oversized raw read batch before checking memory") {
    Client client = Attach();
    Request request{};
    request.op = op::ReadBatch;
    request.length = 2;
    const ReadSpan reads[] = {{0x10, MaxRead - sizeof(Status) * 2}, {0x20, 1}};
    memcpy(request.data, reads, sizeof(reads));
    const auto response = client.Send(request);
    REQUIRE(response.has_value());
    CHECK(response->status == Status::BadRequest);
    CHECK(response->length == 0);
}

TEST_CASE("read range splits across requests") {
    Client client = Attach();
    std::vector<uint8_t> buffer(10000);
    for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = static_cast<uint8_t>(i ^ (i >> 8));
    const auto bytes = client.ReadRange(reinterpret_cast<uint64_t>(buffer.data()), buffer.size());
    REQUIRE(bytes.has_value());
    CHECK(*bytes == buffer);
}

TEST_CASE("unreadable memory is refused before any copy") {
    Client client = Attach();
    const auto bytes = client.Read(0x10, 8);
    REQUIRE_FALSE(bytes.has_value());
    CHECK(bytes.error().kind == Error::Kind::Agent);
    CHECK(bytes.error().status == Status::NotReadable);
}

TEST_CASE("a fault after a passing preflight is caught and reported") {
    Client client = Attach();
    g_permissive = true;
    const auto bytes = client.Read(0x10, 8);
    g_permissive = false;
    REQUIRE_FALSE(bytes.has_value());
    CHECK(bytes.error().status == Status::Faulted);
    CHECK(bytes.error().detail == 0x10);
}

TEST_CASE("write changes writable memory and refuses read-only pages") {
    Client client = Attach();
    std::vector<uint8_t> buffer(8, 0);
    const uint8_t bytes[] = {1, 2, 3, 4};
    REQUIRE(client.Write(reinterpret_cast<uint64_t>(buffer.data()) + 2, bytes).has_value());
    CHECK(buffer == std::vector<uint8_t>{0, 0, 1, 2, 3, 4, 0, 0});

    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(page);
    DWORD old = 0;
    REQUIRE(VirtualProtect(page, 4096, PAGE_READONLY, &old));
    const auto refused = client.Write(reinterpret_cast<uint64_t>(page), bytes);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().status == Status::NotWritable);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_CASE("modules list this process's exe first, across pages") {
    Client client = Attach();
    const auto modules = client.Modules();
    REQUIRE(modules.has_value());
    REQUIRE_FALSE(modules->empty());
    CHECK(modules->front().base == reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)));
    CHECK(modules->front().size > 0);
    CHECK(modules->front().name == "reagentlive-loopback-tests.exe");
    bool kernel32 = false;
    for (const auto& module : *modules) kernel32 = kernel32 || _stricmp(module.name.c_str(), "kernel32.dll") == 0;
    CHECK(kernel32);
}

TEST_CASE("search finds a pattern in writable memory and resumes after a full page") {
    Client attached = Attach();
    auto* client = &attached;
    // Heap memory the agent can scan; the marker never appears elsewhere.
    std::vector<uint8_t> block(3 * 4096 + 7);
    const uint8_t marker[] = {0xd3, 0x9a, 0x61, 0x5e, 0x0b, 0xc4, 0x77, 0x18, 0xe2, 0x4f, 0x90, 0x3c};
    const size_t at[] = {8, 4096 + 16, 2 * 4096 + 40};
    for (size_t offset : at) memcpy(block.data() + offset, marker, sizeof(marker));
    const auto base = reinterpret_cast<uint64_t>(block.data());
    SearchQuery query;
    query.pattern.assign(std::begin(marker), std::end(marker));
    query.from = base;
    query.to = base + block.size();
    query.alignment = 8;
    query.writableOnly = true;
    auto all = client->Search(query, 10s);
    REQUIRE(all);
    REQUIRE(all->hits.size() == 3);
    for (size_t i = 0; i < 3; ++i) CHECK(all->hits[i] == base + at[i]);
    CHECK(all->resume == 0);

    query.maxHits = 2;
    auto page = client->Search(query, 10s);
    REQUIRE(page);
    CHECK(page->hits.size() == 2);
    REQUIRE(page->resume);
    query.from = page->resume;
    auto rest = client->Search(query, 10s);
    REQUIRE(rest);
    REQUIRE(rest->hits.size() == 1);
    CHECK(rest->hits[0] == base + at[2]);

    query.from = base;
    query.alignment = 16;
    auto aligned = client->Search(query, 10s);
    REQUIRE(aligned);
    for (uint64_t hit : aligned->hits) CHECK(hit % 16 == 0);
}

TEST_CASE("xrefs finds relative fields of every kind, resumes, and runs in the background") {
    Client client = Attach();
    std::vector<uint8_t> targetBlock(64);
    const auto target = reinterpret_cast<uint64_t>(targetBlock.data()) + 16;
    std::vector<uint8_t> block(3 * 4096, 0x90);
    const auto base = reinterpret_cast<uint64_t>(block.data());
    // Plants opcode bytes, then a field that lands `slack` bytes before the target (an immediate follows).
    struct Plant { size_t offset; std::vector<uint8_t> before; uint32_t slack; std::string_view kind; size_t start; };
    const std::vector<Plant> plants = {
        {100, {0xe8}, 0, "call", 1},
        {200, {0xe9}, 0, "jmp", 1},
        {300, {0x0f, 0x85}, 0, "jcc", 2},
        {400, {0x48, 0x8d, 0x05}, 0, "lea", 3},
        {500, {0x48, 0x8b, 0x0d}, 0, "load", 3},
        {600, {0x89, 0x05}, 0, "store", 2},
        {4090, {0xc7, 0x05}, 4, "store", 2},        // Crosses into the next page.
        {4200, {0xff, 0x15}, 0, "rip", 0},
        {4300, {0x12, 0x34}, 0, "unknown", 0},      // Chance bytes.
        {3 * 4096 - 4, {0x90, 0xe8}, 0, "call", 1}, // The very end of the range.
    };
    for (const auto& plant : plants) {
        memcpy(block.data() + plant.offset - plant.before.size(), plant.before.data(), plant.before.size());
        const auto value = static_cast<int32_t>(static_cast<int64_t>(target - plant.slack) -
            static_cast<int64_t>(base + plant.offset + 4));
        memcpy(block.data() + plant.offset, &value, 4);
    }
    XrefQuery query;
    query.target = target;
    query.from = base;
    query.to = base + block.size();
    auto found = client.Xrefs(query, 10s);
    REQUIRE(found);
    REQUIRE(found->hits.size() == plants.size());
    CHECK(found->resume == 0);
    for (size_t i = 0; i < plants.size(); ++i) {
        const auto& plant = plants[i];
        CHECK(found->hits[i] == base + plant.offset);
        std::array<uint8_t, cli::xref::Window> window;
        memcpy(window.data(), block.data() + plant.offset - cli::xref::Before, cli::xref::Window);
        const auto hit = cli::xref::Classify(found->hits[i], window, target);
        CHECK(hit.kind == plant.kind);
        if (plant.start) CHECK(hit.instruction == base + plant.offset - plant.start);
        else CHECK_FALSE(hit.instruction);
    }

    query.maxHits = 4;
    auto first = client.Xrefs(query, 10s);
    REQUIRE(first);
    CHECK(first->hits.size() == 4);
    REQUIRE(first->resume);
    query.from = first->resume;
    query.maxHits = MaxSearchHits;
    auto rest = client.Xrefs(query, 10s);
    REQUIRE(rest);
    CHECK(rest->hits.size() == plants.size() - 4);

    query.from = base;
    query.background = true;
    TheAgent().background = false;
    auto refused = client.Xrefs(query, 10s);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status == Status::Refused);
    TheAgent().background = true;
    auto background = client.Xrefs(query, 10s);
    TheAgent().background = false;
    REQUIRE(background);
    CHECK(background->hits == found->hits);

    // The absolute half of xrefs is the pointer search.
    memcpy(block.data() + 8 * 100, &target, 8);
    SearchQuery pointer;
    pointer.pattern.resize(8);
    memcpy(pointer.pattern.data(), &target, 8);
    pointer.alignment = 8;
    pointer.from = base;
    pointer.to = base + block.size();
    auto pointers = client.Search(pointer, 10s);
    REQUIRE(pointers);
    REQUIRE(pointers->hits.size() == 1);
    CHECK(pointers->hits[0] == base + 800);
}

TEST_CASE("pointer chains resolve hop by hop and report a null or unreadable hop") {
    Client client = Attach();
    std::vector<uint64_t> a(8), b(8), c(8);
    a[2] = reinterpret_cast<uint64_t>(b.data());     // a + 0x10 -> b
    b[1] = reinterpret_cast<uint64_t>(c.data());     // b + 0x8 -> c
    const auto base = reinterpret_cast<uint64_t>(a.data());
    const uint64_t offsets[] = {0x10, 0x8, 0x18};
    auto chain = client.ResolveChain(base, offsets);
    REQUIRE(chain);
    CHECK_FALSE(chain->broken);
    CHECK(chain->address == reinterpret_cast<uint64_t>(c.data()) + 0x18);
    REQUIRE(chain->hops.size() == 2);
    CHECK(chain->hops[0].address == base + 0x10);
    CHECK(chain->hops[1].value == reinterpret_cast<uint64_t>(c.data()));

    const uint64_t one[] = {0x20};
    CHECK(client.ResolveChain(base, one)->address == base + 0x20);   // One offset reads nothing.

    const uint64_t toNull[] = {0x10, 0x10, 0};                        // b + 0x10 holds 0.
    auto null = client.ResolveChain(base, toNull);
    REQUIRE(null);
    CHECK(null->broken == 1);
    CHECK(null->reason == "null pointer");

    b[3] = 0x10;                                                     // Points into the null page.
    const uint64_t toBad[] = {0x10, 0x18, 0, 0};
    auto bad = client.ResolveChain(base, toBad);
    REQUIRE(bad);
    CHECK(bad->broken == 2);
    CHECK(bad->hops.size() == 3);
}

const char g_readOnly[] = "reagentlive read-only global";

TEST_CASE("region tells code, read-only data, heap, freed memory and the top apart") {
    Client client = Attach();
    const auto code = client.Region(reinterpret_cast<uint64_t>(&Region));
    REQUIRE(code);
    CHECK(code->type == MEM_IMAGE);
    CHECK(code->state == MEM_COMMIT);
    CHECK((code->protect & 0xF0) != 0);
    CHECK(code->base <= reinterpret_cast<uint64_t>(&Region));

    const auto global = client.Region(reinterpret_cast<uint64_t>(g_readOnly));
    REQUIRE(global);
    CHECK(global->type == MEM_IMAGE);
    CHECK((global->protect & 0xFF) == PAGE_READONLY);

    auto block = std::make_unique<uint8_t[]>(64);
    const auto heap = client.Region(reinterpret_cast<uint64_t>(block.get()));
    REQUIRE(heap);
    CHECK(heap->type == MEM_PRIVATE);
    CHECK((heap->protect & 0xFF) == PAGE_READWRITE);

    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(page);
    VirtualFree(page, 0, MEM_RELEASE);
    const auto freed = client.Region(reinterpret_cast<uint64_t>(page));
    REQUIRE(freed);
    CHECK(freed->state == MEM_FREE);

    const auto top = client.Region(0xFFFF800000000000ull);
    REQUIRE_FALSE(top);
    CHECK(top.error().status == Status::NotFound);
}

TEST_CASE("the host's thread kind reaches the client") {
    Client client = Attach();
    CHECK(client.AgentHostThread() == HostThread::Engine);
    for (const HostThread kind : {HostThread::Borrowed, HostThread::Own, HostThread::Engine}) {
        TheAgent().kind = kind;
        const uint64_t start = client.Heartbeat();
        while (client.Heartbeat() < start + 2) Sleep(1); // Published every tick.
        CHECK(client.AgentHostThread() == kind);
        CHECK(std::string(HostThreadText(kind)) != "unset");
    }
}

TEST_CASE("search text finds a string in both widths") {
    Client client = Attach();
    std::vector<uint8_t> block(4096);
    const char ascii[] = "Zq7Weapon.cpp";
    memcpy(block.data() + 64, ascii, sizeof(ascii));
    const char16_t wide[] = u"Zq7Aimed";
    memcpy(block.data() + 512, wide, sizeof(wide));
    const auto base = reinterpret_cast<uint64_t>(block.data());
    for (const auto& [line, offset] : {std::pair{std::string("search text Zq7Weapon"), 64}, {"search text Zq7Aimed --wide", 512}}) {
        auto parsed = cli::Parse(line, {});
        REQUIRE(parsed);
        SearchQuery query;
        query.pattern.assign(parsed->request.data, parsed->request.data + parsed->request.length);
        query.from = base;
        query.to = base + block.size();
        auto found = client.Search(query, 10s);
        REQUIRE(found);
        REQUIRE(found->hits.size() == 1);
        CHECK(found->hits[0] == base + offset);
    }
}

TEST_CASE("search with heap filter and every typed width finds planted values") {
    Client client = Attach();
    std::vector<uint8_t> block(2 * 4096);
    const uint32_t word = 0x9e3779b9;
    const uint64_t wide = 0x5bd1e9955bd1e995ull;
    memcpy(block.data() + 64, &word, 4);
    memcpy(block.data() + 4096 + 128, &wide, 8);
    const auto base = reinterpret_cast<uint64_t>(block.data());
    SearchQuery query;
    query.from = base;
    query.to = base + block.size();
    query.privateOnly = true;
    query.pattern.assign(reinterpret_cast<const uint8_t*>(&word), reinterpret_cast<const uint8_t*>(&word) + 4);
    query.alignment = 4;
    auto four = client.Search(query, 10s);
    REQUIRE(four);
    REQUIRE(four->hits.size() == 1);
    CHECK(four->hits[0] == base + 64);
    query.pattern.assign(reinterpret_cast<const uint8_t*>(&wide), reinterpret_cast<const uint8_t*>(&wide) + 8);
    query.alignment = 8;
    auto eight = client.Search(query, 10s);
    REQUIRE(eight);
    REQUIRE(eight->hits.size() == 1);
    CHECK(eight->hits[0] == base + 4096 + 128);
    query.alignment = 1; // Byte scanner over the same value.
    auto bytes = client.Search(query, 10s);
    REQUIRE(bytes);
    REQUIRE(bytes->hits.size() == 1);
    CHECK(bytes->hits[0] == base + 4096 + 128);
}

TEST_CASE("write with expected bytes refuses changed memory and reports it") {
    Client client = Attach();
    std::vector<uint8_t> buffer = {1, 2, 3, 4};
    const auto address = reinterpret_cast<uint64_t>(buffer.data());
    const uint8_t next[] = {9, 9, 9, 9}, expected[] = {1, 2, 3, 4}, stale[] = {7, 7, 7, 7};
    std::vector<uint8_t> found;
    auto refused = client.WriteIf(address, next, stale, &found);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status == Status::Refused);
    CHECK(found == std::vector<uint8_t>{1, 2, 3, 4});
    CHECK(buffer == std::vector<uint8_t>{1, 2, 3, 4});
    REQUIRE(client.WriteIf(address, next, expected));
    CHECK(buffer == std::vector<uint8_t>{9, 9, 9, 9});
    const uint8_t shortExpected[] = {9, 9};
    CHECK_FALSE(client.WriteIf(address, next, shortExpected));
}

TEST_CASE("background search needs the host's opt-in and finds the same hits") {
    Client client = Attach();
    std::vector<uint8_t> block(3 * 4096);
    const uint8_t marker[] = {0x6b, 0x1f, 0xa2, 0x3d, 0xc8, 0x05, 0x99, 0x71, 0x2e, 0xb4};
    memcpy(block.data() + 24, marker, sizeof(marker));
    memcpy(block.data() + 2 * 4096 + 8, marker, sizeof(marker));
    const auto base = reinterpret_cast<uint64_t>(block.data());
    SearchQuery query;
    query.pattern.assign(std::begin(marker), std::end(marker));
    query.from = base;
    query.to = base + block.size();
    query.background = true;

    TheAgent().background = false;
    auto refused = client.Search(query, 10s);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status == Status::Refused);

    TheAgent().background = true;
    auto found = client.Search(query, 10s);
    TheAgent().background = false;
    REQUIRE(found);
    REQUIRE(found->hits.size() == 2);
    CHECK(found->hits[0] == base + 24);
    CHECK(found->hits[1] == base + 2 * 4096 + 8);
    CHECK(found->resume == 0);

    // A full page resumes, and the worker takes the next job.
    TheAgent().background = true;
    query.maxHits = 1;
    auto page = client.Search(query, 10s);
    REQUIRE(page);
    REQUIRE(page->hits.size() == 1);
    REQUIRE(page->resume);
    query.from = page->resume;
    auto rest = client.Search(query, 10s);
    TheAgent().background = false;
    REQUIRE(rest);
    REQUIRE(rest->hits.size() == 1);
    CHECK(rest->hits[0] == base + 2 * 4096 + 8);
}

TEST_CASE("search skips other views of the agent's own section") {
    Client client = Attach();
    // A second view, as an agent injected earlier into the same process holds.
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, SectionName().c_str());
    REQUIRE(mapping);
    const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    REQUIRE(view);
    std::vector<uint8_t> block(4096);
    const uint8_t marker[] = {0x47, 0xe1, 0x0c, 0x93, 0x5a, 0xd2, 0x38, 0x6f, 0xb7, 0x21, 0x84};
    memcpy(block.data() + 100, marker, sizeof(marker));
    SearchQuery query;
    query.pattern.assign(std::begin(marker), std::end(marker));
    query.maxHits = 16;
    for (bool background : {false, true}) {
        TheAgent().background = background;
        query.background = background;
        auto found = client.Search(query, 60s);
        REQUIRE(found);
        const auto start = reinterpret_cast<uint64_t>(view);
        for (uint64_t hit : found->hits) CHECK_FALSE((hit >= start && hit < start + sizeof(Section)));
        CHECK(std::find(found->hits.begin(), found->hits.end(), reinterpret_cast<uint64_t>(block.data() + 100)) !=
              found->hits.end());
    }
    TheAgent().background = false;
    UnmapViewOfFile(view);
    CloseHandle(mapping);
}

TEST_CASE("search refuses an empty pattern, a bad alignment and an inverted range") {
    Client attached = Attach();
    auto* client = &attached;
    SearchQuery query;
    CHECK_FALSE(client->Search(query, 1s));
    query.pattern = {1, 2, 3};
    query.alignment = 3;
    CHECK_FALSE(client->Search(query, 1s));
    query.alignment = 1;
    query.from = 0x2000;
    query.to = 0x1000;
    CHECK_FALSE(client->Search(query, 1s));
}
